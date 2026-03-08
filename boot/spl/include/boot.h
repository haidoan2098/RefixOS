/* ===========================================================
 * boot/spl/include/boot.h
 *
 * Internal function prototypes shared across SPL C files.
 * This header stays private to the SPL stage and describes the
 * callable surface used after start.S transfers control into C.
 * =========================================================== */

#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

/* SPL UART console used for early boot diagnostics in polling mode. */
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_print_hex(uint32_t val);
void uart_flush(void);

void clock_init(void);
void ddr_init(void);
int ddr_test(void);
int mmc_init(void);
int mmc_read_sectors(uint32_t start_sector, uint32_t count, void *dest);

/* Fatal stop path used when SPL cannot continue boot safely. */
void panic(const char *msg);

/* C-level exception handlers entered from start.S after the assembly
 * wrapper saves scratch registers and switches onto the mode stack. */
void c_undef_handler(void);
void c_prefetch_abort_handler(void);
void c_data_abort_handler(void);

#endif /* BOOT_H */
