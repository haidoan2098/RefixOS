/* ============================================================
 * boot/spl/src/clock.c
 * RefixOS SPL Clock Bring-up — BeagleBone Black (AM335x)
 *
 * Prepares the DDR PLL and peripheral clocks required before
 * DDR initialization and SD-based kernel loading can proceed.
 * ============================================================ */

#include "am335x.h"
#include "boot.h"

/*
 * SPL only reprograms the DDR PLL here.
 * Boot ROM already leaves the other PLL domains in a usable state for early UART.
 */
static void config_ddr_pll(void)
{
    uint32_t val;
    uint32_t timeout;

    /* Put DPLL_DDR into bypass before changing M/N. */
    val = readl(CM_CLKMODE_DPLL_DDR);
    val = (val & ~0x7U) | DPLL_MN_BYP_MODE;
    writel(val, CM_CLKMODE_DPLL_DDR);

    /* Wait until the hardware reports bypass state. */
    timeout = 10000U;
    while (((readl(CM_IDLEST_DPLL_DDR) & BIT(8)) == 0U) && (timeout-- > 0U))
        ;

    /* 24MHz crystal, M=400, N=23, M2=1 -> DDR clock path at 400MHz. */
    val = readl(CM_CLKSEL_DPLL_DDR);
    val &= ~0x7FFFFU;
    val |= (400U << 8) | 23U;
    writel(val, CM_CLKSEL_DPLL_DDR);

    val = readl(CM_DIV_M2_DPLL_DDR);
    val = (val & ~0x1FU) | 1U;
    writel(val, CM_DIV_M2_DPLL_DDR);

    /* Re-lock DPLL_DDR with the new divider tuple. */
    val = readl(CM_CLKMODE_DPLL_DDR);
    val = (val & ~0x7U) | DPLL_LOCK_MODE;
    writel(val, CM_CLKMODE_DPLL_DDR);

    /* Wait until the lock bit becomes visible before DDR init touches EMIF. */
    timeout = 10000U;
    while (((readl(CM_IDLEST_DPLL_DDR) & BIT(0)) == 0U) && (timeout-- > 0U))
        ;
}

/*
 * EMIF, MMC0, and UART0 must stay ungated before later SPL stages touch them.
 */
static void enable_module_clock(uint32_t reg)
{
    uint32_t timeout;

    writel(MODULE_ENABLE, reg);

    /* IDLEST bits clear when the module clock domain becomes functional. */
    timeout = 100000U;
    while (((readl(reg) & 0x30000U) != 0U) && (timeout-- > 0U))
        ;
}

void clock_init(void)
{
    /* DDR timing depends on DPLL_DDR being valid before EMIF programming. */
    config_ddr_pll();

    /* Later stages use EMIF for DDR and MMC0 for kernel fetch from SD. */
    enable_module_clock(CM_PER_EMIF_CLKCTRL);
    enable_module_clock(CM_PER_MMC0_CLKCTRL);
    enable_module_clock(CM_WKUP_UART0_CLKCTRL);
}
