#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

/* ============================================================
 * scheduler.h — Round-robin preemptive scheduler
 *
 * scheduler_tick() is registered as the timer callback. It only
 * raises a flag; the actual context switch happens inside
 * schedule(), which is invoked at the tail of handle_irq so the
 * IRQ stack frame is already settled when we swap processes.
 * ============================================================ */

void scheduler_tick(void);  /* timer IRQ callback                */
void schedule(void);        /* consults flag, switches if needed */

#endif /* KERNEL_SCHED_H */
