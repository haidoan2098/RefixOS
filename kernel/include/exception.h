/* ===========================================================
 * kernel/include/exception.h — Exception handling interface
 *
 * Defines the register context saved by assembly entry stubs
 * and prototypes for C-level exception handlers.
 *
 * Dependencies: exception_entry.S (saves this layout on stack)
 * =========================================================== */

#ifndef KERNEL_EXCEPTION_H
#define KERNEL_EXCEPTION_H

#include <stdint.h>

/* -----------------------------------------------------------
 * Exception context — matches the stack frame pushed by
 * exception_entry.S: SPSR first (lowest addr), then r0-r12, LR
 * ----------------------------------------------------------- */
typedef struct {
    uint32_t spsr;
    uint32_t r[13];     /* r0 – r12 */
    uint32_t lr;        /* adjusted return address */
} exception_context_t;

/* -----------------------------------------------------------
 * C handlers — called from exception_entry.S
 * ----------------------------------------------------------- */
void handle_data_abort(exception_context_t *ctx);
void handle_prefetch_abort(exception_context_t *ctx);
void handle_undefined(exception_context_t *ctx);
void handle_svc(exception_context_t *ctx);
void handle_irq(exception_context_t *ctx);

/* -----------------------------------------------------------
 * VBAR setup — called from kmain after UART init
 * ----------------------------------------------------------- */
void exception_init(void);

#endif /* KERNEL_EXCEPTION_H */
