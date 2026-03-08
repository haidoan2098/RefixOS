/* ============================================================
 * boot/spl/linker/spl.ld
 * Linker Script for AM335x SPL (Secondary Program Loader)
 *
 * Target: AM335x Internal SRAM
 *   Base: 0x402F0400 (ROM Code reserved area end)
 *   Size: 0x0001B800 (109KB available to SPL)
 *   TRM §2.1 Memory Map — "OCMC RAM" internal SRAM
 *
 * MEMORY LAYOUT (SRAM — total 109KB):
 *
 *   0x402F0400  ┌────────────────────────┐
 *               │  .vectors (32 bytes)   │ ← Vector table (8 entries × 4)
 *               ├────────────────────────┤
 *               │  .text                 │ ← SPL code + rodata
 *               ├────────────────────────┤
 *               │  .data                 │ ← Initialized globals
 *               ├────────────────────────┤
 *               │  .bss                  │ ← Zero-initialized globals
 *               ├────────────────────────┤
 *               │  .stack                │ ← Per-mode stacks
 *               │    FIQ   512B          │
 *               │    IRQ    1KB          │
 *               │    ABT    1KB          │
 *               │    UND    1KB          │
 *               │    SYS    1KB          │
 *               │    SVC    8KB (main)   │
 *               └────────────────────────┘
 *   0x402F0400 + 0x0001B800 (end of SRAM)
 *
 * NOTE: .text + .data + .bss + .stack MUST fit under 109KB total.
 * ============================================================ */

ENTRY(_start)

MEMORY
{
    /* AM335x Internal SRAM — TRM §2.1 Table 2-2
     * ROM uses 0x40200000–0x402EFFFF, SPL gets 0x402F0400 onward */
    SRAM (rwx) : ORIGIN = 0x402F0400, LENGTH = 0x0001B800
}

SECTIONS
{
    /* Vector table — must be very first thing in image
     * ROM Code jumps to ORIGIN which is _vectors */
    .vectors :
    {
        _vectors_start = .;
        *(.vectors)
        . = ALIGN(32);  /* 32-byte alignment required for VBAR — ARM ARM §B3.2.68 */
        _vectors_end = .;
    } > SRAM

    /* Executable code and read-only data */
    .text :
    {
        _text_start = .;
        *(.text.start)
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _text_end = .;
    } > SRAM

    /* Initialized data */
    .data :
    {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM

    /* Zero-initialized data — not stored in binary, zeroed by start.S */
    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > SRAM

    /* Per-mode exception stacks — NOLOAD, not in binary
     * Total: 512 + 1K + 1K + 1K + 1K + 8K = 13KB */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_start = .;

        /* FIQ stack — 512 B */
        . = . + 0x200;
        PROVIDE(_fiq_stack_top = .);

        /* IRQ stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_irq_stack_top = .);

        /* Abort (prefetch + data) stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_abt_stack_top = .);

        /* Undefined instruction stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_und_stack_top = .);

        /* System mode stack — 1 KB */
        . = . + 0x400;
        PROVIDE(_sys_stack_top = .);

        /* SVC (Supervisor) stack — 8 KB (main SPL execution stack) */
        . = . + 0x2000;
        PROVIDE(_svc_stack_top = .);

        _stack_end = .;
    } > SRAM

    PROVIDE(_spl_end = .);
    PROVIDE(_heap_start = .);   /* Remaining SRAM available as scratch buffer */
}
