/* ============================================================
 * boot/spl/src/mmc.c
 * RefixOS SPL MMC Loader — BeagleBone Black (AM335x)
 *
 * Brings up MMC0 in polling mode and reads the raw kernel image
 * from SD sectors into DDR for the SPL-to-kernel handoff.
 * ============================================================ */

#include "am335x.h"
#include "boot.h"

/*
 * SPL uses raw sector reads only.
 * The boot contract is simple: initialize MMC0, read kernel.bin into DDR, jump.
 */
static int sdhc_card;

#define MMC_STAT_CC         BIT(0)
#define MMC_STAT_TC         BIT(1)
#define MMC_STAT_BRR        BIT(5)
#define MMC_STAT_ERRI       BIT(15)

#define MMC_CMD_RSP_NONE    (0U << 16)
#define MMC_CMD_RSP_136     (1U << 16)
#define MMC_CMD_RSP_48      (2U << 16)
#define MMC_CMD_RSP_48_BUSY (3U << 16)
#define MMC_CMD_CCCE        BIT(19)
#define MMC_CMD_CICE        BIT(20)
#define MMC_CMD_DP          BIT(21)
#define MMC_CMD_DDIR_READ   BIT(4)

#define MMC_RSP_NONE        MMC_CMD_RSP_NONE
#define MMC_RSP_R1          (MMC_CMD_RSP_48 | MMC_CMD_CCCE | MMC_CMD_CICE)
#define MMC_RSP_R1b         (MMC_CMD_RSP_48_BUSY | MMC_CMD_CCCE | MMC_CMD_CICE)
#define MMC_RSP_R2          (MMC_CMD_RSP_136 | MMC_CMD_CCCE)
#define MMC_RSP_R3          MMC_CMD_RSP_48
#define MMC_RSP_R6          (MMC_CMD_RSP_48 | MMC_CMD_CCCE | MMC_CMD_CICE)
#define MMC_RSP_R7          (MMC_CMD_RSP_48 | MMC_CMD_CCCE | MMC_CMD_CICE)

/*
 * Every command goes through the same polled path.
 * On error the CMD state machine is reset so later commands are not poisoned.
 */
static int mmc_send_cmd(uint32_t cmd, uint32_t arg, uint32_t flags)
{
    uint32_t status;
    int timeout;

    /* Clear stale status before issuing the next request. */
    writel(0xFFFFFFFFU, MMC_STAT);
    writel(arg, MMC_ARG);
    writel((cmd << 24) | flags, MMC_CMD);

    /* Poll until the controller reports either completion or an error bit. */
    timeout = 10000000;
    do {
        status = readl(MMC_STAT);
        if (--timeout == 0) {
            return -1;
        }
    } while ((status & (MMC_STAT_CC | MMC_STAT_ERRI)) == 0U);

    if ((status & MMC_STAT_ERRI) != 0U) {
        /* Reset CMD handling after an error so the next command can progress. */
        writel(readl(MMC_SYSCTL) | MMC_SYSCTL_SRC, MMC_SYSCTL);
        timeout = 100000;
        while (((readl(MMC_SYSCTL) & MMC_SYSCTL_SRC) != 0U) && (--timeout > 0))
            ;
        writel(0xFFFFFFFFU, MMC_STAT);
        return -1;
    }

    writel(MMC_STAT_CC, MMC_STAT);
    return 0;
}

