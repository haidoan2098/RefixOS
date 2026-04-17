# Memory Architecture — RingNova Kernel

> Thiết kế toàn bộ memory layout cho RingNova. 6 giai đoạn, mỗi giai đoạn xây trên giai đoạn trước.
> Tham khảo: VinixOS (đã chạy trên BBB thật), Linux ARM 32-bit.

---

## Giai đoạn 1 — Physical Memory Layout

Mọi thứ nằm ở đâu trong RAM vật lý. Dùng offset từ `RAM_BASE` để cùng layout chạy được trên cả QEMU và BBB.

| Platform | RAM_BASE | RAM Size |
| --- | --- | --- |
| QEMU (realview-pb-a8) | `0x70000000` | 128 MB |
| BBB (AM335x) | `0x80000000` | 512 MB |

```text
RAM_BASE + 0x00000000  ┌──────────────────────────────┐
                       │  Kernel Image                 │
                       │  .text                        │
                       │  .rodata                      │
                       │  .data                        │
                       │  .bss                         │
RAM_BASE + 0x00100000  ├──────────────────────────────┤  1 MB
                       │  Boot Page Table (16 KB)      │  Dùng lúc boot, bỏ sau
RAM_BASE + 0x00104000  ├──────────────────────────────┤
                       │  Process 0 Page Table (16 KB) │
RAM_BASE + 0x00108000  ├──────────────────────────────┤
                       │  Process 1 Page Table (16 KB) │
RAM_BASE + 0x0010C000  ├──────────────────────────────┤
                       │  Process 2 Page Table (16 KB) │
RAM_BASE + 0x00110000  ├──────────────────────────────┤
                       │  Exception Stacks             │
                       │    SVC:  8 KB                 │
                       │    IRQ:  8 KB                 │
                       │    ABT:  4 KB                 │
                       │    UND:  4 KB                 │
                       │    FIQ:  4 KB                 │
RAM_BASE + 0x00117000  ├──────────────────────────────┤  28 KB total
                       │  Kernel Stack Process 0 (8 KB)│
RAM_BASE + 0x00119000  ├──────────────────────────────┤
                       │  Kernel Stack Process 1 (8 KB)│
RAM_BASE + 0x0011B000  ├──────────────────────────────┤
                       │  Kernel Stack Process 2 (8 KB)│
RAM_BASE + 0x0011D000  ├──────────────────────────────┤
                       │  (reserved)                   │
RAM_BASE + 0x00200000  ├══════════════════════════════┤  2 MB
                       │  Process 0 Memory     (1 MB) │  .text + .data + .bss + user stack
RAM_BASE + 0x00300000  ├──────────────────────────────┤  3 MB
                       │  Process 1 Memory     (1 MB) │
RAM_BASE + 0x00400000  ├──────────────────────────────┤  4 MB
                       │  Process 2 Memory     (1 MB) │
RAM_BASE + 0x00500000  ├──────────────────────────────┤  5 MB
                       │  (free)                       │
                       └──────────────────────────────┘
```

**Tại sao mỗi process 1 MB:** Khớp với 1-level page table section descriptor (1 MB granularity). Một entry map đúng 1 section → đơn giản, không cần L2 page table.

**Tại sao physical pages tách biệt per-process:** Khác VinixOS (shared PA) — RingNova mỗi process có vùng physical riêng → MMU isolation là thật, không chỉ trên VA.

**So sánh VinixOS:** VinixOS để tất cả task dùng chung `PA 0x80000000`, chỉ phân biệt bằng VA. RingNova tách physical → process crash không corrupt memory của process khác.

---

## Giai đoạn 2 — Virtual Memory Map (Final State)

Sau khi MMU bật và identity map đã bị xóa — đây là trạng thái cuối cùng.

