/* ===========================================================
 * user/apps/counter/counter.c — User process #0
 *
 * Prints an incrementing counter, yields, repeats. Demonstrates
 * the full user-kernel boundary: getpid + write + yield every
 * iteration go through svc #0.
 * =========================================================== */

#include "ulib.h"

/* Rough one-second busy loop on QEMU TCG. Adjust if output is
 * too fast or too slow on hardware. */
#define DELAY_ITERATIONS    2000000U

static void busy_delay(unsigned int n)
{
    volatile unsigned int i;
    for (i = 0; i < n; i++)
        __asm__ volatile("" ::: "memory");
}

int main(void)
{
    unsigned int count = 0;

    for (;;) {
        ulib_tag();
        ulib_puts("count=");
        ulib_putu(count++);
        ulib_putc('\n');

        busy_delay(DELAY_ITERATIONS);
        sys_yield();
    }

    return 0;   /* unreachable */
}
