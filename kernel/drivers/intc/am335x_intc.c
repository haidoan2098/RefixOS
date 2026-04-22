/* ============================================================
 * kernel/drivers/intc/am335x_intc.c — TI AM335x INTC driver
 *
 * 128 lines across 4 banks × 32. EOI via NEWIRQAGR. Single
 * base address (no split cpu/dist like GIC).
 *
 * Uses dev->base from struct intc_device.
 * ============================================================ */

#include <stdint.h>
#include "drivers/intc.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

#define INTC_SYSCONFIG          0x010U
#define INTC_SYSSTATUS          0x014U
#define INTC_SIR_IRQ            0x040U
#define INTC_CONTROL            0x048U
#define INTC_THRESHOLD          0x068U
#define INTC_MIR_CLEAR(n)       (0x088U + (n) * 0x20U)
#define INTC_MIR_SET(n)         (0x08CU + (n) * 0x20U)
#define INTC_ILR(m)             (0x100U + (m) * 0x04U)

#define INTC_SYSCONFIG_SOFTRESET (1U << 1)
#define INTC_SYSSTATUS_RESETDONE (1U << 0)
#define INTC_CONTROL_NEWIRQAGR   (1U << 0)
#define INTC_SIR_SPURIOUS_MASK   0xFFFFFF80U

static void am335x_intc_init(struct intc_device *dev)
{
    uint32_t base = dev->base;

    REG32(base + INTC_SYSCONFIG) = INTC_SYSCONFIG_SOFTRESET;
    while ((REG32(base + INTC_SYSSTATUS) & INTC_SYSSTATUS_RESETDONE) == 0)
        ;

    for (uint32_t b = 0; b < 4; b++)
        REG32(base + INTC_MIR_SET(b)) = 0xFFFFFFFFU;

    REG32(base + INTC_THRESHOLD) = 0xFFU;

    for (uint32_t m = 0; m < MAX_IRQS; m++)
        REG32(base + INTC_ILR(m)) = 0;

    REG32(base + INTC_CONTROL) = INTC_CONTROL_NEWIRQAGR;
    dsb();
}

static void am335x_intc_enable_line(struct intc_device *dev, uint32_t irq)
{
    uint32_t bank = irq >> 5;
    uint32_t bit  = irq & 0x1FU;
    REG32(dev->base + INTC_MIR_CLEAR(bank)) = (1U << bit);
    dsb();
}

static void am335x_intc_disable_line(struct intc_device *dev, uint32_t irq)
{
    uint32_t bank = irq >> 5;
    uint32_t bit  = irq & 0x1FU;
    REG32(dev->base + INTC_MIR_SET(bank)) = (1U << bit);
    dsb();
}

static uint32_t am335x_intc_get_active(struct intc_device *dev)
{
    uint32_t sir = REG32(dev->base + INTC_SIR_IRQ);
    if (sir & INTC_SIR_SPURIOUS_MASK)
        return MAX_IRQS;
    return sir & 0x7FU;
}

static void am335x_intc_eoi(struct intc_device *dev, uint32_t irq)
{
    (void)irq;
    REG32(dev->base + INTC_CONTROL) = INTC_CONTROL_NEWIRQAGR;
    dsb();
}

const struct intc_ops am335x_intc_ops = {
    .init         = am335x_intc_init,
    .enable_line  = am335x_intc_enable_line,
    .disable_line = am335x_intc_disable_line,
    .get_active   = am335x_intc_get_active,
    .eoi          = am335x_intc_eoi,
};
