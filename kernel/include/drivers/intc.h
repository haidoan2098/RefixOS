#ifndef KERNEL_DRIVERS_INTC_H
#define KERNEL_DRIVERS_INTC_H

/* ============================================================
 * drivers/intc.h — Interrupt controller subsystem contract
 *
 * Two layers:
 *   - struct intc_ops : chip-level (enable, ack, EOI)
 *   - irq_* API       : generic dispatch table, platform-agnostic
 *
 * Exception path calls irq_dispatch() which asks the chip driver
 * for the active line, invokes the registered handler, then EOIs.
 * ============================================================ */

#include <stdint.h>

struct intc_device;

/* Contract every INTC chip driver implements. */
struct intc_ops {
    void     (*init)(struct intc_device *dev);
    void     (*enable_line)(struct intc_device *dev, uint32_t irq);
    void     (*disable_line)(struct intc_device *dev, uint32_t irq);
    uint32_t (*get_active)(struct intc_device *dev);      /* MAX_IRQS on spurious */
    void     (*eoi)(struct intc_device *dev, uint32_t irq);
};

/* Board instantiates one of these.
 * GIC splits addresses into cpu/dist; AM335x uses only `base`. */
struct intc_device {
    const struct intc_ops *ops;
    uint32_t base;             /* single-block controllers (AM335x INTC) */
    uint32_t cpu_base;         /* GIC CPU interface */
    uint32_t dist_base;        /* GIC distributor   */
};

#define MAX_IRQS 128U
typedef void (*irq_handler_t)(void);

/* ---- Subsystem API — called by kernel core ---- */
void intc_set_device(struct intc_device *dev);

void irq_init(void);                                    /* zeroes table + chip init */
void irq_register(uint32_t irq, irq_handler_t fn);
void irq_enable(uint32_t irq);
void irq_dispatch(void);                                /* called from handle_irq */
void irq_cpu_enable(void);

#endif /* KERNEL_DRIVERS_INTC_H */
