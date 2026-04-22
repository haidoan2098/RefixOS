# Memory Architecture — RingNova Kernel

> Toàn bộ memory layout của RingNova: physical RAM, virtual address space,
> boot transition, per-process page table, stack layout. Cùng layout chạy
> được trên QEMU realview-pb-a8 và BeagleBone Black bằng cách dùng offset
> từ `RAM_BASE`.

---

## 1. Physical Memory Layout

| Platform | RAM_BASE | RAM Size |
| --- | --- | --- |
| QEMU (realview-pb-a8) | `0x70000000` | 128 MB |
| BBB (AM335x) | `0x80000000` | 512 MB |

Kernel image load tại `RAM_BASE + 0x100000` (1 MB offset trên QEMU,
0 offset trên BBB — SPL load thẳng vào `0x80000000`). Per-process user
PA slots cố định ở:

| Region | PA (offset từ RAM_BASE) | Size | Mục đích |
| --- | --- | --- | --- |
| Kernel image (.text → .stack) | 0 → ~0x14000 | ~80 KB | Bytes load từ ELF |
| `boot_pgd` (.bss.pgd) | trong .bss | 16 KB | Boot L1 page table, 16 KB aligned |
| `proc_pgd[3]` (.bss.proc_pgd) | trong .bss | 48 KB | Per-process L1 tables |
| Exception + per-process stacks | trong .bss/.stack | ~36 KB | SVC/IRQ/ABT/UND/FIQ + 3 kernel stacks |
| Process 0 user memory | `+0x200000` | 1 MB | counter binary + .bss + user stack |
| Process 1 user memory | `+0x300000` | 1 MB | runaway |
| Process 2 user memory | `+0x400000` | 1 MB | shell |

Kernel image (.text + .data + .bss) chiếm ~1.4 MB. Bảng pgd nằm trong
.bss với alignment 16 KB. Layout cụ thể do linker quyết — xem
[kernel/linker/kernel_qemu.ld](../kernel/linker/kernel_qemu.ld).

**Tại sao mỗi process 1 MB:** khớp với 1-level page table section
descriptor (1 MB granularity). Một entry map đúng 1 section → đơn giản,
không cần L2 page table.

**Tại sao physical pages tách biệt per-process:** mỗi process có vùng
physical riêng → MMU isolation là thật. Process A crash hoặc `kill` không
corrupt memory của B.

---

## 2. Virtual Memory Map (final state)

Trạng thái sau khi `mmu_drop_identity()` chạy — đây là layout kernel
thấy trong cả thời gian sống còn lại.

```text
0xFFFFFFFF  ┌──────────────────────┐
            │  Unmapped             │
0xC7FFFFFF  ├══════════════════════┤  ← KERNEL_VIRT_BASE + RAM_SIZE
            │  Kernel Space         │  Mirror trong MỌI page table
            │  .text, .rodata       │  (kernel + per-process pgd cùng entries
            │  .data, .bss          │   ở range này)
            │  page tables          │
            │  exception stacks     │
            │  kernel stacks        │
            │  user_binaries blob   │
0xC0000000  ├══════════════════════┤  ← KERNEL_VIRT_BASE
            │  Unmapped             │
0x1E001FFF  ├──────────────────────┤
            │  GIC distributor      │  Identity (QEMU)
0x1E000000  ├──────────────────────┤
            │  GIC CPU interface    │  Identity (QEMU)
            │  Unmapped             │
0x101FFFFF  ├──────────────────────┤
            │  Peripherals          │  UART0, SP804 (QEMU) — identity
0x10000000  ├──────────────────────┤
            │  Unmapped             │
0x40100000  ├──────────────────────┤  ← USER_STACK_TOP
            │  User stack           │  Grows down
            ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
            │  User .bss + .data    │
            │  User .text           │
0x40000000  ├══════════════════════┤  ← USER_VIRT_BASE
            │  Unmapped             │
0x000FFFFF  ├──────────────────────┤
            │  NULL guard          │  Entry 0 = FAULT — `*NULL` traps
0x00000000  └──────────────────────┘
```

Trên BBB peripherals đổi sang `0x44E00000` (UART0/CM_PER) + `0x48000000`
(INTC/DMTIMER), cũng identity-mapped.

---

## 3. Boot Memory Transition

Từ power-on đến lúc kernel chạy ổn định ở high VA. 7 bước.

### 3.1. CPU vào kernel

QEMU `-kernel` đọc ELF, load segments tới LMA (= PA), set PC =
`_start_phys` (LMA của `_start`). MMU off, PA = bus address.

### 3.2. start.S — early stack + BSS zero (PA)