int mmc_init(void)
{
    uint32_t rsp;
    uint32_t sysctl;
    uint32_t rca;
    int timeout;
    int i;

    /* Put the MMC0 pins into their SD-card function before enabling traffic. */
    writel(PIN_MODE_0 | PIN_INPUT_EN | PIN_PULLUP_EN, CONF_MMC0_DAT3);
    writel(PIN_MODE_0 | PIN_INPUT_EN | PIN_PULLUP_EN, CONF_MMC0_DAT2);
    writel(PIN_MODE_0 | PIN_INPUT_EN | PIN_PULLUP_EN, CONF_MMC0_DAT1);
    writel(PIN_MODE_0 | PIN_INPUT_EN | PIN_PULLUP_EN, CONF_MMC0_DAT0);
    writel(PIN_MODE_0 | PIN_INPUT_EN | PIN_PULLUP_EN, CONF_MMC0_CLK);
    writel(PIN_MODE_0 | PIN_INPUT_EN | PIN_PULLUP_EN, CONF_MMC0_CMD);

    /* Soft-reset the controller so SPL starts from a clean MMC state. */
    writel(0x02U, MMC_SYSCONFIG);
    timeout = 1000000;
    while (((readl(MMC_SYSSTATUS) & 0x01U) == 0U) && (--timeout > 0))
        ;
    if (timeout == 0) {
        return -1;
    }

    /* Keep both interface clocks active while SPL uses polling mode. */
    writel(0x00000308U, MMC_SYSCONFIG);
    writel(0x00000000U, MMC_CON);

    /* Reset CMD and DAT lines independently before card enumeration starts. */
    writel(readl(MMC_SYSCTL) | MMC_SYSCTL_SRC | MMC_SYSCTL_SRD, MMC_SYSCTL);
    timeout = 10000000;
    while (((readl(MMC_SYSCTL) & (MMC_SYSCTL_SRC | MMC_SYSCTL_SRD)) != 0U) && (--timeout > 0))
        ;
    if (timeout == 0) {
        return -1;
    }

    /* Advertise 3.3V support, then switch bus power on at that voltage. */
    writel(readl(MMC_CAPA) | BIT(24), MMC_CAPA);
    writel(MMC_HCTL_SDVS_3_3V, MMC_HCTL);
    writel(readl(MMC_HCTL) | MMC_HCTL_SDBP, MMC_HCTL);

    timeout = 100000;
    while (((readl(MMC_HCTL) & MMC_HCTL_SDBP) == 0U) && (--timeout > 0))
        ;
    if (timeout == 0) {
        return -1;
    }

    /* Start at card-identification speed before any real command exchange. */
    writel(0x000E3C01U, MMC_SYSCTL);
    delay(1000U);

    timeout = 1000000;
    while (((readl(MMC_SYSCTL) & MMC_SYSCTL_ICS) == 0U) && (--timeout > 0))
        ;
    if (timeout == 0) {
        return -1;
    }

    /* Once ICS is set, the generated clock may be forwarded to the card. */
    writel(readl(MMC_SYSCTL) | MMC_SYSCTL_CEN, MMC_SYSCTL);
    writel(0xFFFFFFFFU, MMC_IE);
    writel(0xFFFFFFFFU, MMC_ISE);
    writel(0xFFFFFFFFU, MMC_STAT);

    /* SD requires an initial 80-clock train before CMD0/CMD8/ACMD41 become reliable. */
    writel(readl(MMC_CON) | BIT(1), MMC_CON);
    writel(0x00000000U, MMC_CMD);
    delay(10000U);

    timeout = 1000000;
    while (((readl(MMC_STAT) & MMC_STAT_CC) == 0U) && (--timeout > 0))
        ;
    if (timeout == 0) {
        return -1;
    }

    writel(MMC_STAT_CC, MMC_STAT);
    writel(readl(MMC_CON) & ~BIT(1), MMC_CON);
    delay(5000U);
    writel(0xFFFFFFFFU, MMC_STAT);

    /* CMD0 moves the card into idle after the raw init clocks. */
    if (mmc_send_cmd(0U, 0U, MMC_RSP_NONE) != 0) {
        return -1;
    }
    delay(5000U);

    /* CMD8 distinguishes SDv2-capable cards; failure is tolerated here. */
    (void)mmc_send_cmd(8U, 0x1AAU, MMC_RSP_R7);

    /* ACMD41 loops until the card finishes power-up and reports OCR ready. */
    for (i = 0; i < 2000; i++) {
        if (mmc_send_cmd(55U, 0x00000000U, MMC_RSP_R1) != 0) {
            continue;
        }
        if (mmc_send_cmd(41U, 0x40300000U, MMC_RSP_R3) != 0) {
            continue;
        }
        if ((readl(MMC_RSP10) & BIT(31)) != 0U) {
            if ((readl(MMC_RSP10) & BIT(30)) != 0U) {
                sdhc_card = 1;
            }
            break;
        }
        delay(1000U);
    }

    if (i == 2000) {
        return -1;
    }

    /* CID/RCA/CSD/SELECT complete the transition into transfer state. */
    if (mmc_send_cmd(2U, 0U, MMC_RSP_R2) != 0) {
        return -1;
    }
    if (mmc_send_cmd(3U, 0U, MMC_RSP_R6) != 0) {
        return -1;
    }
    rsp = readl(MMC_RSP10);
    rca = (rsp >> 16) & 0xFFFFU;

    if (mmc_send_cmd(9U, rca << 16, MMC_RSP_R2) != 0) {
        return -1;
    }
    if (mmc_send_cmd(7U, rca << 16, MMC_RSP_R1b) != 0) {
        return -1;
    }

    /* Use the largest DTO during raw kernel fetches. */
    sysctl = readl(MMC_SYSCTL);
    sysctl = (sysctl & ~(0xFU << 16)) | (14U << 16);
    writel(sysctl, MMC_SYSCTL);

    /* After identification, raise the card clock closer to data-transfer speed. */
    sysctl = readl(MMC_SYSCTL);
    sysctl &= ~MMC_SYSCTL_CEN;
    writel(sysctl, MMC_SYSCTL);

    sysctl = readl(MMC_SYSCTL);
    sysctl &= ~0xFFC0U;
    sysctl |= (4U << 6);
    writel(sysctl, MMC_SYSCTL);

    timeout = 1000000;
    while (((readl(MMC_SYSCTL) & MMC_SYSCTL_ICS) == 0U) && (--timeout > 0))
        ;
    if (timeout == 0) {
        return -1;
    }

    writel(readl(MMC_SYSCTL) | MMC_SYSCTL_CEN, MMC_SYSCTL);

    /* SPL always reads 512-byte sectors from the raw kernel area. */
    if (mmc_send_cmd(16U, 512U, MMC_RSP_R1) != 0) {
        return -1;
    }

    return 0;
}

