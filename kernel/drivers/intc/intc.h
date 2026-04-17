#ifndef KERNEL_INTC_H
#define KERNEL_INTC_H

/*
 * intc.h — Platform interrupt controller interface
 *
 * Backends selected at compile time via board.h:
 *   PLATFORM_QEMU → PL190 VIC  (ARM DDI 0181)
 *   PLATFORM_BBB  → AM335x INTC (TRM §6)
 *
 * Only the dispatch layer (irq.c) should call these directly.
 */

#include <stdint.h>
#include "irq.h"

void     intc_init(void);
void     intc_enable_line(uint32_t irq);
void     intc_disable_line(uint32_t irq);
uint32_t intc_get_active(void);   /* returns MAX_IRQS on spurious */
void     intc_eoi(uint32_t irq);

#endif /* KERNEL_INTC_H */
