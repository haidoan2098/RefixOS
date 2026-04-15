/* ===========================================================
 * kernel/main.c — Kernel C entry point
 *
 * Called from kernel/arch/arm/boot/start.S after:
 *   - Exception-mode stacks are set up
 *   - BSS is zeroed
 *   - MMU is OFF (physical addresses only)
 *
 * Purpose at this stage: verify UART output works.
 *   Next phase will add exception vector table, then MMU.
 * =========================================================== */

#include "drivers/uart/uart.h"
#include "include/board.h"
#include "include/exception.h"

/* Linker-provided symbols */
extern uint32_t _text_start, _text_end;
extern uint32_t _data_start, _data_end;
extern uint32_t _bss_start, _bss_end;
extern uint32_t _svc_stack_top;

/* Platform name string — resolved at compile time */
#ifdef PLATFORM_QEMU
  #define PLATFORM_NAME "QEMU realview-pb-a8"
#elif defined(PLATFORM_BBB)
  #define PLATFORM_NAME "BeagleBone Black (AM335x)"
#endif

static uint32_t read_cpsr(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cpsr" : "=r"(val));
    return val;
}

void kmain(void)
{
    uint32_t cpsr;

    uart_init();

    uart_printf("\n");
    uart_printf("================================================\n");
    uart_printf("  RingNova — ARMv7-A bare-metal kernel\n");
    uart_printf("  Built %s %s\n", __DATE__, __TIME__);
    uart_printf("================================================\n");

    uart_printf("[UART] init done @ %p\n", UART0_BASE);
    uart_printf("[BOOT] platform : %s\n", PLATFORM_NAME);
    uart_printf("[BOOT] .text    : %p — %p\n",
                (uint32_t)&_text_start, (uint32_t)&_text_end);
    uart_printf("[BOOT] .data    : %p — %p\n",
                (uint32_t)&_data_start, (uint32_t)&_data_end);
    uart_printf("[BOOT] .bss     : %p — %p\n",
                (uint32_t)&_bss_start, (uint32_t)&_bss_end);
    uart_printf("[BOOT] SVC stack: %p\n", (uint32_t)&_svc_stack_top);

    cpsr = read_cpsr();
    uart_printf("[BOOT] CPSR     : %p (mode=%x, IRQ=%s, FIQ=%s)\n",
                cpsr,
                cpsr & 0x1FU,
                (cpsr & (1U << 7)) ? "masked" : "on",
                (cpsr & (1U << 6)) ? "masked" : "on");
    uart_printf("[BOOT] MMU      : off\n");

    exception_init();

    /* Test SVC — should print syscall number and return */
    uart_printf("[TEST] triggering SVC #42...\n");
    __asm__ volatile("svc #42");
    uart_printf("[TEST] SVC returned OK\n");

    /* Uncomment ONE test at a time — each one halts the system */
#define TEST_NONE       0
#define TEST_DATA_ABORT 1
#define TEST_UNDEFINED  2

#define EXCEPTION_TEST  TEST_NONE

#if EXCEPTION_TEST == TEST_DATA_ABORT
    /* Test Data Abort — enable alignment check then do unaligned
     * 32-bit read. QEMU doesn't fault on unmapped PA reads, but
     * SCTLR.A=1 + unaligned access triggers Data Abort reliably. */
    uart_printf("[TEST] triggering Data Abort (unaligned access)...\n");
    {
        /* Enable alignment checking: SCTLR.A (bit 1) = 1 */
        uint32_t sctlr;
        __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
        sctlr |= (1U << 1);
        __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
        __asm__ volatile("isb");
        /* Unaligned 32-bit read → Data Abort */
        volatile uint32_t x = *((volatile uint32_t *)0x70100001U);
        (void)x;
    }
#elif EXCEPTION_TEST == TEST_UNDEFINED
    /* Test Undefined Instruction — execute an undefined opcode,
     * should print PC + register dump then halt */
    uart_printf("[TEST] triggering Undefined Instruction...\n");
    __asm__ volatile(".word 0xE7F000F0");   /* permanently undefined */
#endif

    uart_printf("[BOOT] boot complete — entering idle loop\n");

    /* Halt — scheduler not implemented yet */
    for (;;)
        __asm__ volatile("wfi");
}
