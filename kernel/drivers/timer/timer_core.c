/* ============================================================
 * kernel/drivers/timer/timer_core.c — Timer subsystem core
 *
 * Owns:
 *   - Active timer pointer (set by platform/<board>/board.c)
 *   - tick_count + optional user callback (scheduler_tick)
 *
 * Chip drivers implement init(dev, period_us) and irq(dev).
 * irq() ack's hardware then calls timer_tick() so the subsystem
 * bumps the counter and fires the handler — identical flow
 * regardless of chip.
 * ============================================================ */

#include <stdint.h>
#include "drivers/timer.h"

static struct timer_device *active_timer;

static volatile uint32_t tick_count;
static timer_handler_t   user_handler;

void timer_set_device(struct timer_device *dev) { active_timer = dev; }

void timer_set_handler(timer_handler_t fn) { user_handler = fn; }

uint32_t timer_get_ticks(void) { return tick_count; }

void timer_init(uint32_t period_us)
{
    active_timer->ops->init(active_timer, period_us);
}

void timer_irq(void)
{
    active_timer->ops->irq(active_timer);
}

/* Called by chip drivers from their irq() after ack'ing the HW. */
void timer_tick(void)
{
    tick_count++;
    if (user_handler)
        user_handler();
}
