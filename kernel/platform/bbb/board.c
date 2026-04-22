/* ============================================================
 * kernel/platform/bbb/board.c — BeagleBone Black (AM335x) wire-up
 *
 * Picks a chip driver for each subsystem and binds it to a
 * physical address + IRQ. DMTIMER2 needs CM_PER clock gating
 * enabled before the timer driver can touch it — that's board-
 * specific (CM_PER belongs to this SoC), so we do it here.
 *
 *   UART : NS16550      @ 0x44E09000, IRQ 72
 *   Timer: DMTIMER2     @ 0x48040000, IRQ 68, 24 MHz
 *   INTC : AM335x INTC  @ 0x48200000
 * ============================================================ */

#include <stdint.h>
#include "board.h"
#include "drivers/intc.h"
#include "drivers/timer.h"
#include "drivers/uart.h"

#define REG32(addr)  (*((volatile uint32_t *)(addr)))

extern const struct uart_ops  ns16550_ops;
extern const struct timer_ops dmtimer_ops;
extern const struct intc_ops  am335x_intc_ops;

static struct uart_device bbb_uart0 = {
    .ops  = &ns16550_ops,
    .base = UART0_BASE,
    .irq  = IRQ_UART0,
};

static struct timer_device bbb_timer2 = {
    .ops    = &dmtimer_ops,
    .base   = TIMER2_BASE,
    .irq    = IRQ_TIMER,
    .clk_hz = TIMER_CLK_HZ,
};

static struct intc_device bbb_intc = {
    .ops  = &am335x_intc_ops,
    .base = INTC_BASE,
};

/* AM335x CM_PER registers — gate DMTIMER2 module clock */
#define CM_PER_L4LS_CLKSTCTRL   0x000U
#define CM_PER_TIMER2_CLKCTRL   0x080U

#define CLKSTCTRL_SW_WKUP       0x2U
#define MODULEMODE_DISABLE      0x0U
#define MODULEMODE_ENABLE       0x2U
#define IDLEST_SHIFT            16
#define IDLEST_MASK             (0x3U << IDLEST_SHIFT)
#define IDLEST_FUNC             0x0U
#define IDLEST_DISABLED         0x3U

/* AM335x CM_DPLL — clock-source mux for DMTIMER1..7
 *
 * CLKSEL_TIMER2_CLK bits[1:0] select which clock feeds DMTIMER2:
 *   0x0 = TCLKIN      (external pin — usually floating)
 *   0x1 = CLK_M_OSC   (24 MHz — what our driver assumes)
 *   0x2 = CLK_32KHZ   (32.768 kHz)
 *
 * Reset default is not M_OSC, so if the bootloader leaves this
 * alone the timer runs at a random slow rate. AM335x TRM requires
 * changing CLKSEL only while the module is in DISABLED idle state.
 */
#define CM_DPLL_BASE            0x44E00500U
#define CM_CLKSEL_TIMER2_CLK    0x08U
#define TIMER2_CLKSEL_M_OSC     0x1U

static void clock_enable_timer2(void)
{
    /* 1. Force module into DISABLED so CLKSEL is safe to write. */
    REG32(CM_PER_BASE + CM_PER_TIMER2_CLKCTRL) = MODULEMODE_DISABLE;
    while (((REG32(CM_PER_BASE + CM_PER_TIMER2_CLKCTRL) & IDLEST_MASK)
             >> IDLEST_SHIFT) != IDLEST_DISABLED)
        ;

    /* 2. Pick CLK_M_OSC (24 MHz) as DMTIMER2 functional clock. */
    REG32(CM_DPLL_BASE + CM_CLKSEL_TIMER2_CLK) = TIMER2_CLKSEL_M_OSC;

    /* 3. Wake the L4LS clock domain that hosts DMTIMER2. */
    REG32(CM_PER_BASE + CM_PER_L4LS_CLKSTCTRL) = CLKSTCTRL_SW_WKUP;

    /* 4. Enable the module clock and wait for it to go functional. */
    REG32(CM_PER_BASE + CM_PER_TIMER2_CLKCTRL) = MODULEMODE_ENABLE;
    while (((REG32(CM_PER_BASE + CM_PER_TIMER2_CLKCTRL) & IDLEST_MASK)
             >> IDLEST_SHIFT) != IDLEST_FUNC)
        ;
}

void platform_init_devices(void)
{
    /* DMTIMER2 clock gate — must be on before timer driver touches it. */
    clock_enable_timer2();

    uart_set_console(&bbb_uart0);
    timer_set_device(&bbb_timer2);
    intc_set_device(&bbb_intc);
}
