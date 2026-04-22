/* ============================================================
 * kernel/syscall/syscall.c — Syscall dispatch + handlers
 *
 * handle_svc (exception_handlers.c) calls syscall_dispatch with
 * the preempted user context. We read r7 as the syscall number,
 * r0..r3 as arguments, route through syscall_table, and store
 * the return value back into ctx->r[0] so exception_entry_svc's
 * atomic restore delivers it to the user.
 *
 * Any pointer received from user space must be validated before
 * the kernel dereferences it — user VA is restricted to the 1 MB
 * window [USER_VIRT_BASE, USER_VIRT_BASE + USER_REGION_SIZE).
 *
 * Dependencies: syscall.h, exception.h, proc.h, scheduler.h,
 *               uart/uart.h, board.h
 * ============================================================ */

#include <stdint.h>
#include "board.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "exception.h"
#include "platform.h"
#include "proc.h"
#include "scheduler.h"
#include "syscall.h"

/* Negative return codes — kept small; user-side libc maps these
 * to errno-style values. */
#define E_BADCALL   -1
#define E_FAULT     -2

/* ------------------------------------------------------------
 * User-pointer validator
 *
 * Accepts [p, p+len) iff it lies entirely inside the per-process
 * 1 MB user window and the arithmetic does not overflow.
 * ------------------------------------------------------------ */
static int valid_user_ptr(const void *p, uint32_t len)
{
    uint32_t addr = (uint32_t)p;
    uint32_t end  = addr + len;

    if (end < addr)                           return 0;  /* overflow */
    if (addr < USER_VIRT_BASE)                return 0;
    if (end > USER_VIRT_BASE + USER_REGION_SIZE) return 0;
    return 1;
}

/* ------------------------------------------------------------
 * Handlers — each returns `long` so we can encode errors as
 * negative values alongside positive results. Kernel sign-
 * extends/narrows into ctx->r[0] in the dispatcher.
 * ------------------------------------------------------------ */

/* sys_write(fd, buf, len) — fd ignored (only UART output exists).
 * Returns number of bytes written, or E_FAULT for bad buf. */
static long sys_write(uint32_t fd, uint32_t buf_addr, uint32_t len,
                      uint32_t unused)
{
    (void)fd;
    (void)unused;

    if (!valid_user_ptr((void *)buf_addr, len))
        return E_FAULT;

    const char *buf = (const char *)buf_addr;
    for (uint32_t i = 0; i < len; i++)
        uart_putc(buf[i]);

    return (long)len;
}

static long sys_getpid(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return current ? (long)current->pid : -1;
}

/* sys_yield — hand the CPU over immediately. Flag is consumed by
 * schedule() in handle_svc's tail (not at the next timer tick). */
static long sys_yield(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    scheduler_request_resched();
    return 0;
}

/* sys_exit — mark the caller DEAD and force an immediate switch.
 * Scheduler skips DEAD processes, so the kernel never returns to
 * this process's saved user frame. */
static long sys_exit(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    if (current) {
        uart_printf("[EXIT]  pid=%u name=%s\n",
                    current->pid, current->name);
        current->state = TASK_DEAD;
    }
    scheduler_request_resched();
    return 0;
}

/* sys_read(fd, buf, len) — blocking read from the UART.
 *
 * Returns as soon as at least one byte is available; callers that
 * want a whole line loop. When the ring is empty the caller is
 * parked with scheduler_block_on_input(); the UART RX IRQ path
 * runs scheduler_wake_reader(), the scheduler switches us back in
 * and we resume the copy loop.
 *
 * fd ignored — UART is the only input source. */
static long sys_read(uint32_t fd, uint32_t buf_addr, uint32_t len,
                     uint32_t unused)
{
    (void)fd;
    (void)unused;

    if (len == 0)
        return 0;
    if (!valid_user_ptr((void *)buf_addr, len))
        return E_FAULT;

    char    *buf = (char *)buf_addr;
    uint32_t n   = 0;

    while (n < len) {
        int c = uart_rx_pop();
        if (c < 0) {
            if (n > 0)
                break;                  /* return what we have */
            scheduler_block_on_input();
            continue;                   /* retry after wake-up */
        }
        buf[n++] = (char)c;
    }

    return (long)n;
}

