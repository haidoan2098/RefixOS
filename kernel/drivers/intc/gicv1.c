/* ============================================================
 * kernel/drivers/intc/gicv1.c — ARM GIC v1 driver
 *
 * Distributor drives enable + routing; CPU interface delivers
 * the active IRQ via IAR and accepts EOI via EOIR. Supports up
 * to 96 lines on QEMU realview-pb-a8 (typer-reported).
 *
 * Uses dev->dist_base and dev->cpu_base from struct intc_device.
 * ============================================================ */

#include <stdint.h>
#include "drivers/intc.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

/* ---- Distributor ---- */
#define GICD_CTLR          0x000U
#define GICD_TYPER         0x004U
#define GICD_ISENABLER(n)  (0x100U + (n) * 4U)
#define GICD_ICENABLER(n)  (0x180U + (n) * 4U)
#define GICD_IPRIORITYR(n) (0x400U + (n))
#define GICD_ITARGETSR(n)  (0x800U + (n))

/* ---- CPU interface ---- */
#define GICC_CTLR          0x000U
#define GICC_PMR           0x004U
#define GICC_BPR           0x008U
#define GICC_IAR           0x00CU
#define GICC_EOIR          0x010U

#define GIC_SPURIOUS       1023U

static void gicv1_init(struct intc_device *dev)
{
    uint32_t dist = dev->dist_base;
    uint32_t cpu  = dev->cpu_base;

    REG32(dist + GICD_CTLR) = 0;

    uint32_t typer = REG32(dist + GICD_TYPER);
    uint32_t lines = ((typer & 0x1FU) + 1U) * 32U;
    if (lines > MAX_IRQS)
        lines = MAX_IRQS;

    for (uint32_t i = 32; i < lines; i += 32)
        REG32(dist + GICD_ICENABLER(i / 32)) = 0xFFFFFFFFU;

    for (uint32_t i = 32; i < lines; i++) {
        *((volatile uint8_t *)(dist + GICD_IPRIORITYR(i))) = 0xA0U;
        *((volatile uint8_t *)(dist + GICD_ITARGETSR(i))) = 0x01U;
    }

    REG32(dist + GICD_CTLR) = 1U;

    REG32(cpu + GICC_PMR)  = 0xF0U;
    REG32(cpu + GICC_BPR)  = 0x00U;
    REG32(cpu + GICC_CTLR) = 1U;
    dsb();
}

static void gicv1_enable_line(struct intc_device *dev, uint32_t irq)
{
    REG32(dev->dist_base + GICD_ISENABLER(irq / 32U)) = 1U << (irq & 0x1FU);
    dsb();
}

static void gicv1_disable_line(struct intc_device *dev, uint32_t irq)
{
    REG32(dev->dist_base + GICD_ICENABLER(irq / 32U)) = 1U << (irq & 0x1FU);
    dsb();
}

static uint32_t gicv1_get_active(struct intc_device *dev)
{
    uint32_t iar = REG32(dev->cpu_base + GICC_IAR);
    uint32_t id  = iar & 0x3FFU;
    if (id >= GIC_SPURIOUS)
        return MAX_IRQS;
    return id;
}

static void gicv1_eoi(struct intc_device *dev, uint32_t irq)
{
    /* GIC requires EOIR = same ID as IAR read, even for spurious (1023) */
    uint32_t id = (irq >= MAX_IRQS) ? GIC_SPURIOUS : irq;
    REG32(dev->cpu_base + GICC_EOIR) = id;
    dsb();
}

const struct intc_ops gicv1_ops = {
    .init         = gicv1_init,
    .enable_line  = gicv1_enable_line,
    .disable_line = gicv1_disable_line,
    .get_active   = gicv1_get_active,
    .eoi          = gicv1_eoi,
};
