/* ===========================================================
 * kernel/arch/arm/proc/user_stub.S — Inline user-mode stub
 *
 * A tiny ARM snippet embedded in the kernel image. Phase 1 of
 * process management copies this into each process's 1 MB user
 * physical slot. User VA 0x40000000 in every process maps to the
 * process's own copy.
 *
 * Content is a spin loop — the process never actually runs in
 * this phase (no context switch yet). The mov sets r0 so the
 * future first context switch can visually distinguish "user
 * code entered" from "still in kernel" during debug.
 *
 * The output section is 1 MB-aligned so the LMA naturally sits
 * at a section boundary, simplifying page-table math.
 * =========================================================== */

    .section .user_stub, "ax"
    .arm

    .global user_stub_start
    .global user_stub_end

user_stub_start:
    mov     r0, #1                  @ marker: "user mode reached"
1:  b       1b                      @ spin forever
user_stub_end:
