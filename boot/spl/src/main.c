/* ============================================================
 * boot/spl/src/main.c
 * RefixOS SPL Main — BeagleBone Black (AM335x)
 *
 * Runs in SRAM @ 0x402F0400 (loaded by AM335x ROM Code as MLO).
 * Target boot flow: ROM → SPL → kernel.
 * Current responsibility at this stage: bring up essential hardware,
 * prepare DDR, load the kernel image into DDR, then jump to 0x80000000.
 * Current implementation is still in bring-up and stops before DDR/MMC jump.
 *
 * Stages:
 *   0. Enable clocks (PRCM)
 *   1. Disable WDT1 (done in start.S already — verified here)
 *   2. UART init + banner
 *   3. Clock init (DDR PLL)       ← TODO: implement clock.c
 *   4. DDR3 init (EMIF)           ← TODO: implement ddr.c
 *   5. MMC/SD init + load kernel  ← TODO: implement mmc.c
 *   6. Jump to kernel @ 0x80000000
 * ============================================================ */

#include "am335x.h"
#include "boot.h"

#define KERNEL_LOAD_ADDR    0x80000000U
#define KERNEL_START_SECTOR 2048U
#define KERNEL_MAX_SECTORS  2048U

void panic(const char *msg)
{
    uart_puts("\r\nPANIC: ");
    uart_puts(msg);
    uart_puts("\r\nSPL HALTED.\r\n");
    while (1);
}

void c_prefetch_abort_handler(void)
{
    uint32_t ifsr, ifar;
    asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r" (ifsr));  /* IFSR */
    asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r" (ifar));  /* IFAR */

    uart_puts("\r\n=== PREFETCH ABORT ===\r\n");
    uart_puts("IFSR: "); uart_print_hex(ifsr); uart_puts("\r\n");
    uart_puts("IFAR: "); uart_print_hex(ifar); uart_puts("\r\n");
    while (1);
}

void c_data_abort_handler(void)
{
    uint32_t dfsr, dfar;
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r" (dfsr));  /* DFSR */
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r" (dfar));  /* DFAR */

    uart_puts("\r\n=== DATA ABORT ===\r\n");
    uart_puts("DFAR: "); uart_print_hex(dfar); uart_puts("\r\n");
    uart_puts("DFSR: "); uart_print_hex(dfsr); uart_puts("\r\n");
    while (1);
}

void c_undef_handler(void)
{
    uart_puts("\r\n=== UNDEFINED INSTRUCTION ===\r\n");
    while (1);
}

void bootloader_main(void)
{
    /* --------------------------------------------------------
     * STAGE 0: Enable essential clocks
     * L3 and L4LS clock domains must be active before
     * accessing any peripheral in those domains.
     * -------------------------------------------------------- */
    writel(CLKSTCTRL_SW_WKUP, CM_PER_L3_CLKSTCTRL);
    writel(CLKSTCTRL_SW_WKUP, CM_PER_L4LS_CLKSTCTRL);
    writel(CLKCTRL_MODULEMODE_ENABLE, CM_WKUP_CLKSTCTRL);
    delay(1000);

    /* --------------------------------------------------------
     * STAGE 1: UART init + banner
     * -------------------------------------------------------- */
    uart_init();
    delay(100000);

    /* Push ROM garbage off screen */
    int i;
    for (i = 0; i < 5; i++) {
        uart_puts("\r\n");
    }

    uart_puts("========================================\r\n");
    uart_puts("  RefixOS SPL\r\n");
    uart_puts("  Board: BeagleBone Black (AM335x)\r\n");
    uart_puts("  UART: 115200 8N1 @ 48MHz\r\n");
    uart_puts("========================================\r\n\r\n");

    /* --------------------------------------------------------
     * STAGE 2: DDR PLL + Clock configuration
     * TODO: implement boot/spl/src/clock.c
     * Requires: DDR PLL locked @ 400MHz before DDR init
     * -------------------------------------------------------- */
    uart_puts("  > clock init... ");
    clock_init();
    uart_puts("ok\r\n");

    /* --------------------------------------------------------
     * STAGE 3: DDR3 initialization
     * TODO: implement boot/spl/src/ddr.c
     * After: full board DDR is available from physical 0x80000000.
     * -------------------------------------------------------- */
    uart_puts("  > ddr init... ");
    ddr_init();
    if (ddr_test() != 0) {
        panic("DDR test failed");
    }
    uart_puts("ok\r\n");

    /* --------------------------------------------------------
     * STAGE 4: Load kernel from SD card
     * TODO: implement boot/spl/src/mmc.c
     * Load kernel.bin from sector 2048 (offset 1MB on SD)
     * into DDR @ 0x80000000
     * -------------------------------------------------------- */
    uart_puts("  > mmc init + kernel load... ");
    if (mmc_init() != 0) {
        panic("MMC init failed");
    }
    if (mmc_read_sectors(KERNEL_START_SECTOR, KERNEL_MAX_SECTORS, (void *)KERNEL_LOAD_ADDR) != 0) {
        panic("Kernel load failed");
    }
    uart_puts("ok\r\n");

    /* --------------------------------------------------------
     * STAGE 5: Jump to kernel
     * SPL transfers control directly to the kernel entry at 0x80000000.
     * r0=0, r1=mach_type (BeagleBone Black = 0x0E05 / 3589),
     * r2=0 (no ATAGS for now)
     * -------------------------------------------------------- */
    uart_puts("\r\n");
    uart_puts("SPL: jump kernel @ 0x80000000\r\n");
    uart_flush();

    asm volatile(
        "mov r0, #0\n"
        "ldr r1, =0x0E05\n"
        "mov r2, #0\n"
        "ldr pc, =0x80000000\n"
    );

    panic("Kernel jump returned unexpectedly");
}
