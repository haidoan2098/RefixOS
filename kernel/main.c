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
#include "drivers/timer/timer.h"
#include "include/board.h"
#include "include/exception.h"
#include "include/irq.h"
#include "include/mmu.h"
#include "include/proc.h"

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

    /* T6 — timer IRQ delivery verified by spinning until several ticks
     * are observed. Bail out after a generous iteration cap so a dead
     * timer fails fast instead of hanging the boot tests. */
    {
        const uint32_t TARGET = 5;
        const uint32_t SPIN_MAX = 200000000U;
        uint32_t t0 = timer_get_ticks();
        uint32_t delta = 0;
        for (volatile uint32_t i = 0; i < SPIN_MAX; i++) {
            delta = timer_get_ticks() - t0;
            if (delta >= TARGET)
                break;
        }
        if (delta >= TARGET) {
            uart_printf("[TEST] [PASS] T6 timer ticks: %u observed\n", delta);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T6 ticks=%u (target %u)\n",
                        delta, TARGET);
            fail++;
        }
    }

    /* T7 — PCB array populated correctly */
    {
        int ok = 1;
        for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
            process_t *p = &processes[i];
            uint32_t expect_upa = USER_PHYS_BASE + i * USER_PHYS_STRIDE;
            if (p->pid != i || p->state != TASK_READY || p->name == 0
                || p->pgd == 0 || ((uint32_t)p->pgd & 0x3FFFU) != 0
                || p->user_phys_base != expect_upa) {
                uart_printf("[TEST] [FAIL] T7 pid=%u state=%u pgd=0x%08x "
                            "upa=0x%08x (expect 0x%08x)\n",
                            p->pid, p->state, (uint32_t)p->pgd,
                            p->user_phys_base, expect_upa);
                ok = 0;
            }
        }
        if (ok) {
            uart_printf("[TEST] [PASS] T7 PCB array (%u processes)\n",
                        NUM_PROCESSES);
            pass++;
        } else {
            fail++;
        }
    }

    /* T8 — per-process L1 table mappings */
    {
        int ok = 1;
        uint32_t seen_user_pde[NUM_PROCESSES];
        for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
            process_t *p = &processes[i];
            uint32_t pde_null = p->pgd[0];
            uint32_t pde_user = p->pgd[USER_VIRT_BASE >> 20];
            uint32_t pde_kern = p->pgd[VA_KERNEL_BASE >> 20];

            uint32_t user_base = pde_user & 0xFFF00000U;
            uint32_t is_section = (pde_user & 0x3U) == PDE_TYPE_SECTION;
            uint32_t ap_user_rw = (pde_user & (PDE_AP0 | PDE_AP1))
                                  == (PDE_AP0 | PDE_AP1);
            uint32_t xn_clear  = (pde_user & PDE_XN) == 0;
            uint32_t ap2_clear = (pde_user & PDE_AP2) == 0;

            if (pde_null != 0 || !is_section
                || user_base != (p->user_phys_base & 0xFFF00000U)
                || !ap_user_rw || !xn_clear || !ap2_clear
                || pde_kern != boot_pgd[VA_KERNEL_BASE >> 20]) {
                uart_printf("[TEST] [FAIL] T8 pid=%u null=0x%08x "
                            "user=0x%08x kern=0x%08x (boot=0x%08x)\n",
                            p->pid, pde_null, pde_user, pde_kern,
                            boot_pgd[VA_KERNEL_BASE >> 20]);
                ok = 0;
            }
            seen_user_pde[i] = pde_user;
        }
        /* All 3 user PDEs must differ — proves per-process PA isolation */
        if (seen_user_pde[0] == seen_user_pde[1]
            || seen_user_pde[1] == seen_user_pde[2]
            || seen_user_pde[0] == seen_user_pde[2]) {
            uart_printf("[TEST] [FAIL] T8 user PDEs not distinct: "
                        "%08x %08x %08x\n",
                        seen_user_pde[0], seen_user_pde[1], seen_user_pde[2]);
            ok = 0;
        }
        if (ok) {
            uart_printf("[TEST] [PASS] T8 L1 mappings (null+user+kern OK, "
                        "3 distinct user PAs)\n");
            pass++;
        } else {
            fail++;
        }
    }

    /* T9 — initial kernel stack frame is laid out for user-mode entry */
    {
        int ok = 1;
        for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
            process_t *p = &processes[i];
            uint32_t *frame = (uint32_t *)p->ctx.sp_svc;
            for (uint32_t r = 0; r < 13 && ok; r++) {
                if (frame[r] != 0) {
                    uart_printf("[TEST] [FAIL] T9 pid=%u frame[%u]=0x%08x\n",
                                p->pid, r, frame[r]);
                    ok = 0;
                }
            }
            if (frame[13] != USER_VIRT_BASE
                || p->ctx.spsr != 0x10U
                || p->ctx.sp_usr != USER_STACK_TOP) {
                uart_printf("[TEST] [FAIL] T9 pid=%u pc=0x%08x spsr=0x%08x "
                            "sp_usr=0x%08x\n",
                            p->pid, frame[13], p->ctx.spsr, p->ctx.sp_usr);
                ok = 0;
            }
        }
        if (ok) {
            uart_printf("[TEST] [PASS] T9 initial stack frames "
                        "(pc=USER_VIRT_BASE, spsr=0x10)\n");
            pass++;
        } else {
            fail++;
        }
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

    irq_init();
    timer_init(10000);                      /* 10 ms tick */
    irq_register(IRQ_TIMER, timer_irq);
    irq_enable(IRQ_TIMER);
    irq_cpu_enable();
    uart_printf("[IRQ]   CPU IRQ enabled (CPSR.I=0)\n");

    process_init_all();

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
