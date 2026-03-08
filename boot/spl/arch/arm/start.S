/* ============================================================
 * boot/spl/arch/arm/start.S
 * SPL Entry Point for AM335x (BeagleBone Black)
 *
 * AM335x ROM Code loads this binary (MLO) into internal SRAM
 * at 0x402F0400 and jumps here.
 *
 * Sequence:
 *   1. Vector table (must be at load address — ROM expects it)
 *   2. Mask IRQ/FIQ
 *   3. Setup exception mode stacks (all in SRAM)
 *   4. Disable WDT1 (ROM enables it ~3min timeout)
 *   5. Set VBAR to vector table base
 *   6. Zero BSS
 *   7. bl bootloader_main — never returns
 * ============================================================ */

    /* Vector table — must be first, aligned to 32 bytes
     * ROM Code jumps to beginning of MLO image which is here.
     * Each entry is an indirect branch via a literal pool.
     * ARM ARM §B1.8.1 — vector table format */
    .section .vectors, "ax"
    .align 5
    .global _vectors

_vectors:
    ldr     pc, =_start               /* Reset                  */
    ldr     pc, =_undef_handler       /* Undefined Instruction  */
    ldr     pc, =_svc_handler         /* SVC (unused in SPL)    */
    ldr     pc, =_prefetch_handler    /* Prefetch Abort         */
    ldr     pc, =_data_handler        /* Data Abort             */
    ldr     pc, =_reserved_handler    /* Reserved               */
    ldr     pc, =_irq_handler         /* IRQ (disabled in SPL)  */
    ldr     pc, =_fiq_handler         /* FIQ (disabled in SPL)  */

    .section .text.start, "ax"
    .align 4
    .global _start

_start:
    /* -------------------------------------------------------
     * 1. Mask all interrupts at CPU level
     *    Do this before touching any banked register.
     *    CPSR.I=1 → mask IRQ, CPSR.F=1 → mask FIQ
     *    ARM ARM §A2.5 */
    cpsid   if

    /* -------------------------------------------------------
     * 2. Setup per-mode stacks (all in SRAM)
     *    ARMv7-A has banked SP per exception mode.
     *    Switch to each mode, load SP from linker symbol.
     *
     *    Mode bits CPSR[4:0]:
     *      FIQ=0x11, IRQ=0x12, SVC=0x13
     *      ABT=0x17, UND=0x1B, SYS=0x1F
     * ------------------------------------------------------- */

    /* FIQ mode — 512 B */
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x11
    msr     cpsr_c, r0
    ldr     sp, =_fiq_stack_top

    /* IRQ mode — 1 KB */
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x12
    msr     cpsr_c, r0
    ldr     sp, =_irq_stack_top

    /* Abort mode — 1 KB */
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x17
    msr     cpsr_c, r0
    ldr     sp, =_abt_stack_top

    /* Undefined mode — 1 KB */
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x1B
    msr     cpsr_c, r0
    ldr     sp, =_und_stack_top

    /* System mode — 1 KB (privileged user mode, same banked SP as User) */
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x1F
    msr     cpsr_c, r0
    ldr     sp, =_sys_stack_top

    /* SVC (Supervisor) mode — 8 KB: main SPL execution mode */
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x13
    msr     cpsr_c, r0
    ldr     sp, =_svc_stack_top

    /* -------------------------------------------------------
     * 3. Disable Watchdog Timer 1 (WDT1)
     *    ROM Code enables WDT1 with ~3min timeout.
     *    Sequence: write 0xAAAA → poll WWPS[4] → write 0x5555
     *    TRM §20.4.1.8 (WSPR register) */
    ldr     r0, =0x44E35000         /* WDT1 base */
    ldr     r1, =0xAAAA
    str     r1, [r0, #0x48]         /* WDT1_WSPR = 0xAAAA (step 1) */
.Lwdt_poll1:
    ldr     r2, [r0, #0x34]         /* Read WDT1_WWPS */
    tst     r2, #(1 << 4)           /* Check W_PEND_WSPR bit */
    bne     .Lwdt_poll1             /* Wait for write to complete */
    ldr     r1, =0x5555
    str     r1, [r0, #0x48]         /* WDT1_WSPR = 0x5555 (step 2: disable) */
.Lwdt_poll2:
    ldr     r2, [r0, #0x34]
    tst     r2, #(1 << 4)
    bne     .Lwdt_poll2

    /* -------------------------------------------------------
     * 4. Set Vector Base Address Register (VBAR)
     *    Must be after stacks are set up — if an exception fires
     *    before VBAR is set it will go to 0x00000000 (ROM area).
     *    ARM ARM §B3.2.68 */
    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0  /* Write VBAR */
    isb                              /* Flush pipeline — required after VBAR write */

    /* -------------------------------------------------------
     * 5. Zero BSS
     *    C code assumes .bss == 0 at startup.
     *    _bss_start / _bss_end provided by linker. */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
.Lzero_bss:
    cmp     r0, r1
    strlo   r2, [r0], #4            /* Store 0, post-increment 4 bytes */
    blo     .Lzero_bss

    /* -------------------------------------------------------
     * 6. Jump to C entry point
     *    bootloader_main() must never return. */
    bl      bootloader_main

    /* -------------------------------------------------------
     * 7. Safety halt — should never reach here */
.Lhalt:
    b       .Lhalt

/* ============================================================
 * Exception Handlers
 *
 * ARMv7-A ABI: C functions destroy r0-r3, r12, LR (scratch regs).
 * On exception entry, ARM saves previous PC to exception LR (banked).
 * We push {r0-r3, r12, lr} before any C call.
 *
 * Stack frame (grows down, AAPCS):
 *   [LR_exc] [r12] [r3] [r2] [r1] [r0] ← SP after push
 * ============================================================ */

    .section .text.handlers, "ax"
    .align 4

_undef_handler:
    /* UNDEFINED mode (0x1B), LR = address after undefined instr */
    push    {r0-r3, r12, lr}
    bl      c_undef_handler
    pop     {r0-r3, r12, lr}
    movs    pc, lr              /* Restore CPSR from SPSR, return */
1:  b       1b                  /* Unreachable — handler loops forever */

_svc_handler:
    /* SVC — not used in SPL, hang */
    b       _svc_handler

_prefetch_handler:
    /* ABORT mode (0x17), LR = fault address + 4 */
    push    {r0-r3, r12, lr}
    bl      c_prefetch_abort_handler
    pop     {r0-r3, r12, lr}
    subs    pc, lr, #4          /* Return to faulting instruction */
1:  b       1b

_data_handler:
    /* ABORT mode (0x17), LR = fault address + 8 */
    push    {r0-r3, r12, lr}
    bl      c_data_abort_handler
    pop     {r0-r3, r12, lr}
    subs    pc, lr, #8
1:  b       1b

_reserved_handler:
    b       _reserved_handler

_irq_handler:
    /* IRQ disabled in SPL, hang if somehow triggered */
    b       _irq_handler

_fiq_handler:
    /* FIQ disabled in SPL, hang if somehow triggered */
    b       _fiq_handler

/* External symbols from linker script */
    .extern _bss_start
    .extern _bss_end
    .extern _svc_stack_top
    .extern _irq_stack_top
    .extern _fiq_stack_top
    .extern _abt_stack_top
    .extern _und_stack_top
    .extern _sys_stack_top
    .extern bootloader_main
    .extern c_undef_handler
    .extern c_prefetch_abort_handler
    .extern c_data_abort_handler