```text
0xFFFF0000  ┌──────────────────────┐
            │  Exception Vectors    │  4 KB, kernel only
            │  (high vectors)       │
0xFFFF1000  ├──────────────────────┤
            │  Unmapped             │
0xC0000000  ├══════════════════════┤  ← KERNEL_VIRT_BASE
            │  Kernel Space         │  Giống nhau trong MỌI page table
            │  .text, .rodata       │
            │  .data, .bss          │
            │  page tables          │
            │  exception stacks     │
            │  kernel stacks        │
            │  (128 MB mapped)      │
0xC7FFFFFF  ├──────────────────────┤
            │  Unmapped             │
0x48000000  ├──────────────────────┤  ← Peripherals (BBB)
            │  L4_PER identity map  │  INTC, Timer — kernel only
0x482FFFFF  ├──────────────────────┤
            │  Unmapped             │
0x44E00000  ├──────────────────────┤  ← Peripherals (BBB)
            │  L4_WKUP identity map │  UART0, CM_PER — kernel only
0x44E0FFFF  ├──────────────────────┤
            │  Unmapped             │
0x40100000  ├──────────────────────┤
            │  User Stack           │  Grows down
            ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
            │  User .bss + .data    │
            │  User .text           │
0x40000000  ├══════════════════════┤  ← USER_VIRT_BASE
            │  Unmapped             │
0x00001000  ├──────────────────────┤
            │  NULL guard page      │  Unmapped → fault on *NULL
0x00000000  └──────────────────────┘
```

**So sánh với VinixOS:**

| | VinixOS | RingNova |
| --- | --- | --- |
| NULL page | Không handle | Unmapped → Translation Fault |
| Vectors | Đầu kernel `0xC0000000` | High vectors `0xFFFF0000` (giống Linux) |
| User VA | `0x40000000`, shared PA | `0x40000000`, PA riêng per-process |

**Peripheral mapping:** QEMU và BBB có địa chỉ peripheral khác nhau — identity map theo từng platform. Định nghĩa trong `board.h`, linker script và page table build phải tham chiếu từ đó.

---

## Giai đoạn 3 — Boot Transition

Từ power on đến final memory state. Mỗi bước cho thấy memory map thay đổi ra sao.

### Step 1: U-Boot load kernel → RAM_BASE (MMU OFF)

```text
Lúc này: PA = VA (không có MMU)

RAM_BASE + 0x00000000:  kernel.bin loaded here
0x44E09000:             UART0 (BBB) — access trực tiếp bằng PA
```

### Step 2: entry.S — Clear BSS tại PA

```text
Linker symbols (_bss_start, _bss_end) là VA (0xC0xxxxxx)
→ Phải trừ PHYS_OFFSET để có PA
→ Zero toàn bộ BSS
```

### Step 3: Build boot page table tại PA

```text
Boot page table ở RAM_BASE + 0x00100000

Mapping tạo ra:
  Identity:    RAM_BASE → RAM_BASE          (temporary, để CPU tiếp tục chạy)
  Kernel:      0xC0000000 → RAM_BASE        (permanent)
  Peripherals: identity map                  (permanent)
  High vectors: 0xFFFF0000 → vectors PA      (permanent)

CHƯA map user space — sẽ map khi tạo per-process page table.
```

### Step 4: Enable MMU

```text
TTBR0 ← boot page table PA
DACR  ← Domain 0 = Client (enforce AP)
SCTLR ← M bit = 1
ISB

CPU vẫn đang chạy ở PA → qua identity map → vẫn work
```

### Step 5: Reload stack pointers sang VA

```text
CPS #SVC → SP = VA của SVC stack
CPS #IRQ → SP = VA của IRQ stack
CPS #ABT → SP = VA của ABT stack
CPS #UND → SP = VA của UND stack
CPS #FIQ → SP = VA của FIQ stack
CPS #SVC → trở lại SVC mode
```

### Step 6: Trampoline

```text
ldr pc, =kernel_main    → PC = 0xC0xxxxxx
                         → CPU fetch từ kernel high mapping
                         → Từ đây chạy hoàn toàn trên VA
```

### Step 7: kernel_main → mmu_init()

```text
1. Xóa identity map entries → set = 0 (Fault)
2. Flush TLB (TLBIALL + DSB + ISB)
3. Update VBAR → VA của exception vectors

Memory map lúc này = Final State (giai đoạn 2)
```

---

## Giai đoạn 4 — Per-process Page Table

Mỗi process có 1 page table riêng (16 KB, 4096 entries). Kernel region giống nhau, user region khác nhau.

### Cấu trúc chung

| VA Region | Process 0 (counter) | Process 1 (runaway) | Process 2 (shell) |
| --- | --- | --- | --- |
| `0x00000000` | Fault (NULL guard) | Fault | Fault |
| `0x40000000` (User) | → `RAM_BASE+0x200000` | → `RAM_BASE+0x300000` | → `RAM_BASE+0x400000` |
| `0xC0000000` (Kernel) | → `RAM_BASE` | → `RAM_BASE` | → `RAM_BASE` |
| Peripherals | Identity | Identity | Identity |
| `0xFFFF0000` (Vectors) | → vectors PA | → vectors PA | → vectors PA |

