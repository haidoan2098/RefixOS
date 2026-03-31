/* ===========================================================
 * kernel/linker/kernel_bbb.ld — RingNova Kernel Linker Script
 *
 * Target: BeagleBone Black (AM335x)
 *   Physical DDR base : 0x80000000 (512 MB)
 *   Kernel load PA    : 0x80000000  (SPL loads kernel here)
 *   Linker RAM window  : 0x80000000-0x9FFFFFFF (full 512 MB DDR)
 *
 * MMU is OFF at entry — CPU uses physical addresses directly.
 * SPL jumps here after DDR init.
 * =========================================================== */

ENTRY(_start)

MEMORY
{
    /* Full physical DDR exposed to the kernel image + early stacks.
     * BBB SPL enters the kernel at 0x80000000 and this linker stage does not reserve a carve-out. */
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 512M
}

SECTIONS
{
    /* -------------------------------------------------------
     * .text — executable code
     *   .text.start must be first so _start lands at ORIGIN
     * ------------------------------------------------------- */
    .text :
    {
        _text_start = .;
        *(.text.start)      /* _start — must be first          */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _text_end = .;
    } > RAM

    /* -------------------------------------------------------
     * .data — initialised data
     * ------------------------------------------------------- */
    .data :
    {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM

    /* -------------------------------------------------------
     * .bss — zero-initialised data
     *   NOLOAD: occupies PA space but not in ELF file.
     *   Zeroed manually in start.S before bl kmain.
     * ------------------------------------------------------- */
    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > RAM

    /* -------------------------------------------------------
     * .stack — per-mode exception stacks
     *   Placed after BSS, grows down within each slot.
     *   TOTAL: 512 + 1K + 1K + 1K + 8K = 11.5 KB
     * ------------------------------------------------------- */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);

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

        /* SVC (Supervisor) stack — 8 KB
           Kernel C code runs in SVC mode → needs the most space */
        . = . + 0x2000;
        PROVIDE(_svc_stack_top = .);
    } > RAM

    /* Symbols for heap start (future kmalloc) */
    PROVIDE(_heap_start = .);
}
