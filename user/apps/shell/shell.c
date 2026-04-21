/* ===========================================================
 * user/apps/shell/shell.c — User process #2 (placeholder)
 *
 * Full interactive shell lives in chapter 09 once sys_read /
 * sys_ps / sys_kill land. For now this process just idles
 * cooperatively so the kernel has three real programs on its
 * run-queue — good enough to visualise round-robin with three
 * distinct producers in the boot log.
 * =========================================================== */

#include "ulib.h"

#define HEARTBEAT_BUDGET    40000000U

int main(void)
{
    unsigned int beat = 0;

    for (;;) {
        volatile unsigned int i;
        for (i = 0; i < HEARTBEAT_BUDGET; i++)
            __asm__ volatile("" ::: "memory");

        ulib_tag();
        ulib_puts("shell placeholder beat=");
        ulib_putu(beat++);
        ulib_putc('\n');

        sys_yield();
    }

    return 0;   /* unreachable */
}