Ba process có 3 PA riêng cho user region → process A crash không corrupt memory của B,
C. Kernel region (`0xC0000000`) và peripherals được mirror vào cả 3 PGD để syscall
handler chạy được trên bất kỳ process nào.

### Context switch — swap page table

```text
1. Save current process registers vào PCB
2. TTBR0 ← new process page table PA
3. Flush TLB (TLBIALL + DSB + ISB)
4. Restore new process registers từ PCB
5. Return → CPU chạy trong address space mới
```

**So sánh VinixOS:** VinixOS dùng 1 page table duy nhất, không swap TTBR0. RingNova swap TTBR0 mỗi context switch — chậm hơn một chút nhưng isolation thật.

---

## Giai đoạn 5 — Stack Layout

### Exception mode stacks (shared, kernel space)

Dùng chung cho tất cả process — vì chỉ 1 CPU, 1 exception tại 1 thời điểm. Các stack này chỉ dùng làm **trampoline** — save vài registers rồi switch sang SVC mode và kernel stack per-process ngay lập tức. Không chạy logic phức tạp trên exception stack.

| Mode | Size | PA (offset từ RAM_BASE) | VA |
| --- | --- | --- | --- |
| SVC | 8 KB | `+0x110000` → `+0x112000` | `0xC0110000` → `0xC0112000` |
| IRQ | 8 KB | `+0x112000` → `+0x114000` | `0xC0112000` → `0xC0114000` |
| ABT | 4 KB | `+0x114000` → `+0x115000` | `0xC0114000` → `0xC0115000` |
| UND | 4 KB | `+0x115000` → `+0x116000` | `0xC0115000` → `0xC0116000` |
| FIQ | 4 KB | `+0x116000` → `+0x117000` | `0xC0116000` → `0xC0117000` |

**Constraint quan trọng:** Không re-enable IRQ bên trong IRQ handler. Nested IRQ trên shared IRQ stack sẽ corrupt. Single-core + no IRQ nesting = safe.

**Exception-to-kernel-stack flow:**

```text
Ví dụ: Timer IRQ fire khi user process đang chạy

1. CPU → IRQ mode, SP = IRQ stack (shared)
2. sub   lr, lr, #4              @ fix return address
   stmfd sp!, {r0-r3, r12, lr}  @ save scratch regs lên IRQ stack (trampoline)
3. mrs   r0, spsr               @ lấy user CPSR
4. cps   #SVC_MODE              @ SWITCH sang SVC mode ngay
5. SP = kernel stack của current process (từ PCB)
6. stmfd sp!, {r0-r12, lr}      @ save FULL context lên kernel stack per-process
7. ... xử lý timer tick, scheduler, context switch ...
8. ldmfd sp!, {r0-r12, lr}      @ restore từ kernel stack
9. movs  pc, lr                  @ return về user mode (restore CPSR từ SPSR)
```

Toàn bộ xử lý thực tế diễn ra trên kernel stack per-process — exception stack chỉ giữ vài registers tạm trong 3-4 instruction.

### Kernel stack per-process

Mỗi process cần kernel stack riêng — dùng khi process vào kernel qua syscall hoặc IRQ. Toàn bộ exception handling, syscall xử lý, scheduler logic đều chạy trên stack này.

Size 8 KB per-process (theo chuẩn Linux ARM `THREAD_SIZE`). 4 KB quá tight khi syscall handler có nested function calls — Linux đã thử 4KB rồi quay lại 8KB.

| Process | Size | PA (offset từ RAM_BASE) | VA |
| --- | --- | --- | --- |
| Process 0 | 8 KB | `+0x117000` → `+0x119000` | `0xC0117000` → `0xC0119000` |
| Process 1 | 8 KB | `+0x119000` → `+0x11B000` | `0xC0119000` → `0xC011B000` |
| Process 2 | 8 KB | `+0x11B000` → `+0x11D000` | `0xC011B000` → `0xC011D000` |

### User stack per-process

Nằm trong user VA space, backed bởi physical pages riêng của mỗi process.

