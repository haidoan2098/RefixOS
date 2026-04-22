/* ============================================================
 * kernel/drivers/uart/pl011.c — ARM PrimeCell PL011 driver
 *
 * Chip: PL011 UART (PrimeCell). TX via polling on FR.TXFF;
 * RX via IRQ (level + timeout) drained into the subsystem's
 * ring buffer.
 *
 * No knowledge of board wiring — every hardware access goes
 * through `dev->base`, supplied by platform/<board>/board.c.
 * ============================================================ */

#include <stdint.h>
#include "drivers/uart.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

/* ---- PL011 register offsets ---- */
#define PL011_DR        0x000U
#define PL011_FR        0x018U
#define PL011_IBRD      0x024U
#define PL011_FBRD      0x028U
#define PL011_LCR_H     0x02CU
#define PL011_CR        0x030U
#define PL011_IMSC      0x038U
#define PL011_ICR       0x044U

#define PL011_FR_TXFF   (1U << 5)
#define PL011_FR_RXFE   (1U << 4)

#define PL011_LCR_WLEN8 (0x3U << 5)
#define PL011_LCR_FEN   (1U << 4)

#define PL011_CR_UARTEN (1U << 0)
#define PL011_CR_TXE    (1U << 8)
#define PL011_CR_RXE    (1U << 9)

#define PL011_INT_RX    (1U << 4)
#define PL011_INT_RT    (1U << 6)

void scheduler_wake_reader(void);

static void pl011_init(struct uart_device *dev)
{
    uint32_t base = dev->base;

    REG32(base + PL011_CR) = 0;

    REG32(base + PL011_IBRD) = 13U;
    REG32(base + PL011_FBRD) = 1U;

    REG32(base + PL011_LCR_H) = PL011_LCR_WLEN8 | PL011_LCR_FEN;

    REG32(base + PL011_IMSC) = PL011_INT_RX | PL011_INT_RT;

    REG32(base + PL011_CR) = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

static void pl011_putc(struct uart_device *dev, char c)
{
    uint32_t base = dev->base;

    while (REG32(base + PL011_FR) & PL011_FR_TXFF)
        ;

    REG32(base + PL011_DR) = (uint32_t)c;
}

static void pl011_rx_irq(struct uart_device *dev)
{
    uint32_t base = dev->base;

    while (!(REG32(base + PL011_FR) & PL011_FR_RXFE)) {
        char c = (char)(REG32(base + PL011_DR) & 0xFFU);
        uart_rx_push(c);
    }

    REG32(base + PL011_ICR) = PL011_INT_RX | PL011_INT_RT;

    scheduler_wake_reader();
}

const struct uart_ops pl011_ops = {
    .init   = pl011_init,
    .putc   = pl011_putc,
    .rx_irq = pl011_rx_irq,
};
