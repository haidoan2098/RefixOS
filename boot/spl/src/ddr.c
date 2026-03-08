/* ============================================================
 * boot/spl/src/ddr.c
 * RefixOS SPL DDR Bring-up — BeagleBone Black (AM335x)
 *
 * Initializes external DDR so SPL can place the kernel image in
 * memory before handing control to kernel entry.
 * ============================================================ */

#include "am335x.h"
#include "boot.h"

/*
 * These values match the BBB DDR3 path already validated in RefARM-OS.
 * SPL uses them as the fixed bring-up tuple for EMIF + PHY before kernel load.
 */
#define EMIF_TIM1_VAL        0x0AAAE51BU
#define EMIF_TIM2_VAL        0x266B7FDAU
#define EMIF_TIM3_VAL        0x501F867FU
#define EMIF_REF_CTRL_VAL    0x00000C30U
#define EMIF_CFG_VAL         0x61C05332U
#define DDR_PHY_CTRL_VAL     0x00100007U
#define DDR_IO_CTRL_VAL      0x0000018BU
#define VTP_CTRL_ENABLE      BIT(6)
#define VTP_CTRL_READY       BIT(5)

void ddr_init(void)
{
    uint32_t i;

    /* VTP must calibrate the DDR IO impedance before PHY timing is applied. */
    writel(readl(VTP_CTRL) & ~VTP_CTRL_ENABLE, VTP_CTRL);
    writel(readl(VTP_CTRL) | VTP_CTRL_ENABLE, VTP_CTRL);

    /* READY indicates the compensation engine has converged. */
    for (i = 0; i < 10000U; i++) {
        if ((readl(VTP_CTRL) & VTP_CTRL_READY) != 0U) {
            break;
        }
    }

    /* Program shared IO control before touching per-byte training ratios. */
    writel(DDR_IO_CTRL_VAL, DDR_IO_CTRL);
    writel(DDR_IO_CTRL_VAL, DDR_DATA0_IOCTRL);
    writel(DDR_IO_CTRL_VAL, DDR_DATA1_IOCTRL);
    writel(0x1U, DDR_CKE_CTRL);

    /* Command path slave ratios and clock inversion must match the board layout. */
    writel(0x80U, DDR_CMD0_SLAVE_RATIO_0);
    writel(0x80U, DDR_CMD1_SLAVE_RATIO_0);
    writel(0x80U, DDR_CMD2_SLAVE_RATIO_0);
    writel(0x00U, DDR_CMD0_INVERT_CLKOUT_0);
    writel(0x00U, DDR_CMD1_INVERT_CLKOUT_0);
    writel(0x00U, DDR_CMD2_INVERT_CLKOUT_0);

    /* Byte-lane read/write ratios seed the PHY before EMIF starts SDRAM init. */
    writel(0x38U, DDR_DATA0_RD_DQS_SLAVE_RATIO_0);
    writel(0x44U, DDR_DATA0_WR_DQS_SLAVE_RATIO_0);
    writel(0x94U, DDR_DATA0_FIFO_WE_SLAVE_RATIO_0);
    writel(0x7DU, DDR_DATA0_WR_DATA_SLAVE_RATIO_0);
    writel(0x00U, DDR_DATA0_GATE_LEVEL_INIT_RATIO_0);

    writel(0x38U, DDR_DATA1_RD_DQS_SLAVE_RATIO_0);
    writel(0x44U, DDR_DATA1_WR_DQS_SLAVE_RATIO_0);
    writel(0x94U, DDR_DATA1_FIFO_WE_SLAVE_RATIO_0);
    writel(0x7DU, DDR_DATA1_WR_DATA_SLAVE_RATIO_0);
    writel(0x00U, DDR_DATA1_GATE_LEVEL_INIT_RATIO_0);

    /* EMIF timing, refresh, and geometry must be valid before the first DDR access. */
    writel(DDR_PHY_CTRL_VAL, EMIF_DDR_PHY_CTRL_1);
    writel(DDR_PHY_CTRL_VAL, EMIF_DDR_PHY_CTRL_2);
    writel(EMIF_TIM1_VAL, EMIF_SDRAM_TIM_1);
    writel(EMIF_TIM2_VAL, EMIF_SDRAM_TIM_2);
    writel(EMIF_TIM3_VAL, EMIF_SDRAM_TIM_3);
    writel(EMIF_REF_CTRL_VAL, EMIF_SDRAM_REF_CTRL);
    writel(EMIF_CFG_VAL, EMIF_SDRAM_CONFIG);

    /* Give EMIF time to run the DDR3 init sequence and exit reset/training. */
    for (i = 0; i < 5000U; i++)
        ;

    /* Refresh is written again after init so normal periodic refresh stays active. */
    writel(EMIF_REF_CTRL_VAL, EMIF_SDRAM_REF_CTRL);
}

int ddr_test(void)
{
    volatile uint32_t *ddr = (volatile uint32_t *)DDR_BASE;
    uint32_t read_val;
    uint32_t i;

    /* Minimal destructive test: two complementary patterns across the first 4KB. */
    for (i = 0; i < 1024U; i++) {
        ddr[i] = 0xAA55AA55U;
    }

    /* Read-back confirms the first pattern actually sticks in external DDR. */
    for (i = 0; i < 1024U; i++) {
        read_val = ddr[i];
        if (read_val != 0xAA55AA55U) {
            uart_puts("\r\nDDR FAIL @ ");
            uart_print_hex((uint32_t)&ddr[i]);
            uart_puts("\r\n");
            return -1;
        }
    }

    /* Second pattern catches stuck-at and lane-mapping issues missed by the first pass. */
    for (i = 0; i < 1024U; i++) {
        ddr[i] = 0x55AA55AAU;
    }

    for (i = 0; i < 1024U; i++) {
        read_val = ddr[i];
        if (read_val != 0x55AA55AAU) {
            uart_puts("\r\nDDR FAIL @ ");
            uart_print_hex((uint32_t)&ddr[i]);
            uart_puts("\r\n");
            return -1;
        }
    }

    return 0;
}
