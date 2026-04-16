/* ============================================================
 * kernel/arch/arm/mm/mmu.c — MMU high-level initialisation
 *
 * mmu_init() orchestrates the boot MMU setup:
 *   1. mmu_build_boot_pgd()  — populate boot_pgd[] with mappings
 *   2. mmu_enable(pgd_pa)    — flip SCTLR.M (assembly)
 *
 * After mmu_init() returns, the kernel keeps running at its
 * physical address via the identity map; the 0xC0000000 alias
 * is also live for future per-process page table work.
 *
 * Dependencies: mmu.h, board.h, uart driver
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

void mmu_init(void)
{
    uint32_t pgd_pa = (uint32_t)boot_pgd;

    uart_printf("[MMU]  boot_pgd @ 0x%08x (%u entries, 16 KB aligned)\n",
                pgd_pa, (unsigned)PGD_ENTRIES);

    mmu_build_boot_pgd();

    uart_printf("[MMU]  identity : 0x%08x .. 0x%08x\n",
                RAM_BASE, RAM_BASE + RAM_SIZE - 1U);
    uart_printf("[MMU]  high VA  : 0x%08x .. 0x%08x -> 0x%08x\n",
                VA_KERNEL_BASE, VA_KERNEL_BASE + RAM_SIZE - 1U, RAM_BASE);
    uart_printf("[MMU]  null gu. : 0x00000000 .. 0x000FFFFF (FAULT)\n");
    uart_printf("[MMU]  enabling ...\n");

    mmu_enable(pgd_pa);

    uart_printf("[MMU]  on. SCTLR=0x%08x TTBR0=0x%08x\n",
                mmu_read_sctlr(), mmu_read_ttbr0());
}
