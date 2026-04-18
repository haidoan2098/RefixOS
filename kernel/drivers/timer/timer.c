/* ===========================================================
 * kernel/drivers/timer/timer.c — Periodic tick source
 *
 * Two hardware backends:
 *   PLATFORM_QEMU → SP804 Dual Timer, 1 MHz clock
 *   PLATFORM_BBB  → AM335x DMTIMER2,  24 MHz clock
 *
 * Counts ticks at 10 ms by default. The IRQ handler
 * (timer_irq) acks the timer, bumps tick_count, and calls the
 * optional user callback.
 *
 * Dependencies: board.h, timer.h, uart (boot log)
 * =========================================================== */

#include <stdint.h>
#include "board.h"
#include "timer.h"
#include "uart/uart.h"

#define REG32(addr)   (*((volatile uint32_t *)(addr)))

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

/* ----------------------------------------------------------------
 *  Shared state — tick counter + optional client callback
 * ---------------------------------------------------------------- */
static volatile uint32_t tick_count;
static timer_handler_t   user_handler;

void timer_set_handler(timer_handler_t fn) { user_handler = fn; }
uint32_t timer_get_ticks(void)             { return tick_count; }

/* ================================================================
 *  PLATFORM_QEMU — ARM SP804 Dual Timer, Timer 0 sub-unit
 *
 *  Clock: 1 MHz → 1 count == 1 µs.
 * ================================================================ */
#ifdef PLATFORM_QEMU

#define SP804_LOAD        0x00U
#define SP804_VALUE       0x04U
#define SP804_CTRL        0x08U
#define SP804_INTCLR      0x0CU
#define SP804_RIS         0x10U
#define SP804_MIS         0x14U

#define SP804_CTRL_ONESHOT (1U << 0)
#define SP804_CTRL_32BIT   (1U << 1)
#define SP804_CTRL_INTEN   (1U << 5)
#define SP804_CTRL_PERIODIC (1U << 6)
#define SP804_CTRL_ENABLE  (1U << 7)

void timer_init(uint32_t period_us)
{
    uint32_t reload = period_us;    /* 1 MHz → 1 tick = 1 µs */

    /* Disable while reconfiguring */
    REG32(TIMER0_BASE + SP804_CTRL) = 0;

    REG32(TIMER0_BASE + SP804_LOAD) = reload;

    REG32(TIMER0_BASE + SP804_CTRL) = SP804_CTRL_ENABLE
                                    | SP804_CTRL_PERIODIC
                                    | SP804_CTRL_INTEN
                                    | SP804_CTRL_32BIT;
    dsb();

    uart_printf("[TIMER] SP804 @ 0x%08x period=%uus reload=%u\n",
                TIMER0_BASE, period_us, reload);
}

void timer_irq(void)
{
    /* Clear timer interrupt (writing any value works) */
    REG32(TIMER0_BASE + SP804_INTCLR) = 1;
    dsb();

    tick_count++;
    if (user_handler)
        user_handler();
}

/* ================================================================
 *  PLATFORM_BBB — AM335x DMTIMER2
 *
 *  Clocked from CLK_M_OSC (24 MHz) via CM_PER. Uses 32-bit
 *  auto-reload overflow interrupt.
 * ================================================================ */
#elif defined(PLATFORM_BBB)

/* CM_PER registers */
#define CM_PER_L4LS_CLKSTCTRL   0x000U
#define CM_PER_TIMER2_CLKCTRL   0x080U

#define CLKSTCTRL_SW_WKUP       0x2U
#define MODULEMODE_ENABLE       0x2U
#define IDLEST_SHIFT            16
#define IDLEST_MASK             (0x3U << IDLEST_SHIFT)
#define IDLEST_FUNC             0x0U

/* DMTIMER registers */
#define DMT_TIOCP_CFG   0x010U
#define DMT_IRQSTATUS   0x028U
#define DMT_IRQENABLE_SET 0x02CU
#define DMT_TCLR        0x038U
#define DMT_TCRR        0x03CU
#define DMT_TLDR        0x040U
#define DMT_TWPS        0x048U
#define DMT_TSICR       0x054U

#define TIOCP_SOFTRESET (1U << 0)
#define TCLR_ST         (1U << 0)
#define TCLR_AR         (1U << 1)
#define TSICR_POSTED    (1U << 2)
#define IRQ_OVF         (1U << 1)
#define TWPS_TCLR       (1U << 0)
#define TWPS_TCRR       (1U << 1)
#define TWPS_TLDR       (1U << 2)

static void clock_enable_timer2(void)
{
    /* Force L4LS clock domain awake */
    REG32(CM_PER_BASE + CM_PER_L4LS_CLKSTCTRL) = CLKSTCTRL_SW_WKUP;

    /* Enable DMTIMER2 module clock */
    REG32(CM_PER_BASE + CM_PER_TIMER2_CLKCTRL) = MODULEMODE_ENABLE;
    while (((REG32(CM_PER_BASE + CM_PER_TIMER2_CLKCTRL) & IDLEST_MASK)
             >> IDLEST_SHIFT) != IDLEST_FUNC)
        ;
}

void timer_init(uint32_t period_us)
{
    clock_enable_timer2();

    /* Soft reset — write TIOCP_CFG.SOFTRESET, wait for self-clear */
    REG32(TIMER2_BASE + DMT_TIOCP_CFG) = TIOCP_SOFTRESET;
    while (REG32(TIMER2_BASE + DMT_TIOCP_CFG) & TIOCP_SOFTRESET)
        ;

    /* Posted mode: writes return immediately; check TWPS before next write */
    REG32(TIMER2_BASE + DMT_TSICR) = TSICR_POSTED;

    /* Stop timer */
    while (REG32(TIMER2_BASE + DMT_TWPS) & TWPS_TCLR)
        ;
    REG32(TIMER2_BASE + DMT_TCLR) = 0;

    /* Clear any pending IRQ status */
    REG32(TIMER2_BASE + DMT_IRQSTATUS) = 0x7U;

    /* Reload value: counter overflows at 0xFFFFFFFF.
     * For period_us at 24 MHz: count = period_us * 24
     * reload = 0xFFFFFFFF - count + 1 = -count */
    uint32_t count  = period_us * (TIMER_CLK_HZ / 1000000U);
    uint32_t reload = (uint32_t)(0U - count);

    while (REG32(TIMER2_BASE + DMT_TWPS) & TWPS_TLDR)
        ;
    REG32(TIMER2_BASE + DMT_TLDR) = reload;

    while (REG32(TIMER2_BASE + DMT_TWPS) & TWPS_TCRR)
        ;
    REG32(TIMER2_BASE + DMT_TCRR) = reload;

    /* Enable overflow IRQ */
    REG32(TIMER2_BASE + DMT_IRQENABLE_SET) = IRQ_OVF;

    /* Start with auto-reload */
    while (REG32(TIMER2_BASE + DMT_TWPS) & TWPS_TCLR)
        ;
    REG32(TIMER2_BASE + DMT_TCLR) = TCLR_ST | TCLR_AR;
    dsb();

    uart_printf("[TIMER] DMTIMER2 @ 0x%08x period=%uus reload=0x%08x\n",
                TIMER2_BASE, period_us, reload);
}

void timer_irq(void)
{
    /* Ack overflow at the timer side (write-1-to-clear) */
    REG32(TIMER2_BASE + DMT_IRQSTATUS) = IRQ_OVF;
    dsb();

    tick_count++;
    if (user_handler)
        user_handler();
}

#else
  #error "timer.c: unknown PLATFORM"
#endif
