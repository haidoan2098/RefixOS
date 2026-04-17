/* ===========================================================
 * kernel/drivers/intc/intc.c — Interrupt controller + dispatch
 *
 * Two hardware backends selected at compile time:
 *   PLATFORM_QEMU → ARM GIC v1  @ GIC_{CPU,DIST}_BASE (realview-pb-a8)
 *   PLATFORM_BBB  → AM335x INTC @ INTC_BASE           (0x48200000)
 *
 * Also hosts the generic IRQ table + dispatcher (irq_*).
 * Exception entry calls irq_dispatch(); dispatch reads the active
 * line from the INTC, invokes the registered handler, then EOIs.
 *
 * Dependencies: board.h, irq.h, uart (boot log only)
 * =========================================================== */

#include <stdint.h>
#include "board.h"
#include "irq.h"
#include "intc.h"
#include "uart/uart.h"

#define REG32(addr)   (*((volatile uint32_t *)(addr)))

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }

/* ================================================================
 *  Generic IRQ table + dispatcher (shared across platforms)
 * ================================================================ */
static irq_handler_t irq_table[MAX_IRQS];

void irq_init(void)
{
    for (uint32_t i = 0; i < MAX_IRQS; i++)
        irq_table[i] = 0;
    intc_init();
}

void irq_register(uint32_t irq, irq_handler_t fn)
{
    if (irq < MAX_IRQS)
        irq_table[irq] = fn;
}

void irq_enable(uint32_t irq)
{
    if (irq < MAX_IRQS)
        intc_enable_line(irq);
}

void irq_dispatch(void)
{
    uint32_t n = intc_get_active();

    if (n < MAX_IRQS && irq_table[n])
        irq_table[n]();

    /* Always EOI, even for spurious: otherwise INTC stays latched */
    intc_eoi(n);
}

void irq_cpu_enable(void)
{
    __asm__ volatile("cpsie i" ::: "memory");
}

/* ================================================================
 *  PLATFORM_QEMU — ARM GIC v1 on realview-pb-a8
 *
 *  96 lines (num-irq=96 in QEMU). Distributor drives routing /
 *  enable; CPU interface delivers the active IRQ via IAR and
 *  accepts EOI via EOIR.
 * ================================================================ */
#ifdef PLATFORM_QEMU

/* --- Distributor -------------------------------------------------- */
#define GICD_CTLR         0x000U
#define GICD_TYPER        0x004U
#define GICD_ISENABLER(n) (0x100U + (n) * 4U)   /* set-enable   */
#define GICD_ICENABLER(n) (0x180U + (n) * 4U)   /* clear-enable */
#define GICD_IPRIORITYR(n) (0x400U + (n))       /* 1 byte/IRQ   */
#define GICD_ITARGETSR(n)  (0x800U + (n))       /* 1 byte/IRQ   */
#define GICD_ICFGR(n)     (0xC00U + (n) * 4U)   /* 2 bits/IRQ   */

/* --- CPU interface ------------------------------------------------ */
#define GICC_CTLR         0x000U
#define GICC_PMR          0x004U   /* priority mask threshold */
#define GICC_BPR          0x008U
#define GICC_IAR          0x00CU   /* read to ack             */
#define GICC_EOIR         0x010U

#define GIC_SPURIOUS      1023U

void intc_init(void)
{
    /* Disable distributor while we reconfigure */
    REG32(GIC_DIST_BASE + GICD_CTLR) = 0;

    uint32_t typer = REG32(GIC_DIST_BASE + GICD_TYPER);
    uint32_t lines = ((typer & 0x1FU) + 1U) * 32U;   /* total IRQ lines */
    if (lines > MAX_IRQS)
        lines = MAX_IRQS;

    /* Mask every SPI (start at 32 — SGI/PPI left alone) */
    for (uint32_t i = 32; i < lines; i += 32)
        REG32(GIC_DIST_BASE + GICD_ICENABLER(i / 32)) = 0xFFFFFFFFU;

    /* Default priority 0xA0, target CPU0 for every SPI */
    for (uint32_t i = 32; i < lines; i++) {
        *((volatile uint8_t *)(GIC_DIST_BASE + GICD_IPRIORITYR(i))) = 0xA0U;
        *((volatile uint8_t *)(GIC_DIST_BASE + GICD_ITARGETSR(i))) = 0x01U;
    }

    /* Re-enable distributor */
    REG32(GIC_DIST_BASE + GICD_CTLR) = 1U;

    /* CPU interface: accept all priorities, then enable signaling */
    REG32(GIC_CPU_BASE + GICC_PMR)  = 0xF0U;
    REG32(GIC_CPU_BASE + GICC_BPR)  = 0x00U;
    REG32(GIC_CPU_BASE + GICC_CTLR) = 1U;
    dsb();

    uart_printf("[INTC]  GIC v1 dist=0x%08x cpu=0x%08x lines=%u\n",
                GIC_DIST_BASE, GIC_CPU_BASE, lines);
}

