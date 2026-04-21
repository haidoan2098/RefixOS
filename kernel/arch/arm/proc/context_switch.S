/* ===========================================================
 * kernel/arch/arm/proc/context_switch.S — first user-mode entry
 *
 * void context_switch_to_user(uint32_t pgd_pa,
 *                             uint32_t sp_svc,
 *                             uint32_t spsr,
 *                             uint32_t sp_usr) __attribute__((noreturn));
 *
 * One-way trip from kernel into a process's user mode. Called
 * exactly once from kmain to bootstrap the first process; never
 * returns. Bidirectional save/restore (real context switch) lives
 * in a future iteration that wires into the IRQ exit path.
 *
 * Caller passes the process's pgd_pa, the kernel SP that points
 * at the pre-built initial frame (process_build_initial_frame),
 * the target USR-mode CPSR (0x10), and the banked USR-mode SP.
 *
 * Sequence:
 *   1. Mask IRQ + FIQ for the transition.
 *   2. TTBR0 = pgd_pa | walk-attrs   — boot_pgd lacks user VA
 *      0x40000000; only the per-process PGD has it.
 *   3. TLBIALL + DSB + ISB — drop stale boot_pgd translations.
 *   4. Set banked SP_usr via SYS-mode shortcut (shares USR regs).
 *   5. SVC stack = sp_svc — the 14-word frame to consume.
 *   6. spsr_cxsf = target USR CPSR.
 *   7. ldmfd sp!, {r0-r12, pc}^ — atomic register restore + mode
 *      switch (^ on pc-load copies SPSR -> CPSR).
 * =========================================================== */

    .section .text.ctxsw, "ax"
    .align  4
    .global context_switch_to_user

context_switch_to_user:
    /* r0 = pgd_pa, r1 = sp_svc, r2 = spsr, r3 = sp_usr */

    /* 1. Mask interrupts during the transition. */
    cpsid   if

    /* 2. Swap TTBR0 with walk attributes (S=1, RGN=01, IRGN=01). */
    orr     r0, r0, #0x4A
    mcr     p15, 0, r0, c2, c0, 0       @ TTBR0 = pgd_pa | attrs

    /* 3. Invalidate entire TLB then sync. */
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0       @ TLBIALL
    dsb
    isb

    /* 4. Set banked SP_usr by hopping into SYS mode (shares USR
     *    register file) and back to SVC. */
    cps     #0x1F                       @ SYS mode
    mov     sp, r3                      @ SP_usr = sp_usr
    cps     #0x13                       @ back to SVC

    /* 5. Point SVC SP at the pre-built initial frame's r0 slot. */
    mov     sp, r1

    /* 6. Stage target CPSR for the atomic mode switch below. */
    msr     spsr_cxsf, r2

    /* 7. Atomic restore: pop r0-r12 + pc, AND copy SPSR -> CPSR.
     *    The '^' modifier on ldmfd-with-pc applies the SPSR copy
     *    as part of the same instruction, switching to USR mode
     *    with IRQ unmasked exactly when PC moves to user_entry. */
    ldmfd   sp!, {r0-r12, pc}^
