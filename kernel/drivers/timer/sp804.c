/* ============================================================
 * kernel/drivers/timer/sp804.c — ARM SP804 Dual Timer driver
 *
 * Uses Timer 0 sub-unit. Periodic mode with overflow IRQ.
 * Clock rate is supplied via dev->clk_hz — QEMU realview-pb-a8
 * wires SP804 at 1 MHz, so 1 count == 1 µs.
 *
 * No knowledge of board wiring — addresses + clock come from
 * platform/<board>/board.c via struct timer_device.
 * ============================================================ */

#include <stdint.h>
#include "drivers/timer.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

#define SP804_LOAD          0x00U
#define SP804_CTRL          0x08U
#define SP804_INTCLR        0x0CU

#define SP804_CTRL_INTEN    (1U << 5)
#define SP804_CTRL_PERIODIC (1U << 6)
#define SP804_CTRL_ENABLE   (1U << 7)
#define SP804_CTRL_32BIT    (1U << 1)

static void sp804_init(struct timer_device *dev, uint32_t period_us)
{
    uint32_t base   = dev->base;
    uint32_t reload = period_us * (dev->clk_hz / 1000000U);

    REG32(base + SP804_CTRL) = 0;
    REG32(base + SP804_LOAD) = reload;

    REG32(base + SP804_CTRL) = SP804_CTRL_ENABLE
                             | SP804_CTRL_PERIODIC
                             | SP804_CTRL_INTEN
                             | SP804_CTRL_32BIT;
    dsb();
}

static void sp804_irq(struct timer_device *dev)
{
    REG32(dev->base + SP804_INTCLR) = 1;
    dsb();

    timer_tick();
}

const struct timer_ops sp804_ops = {
    .init = sp804_init,
    .irq  = sp804_irq,
};
