#ifndef KERNEL_PLATFORM_BBB_BOARD_H
#define KERNEL_PLATFORM_BBB_BOARD_H

/* ============================================================
 * board.h — BeagleBone Black (AM335x) addresses and constants
 *
 * Physical addresses + IRQ numbers + banner string for the
 * BBB target. All platform-specific numeric constants used by
 * kernel core live here; shared interfaces + VA layout live in
 * <platform.h>.
 * ============================================================ */

#define PLATFORM_NAME    "BeagleBone Black (AM335x)"

/* Physical RAM base — DDR3 */
#define RAM_BASE         0x80000000U
#define RAM_SIZE         (512U * 1024U * 1024U)

/* Kernel physical load address */
#define KERNEL_PHYS_BASE 0x80000000U

/* Physical-to-virtual offset */
#define PHYS_OFFSET      (0xC0000000U - RAM_BASE) /* 0x40000000 */

/* UART0 — NS16550 compatible */
#define UART0_BASE       0x44E09000U

/* DMTIMER2 — OS tick source */
#define TIMER2_BASE      0x48040000U

/* INTC — Interrupt Controller */
#define INTC_BASE        0x48200000U

/* CM_PER — Clock Module for peripherals (DMTIMER2 gate) */
#define CM_PER_BASE      0x44E00000U

/* DMTIMER2 fed from CLK_M_OSC = 24 MHz */
#define TIMER_CLK_HZ     24000000U

/* IRQ lines on the AM335x INTC (TRM Table 6-1) */
#define IRQ_TIMER        68U  /* DMTIMER2 */
#define IRQ_UART0        72U  /* UART0INT */

/* Per-process user PA slots — see docs/memory-architecture.md §1. */
#define USER_PHYS_BASE   (RAM_BASE + 0x00200000U)
#define USER_PHYS_STRIDE 0x00100000U

#endif /* KERNEL_PLATFORM_BBB_BOARD_H */
