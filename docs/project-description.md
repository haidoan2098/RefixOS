# RingNova — Project Description

## Dự án là gì

RingNova là một kernel viết từ đầu trên kiến trúc ARMv7-A, target chính là
BeagleBone Black (TI AM335x, Cortex-A8) với QEMU realview-pb-a8 làm môi
trường phát triển. Không dùng framework, không port từ OS có sẵn — mọi cơ
chế đều tự implement.

Mục tiêu duy nhất là **hiểu OS hoạt động bên trong bằng cách tự tay xây
dựng từng thành phần cốt lõi** — từ boot sequence, MMU, exception handling,
context switch, preemptive scheduling, đến syscall interface và interactive
shell.

Tư duy thiết kế giống Linux ARM 32-bit: 3G/1G split, per-process page
table, syscall qua SVC, preemptive scheduler. Toolchain là GNU. Code là
của riêng mình.

---

## Trạng thái

Implemented end-to-end trên cả QEMU và BeagleBone Black. Boot → 3 user
process → shell tương tác qua UART. ~5000 dòng C + assembly + linker
scripts. Chọn platform tại build time (`make PLATFORM=qemu|bbb`); kernel
core chung, chỉ chip drivers + board wire-up khác.

---

## Kiến trúc tổng thể

```text
┌─────────────────────────────────────────────┐
│                 USERSPACE                   │
│                                             │
│   Process 0        Process 1      Process 2 │
│   (counter)       (runaway)        (shell)  │
│                                             │
│          Minimal libc + Syscall wrappers    │
├──────────────────┬──────────────────────────┤
│                  │ Syscall (SVC)            │
│     KERNEL       │                          │
│                  │                          │
│  ┌────────────┐  │  ┌─────────────────────┐ │
│  │ Scheduler  │  │  │   Syscall Handler   │ │
│  │ round-robin│  │  │ write, read, exit   │ │
│  │  10ms tick │  │  │ yield, getpid       │ │
│  │            │  │  │ ps, kill            │ │
│  └─────┬──────┘  │  └─────────────────────┘ │
│        │         │  ┌─────────────────────┐ │
│  ┌─────▼──────┐  │  │        MMU          │ │
│  │  Process   │  │  │  per-process page   │ │
│  │ Management │  │  │  table, isolation   │ │
│  │    PCB     │  │  └─────────────────────┘ │
│  └────────────┘  │                          │
│                  │                          │
│  ┌────────────────────────────────────────┐ │
│  │           Exception Handler            │ │
│  │   IRQ · SVC · Data Abort · Undefined   │ │
│  └────────────────────────────────────────┘ │
│                                             │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐ │
│  │   UART   │  │  Timer   │  │   INTC    │ │
│  │  driver  │  │  10ms    │  │  GIC v1   │ │
│  └──────────┘  └──────────┘  └───────────┘ │
├─────────────────────────────────────────────┤
│              HARDWARE                       │
│   Cortex-A8 · DDR / RAM · UART · Timer      │
└─────────────────────────────────────────────┘
```

---

## Các thành phần

### Boot & Entry

CPU vào kernel ở PA. start.S compute PHYS_OFFSET, gọi `mmu_init` xây boot
page table, bật MMU, rồi `ldr pc, =_start_va` trampoline PC sang VA cao
(`KERNEL_VIRT_BASE = 0xC0000000+`). Từ đó kernel chạy hoàn toàn ở high VA.
Identity RAM bị drop sau khi process_init_all xong để stray PA dereference
fault ngay.

### Memory & MMU

Section mapping 1 MB. Mỗi process có L1 page table 16 KB riêng (`proc_pgd[3]`
trong BSS). Kernel high-half (`0xC0000000+`) mirror trong mọi page table,
user section `0x40000000` map sang PA riêng per-process. NULL guard ở entry 0.

Cache coherency tự lo: `process_init_all` dọn D-cache + invalidate I-cache
sau khi kmemcpy user binary; `context_switch` invalidate TLB + I-cache sau
khi swap TTBR0.

### Exception & Interrupt

Vector table cài thủ công, VBAR trỏ vào kernel high VA. 7 vector entry:
Reset, Undef, SVC, Prefetch Abort, Data Abort, IRQ, FIQ.

**Exception-to-kernel-stack flow:**

Khi exception xảy ra, CPU tự chuyển sang exception mode (IRQ/SVC/ABT) và
dùng stack riêng của mode đó. Nhưng exception stack là **shared** giữa các
process — nếu chạy logic phức tạp trên đó, preemption sẽ corrupt stack.
Flow đúng:

