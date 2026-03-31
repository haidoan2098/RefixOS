#ifndef KERNEL_UART_H
#define KERNEL_UART_H

#include <stdint.h>
/*
 * uart.h — Public UART driver interface
 *
 * Platform-agnostic API. Implementation uses board.h to select
 * between PL011 (QEMU) and NS16550 (BBB).
 */

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_print_hex(uint32_t val);
void uart_printf(const char *fmt, ...);

#endif /* KERNEL_UART_H */