int mmc_read_sectors(uint32_t start_sector, uint32_t count, void *dest)
{
    uint32_t *buf = (uint32_t *)dest;
    uint32_t arg;
    uint32_t i;
    uint32_t j;
    uint32_t stat;
    int timeout;

    /* Read one 512-byte block at a time directly into the destination buffer. */
    for (i = 0; i < count; i++) {
        writel((1U << 16) | 512U, MMC_BLK);
        writel(0xFFFFFFFFU, MMC_STAT);

        /* SDSC uses byte addressing; SDHC uses block addressing. */
        arg = sdhc_card ? (start_sector + i) : ((start_sector + i) * 512U);
        writel(arg, MMC_ARG);
        writel((17U << 24) | MMC_RSP_R1 | MMC_CMD_DP | MMC_CMD_DDIR_READ, MMC_CMD);

        /* BRR means a full block is available in the MMCHS data window. */
        timeout = 10000000;
        while (((readl(MMC_STAT) & MMC_STAT_BRR) == 0U) && (--timeout > 0)) {
            stat = readl(MMC_STAT);
            if ((stat & MMC_STAT_ERRI) != 0U) {
                return -1;
            }
        }
        if (timeout == 0) {
            return -1;
        }

        /* 512 bytes = 128 words from MMC_DATA. */
        for (j = 0; j < 128U; j++) {
            *buf++ = readl(MMC_DATA);
        }

        /* TC closes the block transfer before SPL advances to the next sector. */
        timeout = 10000000;
        while (((readl(MMC_STAT) & MMC_STAT_TC) == 0U) && (--timeout > 0))
            ;
        if (timeout == 0) {
            return -1;
        }

        writel(0xFFFFFFFFU, MMC_STAT);
    }

    return 0;
}
