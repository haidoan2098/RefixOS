#ifndef KERNEL_PLATFORM_QEMU_BOARD_H
#define KERNEL_PLATFORM_QEMU_BOARD_H

/* ============================================================
 * board.h — QEMU realview-pb-a8 addresses and constants
 *
 * Physical addresses + IRQ numbers + banner string for the
 * QEMU target. All platform-specific numeric constants used by
 * kernel core live here; shared interfaces + VA layout live in
 * <platform.h>.
 * ============================================================ */

#define PLATFORM_NAME    "QEMU realview-pb-a8"

/* Physical RAM base */
#define RAM_BASE         0x70000000U
#define RAM_SIZE         (128U * 1024U * 1024U)

/* Kernel physical load address (linker uses this, not kernel core) */
#define KERNEL_PHYS_BASE 0x70100000U

/* Physical-to-virtual offset (used in early boot before MMU).
 * VA = PA + PHYS_OFFSET; PA = VA - PHYS_OFFSET. */
#define PHYS_OFFSET      (0xC0000000U - RAM_BASE) /* 0x50000000 */

/* PL011 UART0 — ARM PrimeCell */
#define UART0_BASE       0x10009000U

/* SP804 Dual Timer 0 — tick source for scheduler */
#define TIMER0_BASE      0x10011000U

/* GIC v1 — distributor + CPU interface */
#define GIC_CPU_BASE     0x1E000000U
#define GIC_DIST_BASE    0x1E001000U

/* SP804 is clocked at 1 MHz on realview-pb-a8 */
#define TIMER_CLK_HZ     1000000U

/* IRQ lines on the GIC */
#define IRQ_TIMER        36U  /* SP804 Timer0_1 = SPI #4  → GIC ID 36 */
#define IRQ_UART0        44U  /* UART0 (PL011)  = SPI #12 → GIC ID 44 */

/* Per-process user PA slots — see docs/memory-architecture.md §1. */
#define USER_PHYS_BASE   (RAM_BASE + 0x00200000U)
#define USER_PHYS_STRIDE 0x00100000U

#endif /* KERNEL_PLATFORM_QEMU_BOARD_H */
