/* ============================================================
 * kernel/arch/arm/mm/pgtable.c — Boot L1 page table builder
 *
 * Constructs the initial kernel page table using 1 MB sections.
 * Called by mmu_init() before mmu_enable().
 *
 * Layout produced:
 *   0x00000000 .. 0x000FFFFF : FAULT  (NULL guard, left zero by BSS)
 *   [identity ] RAM_BASE     : cached, kernel RW  — boot PC stays
 *                              valid after MMU turns on
 *   0xC0000000 .. +RAM_SIZE  : high VA alias of kernel RAM
 *   [peripherals]            : identity, strongly-ordered, XN
 *
 * Dependencies: board.h, mmu.h
 * ============================================================ */

#include "mmu.h"
#include "board.h"

/* Boot L1 translation table — 16 KB, 16 KB aligned.
 * Placed in .bss.pgd (inside .bss) so start.S zeroes it → all
 * entries default to FAULT until mmu_build_boot_pgd() populates. */
uint32_t boot_pgd[PGD_ENTRIES]
    __attribute__((aligned(PGD_ALIGN)))
    __attribute__((section(".bss.pgd")));

static inline void map_section(uint32_t va, uint32_t pa, uint32_t attrs)
{
    boot_pgd[va >> 20] = (pa & 0xFFF00000U) | attrs;
}

static void map_range(uint32_t va, uint32_t pa, uint32_t size_bytes,
                      uint32_t attrs)
{
    uint32_t count = size_bytes >> 20;   /* number of 1 MB sections */
    uint32_t i;
    for (i = 0; i < count; i++) {
        map_section(va + (i << 20), pa + (i << 20), attrs);
    }
}

void mmu_build_boot_pgd(void)
{
    /* 1. Identity map kernel RAM.
     *
     *    When the MMU turns on, PC is still pointing at a physical
     *    address (linker VMA == LMA == PA in this project). Absolute
     *    symbol loads (ldr rX, =sym) also resolve to PA. The identity
     *    map must stay permanent so both forms keep working after
     *    the MMU is enabled. */
    map_range(RAM_BASE, RAM_BASE, RAM_SIZE, PDE_KERNEL_MEM);

    /* 2. Kernel high VA alias at 0xC0000000.
     *
     *    The same physical RAM is also reachable at 0xC0000000+.
     *    Not used by kmain yet, but chapter 05 (per-process page
     *    tables) expects kernel memory to live here, and NULL guard
     *    for user space at 0x00000000 only works once the high alias
     *    owns the kernel range. */
    map_range(VA_KERNEL_BASE, RAM_BASE, RAM_SIZE, PDE_KERNEL_MEM);

    /* 3. Peripheral regions — identity mapped, strongly-ordered, XN. */
#ifdef PLATFORM_QEMU
    /* realview-pb-a8:
     *   UART0 @ 0x10009000, SP804 @ 0x10011000 — 2 sections
     *   GIC   @ 0x1E000000 (CPU iface + distributor)         — 1 section */
    map_range(0x10000000U, 0x10000000U, 2U << 20, PDE_DEVICE);
    map_range(0x1E000000U, 0x1E000000U, 1U << 20, PDE_DEVICE);
#elif defined(PLATFORM_BBB)
    /* AM335x:
     *   L4_WKUP  @ 0x44E00000 (UART0, WDT, CM_PER)       — 1 MB
     *   L4_PER   @ 0x48000000 (INTC, DMTIMER, GPIO, ...) — 16 MB */
    map_range(0x44E00000U, 0x44E00000U,  1U << 20, PDE_DEVICE);
    map_range(0x48000000U, 0x48000000U, 16U << 20, PDE_DEVICE);
#endif

    /* Entry 0 (VA 0x00000000 .. 0x000FFFFF) is left as FAULT:
     * null-pointer dereference raises Data Abort. */
}
