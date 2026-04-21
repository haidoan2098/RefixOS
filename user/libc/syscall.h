#ifndef RINGNOVA_USER_SYSCALL_H
#define RINGNOVA_USER_SYSCALL_H

/* ===========================================================
 * user/libc/syscall.h — User-side syscall wrappers
 *
 * ABI matches kernel/include/syscall.h:
 *   r7  = syscall number
 *   r0..r3 = arguments
 *   svc #0
 *   r0 = return value
 *
 * All wrappers are inline; each syscall compiles to a handful
 * of mov/svc instructions per call site.
 * =========================================================== */

/* Tier-1 syscall numbers. MUST match kernel/include/syscall.h. */
#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_EXIT        3

typedef int           int32_t_;
typedef unsigned int  size_t_;

static inline int sys_write(int fd, const char *buf, unsigned int len)
{
    register int          r0 __asm__("r0") = fd;
    register const char  *r1 __asm__("r1") = buf;
    register unsigned int r2 __asm__("r2") = len;
    register int          r7 __asm__("r7") = SYS_WRITE;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7)
                     : "memory");
    return r0;
}

static inline int sys_getpid(void)
{
    register int r0 __asm__("r0");
    register int r7 __asm__("r7") = SYS_GETPID;
    __asm__ volatile("svc #0"
                     : "=r"(r0)
                     : "r"(r7));
    return r0;
}

static inline void sys_yield(void)
{
    register int r7 __asm__("r7") = SYS_YIELD;
    __asm__ volatile("svc #0"
                     :
                     : "r"(r7)
                     : "memory");
}

static inline void __attribute__((noreturn)) sys_exit(void)
{
    register int r7 __asm__("r7") = SYS_EXIT;
    __asm__ volatile("svc #0"
                     :
                     : "r"(r7));
    for (;;) { /* unreachable */ }
}

#endif /* RINGNOVA_USER_SYSCALL_H */
