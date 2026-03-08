/* ===========================================================
 * boot/spl/include/am335x.h
 *
 * AM335x (BeagleBone Black) hardware register map
 * =========================================================== */

#ifndef AM335X_H
#define AM335X_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* MMIO helpers. */
static inline void writel(uint32_t val, uint32_t addr)
{
    *((volatile uint32_t *)addr) = val;
}

static inline uint32_t readl(uint32_t addr)
{
    return *((volatile uint32_t *)addr);
}

static inline void writeb(uint8_t val, uint32_t addr)
{
    *((volatile uint8_t *)addr) = val;
}

/* Simple busy-wait delay (cycle counting, not calibrated) */
static inline void delay(volatile uint32_t count)
{
    while (count--);
}

/* Watchdog Timer 1. */
#define WDT1_BASE       0x44E35000U
#define WDT1_WIDR       (WDT1_BASE + 0x00U)  /* Identification Register */
#define WDT1_WDSC       (WDT1_BASE + 0x10U)  /* System Control Register */
#define WDT1_WDST       (WDT1_BASE + 0x14U)  /* System Status Register  */
#define WDT1_WISR       (WDT1_BASE + 0x18U)  /* Interrupt Status Register */
#define WDT1_WIER       (WDT1_BASE + 0x1CU)  /* Interrupt Enable Register */
#define WDT1_WCLR       (WDT1_BASE + 0x24U)  /* Control Register */
#define WDT1_WCRR       (WDT1_BASE + 0x28U)  /* Counter Register */
#define WDT1_WLDR       (WDT1_BASE + 0x2CU)  /* Load Register */
#define WDT1_WTGR       (WDT1_BASE + 0x30U)  /* Trigger Register */
#define WDT1_WWPS       (WDT1_BASE + 0x34U)  /* Write Posting Bits Register */
#define WDT1_WSPR       (WDT1_BASE + 0x48U)  /* Start/Stop Register */

#define WDT1_WWPS_W_PEND_WSPR   (1U << 4)

/* UART0. */
#define UART0_BASE      0x44E09000U

#define UART0_THR       (UART0_BASE + 0x00U)  /* Transmit Holding Register */
#define UART0_RHR       (UART0_BASE + 0x00U)  /* Receive Holding Register  */
#define UART0_DLL       (UART0_BASE + 0x00U)  /* Divisor Latch Low  (LCR[7]=1) */
#define UART0_DLH       (UART0_BASE + 0x04U)  /* Divisor Latch High (LCR[7]=1) */
#define UART0_IER       (UART0_BASE + 0x04U)  /* Interrupt Enable Register */
#define UART0_FCR       (UART0_BASE + 0x08U)  /* FIFO Control Register (W) */
#define UART0_LCR       (UART0_BASE + 0x0CU)  /* Line Control Register */
#define UART0_MCR       (UART0_BASE + 0x10U)  /* Modem Control Register */
#define UART0_LSR       (UART0_BASE + 0x14U)  /* Line Status Register */
#define UART0_MDR1      (UART0_BASE + 0x20U)  /* Mode Definition Register 1 */

#define UART_LSR_RXFIFOE    (1U << 0)  /* RX FIFO has data */
#define UART_LSR_TXFIFOE    (1U << 5)  /* TX FIFO empty (THRE) */
#define UART_LSR_TEMT       (1U << 6)  /* TX empty (FIFO + shift reg) */

#define UART_16X_MODE       0x00U       /* 16x UART mode */
#define UART_DISABLE        0x07U       /* Disable UART */

/* PRCM. */
#define CM_PER_BASE             0x44E00000U
#define CM_WKUP_BASE            0x44E00400U
#define CM_DPLL_BASE            0x44E00500U
#define CM_MPU_BASE             0x44E00600U

