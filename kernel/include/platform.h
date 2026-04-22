#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

/* ============================================================
 * platform.h — Shared platform constants + board wire-up hooks
 *
 * Kernel core uses this header only for:
 *   - Shared VA layout + process table sizing
 *   - platform_init_devices()   — called early in kmain, wires
 *                                 chip drivers into subsystems
 *   - platform_map_peripherals() — called from MMU setup at PA
 *
 * Driver contracts live in <drivers/uart.h>, <drivers/timer.h>,
 * <drivers/intc.h>. Kernel core that wants UART/timer/intc API
 * includes those directly.
 * ============================================================ */

#include <stdint.h>

/* ---- Shared VA layout — identical across platforms ---- */
#define KERNEL_VIRT_BASE    0xC0000000U
#define USER_VIRT_BASE      0x40000000U
#define USER_REGION_SIZE    0x00100000U                       /* 1 MB */
#define USER_STACK_TOP      (USER_VIRT_BASE + USER_REGION_SIZE)

/* ---- Process table + kernel stack — sized at build time ---- */
#define NUM_PROCESSES       3U
#define KSTACK_SIZE         8192U

/* ---- Board wire-up — kernel/platform/<p>/board.c ----
 *
 * Bind chip drivers (pl011_ops, sp804_ops, ...) to concrete
 * addresses + IRQ lines, then publish the resulting devices via
 * uart_set_console / timer_set_device / intc_set_device. Called
 * once from kmain before any subsystem API is used.
 */
void platform_init_devices(void);

/* ---- Peripheral map installer — kernel/platform/<p>/periph_map.c
 *
 * Called from mmu_build_boot_pgd() at PA (MMU off). Each platform
 * implements this by calling pgtable_map_range() with literal
 * addresses — no VA pointer dereference, safe pre-MMU.
 */
void platform_map_peripherals(uint32_t *pgd);

#endif /* KERNEL_PLATFORM_H */
