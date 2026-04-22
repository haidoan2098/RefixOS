#ifndef KERNEL_DRIVERS_TIMER_H
#define KERNEL_DRIVERS_TIMER_H

/* ============================================================
 * drivers/timer.h — Timer subsystem contract
 *
 * Kernel core calls timer_init / timer_get_ticks without knowing
 * which chip runs underneath. timer_core.c owns the tick counter
 * and the optional user callback (scheduler_tick); chip drivers
 * only program hardware and ack IRQs.
 *
 * timer_core.c exposes timer_tick() so chip drivers can bump the
 * counter + fire the callback from their irq handler.
 * ============================================================ */

#include <stdint.h>

struct timer_device;

/* Contract every timer chip driver implements. */
struct timer_ops {
    void (*init)(struct timer_device *dev, uint32_t period_us);
    void (*irq)(struct timer_device *dev);
};

/* Board instantiates one of these per timer it exposes. */
struct timer_device {
    const struct timer_ops *ops;
    uint32_t base;
    uint32_t irq;
    uint32_t clk_hz;           /* input clock — some chips need it */
};

typedef void (*timer_handler_t)(void);

/* ---- Subsystem API — called by kernel core ---- */
void timer_set_device(struct timer_device *dev);

void     timer_init(uint32_t period_us);
void     timer_irq(void);                      /* wired to timer IRQ line */
void     timer_set_handler(timer_handler_t fn);
uint32_t timer_get_ticks(void);

/* ---- Helper exposed to chip drivers ----
 * Chip's irq() ack's hardware then calls timer_tick() so the
 * subsystem bumps tick_count and fires the registered handler.
 */
void timer_tick(void);

#endif /* KERNEL_DRIVERS_TIMER_H */
