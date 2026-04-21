/* ===========================================================
 * user/libc/ulib.c — User-space helper functions
 *
 * Pure user-mode code; everything routes through sys_write.
 * =========================================================== */

#include "ulib.h"

unsigned int ulib_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

void ulib_putc(char c)
{
    sys_write(1, &c, 1);
}

void ulib_puts(const char *s)
{
    sys_write(1, s, ulib_strlen(s));
}

void ulib_putu(unsigned int v)
{
    char buf[11];                /* enough for 32-bit unsigned */
    int  i = 0;

    if (v == 0) {
        ulib_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    /* buf now holds digits least-significant first — flip */
    while (--i >= 0)
        ulib_putc(buf[i]);
}

void ulib_tag(void)
{
    ulib_puts("[pid ");
    ulib_putu((unsigned int)sys_getpid());
    ulib_puts("] ");
}