void intc_enable_line(uint32_t irq)
{
    REG32(GIC_DIST_BASE + GICD_ISENABLER(irq / 32U)) = 1U << (irq & 0x1FU);
    dsb();
}

void intc_disable_line(uint32_t irq)
{
    REG32(GIC_DIST_BASE + GICD_ICENABLER(irq / 32U)) = 1U << (irq & 0x1FU);
    dsb();
}

uint32_t intc_get_active(void)
{
    uint32_t iar = REG32(GIC_CPU_BASE + GICC_IAR);
    uint32_t id  = iar & 0x3FFU;
    if (id >= GIC_SPURIOUS)
        return MAX_IRQS;
    return id;
}

void intc_eoi(uint32_t irq)
{
    /* GIC requires EOIR = same ID as IAR read, even for spurious (1023) */
    uint32_t id = (irq >= MAX_IRQS) ? GIC_SPURIOUS : irq;
    REG32(GIC_CPU_BASE + GICC_EOIR) = id;
    dsb();
}

/* ================================================================
 *  PLATFORM_BBB — AM335x INTC
 *
 *  128 lines (4 banks × 32). TRM §6. EOI via NEWIRQAGR.
 * ================================================================ */
#elif defined(PLATFORM_BBB)

#define INTC_SYSCONFIG    0x010U
#define INTC_SYSSTATUS    0x014U
#define INTC_SIR_IRQ      0x040U   /* bit[6:0] active, bit[31:7] spurious */
#define INTC_CONTROL      0x048U   /* bit 0 = NEWIRQAGR                   */
#define INTC_THRESHOLD    0x068U
#define INTC_MIR_BANK(n)      (0x084U + (n) * 0x20U)
#define INTC_MIR_CLEAR(n)     (0x088U + (n) * 0x20U)
#define INTC_MIR_SET(n)       (0x08CU + (n) * 0x20U)
#define INTC_ITR(n)           (0x080U + (n) * 0x20U)
#define INTC_ILR(m)           (0x100U + (m) * 0x04U)

#define INTC_SYSCONFIG_SOFTRESET  (1U << 1)
#define INTC_SYSSTATUS_RESETDONE  (1U << 0)
#define INTC_CONTROL_NEWIRQAGR    (1U << 0)
#define INTC_SIR_SPURIOUS_MASK    0xFFFFFF80U

void intc_init(void)
{
    /* Software reset */
    REG32(INTC_BASE + INTC_SYSCONFIG) = INTC_SYSCONFIG_SOFTRESET;
    while ((REG32(INTC_BASE + INTC_SYSSTATUS) & INTC_SYSSTATUS_RESETDONE) == 0)
        ;

    /* Mask all 128 lines */
    for (uint32_t b = 0; b < 4; b++)
        REG32(INTC_BASE + INTC_MIR_SET(b)) = 0xFFFFFFFFU;

    /* Priority threshold disabled — accept everything */
    REG32(INTC_BASE + INTC_THRESHOLD) = 0xFFU;

    /* Default ILR: priority 0, IRQ (not FIQ) for every line */
    for (uint32_t m = 0; m < MAX_IRQS; m++)
        REG32(INTC_BASE + INTC_ILR(m)) = 0;

    /* Clear any latched state */
    REG32(INTC_BASE + INTC_CONTROL) = INTC_CONTROL_NEWIRQAGR;
    dsb();
    uart_printf("[INTC]  AM335x @ 0x%08x — reset, all masked\n", INTC_BASE);
}

void intc_enable_line(uint32_t irq)
{
    uint32_t bank = irq >> 5;
    uint32_t bit  = irq & 0x1FU;
    REG32(INTC_BASE + INTC_MIR_CLEAR(bank)) = (1U << bit);
    dsb();
}

void intc_disable_line(uint32_t irq)
{
    uint32_t bank = irq >> 5;
    uint32_t bit  = irq & 0x1FU;
    REG32(INTC_BASE + INTC_MIR_SET(bank)) = (1U << bit);
    dsb();
}

uint32_t intc_get_active(void)
{
    uint32_t sir = REG32(INTC_BASE + INTC_SIR_IRQ);
    if (sir & INTC_SIR_SPURIOUS_MASK)
        return MAX_IRQS;
    return sir & 0x7FU;
}

void intc_eoi(uint32_t irq)
{
    (void)irq;
    REG32(INTC_BASE + INTC_CONTROL) = INTC_CONTROL_NEWIRQAGR;
    dsb();
}

#else
  #error "intc.c: unknown PLATFORM"
#endif
