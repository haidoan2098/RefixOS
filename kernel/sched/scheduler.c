/* ============================================================
 * kernel/sched/scheduler.c — Round-robin preemptive scheduler
 *
 * One time slice = one timer tick (10 ms). scheduler_tick()
 * flips a flag from the IRQ context; schedule() at the tail of
 * handle_irq walks the PCB ring and swaps to the next runnable
 * process via context_switch(). Processes marked BLOCKED or
 * DEAD are skipped.
 *
 * Dependencies: proc.h (PCB + context_switch), uart (debug log)
 * ============================================================ */

#include <stdint.h>
#include "proc.h"
#include "scheduler.h"
#include "uart/uart.h"

/* Raised by the timer IRQ, cleared by schedule(). No locking
 * needed — single-core, accessed only from SVC mode with IRQ
 * currently masked by the exception entry. */
static volatile int need_reschedule;

/* Count switches so the boot log can confirm round-robin is
 * cycling without spamming one line per tick. */
static uint32_t switch_count;

void scheduler_tick(void)
{
    need_reschedule = 1;
}

/* Used by syscall handlers (sys_yield, sys_exit) to ask for an
 * immediate switch when schedule() runs at handle_svc's tail. */
void scheduler_request_resched(void)
{
    need_reschedule = 1;
}

void schedule(void)
{
    if (!need_reschedule || !current)
        return;
    need_reschedule = 0;

    process_t *prev = current;
    uint32_t start = (prev->pid + 1U) % NUM_PROCESSES;

    for (uint32_t i = 0; i < NUM_PROCESSES; i++) {
        uint32_t idx = (start + i) % NUM_PROCESSES;
        process_t *cand = &processes[idx];

        if (cand == prev)
            continue;
        if (cand->state != TASK_READY && cand->state != TASK_RUNNING)
            continue;

        /* Log the first handful of switches so the bring-up
         * verifies round-robin without flooding UART for hours. */
        if (switch_count < 6U) {
            uart_printf("[SCHED] #%u  pid %u -> pid %u\n",
                        switch_count, prev->pid, cand->pid);
        }
        switch_count++;

        prev->state = TASK_READY;
        cand->state = TASK_RUNNING;
        current = cand;

        context_switch(prev, cand);
        return;
    }

    /* No other runnable process — keep the current one. */
}