#define CM_PER_L4LS_CLKSTCTRL   (CM_PER_BASE + 0x00U)
#define CM_PER_L3_CLKSTCTRL     (CM_PER_BASE + 0x04U)
#define CM_PER_L4FW_CLKSTCTRL   (CM_PER_BASE + 0x08U)
#define CM_PER_L3_CLKCTRL       (CM_PER_BASE + 0x60U)
#define CM_PER_L4LS_CLKCTRL     (CM_PER_BASE + 0x60U)
#define CM_PER_EMIF_CLKCTRL     (CM_PER_BASE + 0x28U)
#define CM_PER_MMC0_CLKCTRL     (CM_PER_BASE + 0x3CU)
#define CM_PER_UART0_CLKCTRL    (CM_PER_BASE + 0x6CU)  /* NOTE: UART0 is in CM_WKUP */

#define CM_WKUP_CLKSTCTRL       (CM_WKUP_BASE + 0x00U)
#define CM_WKUP_UART0_CLKCTRL   (CM_WKUP_BASE + 0xB4U)
#define CM_CLKMODE_DPLL_DDR     (CM_DPLL_BASE + 0x94U)
#define CM_IDLEST_DPLL_DDR      (CM_DPLL_BASE + 0x34U)
#define CM_CLKSEL_DPLL_DDR      (CM_DPLL_BASE + 0x40U)
#define CM_DIV_M2_DPLL_DDR      (CM_DPLL_BASE + 0xA0U)

/* Clock module enable values */
#define CLKCTRL_MODULEMODE_ENABLE   0x2U
#define CLKSTCTRL_SW_WKUP           0x2U

#define DPLL_MN_BYP_MODE            0x4U
#define DPLL_LOCK_MODE              0x7U
#define MODULE_ENABLE               0x2U

#define BIT(n)                      (1U << (n))

#define DDR_BASE                    0x80000000U

#define CONTROL_MODULE_BASE         0x44E10000U
#define CONF_MMC0_DAT3              (CONTROL_MODULE_BASE + 0x8F0U)
#define CONF_MMC0_DAT2              (CONTROL_MODULE_BASE + 0x8F4U)
#define CONF_MMC0_DAT1              (CONTROL_MODULE_BASE + 0x8F8U)
#define CONF_MMC0_DAT0              (CONTROL_MODULE_BASE + 0x8FCU)
#define CONF_MMC0_CLK               (CONTROL_MODULE_BASE + 0x900U)
#define CONF_MMC0_CMD               (CONTROL_MODULE_BASE + 0x904U)

#define PIN_MODE_0                  0U
#define PIN_INPUT_EN                BIT(5)
#define PIN_PULLUP_EN               BIT(4)

#define EMIF_BASE                   0x4C000000U
#define EMIF_SDRAM_CONFIG           (EMIF_BASE + 0x08U)
#define EMIF_SDRAM_REF_CTRL         (EMIF_BASE + 0x10U)
#define EMIF_SDRAM_TIM_1            (EMIF_BASE + 0x18U)
#define EMIF_SDRAM_TIM_2            (EMIF_BASE + 0x20U)
#define EMIF_SDRAM_TIM_3            (EMIF_BASE + 0x28U)
#define EMIF_DDR_PHY_CTRL_1         (EMIF_BASE + 0xE4U)
#define EMIF_DDR_PHY_CTRL_2         (EMIF_BASE + 0xECU)

