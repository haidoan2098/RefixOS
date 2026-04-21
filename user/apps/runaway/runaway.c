/* ===========================================================
 * user/apps/runaway/runaway.c — User process #1
 *
 * Intentionally pathological: burns CPU in a tight loop and
 * never calls sys_yield. The kernel must still preempt this
 * process every 10 ms, otherwise the rest of the system would
 * starve. That is exactly what we want to demonstrate.
 *
 * Every so often it does emit a breadcrumb so the boot log
 * proves it is actually running (and being cut short).
 * =========================================================== */

#include "ulib.h"

/* Bigger than counter's delay so runaway shouts less often but
 * still visible. */
#define LOOP_BUDGET         50000000U

int main(void)
{
    unsigned int spin = 0;

    for (;;) {
        /* Tight inner loop — no syscalls, no yield, nothing
         * the kernel can use to switch us except the timer. */
        volatile unsigned int i;
        for (i = 0; i < LOOP_BUDGET; i++)
            __asm__ volatile("" ::: "memory");

        ulib_tag();
        ulib_puts("runaway tick #");
        ulib_putu(spin++);
        ulib_putc('\n');
        /* still no sys_yield — preemption is the scheduler's job */
    }

    return 0;   /* unreachable */
}