Linker symbol (`_svc_stack_top`, `_bss_start`) resolve ra VA cao
`0xC0...`. MMU chưa bật → phải convert sang PA bằng cách trừ
`PHYS_OFFSET` (= VA - PA). PHYS_OFFSET tính tại runtime: `adr r0, _start`
cho PA, `ldr r4, =_start` cho VA, sub ra offset.

```text
adr  r0, _start              @ r0 = PA of _start
ldr  r4, =_start             @ r4 = VA of _start
sub  r4, r4, r0              @ r4 = PHYS_OFFSET
ldr  r1, =_svc_stack_top     @ VA
sub  sp, r1, r4              @ sp = PA of stack
```

### 3.3. mmu_init(phys_offset) — build boot_pgd tại PA

`mmu_init` được gọi với phys_offset trong r0. Nó convert `boot_pgd`
symbol VA → PA pointer, ghi vào L1 table qua PA pointer (vì VA chưa map).
Build:

- Identity: `RAM_BASE → RAM_BASE` (tạm — để PC sống được sau MMU on)
- Kernel high VA: `0xC0000000 → RAM_BASE`
- Peripherals: identity, strongly-ordered, XN

Chưa có user space — sẽ map khi tạo per-process pgd.

### 3.4. mmu_enable(pgd_pa) — bật SCTLR.M

```text
DSB → ICIALLU → DSB → ISB
TTBR0 ← pgd_pa | walk attrs (0x4A)
TTBCR ← 0       (TTBR0 phủ 4 GB)
DACR  ← 0x1     (Domain 0 = Client)
TLBIALL → DSB → ISB
SCTLR ← M=1, C=1, I=1, Z=1
ISB
```

CPU vẫn chạy ở PA → identity map giữ PC valid.

### 3.5. Trampoline PC sang VA

```text
ldr pc, =_start_va    @ absolute load của symbol (resolve sang VA)
```

Sau lệnh này PC = `0xC01000xx`. Mọi instruction fetch tiếp theo đi qua
MMU tại high VA. Kernel không bao giờ còn chạy ở PA.

### 3.6. Re-setup banked stacks ở VA

Trong `_start_va`:

```text
cps #0x11 / ldr sp, =_fiq_stack_top
cps #0x12 / ldr sp, =_irq_stack_top
cps #0x17 / ldr sp, =_abt_stack_top
cps #0x1B / ldr sp, =_und_stack_top
cps #0x13 / ldr sp, =_svc_stack_top    @ kernel mode
```

Pre-trampoline chỉ có SVC stack PA (vừa đủ cho `mmu_init`); bây giờ cài
đầy đủ các mode stack tại VA.

### 3.7. kmain → mmu_drop_identity sau process_init_all

`kmain` chạy hoàn toàn ở VA. Sau khi `process_init_all` đã copy mọi user
binary (qua high-VA alias), gọi `mmu_drop_identity()`:

```text
1. Xóa entries [0x700..0x77F] trong boot_pgd → set 0 (FAULT)
2. TLBIALL + DSB + ISB
```

Stray PA dereference từ điểm này bất kỳ đâu = Data Abort tức thời. Bug
lộ ngay thay vì âm thầm corrupt.

---

## 4. Per-process Page Table

Mỗi process có L1 table riêng (16 KB, 4096 entries). Kernel region giống
nhau, user region khác nhau.

| VA Region | Process 0 (counter) | Process 1 (runaway) | Process 2 (shell) |
| --- | --- | --- | --- |
| `0x00000000` | Fault (NULL guard) | Fault | Fault |
| `0x40000000` (User) | → `RAM_BASE+0x200000` | → `RAM_BASE+0x300000` | → `RAM_BASE+0x400000` |
| `0xC0000000+` (Kernel) | → `RAM_BASE` | → `RAM_BASE` | → `RAM_BASE` |
| Peripherals | Identity | Identity | Identity |

3 process có 3 PA riêng cho user region → process A crash không corrupt
memory của B/C. Kernel region (`0xC0000000+`) và peripherals được mirror
vào cả 3 PGD để syscall handler chạy được trên bất kỳ process nào.

`pgtable_build_for_proc` copy mọi entry non-zero của `boot_pgd` (trừ
identity RAM range), rồi cài 1 entry user section ở `pgd[0x400]`.

### Context switch — swap address space

```text
1. Save callee-saved regs (r4-r11 + lr) onto prev's kernel stack
2. Save banked SP_usr, LR_usr vào prev->ctx
3. TTBR0 ← next->pgd_pa | 0x4A
4. TLBIALL + ICIALLU + DSB + ISB
5. Load banked SP_usr, LR_usr từ next->ctx
6. SP_svc ← next->ctx.sp_svc
7. Pop {r4-r11, lr} từ next's kernel stack → bx lr
```

