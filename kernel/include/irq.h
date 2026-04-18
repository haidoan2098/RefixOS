#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

/*
 * irq.h — Public interrupt dispatch API
 *
 * Thin layer above the platform INTC driver. Kernel code registers
 * per-IRQ callbacks here; the exception entry path calls irq_dispatch()
 * which reads the active line from INTC, invokes the handler, and EOIs.
 */

#include <stdint.h>

/* ----------------------------------------------------------------
 *  Timer IRQ line — platform-specific
 * ---------------------------------------------------------------- */
#ifdef PLATFORM_QEMU
  /* realview-pb-a8 GIC: SP804 Timer0_1 = SPI #4 → GIC ID 36 */
  #define IRQ_TIMER       36U
#elif defined(PLATFORM_BBB)
  /* AM335x: DMTIMER2 interrupt line = 68 */
  #define IRQ_TIMER       68U
#endif

/* Table size covers both platforms (AM335x uses up to 127) */
#define MAX_IRQS          128U

typedef void (*irq_handler_t)(void);

void irq_init(void);
void irq_register(uint32_t irq, irq_handler_t fn);
void irq_enable(uint32_t irq);
void irq_dispatch(void);
void irq_cpu_enable(void);

#endif /* KERNEL_IRQ_H */
