/* ===========================================================
 * user/crt0.S — User-program entry stub
 *
 * The kernel context-switches each process directly to this
 * label (the initial frame's PC = user_entry = 0x40000000 =
 * start of .text). It calls main() then, if main returns,
 * invokes sys_exit so the scheduler retires the process.
 *
 * SP_usr is already set by the kernel (ctx.sp_usr =
 * USER_STACK_TOP); we rely on that and don't re-init it here.
 * =========================================================== */

    .section .text._ustart, "ax"
    .arm
    .global _ustart

_ustart:
    bl      main
    bl      __noreturn_sys_exit

    /* Safety halt — should not be reached. */
1:  b       1b

/* Tiny shim so crt0 stays assembly-only: inline asm svc. */
    .global __noreturn_sys_exit
__noreturn_sys_exit:
    mov     r7, #3              @ SYS_EXIT — must match syscall.h
    svc     #0
2:  b       2b