```text
1. CPU chuyển sang exception mode (ví dụ IRQ mode)
2. Save registers tạm lên IRQ stack (trampoline)
3. Switch sang SVC mode NGAY LẬP TỨC
4. Lấy kernel stack của process hiện tại từ PCB (qua banked SP_svc)
5. Save full context lên kernel stack per-process
6. Xử lý exception (timer tick, syscall, ...)
7. Restore context từ kernel stack per-process
8. Return về user mode (rfefd/ldmfd^)
```

IRQ entry dùng `srsdb` + `stmfd` để push thẳng lên SVC stack — giảm trampoline
xuống còn 3-4 instruction trên IRQ stack.

**Fault isolation:** Data Abort / Prefetch Abort / Undefined Instruction
handler kiểm tra SPSR mode. Nếu fault từ USR → mark current DEAD, schedule
process kế tiếp, kernel sống. Nếu fault từ kernel mode → panic + halt thật.

### Process Management

3 process static, khởi tạo lúc boot. Mỗi process có:

- PCB (`process_t`) lưu banked register state + page table base + kernel SP
- L1 page table riêng 16 KB
- Kernel stack riêng 8 KB (tạo sẵn 25-word initial frame)
- 1 MB user PA slot riêng (counter/runaway/shell binary load vào đây)
- Chạy ở User mode (`CPSR = 0x10`)

**Process state machine:**

```text
         schedule()            yield / timer IRQ
  READY ──────────► RUNNING ──────────────► READY
                      │
                      │ sys_read chờ UART
                      ▼
                   BLOCKED ──► READY   (UART RX IRQ wake)
                      │
                      │ sys_exit / kill / fault
                      ▼
                    DEAD        (scheduler skip vĩnh viễn)
```

| State     | Ý nghĩa                                                       |
| --------- | ------------------------------------------------------------- |
| `READY`   | Sẵn sàng chạy, nằm trong run queue                            |
| `RUNNING` | Đang chiếm CPU                                                |
| `BLOCKED` | Chờ event (UART input), bị loại khỏi run queue                |
| `DEAD`    | Đã exit / kill / fault, scheduler không bao giờ pick          |

**Tại sao cần BLOCKED:** Nếu không có BLOCKED, `read()` chỉ có thể polling
— shell process burn 100% time slice khi chờ input. Với BLOCKED, scheduler
skip process đang chờ → các process khác chạy đầy đủ. Khi UART RX IRQ fire,
handler gọi `scheduler_wake_reader()` chuyển state về READY.

### Scheduler

Preemptive round-robin. Timer fire mỗi 10ms (SP804 trên QEMU, DMTIMER2 trên
BBB) — `scheduler_tick` set flag `need_reschedule`, tail của `handle_irq`
(và `handle_svc`) gọi `schedule()` để swap process. Không process nào giữ
CPU vô hạn.

### Context Switch

Pure assembly: `context_switch(prev, next)` lưu callee-saved regs
(r4-r11, lr) cùng banked SP_usr/LR_usr vào prev, swap TTBR0, flush TLB +
I-cache, load tương ứng cho next, rồi `bx lr` về resume point của next.

Initial kernel stack frame (25 words: 9 kernel-resume + 16 IRQ-exit) cho
phép cùng code path cho first-time entry và preempted resume — first entry
chỉ là special case với prev=NULL và lr trỏ sẵn vào trampoline
`ret_from_first_entry`.

### Syscall Interface

User process không thể gọi kernel trực tiếp. Phải đi qua `svc #0`. ABI
theo Linux ARM EABI:

| Register | Vai trò               |
| -------- | --------------------- |
| `r7`     | Syscall number        |
| `r0..r3` | Argument 0..3         |
| `r0`     | Return value (on ret) |

**Danh sách syscall:**

| Syscall   | Mô tả                                                                |
| --------- | -------------------------------------------------------------------- |
| `write`   | In chuỗi ra UART                                                     |
| `read`    | Đọc ký tự từ UART (blocking — process BLOCKED nếu chưa có data)      |
| `exit`    | Kết thúc process (state → DEAD)                                      |
| `yield`   | Nhường CPU tự nguyện                                                 |
| `getpid`  | Lấy ID của process hiện tại                                          |
| `ps`      | Format process table vào user buffer                                 |
| `kill`    | Kill process theo pid (state → DEAD, scheduler skip)                 |

**Syscall pointer validation:**

Kernel không bao giờ deref pointer từ user space trực tiếp. Mọi syscall
nhận pointer (ví dụ `write(fd, buf, len)`) phải validate trước khi access:

