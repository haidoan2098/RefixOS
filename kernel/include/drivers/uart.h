#ifndef KERNEL_DRIVERS_UART_H
#define KERNEL_DRIVERS_UART_H

/* ============================================================
 * drivers/uart.h — UART subsystem contract
 *
 * Kernel core calls uart_putc / uart_printf / ... without knowing
 * which chip sits behind them. uart_core.c dispatches via the ops
 * pointer held in the active struct uart_device. Each chip driver
 * (pl011.c, ns16550.c, ...) fills a const struct uart_ops and
 * exposes it to platform/<board>/board.c, which picks one and
 * binds it to an address + IRQ line.
 * ============================================================ */

#include <stdint.h>

struct uart_device;

/* Contract every UART chip driver implements. */
struct uart_ops {
    void (*init)(struct uart_device *dev);
    void (*putc)(struct uart_device *dev, char c);
    void (*rx_irq)(struct uart_device *dev);
};

/* Board instantiates one of these per UART it exposes. */
struct uart_device {
    const struct uart_ops *ops;
    uint32_t base;
    uint32_t irq;
};

/* ---- Subsystem API — called by kernel core ---- */
void uart_set_console(struct uart_device *dev);

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_printf(const char *fmt, ...);
void uart_print_hex(uint32_t val);

void uart_rx_irq(void);        /* wired to the console's IRQ line */
int  uart_rx_empty(void);
int  uart_rx_pop(void);

/* ---- Helper exposed to chip drivers ----
 * Driver's rx_irq() reads bytes from hardware and calls uart_rx_push
 * for each one — the subsystem owns the ring buffer.
 */
void uart_rx_push(char c);

#endif /* KERNEL_DRIVERS_UART_H */
