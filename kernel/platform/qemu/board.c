/* ============================================================
 * kernel/platform/qemu/board.c — QEMU realview-pb-a8 wire-up
 *
 * Picks a chip driver for each subsystem and binds it to a
 * physical address + IRQ. platform_init_devices() runs early
 * in kmain (post-MMU) before any uart/timer/intc API is used.
 *
 *   UART : PL011     @ 0x10009000, IRQ 44
 *   Timer: SP804     @ 0x10011000, IRQ 36, 1 MHz
 *   INTC : GIC v1    CPU 0x1E000000, DIST 0x1E001000
 * ============================================================ */

#include <stdint.h>
#include "board.h"
#include "drivers/intc.h"
#include "drivers/timer.h"
#include "drivers/uart.h"

extern const struct uart_ops  pl011_ops;
extern const struct timer_ops sp804_ops;
extern const struct intc_ops  gicv1_ops;

static struct uart_device qemu_uart0 = {
    .ops  = &pl011_ops,
    .base = UART0_BASE,
    .irq  = IRQ_UART0,
};

static struct timer_device qemu_timer0 = {
    .ops    = &sp804_ops,
    .base   = TIMER0_BASE,
    .irq    = IRQ_TIMER,
    .clk_hz = TIMER_CLK_HZ,
};

static struct intc_device qemu_gic = {
    .ops       = &gicv1_ops,
    .cpu_base  = GIC_CPU_BASE,
    .dist_base = GIC_DIST_BASE,
};

void platform_init_devices(void)
{
    uart_set_console(&qemu_uart0);
    timer_set_device(&qemu_timer0);
    intc_set_device(&qemu_gic);
}
