/* ============================================================
 * kernel/arch/arm/mm/pgtable.c — Boot L1 page table builder
 *
 * Constructs the initial kernel page table using 1 MB sections.
 * Called by mmu_init() before mmu_enable(), with a PA-view pointer
 * (kernel is linked at VA but the table itself must be written at
 * PA since the MMU is still off — see mmu_init() for the dance).
 *
 * Layout produced:
 *   0x00000000 .. 0x000FFFFF : FAULT  (NULL guard, left zero by BSS)
 *   [identity ] RAM_BASE     : cached, kernel RW — keeps PC valid
 *                              across the MMU-enable instant and
 *                              the return to start.S; dropped by
 *                              mmu_drop_identity() once kmain is
 *                              executing at high VA.
 *   0xC0000000 .. +RAM_SIZE  : high VA alias of kernel RAM — every
 *                              kernel symbol resolves here; this is
 *                              where execution actually happens.
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

/* pgd is a caller-supplied pointer so mmu_build_boot_pgd() can be
 * invoked at PA (before MMU is on) by passing boot_pgd - phys_offset.
 * All accesses in this file go through that pointer — no symbol
 * dereference that would otherwise resolve to the VA alias. */
static inline void map_section(uint32_t *pgd, uint32_t va,
                               uint32_t pa, uint32_t attrs)
{
    pgd[va >> 20] = (pa & 0xFFF00000U) | attrs;
}

static void map_range(uint32_t *pgd, uint32_t va, uint32_t pa,
                      uint32_t size_bytes, uint32_t attrs)
{
    uint32_t count = size_bytes >> 20;   /* number of 1 MB sections */
    uint32_t i;
    for (i = 0; i < count; i++) {
        map_section(pgd, va + (i << 20), pa + (i << 20), attrs);
    }
}

void mmu_build_boot_pgd(uint32_t *pgd)
{
    /* Caller guarantees pgd points at the physical address of the
     * boot L1 table (16 KB aligned, zeroed by start.S via BSS wipe).
     * All section entries below therefore survive MMU enable. */

    /* 1. Identity map kernel RAM — required only during the narrow
     *    window between MMU enable and the VA trampoline in start.S.
     *    After kmain starts executing at high VA the identity range
     *    can be dropped (mmu_drop_identity(), to be added). */
    map_range(pgd, RAM_BASE, RAM_BASE, RAM_SIZE, PDE_KERNEL_MEM);

    /* 2. Kernel high VA alias at 0xC0000000.
     *    This is where kmain + all post-MMU code runs; linker emits
     *    every kernel symbol in this range. */
    map_range(pgd, VA_KERNEL_BASE, RAM_BASE, RAM_SIZE, PDE_KERNEL_MEM);

    /* 3. Peripheral regions — identity mapped, strongly-ordered, XN. */
#ifdef PLATFORM_QEMU
    /* realview-pb-a8:
     *   UART0 @ 0x10009000, SP804 @ 0x10011000 — 2 sections
     *   GIC   @ 0x1E000000 (CPU iface + distributor)         — 1 section */
    map_range(pgd, 0x10000000U, 0x10000000U, 2U << 20, PDE_DEVICE);
    map_range(pgd, 0x1E000000U, 0x1E000000U, 1U << 20, PDE_DEVICE);
#elif defined(PLATFORM_BBB)
    /* AM335x:
     *   L4_WKUP  @ 0x44E00000 (UART0, WDT, CM_PER)       — 1 MB
     *   L4_PER   @ 0x48000000 (INTC, DMTIMER, GPIO, ...) — 16 MB */
    map_range(pgd, 0x44E00000U, 0x44E00000U,  1U << 20, PDE_DEVICE);
    map_range(pgd, 0x48000000U, 0x48000000U, 16U << 20, PDE_DEVICE);
#endif

    /* Entry 0 (VA 0x00000000 .. 0x000FFFFF) is left as FAULT:
     * null-pointer dereference raises Data Abort. */
}

/* ============================================================
 * pgtable_build_for_proc — populate a per-process L1 table
 *
 * Mirrors kernel high-half + peripherals from boot_pgd, then
 * installs one user section at VA 0x40000000 → user_pa.
 *
 * Deliberately does NOT copy the identity map of RAM: after a
 * context switch to this pgd, only VAs 0x40000000 (user),
 * 0xC0000000+ (kernel high), and peripherals resolve. That is
 * safe because kernel code already executes at its high-VA alias
 * from the trampoline in start.S onwards — a TTBR0 swap never
 * needs an identity fallback.
 * ============================================================ */
void pgtable_build_for_proc(uint32_t *pgd, uint32_t user_pa)
{
    const uint32_t ident_lo = RAM_BASE >> 20;
    const uint32_t ident_hi = (RAM_BASE + RAM_SIZE) >> 20;

    /* 1. Zero all 4096 entries → everything FAULT by default */
    for (uint32_t i = 0; i < PGD_ENTRIES; i++)
        pgd[i] = 0;

    /* 2. Copy every non-zero entry from boot_pgd EXCEPT the
     *    identity map of RAM. This pulls in:
     *       - kernel high-half alias @ 0xC00+
     *       - peripherals (QEMU 0x100/0x1E0, BBB 0x44E/0x480..0x48F)
     *    Order-independent — boot_pgd is already built. */
    for (uint32_t i = 0; i < PGD_ENTRIES; i++) {
        if (boot_pgd[i] == 0)
            continue;
        if (i >= ident_lo && i < ident_hi)
            continue;                         /* skip identity RAM */
        pgd[i] = boot_pgd[i];
    }

    /* 3. Install user section at VA 0x40000000 → user_pa.
     *    User RW + executable. Each process calls with a different
     *    user_pa so physical isolation is real. */
    pgd[USER_VIRT_BASE >> 20] = (user_pa & 0xFFF00000U) | PDE_USER_TEXT;

    /* Ensure writes are visible before TLB/pagetable consumers see
     * the new table (even though TTBR0 swap happens elsewhere). */
    __asm__ volatile("dsb" ::: "memory");
}
