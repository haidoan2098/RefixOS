/* ===========================================================
 * kernel/main.c — Kernel C entry points
 *
 * Boot is split across two C functions, both called from
 * kernel/arch/arm/boot/start.S:
 *
 *   kearly()  — runs at PA (MMU off → on). Initialises UART,
 *               prints the boot banner, then calls mmu_init().
 *               Returns to start.S with MMU on; identity map
 *               keeps PC alive at PA.
 *
 *   kmain()   — runs at VA (after start.S trampolines PC into
 *               the 0xC0... half). Installs vector table, brings
 *               up IRQ + timer + processes, runs self-tests.
 *
 * All pointers/symbols print as VA (0xc01xxxxx); inside kmain
 * the kernel is genuinely executing through the MMU, no longer
 * relying on the identity map.
 * =========================================================== */

#include "drivers/uart/uart.h"
#include "drivers/timer/timer.h"
#include "include/board.h"
#include "include/exception.h"
#include "include/irq.h"
#include "include/mmu.h"
#include "include/proc.h"
#include "include/scheduler.h"

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

    /* T2 — TTBR0 base == PA of boot_pgd.
     * boot_pgd symbol resolves to VA (0xc010xxxx); TTBR0 holds the
     * physical page-table base (0x7010xxxx). Subtract PHYS_OFFSET
     * to compare. */
    {
        uint32_t ttbr0  = mmu_read_ttbr0();
        uint32_t base   = ttbr0 & 0xFFFFC000U;
        uint32_t pgd_pa = (uint32_t)boot_pgd - PHYS_OFFSET;
        if (base == pgd_pa) {
            uart_printf("[TEST] [PASS] T2 TTBR0 base = boot_pgd PA (0x%08x)\n",
                        base);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T2 TTBR0 base 0x%08x != boot_pgd PA 0x%08x\n",
                        base, pgd_pa);
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

    /* T4 — Identity alias still live: write via PA (identity map),
     * read back via the primary VA reference and vice versa. Confirms
     * kmain's PA<->VA aliasing is coherent while the identity map
     * exists (will be torn down by mmu_drop_identity later). */
    {
        static volatile uint32_t scratch;
        uint32_t va_addr = (uint32_t)&scratch;
        uint32_t pa_addr = va_addr - PHYS_OFFSET;

        scratch = 0x11111111U;
        uint32_t pa_readback = *((volatile uint32_t *)pa_addr);

        *((volatile uint32_t *)pa_addr) = 0xCAFEBABEU;
        uint32_t va_readback = scratch;

        if (pa_readback == 0x11111111U && va_readback == 0xCAFEBABEU) {
            uart_printf("[TEST] [PASS] T4 identity alias: VA 0x%08x <-> PA 0x%08x coherent\n",
                        va_addr, pa_addr);
            pass++;
        } else {
            uart_printf("[TEST] [FAIL] T4 identity alias: pa_rd=0x%08x va_rd=0x%08x\n",
                        pa_readback, va_readback);
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

    /* T9 — initial kernel stack = 25 words (9 kernel-resume + 16 IRQ-exit):
     *        [0..7]   r4..r11 (zero, ignored by context_switch epilogue)
     *        [8]      lr = &ret_from_first_entry (trampoline)
     *        [9..21]  r0..r12 (zero)
     *        [22]     svc_lr (zero placeholder)
     *        [23]     user_entry (consumed by rfefd as PC)
     *        [24]     0x10 (consumed by rfefd as CPSR) */
    {
        extern void ret_from_first_entry(void);
        int ok = 1;
        for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
            process_t *p = &processes[i];
            uint32_t *frame = (uint32_t *)p->ctx.sp_svc;
            /* Kernel-resume frame zero check (r4..r11). */
            for (uint32_t r = 0; r < 8 && ok; r++) {
                if (frame[r] != 0) {
                    uart_printf("[TEST] [FAIL] T9 pid=%u kframe[%u]=0x%08x\n",
                                p->pid, r, frame[r]);
                    ok = 0;
                }
            }
            /* IRQ-exit frame r0..r12 zero check. */
            for (uint32_t r = 0; r < 13 && ok; r++) {
                if (frame[9 + r] != 0) {
                    uart_printf("[TEST] [FAIL] T9 pid=%u uframe[r%u]=0x%08x\n",
                                p->pid, r, frame[9 + r]);
                    ok = 0;
                }
            }
            if (frame[8]  != (uint32_t)&ret_from_first_entry
                || frame[22] != 0
                || frame[23] != USER_VIRT_BASE
                || frame[24] != 0x10U
                || p->ctx.sp_usr != USER_STACK_TOP) {
                uart_printf("[TEST] [FAIL] T9 pid=%u lr=0x%08x pc=0x%08x "
                            "cpsr=0x%08x sp_usr=0x%08x\n",
                            p->pid, frame[8], frame[23], frame[24],
                            p->ctx.sp_usr);
                ok = 0;
            }
        }
        if (ok) {
            uart_printf("[TEST] [PASS] T9 initial stack frames "
                        "(25 words, kernel-resume + IRQ-exit)\n");
            pass++;
        } else {
            fail++;
        }
    }

    uart_printf("[TEST] ========== %d passed, %d failed ==========\n\n",
                pass, fail);
}

/*
 * kmain() — kernel C entry point.
 *
 * By the time we get here:
 *   - start.S has computed PHYS_OFFSET and called mmu_init(),
 *     which built boot_pgd at PA and turned the MMU on.
 *   - start.S has trampolined PC into the high-VA alias via
 *     `ldr pc, =_start_va` and re-seated every mode's SP at VA.
 *
 * So UART, exception init, drivers, processes — everything —
 * run with the MMU on and addresses resolving through it.
 */
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
    uart_printf("[BOOT] &kmain   : %p  (expect 0xC01xxxxx)\n",
                (uint32_t)&kmain);

    cpsr = read_cpsr();
    uart_printf("[BOOT] CPSR     : %p (mode=%x, IRQ=%s, FIQ=%s)\n",
                cpsr,
                cpsr & 0x1FU,
                (cpsr & (1U << 7)) ? "masked" : "on",
                (cpsr & (1U << 6)) ? "masked" : "on");

    mmu_print_status();

    exception_init();

    irq_init();
    timer_init(10000);                      /* 10 ms tick */
    irq_register(IRQ_TIMER, timer_irq);
    irq_enable(IRQ_TIMER);
    irq_cpu_enable();
    uart_printf("[IRQ]   CPU IRQ enabled (CPSR.I=0)\n");

    process_init_all();

    run_boot_tests();

    /* Kernel has fully migrated off PA — tear down the identity
     * map so any future stray PA dereference faults immediately. */
    mmu_drop_identity();

    /* Now the PCBs are stable (tests verified T7/T8/T9) and the
     * VA world is tidy — arm scheduler_tick so each timer IRQ
     * flips need_reschedule. Before this point ticks bumped
     * tick_count but schedule() stayed a no-op. */
    timer_set_handler(scheduler_tick);

    /* Destructive tests — enable ONE at a time, each halts the system */
#define TEST_NONE       0
#define TEST_DATA_ABORT 1
#define TEST_UNDEFINED  2
#define TEST_NULL_DEREF 3

#define EXCEPTION_TEST  TEST_NONE

#if EXCEPTION_TEST == TEST_DATA_ABORT
    /* Test Data Abort — do an unaligned 32-bit read with SCTLR.A=1.
     * Note: this block runs AFTER mmu_drop_identity(), so 0x70100001
     * is unmapped in the boot_pgd; the read would also fault as a
     * translation fault regardless of alignment. Either path fires
     * handle_data_abort — use this block to exercise the handler. */
    uart_printf("[TEST] triggering Data Abort (unaligned access)...\n");
    {
        /* Enable alignment checking: SCTLR.A (bit 1) = 1 */
        uint32_t sctlr;
        __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
        sctlr |= (1U << 1);
        __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
        __asm__ volatile("isb");
        /* Read via a VA that guarantees a fault: pick an address in
         * the former identity range and make it unaligned for good
         * measure. DFAR/DFSR will tell us which fault fired. */
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

    uart_printf("[BOOT] boot complete — entering user mode pid=%u "
                "(USR @ 0x%08x)\n",
                processes[0].pid, processes[0].user_entry);

    /* Bootstrap the first process via the shared context_switch
     * path (prev=NULL: no save side, load-only). Once IRQ-driven
     * preemption is wired up, every subsequent switch takes the
     * same route with both pointers non-null. */
    process_first_run(&processes[0]);
    /* noreturn — code below is unreachable */
}
