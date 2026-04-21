#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

/* ============================================================
 * proc.h — Process Control Block and process management API
 *
 * Phase 1 of process management: 3 static processes, each with
 *   - its own 16 KB L1 page table (physical isolation real)
 *   - its own 8 KB kernel stack with a pre-built initial frame
 *   - its own 1 MB user physical slot
 *   - all sharing the user VA window 0x40000000 (different PA)
 *
 * No scheduler and no context switch yet — the PCBs are dressed
 * and ready for the next chapter to drop in.
 *
 * Dependencies: board.h (layout constants), mmu.h (page tables)
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include "board.h"

typedef enum {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

/* Saved CPU context. Layout MUST match what the future
 * context_switch.S pops:
 *
 *   msr    spsr_cxsf, ctx.spsr
 *   ldr    sp, =ctx.sp_svc
 *   ldmfd  sp!, {r0-r12, pc}^
 *
 * Field order here is documentary; the actual initial frame on
 * the kernel stack (built by process_build_initial_frame) is
 * what the restore sequence consumes. */
typedef struct {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11, r12;
    uint32_t sp_svc;        /* kernel SP — points at initial frame */
    uint32_t lr_svc;
    uint32_t spsr;          /* CPSR to restore on return to user   */
    uint32_t sp_usr;        /* banked USR mode SP                  */
    uint32_t lr_usr;
} proc_context_t;

typedef struct process {
    proc_context_t  ctx;            /* offset 0 — assembly-friendly */

    uint32_t        pid;
    task_state_t    state;
    const char     *name;

    uint32_t       *pgd;            /* VA of this process's L1 table   */
    uint32_t        pgd_pa;         /* PA for future TTBR0 swap        */

    void           *kstack_base;    /* low addr of the 8 KB region     */
    uint32_t        kstack_size;

    uint32_t        user_entry;     /* = USER_VIRT_BASE                */
    uint32_t        user_stack_top; /* = USER_VIRT_BASE + 1 MB         */
    uint32_t        user_phys_base; /* per-process physical slot       */
} process_t;

/* Public table + cursor — populated by process_init_all() */
extern process_t  processes[NUM_PROCESSES];
extern process_t *current;

/* Construct all NUM_PROCESSES PCBs:
 *   - allocate per-process L1 table + kernel stack (static BSS)
 *   - copy user_stub into each process's user PA slot
 *   - populate L1 table via pgtable_build_for_proc
 *   - pre-build initial kernel stack frame for user-mode entry
 *   - set current = &processes[0]
 * Safe to call once after mmu_init(). */
void process_init_all(void);

/* Pretty-print one PCB over UART. Debug only. */
void process_dump(const process_t *p);

/* context_switch — implemented in kernel/arch/arm/proc/context_switch.S.
 *
 * Saves prev's kernel state onto prev's SVC stack (callee-saved
 * GPRs + lr + banked SP_usr/LR_usr) and loads the equivalent for
 * next, then returns via bx lr — landing in whatever kernel code
 * next was last executing when it yielded the CPU.
 *
 * prev == NULL means "first-time entry" — no save side; caller
 * (kmain) never needs to return. next's initial kernel stack is
 * pre-built so the bx lr at the epilogue jumps to
 * ret_from_first_entry, which pops the 16-word IRQ-exit frame
 * and enters USR mode. */
void context_switch(struct process *prev, struct process *next);

/* Bootstrap helper: kick off the first process from kmain. Never
 * returns. Equivalent to context_switch(NULL, &processes[0]). */
static inline void __attribute__((noreturn))
process_first_run(struct process *first)
{
    context_switch((void *)0, first);
    for (;;) { /* unreachable */ }
}

/* Struct offsets used by context_switch.S — must stay in sync.
 * _Static_assert below guards accidental layout drift. */
#define CTX_SP_SVC_OFFSET       52
#define CTX_LR_SVC_OFFSET       56
#define CTX_SPSR_OFFSET         60
#define CTX_SP_USR_OFFSET       64
#define CTX_LR_USR_OFFSET       68
#define PROC_PGD_PA_OFFSET      88

_Static_assert(offsetof(proc_context_t, sp_svc) == CTX_SP_SVC_OFFSET,
               "ctx.sp_svc offset drifted");
_Static_assert(offsetof(proc_context_t, sp_usr) == CTX_SP_USR_OFFSET,
               "ctx.sp_usr offset drifted");
_Static_assert(offsetof(proc_context_t, lr_usr) == CTX_LR_USR_OFFSET,
               "ctx.lr_usr offset drifted");
_Static_assert(offsetof(process_t, pgd_pa)    == PROC_PGD_PA_OFFSET,
               "process.pgd_pa offset drifted");

#endif /* KERNEL_PROC_H */
