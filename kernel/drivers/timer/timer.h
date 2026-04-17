#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

/*
 * timer.h — Periodic tick source
 *
 * Backends selected at compile time via board.h:
 *   PLATFORM_QEMU → SP804 Dual Timer @ TIMER0_BASE
 *   PLATFORM_BBB  → DMTIMER2         @ TIMER2_BASE
 *
 * timer_init() programs the timer for the requested period but does
 * NOT unmask IRQ at the CPU level — caller wires up INTC + CPSR.I.
 */

#include <stdint.h>

typedef void (*timer_handler_t)(void);

void     timer_init(uint32_t period_us);
void     timer_set_handler(timer_handler_t fn);
void     timer_irq(void);                /* registered with irq_register() */
uint32_t timer_get_ticks(void);

#endif /* KERNEL_TIMER_H */