| Process | VA | Grows down from |
| --- | --- | --- |
| Process 0 | `0x40000000`–`0x400FFFFF` | `0x40100000` |
| Process 1 | `0x40000000`–`0x400FFFFF` | `0x40100000` |
| Process 2 | `0x40000000`–`0x400FFFFF` | `0x40100000` |

Cùng VA nhưng khác PA → mỗi process có stack riêng thật sự.

**Stack direction:** ARM convention — grows downward (SP giảm dần).

---

## Tổng kết

| Thành phần | Physical (offset) | Virtual | Size |
| --- | --- | --- | --- |
| Kernel image | `+0x000000` | `0xC0000000` | ~1 MB |
| Boot page table | `+0x100000` | `0xC0100000` | 16 KB |
| Process 0 page table | `+0x104000` | `0xC0104000` | 16 KB |
| Process 1 page table | `+0x108000` | `0xC0108000` | 16 KB |
| Process 2 page table | `+0x10C000` | `0xC010C000` | 16 KB |
| Exception stacks | `+0x110000` | `0xC0110000` | 28 KB |
| Kernel stacks (×3) | `+0x117000` | `0xC0117000` | 24 KB |
| Process 0 memory | `+0x200000` | `0x40000000` | 1 MB |
| Process 1 memory | `+0x300000` | `0x40000000` | 1 MB |
| Process 2 memory | `+0x400000` | `0x40000000` | 1 MB |
| **Tổng** | | | **~5 MB** |

---

## Memory theo Implementation Phase

Memory layout lớn dần qua từng phase implement. Mỗi phase thêm vào layout trước đó.

### Phase 1 — Boot + UART (MMU OFF)

Không có MMU. PA = VA. Chỉ cần kernel và 1 stack.

```text
RAM_BASE + 0x000000  ┌───────────────────┐
                     │  Kernel Image      │
                     │  + SVC Stack       │
                     └───────────────────┘
```

### Phase 2 — MMU (thêm page table + stacks, bật VA)

```text
RAM_BASE + 0x000000  ┌───────────────────┐
                     │  Kernel Image      │
RAM_BASE + 0x100000  ├───────────────────┤  ← MỚI
                     │  Boot Page Table   │  16 KB
RAM_BASE + 0x110000  ├───────────────────┤  ← MỚI
                     │  Exception Stacks  │  28 KB (SVC, IRQ, ABT, UND, FIQ)
                     └───────────────────┘

VA mới: 0xC0000000 (kernel), 0xFFFF0000 (vectors), peripherals
```

### Phase 3 — Exception + Interrupt

Không thêm memory. Vectors nằm trong kernel .text, INTC/Timer dùng peripheral map đã có.

### Phase 4 — Process + Scheduler (bước nhảy lớn nhất)

```text
RAM_BASE + 0x000000  ┌───────────────────┐
                     │  Kernel Image      │
RAM_BASE + 0x100000  ├───────────────────┤
                     │  Boot Page Table   │  16 KB
RAM_BASE + 0x104000  │  Process 0 PT      │  16 KB    ← MỚI
RAM_BASE + 0x108000  │  Process 1 PT      │  16 KB    ← MỚI
RAM_BASE + 0x10C000  │  Process 2 PT      │  16 KB    ← MỚI
RAM_BASE + 0x110000  ├───────────────────┤
                     │  Exception Stacks  │  28 KB
RAM_BASE + 0x117000  ├───────────────────┤
                     │  Kernel Stack ×3   │  24 KB    ← MỚI (8 KB/process)
RAM_BASE + 0x200000  ├───────────────────┤
                     │  Process 0 Memory  │  1 MB     ← MỚI
RAM_BASE + 0x300000  │  Process 1 Memory  │  1 MB     ← MỚI
RAM_BASE + 0x400000  │  Process 2 Memory  │  1 MB     ← MỚI
RAM_BASE + 0x500000  └───────────────────┘

VA mới: 0x40000000 (user space, per-process)
```

### Phase 5 — Syscall + Shell

Không thêm memory. Shell dùng Process 2 memory đã cấp.

### Tóm tắt

| Phase | Thêm gì | Tổng dùng |
| --- | --- | --- |
| Phase 1 | Kernel + SVC stack | ~1 MB |
| Phase 2 | Boot PT + exception stacks | ~1.1 MB |
| Phase 3 | Không thêm | ~1.1 MB |
| Phase 4 | 3 PT + 3 kernel stacks (8KB each) + 3 user regions | ~5 MB |
| Phase 5 | Không thêm | ~5 MB |
