#ifndef RINGNOVA_ULIB_H
#define RINGNOVA_ULIB_H

/* ===========================================================
 * user/libc/ulib.h — Minimal string/print helpers for user
 *                     programs. No malloc, no FILE*, no errno.
 *
 * Everything builds on top of sys_write.
 * =========================================================== */

#include "syscall.h"

unsigned int ulib_strlen(const char *s);
void         ulib_puts(const char *s);     /* write(1, s, strlen(s)) */
void         ulib_putu(unsigned int v);    /* unsigned decimal       */
void         ulib_putc(char c);            /* single byte            */

/* Print "pid=N " prefix. Useful when multiple processes share
 * a terminal and you want to know which one spoke. */
void         ulib_tag(void);

/* Tiny string helpers for shell parsing. */
int          ulib_strcmp(const char *a, const char *b);
int          ulib_strncmp(const char *a, const char *b, unsigned int n);
int          ulib_atoi(const char *s);    /* stops at first non-digit */

#endif /* RINGNOVA_ULIB_H */
