/* ===========================================================
 * kernel/linker/kernel_qemu.ld — RingNova Kernel Linker Script
 *
 * Target (QEMU realview-pb-a8):
 *   Physical RAM base : 0x70000000 (128 MB)
 *   Kernel load PA    : 0x70100000  (first 1 MB reserved for
 *                        exception vector table + peripheral map)
 *
 * MMU is OFF at entry — CPU uses physical addresses directly.
 * Virtual address split (3G/1G) is enforced later by MMU init.
 * =========================================================== */

ENTRY(_start)

MEMORY
{
    /* Physical RAM available to the kernel image + stacks */
    RAM (rwx) : ORIGIN = 0x70100000, LENGTH = 127M
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
        . = ALIGN(32);
        *(.vectors)         /* exception vector table           */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _text_end = .;
    } > RAM

    /* -------------------------------------------------------
     * .data — initialised data (ROM → RAM copy not needed
     *         since QEMU -kernel loads ELF sections directly)
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
     *   NOLOAD: occupies VA/PA space but not in ELF file.
     *   Zeroed manually in start.S before bl kmain.
     * ------------------------------------------------------- */
    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _bss_start = .;
        . = ALIGN(16384);    /* boot_pgd must be 16 KB aligned (TTBR0) */
        *(.bss.pgd)          /* L1 translation table — 16 KB */
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