```c
/* Pointer phải nằm trong user 1 MB window */
static int valid_user_ptr(const void *ptr, uint32_t len) {
    uint32_t addr = (uint32_t)ptr;
    uint32_t end  = addr + len;
    if (end < addr)                              return 0;  /* overflow */
    if (addr < USER_VIRT_BASE)                   return 0;
    if (end > USER_VIRT_BASE + USER_REGION_SIZE) return 0;
    return 1;
}
```

Nếu pointer không hợp lệ → syscall return `E_FAULT`, không crash kernel.
Đây là phiên bản đơn giản hóa của `copy_from_user()` / `copy_to_user()`
trong Linux.

### UART / printk

Debug output và I/O input. PL011 trên QEMU, NS16550 trên BBB. TX polling
(uart_putc spin chờ FIFO không full), RX interrupt-driven (PL011 RXIM +
RTIM, GIC IRQ 44) đẩy ký tự lên ring buffer 128 byte cho `sys_read` tiêu
thụ.

---

## Userspace

Không dùng glibc. Tự viết những thứ tối thiểu để user process chạy được:

- **`crt0.S`** — entry point `_ustart`: gọi `main()`, nếu return thì
  `sys_exit`
- **Syscall wrappers** ([user/libc/syscall.h](../user/libc/syscall.h)) —
  inline asm bọc `svc #0`
- **Minimal libc** ([user/libc/ulib.c](../user/libc/ulib.c)) — `ulib_puts`,
  `ulib_putu`, `ulib_strlen`, `ulib_strcmp`, `ulib_atoi`
- **3 user program** — counter, runaway, shell

Mỗi app build riêng thành `.bin` flat (link tại VA `0x40000000`), embed
vào kernel image qua `.incbin`. `process_init_all` copy đúng binary vào
PA slot của đúng pid.

---

## 3 process khi chạy

| Process | Chạy ở | Làm gì | Chứng minh |
| --- | --- | --- | --- |
| **counter** | User mode | In số đếm tag pid, gọi `yield` mỗi vòng | Cooperative + scheduler hoạt động |
| **runaway** | User mode | Vòng lặp vô hạn, **không bao giờ yield** | Preemption thật — kernel cưỡng chế CPU |
| **shell** | User mode | Đọc dòng lệnh từ UART, parse + dispatch 6 lệnh | Interactive + isolation responsive |

Người dùng kết nối qua serial console — đúng paradigm Unix/Linux ban đầu.

**Shell commands:**

| Lệnh         | Mô tả                                                  |
| ------------ | ------------------------------------------------------ |
| `help`       | List 6 lệnh                                            |
| `ps`         | Hiện danh sách process và trạng thái                   |
| `kill <pid>` | Kill một process — test isolation                      |
| `echo <text>`| Print lại text                                         |
| `clear`      | ANSI clear screen                                      |
| `crash`      | Cố ý dereference NULL → trigger Data Abort, fault demo |

---

## Ngoài scope

- Filesystem, VFS
- Networking stack
- `fork` / `exec` (process tạo lúc boot, static)
- Signals, pipe, socket
- IPC (shared memory / message passing)
- Custom bootloader
- POSIX compliance
- Display / HDMI
- Dynamic allocation (`kmalloc`/`free`), page allocator
- SMP / multi-core

---

## Demo scenarios

3 moment cho thấy 3 concept kernel cốt lõi:

| # | Demo | Chứng minh |
| --- | --- | --- |
| 1 | 3 process chạy đồng thời: counter in số, runaway không yield, shell prompt | **Preemption thật** — runaway hang không freeze hệ thống, kernel cưỡng chế CPU mỗi 10ms |
| 2 | Gõ `crash` → shell NULL-deref → shell DEAD, counter + runaway vẫn chạy | **Fault isolation** — MMU + exception handling cô lập, kernel không crash |
| 3 | Gõ `kill 1` → runaway DEAD, counter vẫn chạy. `ps` thấy state thay đổi | **Kernel kiểm soát** — scheduler skip DEAD, `ps` expose state |

---

## Mục tiêu học thuật

Khi hoàn thành, dự án chứng minh hiểu thực tế 4 câu hỏi cốt lõi của OS:

1. **Kernel lấy quyền điều khiển hardware như thế nào?**
2. **Tại sao user process không thể đọc/ghi memory của kernel?**
3. **CPU chuyển giữa các process như thế nào?**
4. **Ai quyết định process nào chạy tiếp theo?**
