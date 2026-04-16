/* ============================================================
 * kernel/arch/arm/mm/mmu_enable.S — MMU enable sequence
 *
 * void mmu_enable(uint32_t pgd_pa);
 *   r0 = physical address of L1 page table (16 KB aligned)
 *
 * Turns on MMU, I-cache, D-cache and branch prediction in a
 * single SCTLR write. Caller must have already populated the
 * page table referenced by r0.
 *
 * Sequence adapted from VinixOS mmu_enable.S — verified on BBB
 * hardware. Order of barriers matters: a missed DSB/ISB here
 * will corrupt speculated fetches after M=1.
 * ============================================================ */

    .section .text.mmu, "ax"
    .align  4
    .global mmu_enable

mmu_enable:
    /* 1. Drain in-flight memory writes before touching CP15 */
    dsb

    /* 2. Invalidate I-cache — stale lines from MMU-off fetches
     *    must not survive the MMU turn-on. */
    mov     r1, #0
    mcr     p15, 0, r1, c7, c5, 0       @ ICIALLU
    dsb
    isb

    /* 3. Load TTBR0 = pgd_pa | walk attributes
     *    Bits set: S=1 (shareable), RGN=01 (outer WB-WA),
     *              IRGN=01 (inner WB-WA) → 0x4A */
    orr     r0, r0, #0x4A
    mcr     p15, 0, r0, c2, c0, 0       @ TTBR0

    /* 4. TTBCR = 0 — TTBR0 covers the entire 4 GB space */
    mov     r1, #0
    mcr     p15, 0, r1, c2, c0, 2       @ TTBCR

    /* 5. DACR — domain 0 = Client (enforce AP bits), rest = NoAccess */
    mov     r1, #0x1
    mcr     p15, 0, r1, c3, c0, 0       @ DACR

    /* 6. Invalidate entire TLB */
    mov     r1, #0
    mcr     p15, 0, r1, c8, c7, 0       @ TLBIALL
    dsb
    isb

    /* 7. Flip SCTLR — enable MMU + caches + branch prediction */
    mrc     p15, 0, r1, c1, c0, 0       @ read SCTLR
    orr     r1, r1, #(1 << 0)           @ M  — MMU enable
    orr     r1, r1, #(1 << 2)           @ C  — D-cache
    orr     r1, r1, #(1 << 11)          @ Z  — branch prediction
    orr     r1, r1, #(1 << 12)          @ I  — I-cache
    mcr     p15, 0, r1, c1, c0, 0       @ write SCTLR
    isb                                 @ next fetch goes through MMU

    bx      lr
