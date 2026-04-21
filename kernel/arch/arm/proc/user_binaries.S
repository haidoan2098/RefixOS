/* ===========================================================
 * kernel/arch/arm/proc/user_binaries.S — Embed user images
 *
 * Each user program is built separately (see Makefile user
 * rules) into a flat .bin. We .incbin them here so the
 * resulting object lives in its own section inside the kernel
 * ELF and exports start/end labels the C side can read.
 *
 * process_init_all() consults these labels to copy the right
 * program bytes into each process's 1 MB user PA slot.
 * =========================================================== */

    .section .user_binaries, "a"
    .align  4

    .global _counter_img_start
    .global _counter_img_end
_counter_img_start:
    .incbin "build/user/counter.bin"
_counter_img_end:

    .align 4
    .global _runaway_img_start
    .global _runaway_img_end
_runaway_img_start:
    .incbin "build/user/runaway.bin"
_runaway_img_end:

    .align 4
    .global _shell_img_start
    .global _shell_img_end
_shell_img_start:
    .incbin "build/user/shell.bin"
_shell_img_end:
