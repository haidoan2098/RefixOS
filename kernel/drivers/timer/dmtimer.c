/* ============================================================
 * kernel/drivers/timer/dmtimer.c — TI AM335x DMTIMER driver
 *
 * Uses 32-bit auto-reload overflow interrupt. Caller supplies
 * clock rate via dev->clk_hz (AM335x DMTIMER2 is fed from
 * CLK_M_OSC at 24 MHz).
 *
 * Note: this driver assumes the CM_PER gate for the timer is
 * already enabled. On AM335x that requires touching CM_PER_BASE,
 * which is board-specific — platform/bbb/board.c does it during
 * platform_init_devices() before handing the device to the
 * subsystem.
 * ============================================================ */

#include <stdint.h>
#include "drivers/timer.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

#define DMT_TIOCP_CFG       0x010U
#define DMT_IRQSTATUS       0x028U
#define DMT_IRQENABLE_SET   0x02CU
#define DMT_TCLR            0x038U
#define DMT_TCRR            0x03CU
#define DMT_TLDR            0x040U
#define DMT_TWPS            0x048U
#define DMT_TSICR           0x054U

#define TIOCP_SOFTRESET     (1U << 0)
#define TCLR_ST             (1U << 0)
#define TCLR_AR             (1U << 1)
#define TSICR_POSTED        (1U << 2)
#define IRQ_OVF             (1U << 1)
#define TWPS_TCLR           (1U << 0)
#define TWPS_TCRR           (1U << 1)
#define TWPS_TLDR           (1U << 2)

static void dmtimer_init(struct timer_device *dev, uint32_t period_us)
{
    uint32_t base = dev->base;

    /* Soft reset */
    REG32(base + DMT_TIOCP_CFG) = TIOCP_SOFTRESET;
    while (REG32(base + DMT_TIOCP_CFG) & TIOCP_SOFTRESET)
        ;

    REG32(base + DMT_TSICR) = TSICR_POSTED;

    while (REG32(base + DMT_TWPS) & TWPS_TCLR)
        ;
    REG32(base + DMT_TCLR) = 0;

    REG32(base + DMT_IRQSTATUS) = 0x7U;

    uint32_t count  = period_us * (dev->clk_hz / 1000000U);
    uint32_t reload = (uint32_t)(0U - count);

    while (REG32(base + DMT_TWPS) & TWPS_TLDR)
        ;
    REG32(base + DMT_TLDR) = reload;

    while (REG32(base + DMT_TWPS) & TWPS_TCRR)
        ;
    REG32(base + DMT_TCRR) = reload;

    REG32(base + DMT_IRQENABLE_SET) = IRQ_OVF;

    while (REG32(base + DMT_TWPS) & TWPS_TCLR)
        ;
    REG32(base + DMT_TCLR) = TCLR_ST | TCLR_AR;
    dsb();
}

static void dmtimer_irq(struct timer_device *dev)
{
    REG32(dev->base + DMT_IRQSTATUS) = IRQ_OVF;
    dsb();

    timer_tick();
}

const struct timer_ops dmtimer_ops = {
    .init = dmtimer_init,
    .irq  = dmtimer_irq,
};
