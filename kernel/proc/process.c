/* ============================================================
 * kernel/proc/process.c — PCB table + static initialisation
 *
 * Phase 1: build 3 static process descriptors. No scheduler,
 * no context switch. After process_init_all() runs, each PCB
 * contains a valid per-process L1 table, a private 1 MB user
 * PA slot (loaded with the inline user_stub), and a pre-built
 * initial kernel stack frame ready for the first context switch
 * to consume — the next chapter plugs the switch in.
 *
 * Memory layout (see docs/memory-architecture.md §1):
 *   proc 0 user PA = RAM_BASE + 0x200000
 *   proc 1 user PA = RAM_BASE + 0x300000
 *   proc 2 user PA = RAM_BASE + 0x400000
 *
 * Dependencies: board.h, proc.h, mmu.h, uart/uart.h
 * ============================================================ */

#include <stdint.h>
#include "board.h"
#include "mmu.h"
#include "proc.h"
#include "uart/uart.h"

/* -----------------------------------------------------------
 * Static backing storage
 *
 * proc_pgd: each process's 16 KB L1 table. The linker places
 *   the whole array at 16 KB alignment via section .bss.proc_pgd
 *   (see kernel/linker/kernel_*.ld) — so proc_pgd[i] for every i
 *   is naturally 16 KB aligned because each row is 16 KB.
 *
 * proc_kstack: 8 KB kernel stack per process. 8-byte alignment
 *   is the ARM EABI requirement.
 * ----------------------------------------------------------- */
static uint32_t proc_pgd[NUM_PROCESSES][PGD_ENTRIES]
    __attribute__((aligned(PGD_ALIGN)))
    __attribute__((section(".bss.proc_pgd")));

static uint8_t proc_kstack[NUM_PROCESSES][KSTACK_SIZE]
    __attribute__((aligned(8)));

/* User stub bounds — provided by user_stub.S via linker */
extern uint8_t user_stub_start[];
extern uint8_t user_stub_end[];

/* Public PCB array + current cursor */
process_t  processes[NUM_PROCESSES];
process_t *current;

/* -----------------------------------------------------------
 * Minimal memcpy/memset — no libc in the kernel. Only used at
 * boot by process_init_all(), so don't bother optimising.
 * ----------------------------------------------------------- */
static void kmemcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void kmemset(void *dst, uint8_t v, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++)
        d[i] = v;
}

/* -----------------------------------------------------------
 * process_build_initial_frame — pre-construct the first frame
 * on the process's kernel stack so a future context_switch can
 * just do:
 *     msr   spsr_cxsf, ctx.spsr
 *     mov   sp, ctx.sp_svc
 *     ldmfd sp!, {r0-r12, pc}^
 * and land in user mode at user_entry with IRQ unmasked.
 *
 * Frame layout (14 words, low→high — ldmfd pops ascending):
 *   [+0x00] r0  = 0
 *   [+0x04] r1  = 0
 *     ...
 *   [+0x30] r12 = 0
 *   [+0x34] pc  = user_entry
 *
 * sp_svc points at r0 slot. Frame sits at the top of the 8 KB
 * kernel stack (stacks grow down).
 * ----------------------------------------------------------- */
static void process_build_initial_frame(process_t *p)
{
    uint32_t top = (uint32_t)p->kstack_base + p->kstack_size;
    uint32_t *frame = (uint32_t *)(top - 14U * 4U);

    for (uint32_t i = 0; i < 13; i++)
        frame[i] = 0;               /* r0..r12 */
    frame[13] = p->user_entry;      /* pc */

    /* ctx starts zero from BSS; set only fields that matter */
    p->ctx.sp_svc = (uint32_t)frame;
    p->ctx.lr_svc = 0;
    p->ctx.spsr   = 0x10U;          /* USR mode, I=0, F=0, T=0 */
    p->ctx.sp_usr = USER_STACK_TOP;
    p->ctx.lr_usr = 0;
}

/* -----------------------------------------------------------
 * Human-readable names — debug only, static lifetime.
 * ----------------------------------------------------------- */
static const char *const proc_names[NUM_PROCESSES] = {
    "counter",
    "runaway",
    "shell",
};

/* -----------------------------------------------------------
 * process_init_all — build all NUM_PROCESSES PCBs
 * ----------------------------------------------------------- */
void process_init_all(void)
{
    uint32_t stub_size = (uint32_t)(user_stub_end - user_stub_start);

    for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
        process_t *p = &processes[i];

        /* Clear PCB — static storage is already zero, but be explicit */
        kmemset(p, 0, sizeof(*p));

        p->pid         = i;
        p->state       = TASK_READY;
        p->name        = proc_names[i];

        p->pgd         = proc_pgd[i];
        /* pgd is the VA pointer (linker symbol); pgd_pa is what
         * the MMU needs in TTBR0. Since the kernel image is linked
         * at VMA 0xC0..., proc_pgd[i]'s VA is high and its PA is
         * one PHYS_OFFSET below. */
        p->pgd_pa      = (uint32_t)proc_pgd[i] - PHYS_OFFSET;

        p->kstack_base = proc_kstack[i];
        p->kstack_size = KSTACK_SIZE;

        p->user_entry     = USER_VIRT_BASE;
        p->user_stack_top = USER_STACK_TOP;
        p->user_phys_base = USER_PHYS_BASE + i * USER_PHYS_STRIDE;

        /* Drop a fresh copy of the user stub into this process's
         * physical slot. Kernel runs at high VA, so we reach the
         * user PA through the high-VA alias (PA + PHYS_OFFSET)
         * rather than via the identity map — the identity range
         * will be torn down once all boot-time PA touches are gone. */
        void *user_va = (void *)(p->user_phys_base + PHYS_OFFSET);
        kmemset(user_va, 0, USER_REGION_SIZE);
        kmemcpy(user_va, user_stub_start, stub_size);

        /* Build the per-process L1 table: kernel mirror + user
         * section at 0x40000000 → p->user_phys_base */
        pgtable_build_for_proc(p->pgd, p->user_phys_base);

        /* Pre-build the first kernel stack frame */
        process_build_initial_frame(p);

        uart_printf("[PROC] pid=%u name=%s pgd=0x%08x "
                    "kstack=0x%08x user_pa=0x%08x\n",
                    p->pid, p->name, (uint32_t)p->pgd,
                    (uint32_t)p->kstack_base, p->user_phys_base);
    }

    current = &processes[0];
}

/* -----------------------------------------------------------
 * process_dump — one-PCB debug print
 * ----------------------------------------------------------- */
void process_dump(const process_t *p)
{
    static const char *const state_name[] = {
        "READY", "RUNNING", "BLOCKED", "DEAD"
    };

    uart_printf("  pid=%u name=%s state=%s\n",
                p->pid, p->name, state_name[p->state]);
    uart_printf("    pgd       = 0x%08x (pa 0x%08x)\n",
                (uint32_t)p->pgd, p->pgd_pa);
    uart_printf("    kstack    = 0x%08x..0x%08x\n",
                (uint32_t)p->kstack_base,
                (uint32_t)p->kstack_base + p->kstack_size);
    uart_printf("    user VA   = 0x%08x entry, stack_top 0x%08x\n",
                p->user_entry, p->user_stack_top);
    uart_printf("    user PA   = 0x%08x\n", p->user_phys_base);
    uart_printf("    ctx.sp    = 0x%08x spsr=0x%08x\n",
                p->ctx.sp_svc, p->ctx.spsr);
}