#define DDR_CMD0_IOCTRL             (CONTROL_MODULE_BASE + 0x1404U)
#define DDR_CMD1_IOCTRL             (CONTROL_MODULE_BASE + 0x1408U)
#define DDR_CMD2_IOCTRL             (CONTROL_MODULE_BASE + 0x140CU)
#define DDR_DATA0_IOCTRL            (CONTROL_MODULE_BASE + 0x1440U)
#define DDR_DATA1_IOCTRL            (CONTROL_MODULE_BASE + 0x1444U)
#define DDR_CMD0_SLAVE_RATIO_0      (CONTROL_MODULE_BASE + 0x0C1CU)
#define DDR_CMD0_INVERT_CLKOUT_0    (CONTROL_MODULE_BASE + 0x0C2CU)
#define DDR_CMD1_SLAVE_RATIO_0      (CONTROL_MODULE_BASE + 0x0C50U)
#define DDR_CMD1_INVERT_CLKOUT_0    (CONTROL_MODULE_BASE + 0x0C60U)
#define DDR_CMD2_SLAVE_RATIO_0      (CONTROL_MODULE_BASE + 0x0C84U)
#define DDR_CMD2_INVERT_CLKOUT_0    (CONTROL_MODULE_BASE + 0x0C94U)
#define DDR_DATA0_RD_DQS_SLAVE_RATIO_0    (CONTROL_MODULE_BASE + 0x0CC8U)
#define DDR_DATA0_WR_DQS_SLAVE_RATIO_0    (CONTROL_MODULE_BASE + 0x0CD4U)
#define DDR_DATA0_FIFO_WE_SLAVE_RATIO_0   (CONTROL_MODULE_BASE + 0x0CD8U)
#define DDR_DATA0_WR_DATA_SLAVE_RATIO_0   (CONTROL_MODULE_BASE + 0x0CDCU)
#define DDR_DATA0_GATE_LEVEL_INIT_RATIO_0 (CONTROL_MODULE_BASE + 0x0CE4U)
#define DDR_DATA1_RD_DQS_SLAVE_RATIO_0    (CONTROL_MODULE_BASE + 0x0D0CU)
#define DDR_DATA1_WR_DQS_SLAVE_RATIO_0    (CONTROL_MODULE_BASE + 0x0D18U)
#define DDR_DATA1_FIFO_WE_SLAVE_RATIO_0   (CONTROL_MODULE_BASE + 0x0D1CU)
#define DDR_DATA1_WR_DATA_SLAVE_RATIO_0   (CONTROL_MODULE_BASE + 0x0D20U)
#define DDR_DATA1_GATE_LEVEL_INIT_RATIO_0 (CONTROL_MODULE_BASE + 0x0D28U)
#define DDR_IO_CTRL                 (CONTROL_MODULE_BASE + 0x0E04U)
#define VTP_CTRL                    (CONTROL_MODULE_BASE + 0x0E0CU)
#define DDR_CKE_CTRL                (CONTROL_MODULE_BASE + 0x131CU)

#define MMC0_BASE                   0x48060000U
#define MMC_SYSCONFIG               (MMC0_BASE + 0x110U)
#define MMC_SYSSTATUS               (MMC0_BASE + 0x114U)
#define MMC_CON                     (MMC0_BASE + 0x12CU)
#define MMC_BLK                     (MMC0_BASE + 0x204U)
#define MMC_ARG                     (MMC0_BASE + 0x208U)
#define MMC_CMD                     (MMC0_BASE + 0x20CU)
#define MMC_RSP10                   (MMC0_BASE + 0x210U)
#define MMC_DATA                    (MMC0_BASE + 0x220U)
#define MMC_HCTL                    (MMC0_BASE + 0x228U)
#define MMC_SYSCTL                  (MMC0_BASE + 0x22CU)
#define MMC_STAT                    (MMC0_BASE + 0x230U)
#define MMC_IE                      (MMC0_BASE + 0x234U)
#define MMC_ISE                     (MMC0_BASE + 0x238U)
#define MMC_CAPA                    (MMC0_BASE + 0x240U)

#define MMC_HCTL_SDBP               BIT(8)
#define MMC_HCTL_SDVS_3_3V          (0x7U << 9)
#define MMC_SYSCTL_ICE              BIT(0)
#define MMC_SYSCTL_ICS              BIT(1)
#define MMC_SYSCTL_CEN              BIT(2)
#define MMC_SYSCTL_SRC              BIT(25)
#define MMC_SYSCTL_SRD              BIT(26)

#endif /* AM335X_H */
