/* ============================================================
 * kernel/arch/arm/mm/mmu.c — MMU high-level initialisation
 *
 * mmu_init() runs at PA (MMU off). Called from start.S with
 * phys_offset = VA - PA in r0. It builds boot_pgd via a PA
 * pointer (because the boot_pgd symbol resolves to VA and VA
 * is unmapped pre-MMU) and then enables the MMU.
 *
 * NO uart_printf() calls inside mmu_init — every string literal
 * would be a VA pointer that the CPU can't dereference with MMU
 * off. Boot-time diagnostics are emitted later by
 * mmu_print_status(), called from kmain after start.S has
 * trampolined PC into the high-VA alias.
 *
 * Dependencies: mmu.h, board.h, uart driver (mmu_print_status only)
 * ============================================================ */

#include "mmu.h"
#include "board.h"
#include "uart/uart.h"

extern uint32_t boot_pgd[];

uint32_t mmu_read_sctlr(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(val));
    return val;
}

uint32_t mmu_read_ttbr0(void)
{
    uint32_t val;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(val));
    return val;
}

/*
 * mmu_init(phys_offset) — invoked from start.S at PA.
 *
 * boot_pgd is a linker-emitted array in .bss; its address
 * resolves to VA at compile time, but with the MMU off we can't
 * access VA. Subtract phys_offset once and do every page-table
 * write through the resulting PA pointer.
 */
void mmu_init(uint32_t phys_offset)
{
    uint32_t pgd_va = (uint32_t)boot_pgd;       /* symbol -> VA */
    uint32_t pgd_pa = pgd_va - phys_offset;
    uint32_t *pgd   = (uint32_t *)pgd_pa;       /* PA alias view */

    mmu_build_boot_pgd(pgd);

    mmu_enable(pgd_pa);

    /* MMU is now on. Kernel is still executing at PA via the
     * identity map installed above. start.S will immediately do
     *     ldr pc, =_start_va
     * to trampoline PC into the high-VA alias. No UART I/O until
     * then — mmu_print_status() handles post-trampoline logging. */
}

/*
 * mmu_print_status() — boot-time diagnostic, called from kmain.
 *
 * Runs at VA with UART initialised, so uart_printf is safe.
 * Prints where boot_pgd sits plus the top-level translation
 * layout, mirroring the log we used to emit from mmu_init.
 */
void mmu_print_status(void)
{
    uint32_t pgd_va = (uint32_t)boot_pgd;
    uint32_t sctlr  = mmu_read_sctlr();
    uint32_t ttbr0  = mmu_read_ttbr0();

    uart_printf("[MMU]  boot_pgd @ VA 0x%08x (%u entries, 16 KB aligned)\n",
                pgd_va, (unsigned)PGD_ENTRIES);
    uart_printf("[MMU]  identity : 0x%08x .. 0x%08x (drop after boot)\n",
                RAM_BASE, RAM_BASE + RAM_SIZE - 1U);
    uart_printf("[MMU]  high VA  : 0x%08x .. 0x%08x -> 0x%08x\n",
                VA_KERNEL_BASE, VA_KERNEL_BASE + RAM_SIZE - 1U, RAM_BASE);
    uart_printf("[MMU]  null gu. : 0x00000000 .. 0x000FFFFF (FAULT)\n");
    uart_printf("[MMU]  SCTLR=0x%08x TTBR0=0x%08x\n", sctlr, ttbr0);
}

/*
 * mmu_drop_identity() — tear down the RAM_BASE..+RAM_SIZE identity
 * PA→PA range. Called from kmain after every boot-time PA touch
 * (T3, T4, user-stub copy) has happened, so any future PA
 * dereference from kernel code is a bug that now faults.
 *
 * High-VA kernel alias + peripherals + NULL guard are untouched.
 */
void mmu_drop_identity(void)
{
    const uint32_t start_idx = RAM_BASE >> 20;
    const uint32_t end_idx   = (RAM_BASE + RAM_SIZE) >> 20;

    for (uint32_t i = start_idx; i < end_idx; i++)
        boot_pgd[i] = 0;

    /* Full TLB flush — drop any cached translations for the
     * identity range, then barriers to make the flush visible
     * before the next instruction fetch. */
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    uart_printf("[MMU]  identity dropped — PA 0x%08x..0x%08x now faults\n",
                RAM_BASE, RAM_BASE + RAM_SIZE - 1U);
}
