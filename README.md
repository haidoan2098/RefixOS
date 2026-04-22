# RingNova

> Bare-metal kernel viết từ đầu trên ARMv7-A. 3 user process chạy preemptive
> qua shell tương tác — toàn bộ ~5000 dòng C + assembly, không framework.

---

## Trạng thái

Implemented end-to-end trên **QEMU realview-pb-a8**: boot → MMU → preemptive
scheduler → syscall ABI → shell với 6 lệnh. BBB port (NS16550 UART RX IRQ) chưa
wire — nhưng kernel + user binaries build clean cho cả hai platform.

---

## Build + chạy

```bash
make                          # build cho QEMU (default)
bash scripts/qemu/run.sh      # chạy interactive — shell prompt sau ~1 s
```

Thoát QEMU: `Ctrl+A` rồi `x`.

```bash
make PLATFORM=bbb             # build cho BeagleBone Black (chưa flash thử)
make clean
```

---

## Demo

Sau khi boot, gõ vào prompt `shell>`:

| Lệnh          | Tác dụng                                                            |
| ------------- | ------------------------------------------------------------------- |
| `help`        | List 6 lệnh                                                         |
| `ps`          | Show pid + state của 3 process                                      |
| `kill <pid>`  | Mark process DEAD, scheduler skip                                   |
| `echo <text>` | Print lại                                                           |
| `clear`       | ANSI clear screen                                                   |
| `crash`       | Shell NULL-deref → fault isolation demo (kernel + counter vẫn chạy) |

---

## Kiến trúc

```text
┌─────────────────────────────────────────────┐
│              USERSPACE (USR)                │
│   counter   ·   runaway   ·   shell         │
│        │           │            │           │
│        └─── crt0 + libc + svc ──┘           │
├─────────────────────────────────────────────┤
│             KERNEL (SVC)                    │
│   Scheduler  ·  Syscall dispatch            │
│   Context switch  ·  Exception handlers     │
│   MMU + per-process page table              │
│   Drivers: UART · Timer · INTC              │
├─────────────────────────────────────────────┤
│                 HARDWARE                    │
│      ARMv7-A Cortex-A8 / DDR3 / UART        │
└─────────────────────────────────────────────┘
```

Kernel linked tại VA `0xC0100000`, user tại VA `0x40000000` (3G/1G split kiểu
Linux ARM). Mỗi process có page table 16 KB + kernel stack 8 KB + user PA slot
1 MB riêng — process A crash không corrupt B/C.

---

## Cốt lõi (mỗi mục có chapter doc riêng)

| Chapter                                          | Topic                                                                  |
| ------------------------------------------------ | ---------------------------------------------------------------------- |
| [00 Foundation](docs/tech_docs/00_foundation.md) | Pre-OS knowledge: ARM modes, banked regs, exception levels             |
| [01 Boot](docs/tech_docs/01_boot.md)             | start.S, dual MEMORY linker (VMA/LMA split), trampoline VA             |
| [02 Exceptions](docs/tech_docs/02_exceptions.md) | Vector table, VBAR, abort/SVC/IRQ entries                              |
| [03 MMU](docs/tech_docs/03_mmu.md)               | 1 MB section mapping, identity bootstrap + drop, high VA alias         |
| [04 Interrupts](docs/tech_docs/04_interrupts.md) | GIC v1, timer driver, IRQ dispatch                                     |
| [05 Process](docs/tech_docs/05_process.md)       | 3 PCB static, per-process L1 + kernel stack, initial frame             |
| [06 Scheduler](docs/tech_docs/06_scheduler.md)   | Bidirectional context_switch, round-robin, BLOCKED state               |
| [07 Syscall](docs/tech_docs/07_syscall.md)       | r7+r0-r3 ABI, dispatch table, user pointer validation, fault isolation |
| [08 Userspace](docs/tech_docs/08_userspace.md)   | crt0, libc, per-app build, .incbin bundle, cache sync                  |
| [09 Shell](docs/tech_docs/09_shell.md)           | UART RX IRQ + ring buffer, sys_read blocking, command parser           |

Memory layout chi tiết: [docs/memory-architecture.md](docs/memory-architecture.md).
Mô tả dự án: [docs/project-description.md](docs/project-description.md).

---

## Toolchain

- `arm-none-eabi-gcc`, GNU Binutils, GDB
- QEMU `qemu-system-arm` (machine `realview-pb-a8`)
- GNU Make

Không phụ thuộc thư viện ngoài.

---

## Tổ chức code

```text
kernel/
├── arch/arm/        # boot, exception, mmu, proc (asm + arch-specific C)
├── drivers/         # uart, timer, intc
├── proc/            # PCB + process_init
├── sched/           # round-robin scheduler
├── syscall/         # dispatch + handlers
├── include/         # public headers
├── linker/          # platform linker scripts
└── main.c

user/
├── crt0.S           # entry: bl main → sys_exit
├── libc/            # syscall wrappers + putu/puts/strcmp/atoi
├── linker/user.ld   # link tại 0x40000000
└── apps/
    ├── counter/     # in số định kỳ + yield
    ├── runaway/     # busy loop không yield (preemption test)
    └── shell/       # 6-command interactive

docs/                # architecture + per-chapter walkthroughs
scripts/qemu/        # QEMU launcher
```

---

## Out of scope

- `fork` / `exec` (process tạo lúc boot, static)
- Filesystem, VFS, networking
- POSIX, signals, pipe, socket
- Dynamic allocation (`kmalloc`), page allocator
- SMP (single-core only)
- IPC (shared memory / message passing)
- Display, HDMI

Đây là kernel học thuật — minimal, không production. Toàn bộ tự viết, không
clone Linux.
