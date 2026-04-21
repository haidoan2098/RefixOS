/* ===========================================================
 * kernel/arch/arm/proc/user_stub.S — Inline user-mode stub
 *
 * Same bytes land at every process's user VA (0x40000000), each
 * backed by distinct physical pages. The stub:
 *   1. Reads its own PID via SYS_GETPID.
 *   2. Patches the 'X' in "[u-X] hello\n" with that pid digit.
 *   3. Emits the line via SYS_WRITE.
 *   4. Burns a ~1M-iteration busy loop so the log is readable.
 *   5. Voluntarily yields via SYS_YIELD and loops.
 *
 * Syscall ABI: r7 = number, r0..r3 = args, r0 = return.
 *
 * Because the whole image is copied byte-for-byte, `adr r1, msg`
 * remains PC-relative and resolves to each process's own copy at
 * runtime — no VA fix-ups needed.
 * =========================================================== */

    .section .user_stub, "ax"
    .arm

    .global user_stub_start
    .global user_stub_end

/* Syscall numbers — must match kernel/include/syscall.h. */
    .equ SYS_WRITE,     0
    .equ SYS_GETPID,    1
    .equ SYS_YIELD,     2

user_stub_start:
.Lloop:
    /* --- 1. getpid ---------------------------------------- */
    mov     r7, #SYS_GETPID
    svc     #0                          @ r0 = pid

    /* --- 2. patch msg[3] with '0' + pid -------------------- */
    add     r0, r0, #'0'
    adr     r1, msg
    strb    r0, [r1, #3]                @ "[u-X] ..."

    /* --- 3. write(1, msg, MSG_LEN) ------------------------- */
    mov     r7, #SYS_WRITE
    mov     r0, #1                      @ fd = stdout
                                        @ r1 = msg (already loaded)
    mov     r2, #MSG_LEN
    svc     #0

    /* --- 4. busy delay so output is readable --------------- */
    ldr     r2, =2000000
.Ldelay:
    subs    r2, r2, #1
    bgt     .Ldelay

    /* --- 5. yield ----------------------------------------- */
    mov     r7, #SYS_YIELD
    svc     #0

    b       .Lloop

msg:
    .ascii "[u-X] hello\n"
    .equ MSG_LEN, . - msg

user_stub_end:
