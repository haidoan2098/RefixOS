#ifndef KERNEL_BOARD_H
#define KERNEL_BOARD_H

/*
 * board.h — Platform-specific addresses and constants
 *
 * All hardware base addresses and platform parameters MUST be defined here.
 * Never hard-code these values inside driver files.
 *
 * Selected via -DPLATFORM_QEMU or -DPLATFORM_BBB at compile time.
 */

/* ------------------------------------------------------------------ */
/*  QEMU  realview-pb-a8                                               */
/* ------------------------------------------------------------------ */
#ifdef PLATFORM_QEMU

  /* Physical RAM base */
  #define RAM_BASE        0x70000000U
  #define RAM_SIZE        (128U * 1024U * 1024U)

  /* Kernel virtual base (3G/1G split) and physical load address */
  #define KERNEL_VIRT_BASE 0xC0000000U
  #define KERNEL_PHYS_BASE 0x70100000U

  /* Physical-to-virtual offset (used in early boot before MMU)
   * VA = PA + VIRT_OFFSET; PA = VA - VIRT_OFFSET               */
  #define PHYS_OFFSET     (KERNEL_VIRT_BASE - RAM_BASE) /* 0x50000000 */

  /* PL011 UART0 — ARM PrimeCell, realview-pb-a8
   * Reference: ARM DDI 0183 (PL011 TRM)                        */
  #define UART0_BASE      0x10009000U

  /* SP804 Dual Timer 0 — tick source for scheduler
   * Reference: ARM DDI 0271 (SP804 TRM)                        */
  #define TIMER0_BASE     0x10011000U

  /* GIC v1 — realview-pb-a8 maps CPU interface then distributor.
   * Reference: ARM IHI 0048 (GIC Architecture Specification)  */
  #define GIC_CPU_BASE    0x1E000000U
  #define GIC_DIST_BASE   0x1E001000U

  /* SP804 is clocked at 1 MHz on realview-pb-a8 */
  #define TIMER_CLK_HZ    1000000U

/* ------------------------------------------------------------------ */
/*  BeagleBone Black  AM335x                                           */
/* ------------------------------------------------------------------ */
#elif defined(PLATFORM_BBB)

  /* Physical RAM base — DDR3 */
  #define RAM_BASE        0x80000000U
  #define RAM_SIZE        (512U * 1024U * 1024U)

  /* Kernel virtual base and physical load address */
  #define KERNEL_VIRT_BASE 0xC0000000U
  #define KERNEL_PHYS_BASE 0x80000000U

  /* Physical-to-virtual offset */
  #define PHYS_OFFSET     (KERNEL_VIRT_BASE - RAM_BASE) /* 0x40000000 */

  /* UART0 — NS16550 compatible
   * Reference: AM335x TRM §19                                  */
  #define UART0_BASE      0x44E09000U

  /* DMTIMER2 — OS tick source
   * Reference: AM335x TRM §20                                  */
  #define TIMER2_BASE     0x48040000U

  /* INTC — Interrupt Controller
   * Reference: AM335x TRM §6                                   */
  #define INTC_BASE       0x48200000U

  /* CM_PER — Clock Module for peripherals (DMTIMER2 gate)
   * Reference: AM335x TRM §8                                   */
  #define CM_PER_BASE     0x44E00000U

  /* DMTIMER2 fed from CLK_M_OSC = 24 MHz */
  #define TIMER_CLK_HZ    24000000U

#else
  #error "Unknown PLATFORM — define PLATFORM_QEMU or PLATFORM_BBB"
#endif

#endif /* KERNEL_BOARD_H */
