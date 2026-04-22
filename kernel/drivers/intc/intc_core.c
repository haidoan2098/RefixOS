/* ============================================================
 * kernel/drivers/intc/intc_core.c — INTC subsystem core
 *
 * Two layers in one file:
 *   1. Generic IRQ dispatch table + API (irq_register / enable /
 *      dispatch) — platform-agnostic, was kernel/irq/irq.c.
 *   2. Chip driver dispatch — forwards to active_intc->ops->*.
 *
 * Exception entry path calls irq_dispatch(), which asks the chip
 * for the active line, invokes the registered handler, and EOIs.
 * Kernel core never sees GIC or AM335x INTC directly.
 * ============================================================ */

#include <stdint.h>
#include "drivers/intc.h"

static struct intc_device *active_intc;

static irq_handler_t irq_table[MAX_IRQS];

void intc_set_device(struct intc_device *dev) { active_intc = dev; }

void irq_init(void)
{
    for (uint32_t i = 0; i < MAX_IRQS; i++)
        irq_table[i] = 0;
    active_intc->ops->init(active_intc);
}

void irq_register(uint32_t irq, irq_handler_t fn)
{
    if (irq < MAX_IRQS)
        irq_table[irq] = fn;
}

void irq_enable(uint32_t irq)
{
    if (irq < MAX_IRQS)
        active_intc->ops->enable_line(active_intc, irq);
}

void irq_dispatch(void)
{
    uint32_t n = active_intc->ops->get_active(active_intc);

    if (n < MAX_IRQS && irq_table[n])
        irq_table[n]();

    /* Always EOI — even spurious — otherwise INTC stays latched */
    active_intc->ops->eoi(active_intc, n);
}

void irq_cpu_enable(void)
{
    __asm__ volatile("cpsie i" ::: "memory");
}
