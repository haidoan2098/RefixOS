/* ===========================================================
 * user/linker/user.ld — Shared linker script for every user
 * program (counter, runaway, shell).
 *
 * Kernel maps one 1 MB user section at VA 0x40000000 per
 * process; this script lays the whole program — code, rodata,
 * data, bss — into that window. crt0's _ustart goes first so
 * the kernel's initial-frame PC lands on it.
 * =========================================================== */

ENTRY(_ustart)

MEMORY
{
    USER (rwx) : ORIGIN = 0x40000000, LENGTH = 1M
}

SECTIONS
{
    .text : {
        KEEP(*(.text._ustart))
        *(.text*)
        *(.rodata*)
    } > USER

    .data : {
        *(.data*)
    } > USER

    .bss : {
        _ubss_start = .;
        *(.bss*)
        *(COMMON)
        _ubss_end = .;
    } > USER

    _uimage_end = .;
}
