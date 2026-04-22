/* ===========================================================
 * user/apps/runaway/runaway.c — User process #1 (silent hog)
 *
 * Runs an unbounded busy loop and never calls sys_yield. The
 * kernel's 10 ms timer IRQ must still preempt it, otherwise
 * counter and shell would starve. Verification is `ps` — the
 * process stays READY / RUNNING regardless of how long it
 * spins.
 *
 * Deliberately silent: interactive demos need a clean prompt,
 * and a second chatty process would bury shell output. The
 * preemption proof is that counter and shell keep making
 * progress while this process hogs CPU.
 * =========================================================== */

#include "ulib.h"

int main(void)
{
    ulib_tag();
    ulib_puts("runaway started — silent, no sys_yield\n");

    for (;;) {
        volatile unsigned int i;
        for (i = 0; i < 1000000U; i++)
            __asm__ volatile("" ::: "memory");
        /* still no sys_yield — preemption is the scheduler's job */
    }

    return 0;   /* unreachable */
}
