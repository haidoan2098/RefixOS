#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

/* ============================================================
 * mmu.h — ARMv7-A MMU definitions and public API
 *
 * Section mapping only (1 MB granularity).
 * Reference: ARM ARM B3.5.1 (L1 translation table format).
 * ============================================================ */

#include <stdint.h>

/* Descriptor type (bits [1:0]) */
#define PDE_TYPE_FAULT      0x0U
#define PDE_TYPE_SECTION    0x2U    /* 1 MB section */

/* Section attribute bits */
#define PDE_B               (1U << 2)   /* Bufferable                 */
#define PDE_C               (1U << 3)   /* Cacheable                  */
#define PDE_XN              (1U << 4)   /* Execute Never              */
#define PDE_DOMAIN(n)       ((n) << 5)  /* Domain field (4 bits)      */
#define PDE_AP0             (1U << 10)  /* AP[0]                      */
#define PDE_AP1             (1U << 11)  /* AP[1]                      */
#define PDE_TEX(n)          ((n) << 12)
#define PDE_AP2             (1U << 15)  /* AP[2]                      */
#define PDE_S               (1U << 16)  /* Shareable                  */
#define PDE_NG              (1U << 17)  /* Not Global                 */

/* AP[2:0] combinations under DACR client mode:
 *   001 (AP0)        → kernel RW, user no-access
 *   011 (AP0|AP1)    → kernel RW, user RW
 *   101 (AP0|AP2)    → kernel RO, user no-access
 */
#define PDE_AP_KERN_RW      (PDE_AP0)
#define PDE_AP_KERN_USER_RW (PDE_AP0 | PDE_AP1)

/* Common compound attributes */

/* Kernel code+data: cacheable, bufferable, kernel RW, executable */
#define PDE_KERNEL_MEM      (PDE_TYPE_SECTION | PDE_C | PDE_B \
                             | PDE_AP_KERN_RW | PDE_DOMAIN(0))

/* Device / peripheral: strongly-ordered, kernel RW, XN */
#define PDE_DEVICE          (PDE_TYPE_SECTION | PDE_AP_KERN_RW \
                             | PDE_DOMAIN(0) | PDE_XN)

/* User code + data + stack: cacheable, kernel+user RW, executable */
#define PDE_USER_TEXT       (PDE_TYPE_SECTION | PDE_C | PDE_B \
                             | PDE_AP_KERN_USER_RW | PDE_DOMAIN(0))

/* Page table dimensions */
#define PGD_ENTRIES         4096U
#define PGD_SIZE            (PGD_ENTRIES * 4U)  /* 16 KB */
#define PGD_ALIGN           16384U

/* Virtual address constants */
#define VA_KERNEL_BASE      0xC0000000U

/* Public API */
void     mmu_init(void);
void     mmu_build_boot_pgd(void);
void     mmu_enable(uint32_t pgd_pa);   /* implemented in assembly */
uint32_t mmu_read_sctlr(void);
uint32_t mmu_read_ttbr0(void);

/* Populate a per-process L1 table:
 *   - mirror kernel high-half + peripherals from boot_pgd
 *   - install 1 MB user section at VA USER_VIRT_BASE → user_pa
 *   - leave NULL guard (entry 0) as FAULT
 * Caller owns the 16 KB-aligned pgd buffer. */
void     pgtable_build_for_proc(uint32_t *pgd, uint32_t user_pa);

#endif /* KERNEL_MMU_H */
