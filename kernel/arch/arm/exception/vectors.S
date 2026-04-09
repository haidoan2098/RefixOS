/* ===========================================================
 * kernel/arch/arm/exception/vectors.S — ARM exception vector table
 *
 * Uses LDR PC from literal pool pattern — allows handlers to
 * reside anywhere in the 4 GB address space.
 *
 * VBAR is set to _vectors_start in start.S after stack setup.
 * Must be 32-byte aligned (ARM requirement for VBAR).
 * =========================================================== */

    .section .vectors, "ax"
    .align  5                       /* 32-byte aligned for VBAR */
    .global _vectors_start

_vectors_start:
    ldr     pc, _vec_reset          /* 0x00 — Reset                */
    ldr     pc, _vec_undef          /* 0x04 — Undefined Instruction */
    ldr     pc, _vec_svc            /* 0x08 — Supervisor Call (SWI) */
    ldr     pc, _vec_pabort         /* 0x0C — Prefetch Abort        */
    ldr     pc, _vec_dabort         /* 0x10 — Data Abort            */
    nop                             /* 0x14 — Reserved              */
    ldr     pc, _vec_irq            /* 0x18 — IRQ                   */
    ldr     pc, _vec_fiq            /* 0x1C — FIQ                   */

/* Literal pool — linker resolves handler addresses */
_vec_reset:     .word _start
_vec_undef:     .word exception_entry_undef
_vec_svc:       .word exception_entry_svc
_vec_pabort:    .word exception_entry_pabort
_vec_dabort:    .word exception_entry_dabort
_vec_irq:       .word exception_entry_irq
_vec_fiq:       .word exception_entry_fiq
