#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

/* ============================================================
 * syscall.h — Kernel entry for user processes
 *
 * ABI (matches Linux ARM EABI):
 *   r7            = syscall number (SYS_*)
 *   r0..r3        = up to four arguments
 *   svc #0        = trap into kernel
 *   r0 (on return) = result; negative for errors
 *
 * User-side wrappers live in user-space libc; kernel-side
 * dispatch is in kernel/syscall/syscall.c.
 * ============================================================ */

#include <stdint.h>
#include "exception.h"

/* Syscall numbers — values fixed because they are encoded
 * into user binaries; do not renumber. */
#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_EXIT        3
#define SYS_READ        4
#define SYS_PS          5
#define SYS_KILL        6
#define SYS_TICKS       7

#define NR_SYSCALLS     8

/* Called from handle_svc with the preempted user context. Reads
 * r7 for the syscall number and r0..r3 for arguments, writes the
 * result back into r0 on the saved frame so the atomic restore
 * delivers it to the user. */
void syscall_dispatch(exception_context_t *ctx);

#endif /* KERNEL_SYSCALL_H */
