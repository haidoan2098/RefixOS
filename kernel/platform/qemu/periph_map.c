/* ============================================================
 * kernel/platform/qemu/periph_map.c — MMIO region installer
 *
 * Installs identity-mapped, strongly-ordered, XN sections for
 * every peripheral window the kernel touches on QEMU
 * realview-pb-a8:
 *
 *   UART0 @ 0x10009000 + SP804 @ 0x10011000 → 2 MB block at 0x10000000
 *   GIC   @ 0x1E000000 (CPU interface + distributor)
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
    pgtable_map_range(pgd, 0x10000000U, 0x10000000U, 2U << 20, PDE_DEVICE);
    pgtable_map_range(pgd, 0x1E000000U, 0x1E000000U, 1U << 20, PDE_DEVICE);
}
