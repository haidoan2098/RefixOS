/* ============================================================
 * kernel/platform/bbb/periph_map.c — MMIO region installer
 *
 * Installs identity-mapped, strongly-ordered, XN sections for
 * every peripheral window the kernel touches on the BeagleBone
 * Black (AM335x):
 *
 *   L4_WKUP @ 0x44E00000 (UART0, WDT, CM_PER)         — 1 MB
 *   L4_PER  @ 0x48000000 (INTC, DMTIMER, GPIO, ...)   — 16 MB
 *
 * Invoked from mmu_build_boot_pgd() while the MMU is still off.
 * Every constant below is an immediate / PC-relative literal —
 * no VA pointer dereference happens inside this translation
 * unit, so executing at PA is safe.
 * ============================================================ */

#include <stdint.h>
#include "mmu.h"
#include "platform.h"

void platform_map_peripherals(uint32_t *pgd)
{
    pgtable_map_range(pgd, 0x44E00000U, 0x44E00000U,  1U << 20, PDE_DEVICE);
    pgtable_map_range(pgd, 0x48000000U, 0x48000000U, 16U << 20, PDE_DEVICE);
}