I-cache invalidate cần thiết: cùng VA `0x40000000` ánh xạ sang PA khác
giữa các process, I-cache có tag VA-indexed.

---

## 5. Stack Layout

### Exception mode stacks (shared, kernel space)

Dùng chung cho tất cả process — chỉ 1 CPU, 1 exception tại 1 thời điểm.
Các stack này chỉ làm **trampoline** — save vài registers rồi switch sang
SVC mode + per-process kernel stack ngay. Không chạy logic phức tạp trên
exception stack.

| Mode | Size |
| --- | --- |
| FIQ | 512 B |
| IRQ | 1 KB |
| ABT | 1 KB |
| UND | 1 KB |
| SVC | 8 KB (chỉ dùng trước khi process đầu tiên chạy) |

Layout cụ thể trong [kernel/linker/kernel_qemu.ld](../kernel/linker/kernel_qemu.ld)
section `.stack`. Tổng ~11.5 KB.

**Constraint:** không re-enable IRQ bên trong IRQ handler. Nested IRQ
trên shared IRQ stack sẽ corrupt. Single-core + no IRQ nesting = safe.

### Exception-to-kernel-stack flow (IRQ)

```text
Khi timer IRQ fire trong USR mode:

1. CPU → IRQ mode, SP = banked IRQ stack (shared)
2. sub   lr, lr, #4              @ fix return address
3. srsdb sp!, #0x13              @ push {LR_irq, SPSR_irq} sang SVC stack
                                 @ SVC stack chính là per-process kernel stack
                                 @ (set bởi context_switch lần trước)
4. cps   #0x13                   @ switch sang SVC mode
5. stmfd sp!, {r0-r12, lr}      @ save GPRs + svc_lr lên kernel stack per-proc
6. bl    handle_irq              @ chạy logic
7. ldmfd sp!, {r0-r12, lr}      @ restore
8. rfefd sp!                     @ pop PC + CPSR atomic, return USR mode
```

Toàn bộ xử lý thực tế diễn ra trên kernel stack per-process (set bằng
`context_switch` qua banked SP_svc) — IRQ stack chỉ giữ vài registers
tạm trong 3-4 instruction.

### Kernel stack per-process

8 KB mỗi process, chạy syscall + IRQ handler + scheduler khi process đó
là current. Linker section `.bss.proc_kstack`.

Initial layout (pre-built bởi `process_build_initial_frame`): 25 words
trên đỉnh stack:

```text
Top (low addr)  ┌────────────────────────┐
                │ r4..r11 = 0 (8 words)  │ \
                │ lr = ret_from_first_   │  | 9-word kernel-resume frame.
                │      entry             │  | context_switch pops, bx lr.
                ├────────────────────────┤ /
                │ r0..r12 = 0 (13 words) │ \
                │ svc_lr = 0             │  | 16-word IRQ-exit frame.
                │ user_pc = 0x40000000   │  | ret_from_first_entry pops via
                │ user_cpsr = 0x10       │  | ldmfd + rfefd → USR mode.
                └────────────────────────┘ /
Bottom (high)   ← kstack_base + kstack_size
```

`ctx.sp_svc` trỏ đỉnh kernel-resume frame. Cùng layout cho preempted
resume — context_switch không cần phân biệt first-time vs subsequent.

### User stack per-process

Trong user VA space, backed bởi physical pages riêng.

| Process | VA window | Stack top |
| --- | --- | --- |
| Process 0 | `0x40000000`–`0x400FFFFF` | `0x40100000` |
| Process 1 | `0x40000000`–`0x400FFFFF` | `0x40100000` |
| Process 2 | `0x40000000`–`0x400FFFFF` | `0x40100000` |

Cùng VA nhưng khác PA → mỗi process có stack riêng thật sự.

Stack direction: ARM convention — grows downward (SP giảm dần).

---

## 6. Tổng kết

| Thành phần | Vị trí | Size |
| --- | --- | --- |
| Kernel image (.text + .data) | `RAM_BASE`, mapped tại `0xC0100000+` | ~16 KB code |
| boot_pgd (L1 boot table) | `.bss.pgd`, 16 KB aligned | 16 KB |
| proc_pgd × 3 | `.bss.proc_pgd` | 48 KB |
| Exception stacks | `.stack` (NOLOAD) | 11.5 KB |
| Kernel stack × 3 | `.bss` | 24 KB (8 KB × 3) |
| User binaries (embedded) | `.user_binaries` | ~3 KB tổng |
| User PA slots × 3 | `RAM_BASE + 0x200000..0x500000` | 3 MB |
| **Tổng RAM dùng** | — | **~3.2 MB** |

Phần còn lại của RAM (125 MB trên QEMU, 509 MB trên BBB) bỏ trống —
không có heap allocator nên không claim.