/* sys_ps(buf, size) — copy a text listing of every process into
 * the user's buffer. Each line looks like "0 counter READY\n".
 * Returns number of bytes written, or E_FAULT for bad buf.       */
static long sys_ps(uint32_t buf_addr, uint32_t size,
                   uint32_t unused0, uint32_t unused1)
{
    (void)unused0; (void)unused1;

    if (!valid_user_ptr((void *)buf_addr, size))
        return E_FAULT;

    static const char *const state_name[] = {
        "READY  ", "RUNNING", "BLOCKED", "DEAD   ",
    };

    char    *out = (char *)buf_addr;
    uint32_t n   = 0;

    for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
        process_t *p = &processes[i];
        const char *st = p->state < 4 ? state_name[p->state] : "???";
        const char *nm = p->name ? p->name : "?";

        /* Compose "<pid> <name> <state>\n" by hand — no snprintf. */
        if (n + 1 >= size) break;
        out[n++] = '0' + (char)p->pid;
        if (n + 1 >= size) break;
        out[n++] = ' ';

        for (const char *c = nm; *c && n + 1 < size; c++)
            out[n++] = *c;
        if (n + 1 >= size) break;
        out[n++] = ' ';

        for (const char *c = st; *c && n + 1 < size; c++)
            out[n++] = *c;
        if (n + 1 >= size) break;
        out[n++] = '\n';
    }

    return (long)n;
}

/* sys_kill(pid) — mark processes[pid] DEAD. Returns 0 on success,
 * E_BADCALL if pid out of range. Killing self is allowed and
 * triggers an immediate reschedule (scheduler skips DEAD).      */
static long sys_kill(uint32_t pid, uint32_t unused0,
                     uint32_t unused1, uint32_t unused2)
{
    (void)unused0; (void)unused1; (void)unused2;

    if (pid >= NUM_PROCESSES)
        return E_BADCALL;

    process_t *p = &processes[pid];
    if (p->state == TASK_DEAD)
        return 0;                       /* already gone — idempotent */

    uart_printf("[KILL] pid=%u name=%s killed by pid=%u via sys_kill\n",
                p->pid, p->name, current ? current->pid : 99U);

    /* If the reader was sleeping on input, pull it off the wait
     * slot before marking DEAD so the next UART IRQ doesn't try
     * to wake a dead process. Handled implicitly via state check
     * in scheduler_wake_reader — but clear via a quick reset. */
    p->state = TASK_DEAD;
    scheduler_request_resched();
    return 0;
}

/* sys_ticks — return kernel tick_count (10 ms granularity). Useful
 * for coarse wall-clock measurement from user space.              */
static long sys_ticks(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return (long)timer_get_ticks();
}

/* ------------------------------------------------------------
 * Dispatch table — index by syscall number.
 * ------------------------------------------------------------ */
typedef long (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);

static const syscall_fn_t syscall_table[NR_SYSCALLS] = {
    [SYS_WRITE]  = sys_write,
    [SYS_GETPID] = sys_getpid,
    [SYS_YIELD]  = sys_yield,
    [SYS_EXIT]   = sys_exit,
    [SYS_READ]   = sys_read,
    [SYS_PS]     = sys_ps,
    [SYS_KILL]   = sys_kill,
    [SYS_TICKS]  = sys_ticks,
};

void syscall_dispatch(exception_context_t *ctx)
{
    uint32_t num = ctx->r[7];

    if (num >= NR_SYSCALLS || syscall_table[num] == (syscall_fn_t)0) {
        ctx->r[0] = (uint32_t)E_BADCALL;
        return;
    }

    long ret = syscall_table[num](ctx->r[0], ctx->r[1],
                                  ctx->r[2], ctx->r[3]);
    ctx->r[0] = (uint32_t)ret;
}
