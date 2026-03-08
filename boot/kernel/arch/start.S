/* ===========================================================
 * boot/arch/start.S — RefixOS reset entry point
 *
 * QEMU path: QEMU -kernel loads the image at physical 0x70100000.
 * BBB path: SPL loads the kernel into DDR at physical 0x80000000
 *           and jumps here after DDR init + image copy.
 * MMU is OFF — every address here is physical.
 *
 * Sequence:
 *   1. Mask all interrupts
 *   2. Set up exception-mode stacks
 *   3. Switch to SVC mode (kernel runs in SVC)
 *   4. Zero BSS
 *   5. bl kmain   — never returns
 * =========================================================== */

    .section .text.start, "ax"
    .align  4
    .global _start

_start:
    /* ------------------------------------------------------- */
    /* 1. Disable IRQ and FIQ at CPU level                      */
    /*    Required before touching banked registers.            */
    /*    ARM ARM §A2.5: CPSR.I=1 masks IRQ, CPSR.F=1 masks FIQ */
    /* ------------------------------------------------------- */
    cpsid   if

    /* ------------------------------------------------------- */
    /* 2. Set up early exception-mode stacks                    */
    /*                                                          */
    /*    ARMv7-A has banked SP per exception mode.             */
    /*    We switch to each mode, load SP, then return to SVC.  */
    /*    Stack tops come from linker symbols (physical addr).  */
    /*    SYS mode is not used in this phase because early       */
    /*    kernel C code runs entirely in SVC mode.               */
    /*                                                          */
    /*    Mode bits (CPSR[4:0]):                                */
    /*      FIQ  = 0x11, IRQ  = 0x12, SVC = 0x13              */
    /*      ABT  = 0x17, UND  = 0x1B, SYS = 0x1F              */
    /* ------------------------------------------------------- */

    /* FIQ mode — 512 B */
    cps     #0x11
    ldr     sp, =_fiq_stack_top

    /* IRQ mode — 1 KB */
    cps     #0x12
    ldr     sp, =_irq_stack_top

    /* Abort mode — 1 KB */
    cps     #0x17
    ldr     sp, =_abt_stack_top

    /* Undefined mode — 1 KB */
    cps     #0x1B
    ldr     sp, =_und_stack_top

    /* SVC (Supervisor) mode — 8 KB; kernel runs here */
    cps     #0x13
    ldr     sp, =_svc_stack_top

    /* ------------------------------------------------------- */
    /* 3. Zero BSS                                              */
    /*    C assumes BSS == 0 at startup.                        */
    /*    _bss_start / _bss_end are linker-provided symbols.    */
    /* ------------------------------------------------------- */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
.Lzero_bss:
    cmp     r0, r1
    strlo   r2, [r0], #4       /* store 0, advance 4 bytes */
    blo     .Lzero_bss

    /* ------------------------------------------------------- */
    /* 4. Jump to C entry point                                 */
    /*    kmain() must never return.                            */
    /* ------------------------------------------------------- */
    bl      kmain

    /* ------------------------------------------------------- */
    /* 5. Safety halt — should never reach here                 */
    /* ------------------------------------------------------- */
.Lhalt:
    b       .Lhalt
