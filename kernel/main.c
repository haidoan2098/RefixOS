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
#include "include/mmu.h"

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

/* ---------------------------------------------------------- *
 *  Boot self-tests
 *
 *  Non-destructive checks run after mmu_init + exception_init.
 *  Each test prints [PASS] or [FAIL]. Destructive tests (NULL
 *  deref, undefined instruction) live in EXCEPTION_TEST below.
 * ---------------------------------------------------------- */
extern uint32_t boot_pgd[];

static void run_boot_tests(void)
{
    int pass = 0, fail = 0;

    uart_printf("\n[TEST] ========== boot self-tests ==========\n");

    /* T1 — MMU is on (SCTLR.M=1) */
    {
        uint32_t sctlr = mmu_read_sctlr();
        if (sctlr & 1U) {
            uart_printf("[TEST] [PASS] T1 MMU enabled (SCTLR=0x%08x)\n", sctlr);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T1 MMU off      (SCTLR=0x%08x)\n", sctlr);
            fail++;
        }
    }

    /* T2 — TTBR0 base == boot_pgd address */
    {
        uint32_t ttbr0 = mmu_read_ttbr0();
        uint32_t base  = ttbr0 & 0xFFFFC000U;
        if (base == (uint32_t)boot_pgd) {
            uart_printf("[TEST] [PASS] T2 TTBR0 base = boot_pgd (0x%08x)\n", base);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T2 TTBR0 base 0x%08x != boot_pgd 0x%08x\n",
                        base, (uint32_t)boot_pgd);
            fail++;
        }
    }

    /* T3 — High VA alias read: PA and VA return identical word */
    {
        uint32_t off     = KERNEL_PHYS_BASE - RAM_BASE;
        uint32_t pa_word = *((volatile uint32_t *)KERNEL_PHYS_BASE);
        uint32_t va_word = *((volatile uint32_t *)(VA_KERNEL_BASE + off));
        if (pa_word == va_word) {
            uart_printf("[TEST] [PASS] T3 VA alias read: "
                        "PA[0x%08x]=VA[0x%08x]=0x%08x\n",
                        KERNEL_PHYS_BASE, VA_KERNEL_BASE + off, pa_word);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T3 VA alias read: PA=0x%08x VA=0x%08x\n",
                        pa_word, va_word);
            fail++;
        }
    }

    /* T4 — High VA alias write-through: write via VA, read via PA */
    {
        static volatile uint32_t scratch;
        uint32_t pa_addr = (uint32_t)&scratch;
        uint32_t va_addr = VA_KERNEL_BASE + (pa_addr - RAM_BASE);

        scratch = 0x11111111U;
        *((volatile uint32_t *)va_addr) = 0xCAFEBABEU;
        if (scratch == 0xCAFEBABEU) {
            uart_printf("[TEST] [PASS] T4 VA alias write: "
                        "wrote 0x%08x via VA 0x%08x, PA readback OK\n",
                        (uint32_t)scratch, va_addr);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T4 VA alias write: PA readback=0x%08x\n",
                        (uint32_t)scratch);
            fail++;
        }
        scratch = 0;
    }

    /* T5 — SVC exception + return (if we crash in handler, kernel hangs) */
    {
        uart_printf("[TEST] T5 triggering SVC #42 ...\n");
        __asm__ volatile("svc #42");
        uart_printf("[TEST] [PASS] T5 SVC handler returned normally\n");
        pass++;
    }

    uart_printf("[TEST] ========== %d passed, %d failed ==========\n\n",
                pass, fail);
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

    mmu_init();

    exception_init();

    run_boot_tests();

    /* Destructive tests — enable ONE at a time, each halts the system */
#define TEST_NONE       0
#define TEST_DATA_ABORT 1
#define TEST_UNDEFINED  2
#define TEST_NULL_DEREF 3

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
#elif EXCEPTION_TEST == TEST_NULL_DEREF
    /* Test NULL guard — with MMU on, VA 0x00000000 is unmapped
     * (entry 0 of boot_pgd is FAULT). Any access must trigger a
     * Data Abort with DFAR = 0x00000000. */
    uart_printf("[TEST] dereferencing NULL (expect Data Abort)...\n");
    *((volatile uint32_t *)0) = 0xDEADBEEF;
#endif

    uart_printf("[BOOT] boot complete — entering idle loop\n");

    /* Halt — scheduler not implemented yet */
    for (;;)
        __asm__ volatile("wfi");
}
