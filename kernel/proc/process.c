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

/* User-program images (embedded via .incbin in
 * kernel/arch/arm/proc/user_binaries.S). One entry per pid. */
extern uint8_t _counter_img_start[], _counter_img_end[];
extern uint8_t _runaway_img_start[], _runaway_img_end[];
extern uint8_t _shell_img_start[],   _shell_img_end[];

typedef struct {
    const char    *name;
    const uint8_t *start;
    const uint8_t *end;
} user_image_t;

static const user_image_t user_images[NUM_PROCESSES] = {
    { "counter", _counter_img_start, _counter_img_end },
    { "runaway", _runaway_img_start, _runaway_img_end },
    { "shell",   _shell_img_start,   _shell_img_end   },
};

/* Trampoline landed on by context_switch's bx lr for first-time
 * entries. Defined in kernel/arch/arm/exception/exception_entry.S.
 * Pops the 16-word IRQ-exit frame and enters USR mode via rfefd. */
extern void ret_from_first_entry(void);

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
 * process_build_initial_frame — pre-construct two stacked frames
 * so that context_switch(NULL|prev, p) lands the process in USR
 * mode at user_entry using the same code path a preempted resume
 * uses.
 *
 * Stack layout (low→high; stack grows down, sp_svc points low):
 *
 *   [+0x00] r4   = 0         \
 *   [+0x04] r5   = 0          |
 *   [+0x08] r6   = 0          |
 *   [+0x0C] r7   = 0          |  9-word kernel-resume frame.
 *   [+0x10] r8   = 0          |  context_switch's epilogue does
 *   [+0x14] r9   = 0          |    ldmfd sp!, {r4-r11, lr}; bx lr
 *   [+0x18] r10  = 0          |  which pops these 9 words and
 *   [+0x1C) r11  = 0          |  transfers control to lr below.
 *   [+0x20] lr   = ret_from_first_entry  /
 *
 *   [+0x24] r0   = 0         \
 *     ...                     |
 *   [+0x54] r12  = 0          |  16-word IRQ-exit frame that
 *   [+0x58] svc_lr = 0        |  ret_from_first_entry drains via
 *   [+0x5C] pc   = user_entry |    ldmfd sp!, {r0-r12, lr}
 *   [+0x60] cpsr = 0x10       |    rfefd sp!
 *                             /
 *
 * sp_svc = start of the 9-word kernel-resume frame. Total 25
 * words (100 bytes) reserved at the top of the 8 KB kstack.
 * ----------------------------------------------------------- */
#define KERNEL_RESUME_WORDS  9U     /* r4-r11 + lr */
#define USER_EXIT_WORDS      16U    /* r0-r12 + svc_lr + pc + cpsr */
#define INIT_STACK_WORDS     (KERNEL_RESUME_WORDS + USER_EXIT_WORDS)

static void process_build_initial_frame(process_t *p)
{
    uint32_t top = (uint32_t)p->kstack_base + p->kstack_size;
    uint32_t *frame = (uint32_t *)(top - INIT_STACK_WORDS * 4U);

    /* Kernel-resume frame (indices 0..8). */
    for (uint32_t i = 0; i < 8; i++)
        frame[i] = 0;                              /* r4..r11 */
    frame[8] = (uint32_t)&ret_from_first_entry;    /* lr */

    /* IRQ-exit frame (indices 9..24). */
    for (uint32_t i = 0; i < 13; i++)
        frame[9 + i] = 0;                          /* r0..r12 */
    frame[22] = 0;                                 /* svc_lr placeholder */
    frame[23] = p->user_entry;                     /* pc */
    frame[24] = 0x10U;                             /* cpsr — USR, I=0 F=0 */

    /* ctx fields consumed by context_switch.S. */
    p->ctx.sp_svc = (uint32_t)frame;
    p->ctx.lr_svc = 0;
    p->ctx.spsr   = 0x10U;          /* mirror of frame[24] — documentary */
    p->ctx.sp_usr = USER_STACK_TOP;
    p->ctx.lr_usr = 0;
}

/* -----------------------------------------------------------
 * process_init_all — build all NUM_PROCESSES PCBs
 * ----------------------------------------------------------- */
void process_init_all(void)
{
    for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
        process_t          *p   = &processes[i];
        const user_image_t *img = &user_images[i];
        uint32_t            img_size = (uint32_t)(img->end - img->start);

        /* Clear PCB — static storage is already zero, but be explicit */
        kmemset(p, 0, sizeof(*p));

        p->pid         = i;
        p->state       = TASK_READY;
        p->name        = img->name;

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

        /* Copy this process's user image into its PA slot. Reach
         * the PA through the high-VA alias (PA + PHYS_OFFSET) so
         * we never touch identity mapping. */
        void *user_va = (void *)(p->user_phys_base + PHYS_OFFSET);
        kmemset(user_va, 0, USER_REGION_SIZE);
        kmemcpy(user_va, img->start, img_size);

        /* Build the per-process L1 table: kernel mirror + user
         * section at 0x40000000 → p->user_phys_base */
        pgtable_build_for_proc(p->pgd, p->user_phys_base);

        /* Pre-build the first kernel stack frame */
        process_build_initial_frame(p);

        uart_printf("[PROC] pid=%u name=%s pgd=0x%08x "
                    "kstack=0x%08x user_pa=0x%08x img=%u bytes\n",
                    p->pid, p->name, (uint32_t)p->pgd,
                    (uint32_t)p->kstack_base, p->user_phys_base,
                    img_size);
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
