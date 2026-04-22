/* ===========================================================
 * user/apps/counter/counter.c — User process #0
 *
 * Prints an incrementing counter, yields, repeats. Demonstrates
 * the full user-kernel boundary: getpid + write + yield every
 * iteration go through svc #0.
 * =========================================================== */

#include "ulib.h"

/* ~3 s per print regardless of platform clock speed. Yield while
 * waiting so other processes get CPU during the delay. */
#define DELAY_TICKS     300U    /* 300 × 10 ms = 3 s */

static void delay_wall(unsigned int ticks)
{
    unsigned int start = sys_ticks();
    while ((sys_ticks() - start) < ticks)
        sys_yield();
}

int main(void)
{
    unsigned int count = 0;
    unsigned int prev  = sys_ticks();

    for (;;) {
        unsigned int now = sys_ticks();
        unsigned int dt  = now - prev;                 /* 10 ms units */
        prev = now;

        ulib_tag();
        ulib_puts("count=");
        ulib_putu(count++);
        ulib_puts("  dt=");
        ulib_putu(dt * 10U);                           /* ms */
        ulib_puts("ms\n");

        delay_wall(DELAY_TICKS);
    }

    return 0;   /* unreachable */
}
