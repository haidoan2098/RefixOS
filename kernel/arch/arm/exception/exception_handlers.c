/* ===========================================================
 * kernel/arch/arm/exception/exception_handlers.c — C-level
 * exception handlers
 *
 * All fault handlers are fatal — dump registers and halt.
 * SVC handler prints the syscall number and returns.
 * IRQ handler halts (interrupts not enabled yet).
 *
 * Dependencies: uart driver, exception.h
 * =========================================================== */

#include "exception.h"
#include "irq.h"
#include "uart/uart.h"

/* ARM mode names indexed by CPSR[4:0] */
static const char *mode_name(uint32_t cpsr)
{
    switch (cpsr & 0x1FU) {
    case 0x10: return "USR";
    case 0x11: return "FIQ";
    case 0x12: return "IRQ";
    case 0x13: return "SVC";
    case 0x17: return "ABT";
    case 0x1B: return "UND";
    case 0x1F: return "SYS";
    default:   return "???";
    }
}

/* Read Data Fault Status Register (DFSR) — cp15 c5 c0 0 */
static uint32_t read_dfsr(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(val));
    return val;
}

/* Read Data Fault Address Register (DFAR) — cp15 c6 c0 0 */
static uint32_t read_dfar(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(val));
    return val;
}

/* Read Instruction Fault Status Register (IFSR) — cp15 c5 c0 1 */
static uint32_t read_ifsr(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(val));
    return val;
}

/* Read Instruction Fault Address Register (IFAR) — cp15 c6 c0 2 */
static uint32_t read_ifar(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(val));
    return val;
}

/* -----------------------------------------------------------
 * Dump full register context
 * ----------------------------------------------------------- */
static void dump_context(const exception_context_t *ctx)
{
    uart_printf("  SPSR = 0x%08x  (mode=%s, IRQ=%s, FIQ=%s)\n",
                ctx->spsr,
                mode_name(ctx->spsr),
                (ctx->spsr & (1U << 7)) ? "masked" : "on",
                (ctx->spsr & (1U << 6)) ? "masked" : "on");
    uart_printf("  PC   = 0x%08x  (faulting/return address)\n", ctx->lr);
    uart_printf("  r0 =%08x  r1 =%08x  r2 =%08x  r3 =%08x\n",
                ctx->r[0], ctx->r[1], ctx->r[2], ctx->r[3]);
    uart_printf("  r4 =%08x  r5 =%08x  r6 =%08x  r7 =%08x\n",
                ctx->r[4], ctx->r[5], ctx->r[6], ctx->r[7]);
    uart_printf("  r8 =%08x  r9 =%08x  r10=%08x  r11=%08x\n",
                ctx->r[8], ctx->r[9], ctx->r[10], ctx->r[11]);
    uart_printf("  r12=%08x\n", ctx->r[12]);
}

static void halt_forever(void)
{
    uart_printf("[PANIC] system halted.\n");
    for (;;)
        __asm__ volatile("wfi");
}

/* ===========================================================
 * Data Abort handler
 * =========================================================== */
void handle_data_abort(exception_context_t *ctx)
{
    uint32_t dfsr = read_dfsr();
    uint32_t dfar = read_dfar();

    uart_printf("\n[PANIC] *** DATA ABORT ***\n");
    uart_printf("  DFAR = 0x%08x  (faulting address)\n", dfar);
    uart_printf("  DFSR = 0x%08x  (fault status)\n", dfsr);
    dump_context(ctx);
    halt_forever();
}

/* ===========================================================
 * Prefetch Abort handler
 * =========================================================== */
void handle_prefetch_abort(exception_context_t *ctx)
{
    uint32_t ifsr = read_ifsr();
    uint32_t ifar = read_ifar();

    uart_printf("\n[PANIC] *** PREFETCH ABORT ***\n");
    uart_printf("  IFAR = 0x%08x  (faulting address)\n", ifar);
    uart_printf("  IFSR = 0x%08x  (fault status)\n", ifsr);
    dump_context(ctx);
    halt_forever();
}

/* ===========================================================
 * Undefined Instruction handler
 * =========================================================== */
void handle_undefined(exception_context_t *ctx)
{
    uart_printf("\n[PANIC] *** UNDEFINED INSTRUCTION ***\n");
    dump_context(ctx);
    halt_forever();
}

/* ===========================================================
 * SVC handler — prints syscall number and returns to caller
 *
 * The SVC number is encoded in the SVC instruction itself:
 *   ARM mode:   svc #N → bottom 24 bits of the instruction
 *   Thumb mode: svc #N → bottom 8 bits
 * We read it from memory at the SVC instruction address.
 * =========================================================== */
void handle_svc(exception_context_t *ctx)
{
    uint32_t svc_addr = ctx->lr - 4;   /* SVC instruction is before LR */
    uint32_t svc_instr = *((volatile uint32_t *)svc_addr);
    uint32_t svc_num = svc_instr & 0x00FFFFFFU;  /* ARM: bits [23:0] */

    uart_printf("[SVC]   syscall #%u from PC=0x%08x\n",
                svc_num, svc_addr);
}

/* ===========================================================
 * IRQ handler — called from exception_entry_irq after srsdb to
 * SVC stack. Defers to the INTC dispatcher in drivers/intc.
 * =========================================================== */
void handle_irq(void)
{
    irq_dispatch();
}

/* ===========================================================
 * exception_init — set VBAR to our vector table
 * =========================================================== */
extern uint32_t _vectors_start;

void exception_init(void)
{
    uint32_t vbar = (uint32_t)&_vectors_start;

    /* Write VBAR — cp15 c12 c0 0 */
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 0" :: "r"(vbar));

    /* Instruction sync barrier — ensure VBAR is visible before
     * any exception could fire */
    __asm__ volatile("isb" ::: "memory");

    uart_printf("[EXCP] vector table installed @ 0x%08x\n", vbar);
}
