# RingNova — Project Description

## Dự án là gì

RingNova là một kernel viết từ đầu trên kiến trúc ARMv7-A, chạy trực tiếp trên bo mạch BeagleBone Black (TI AM335x, Cortex-A8). Không dùng framework, không port từ OS có sẵn — mọi cơ chế đều tự implement.

Mục tiêu duy nhất là **hiểu OS hoạt động bên trong bằng cách tự tay xây dựng từng thành phần cốt lõi** — từ boot sequence, MMU, exception handling, context switch, preemptive scheduling, đến syscall interface và interactive shell.

Tư duy thiết kế giống Linux: privilege separation, per-process address space, syscall interface, preemptive scheduler. Toolchain là GNU. Code là của riêng mình.

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
│  │  10ms tick │  │  │ yield, getpid, kill │ │
│  └─────┬──────┘  │  │ ps, meminfo         │ │
│        │         │  └─────────────────────┘ │
│  ┌─────▼──────┐  │  ┌─────────────────────┐ │
│  │  Process   │  │  │        MMU          │ │
│  │ Management │  │  │  per-process page   │ │
│  │    PCB     │  │  │  table, isolation   │ │
│  └────────────┘  │  └─────────────────────┘ │
│                  │                          │
│  ┌────────────────────────────────────────┐ │
│  │           Exception Handler            │ │
│  │   IRQ · SVC · Data Abort · Undefined   │ │
│  └────────────────────────────────────────┘ │
│                                             │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐ │
│  │   UART   │  │  Timer   │  │   INTC    │ │
│  │  driver  │  │ DMTimer2 │  │  driver   │ │
│  └──────────┘  └──────────┘  └───────────┘ │
├─────────────────────────────────────────────┤
│              HARDWARE (BBB)                 │
│    Cortex-A8 · DDR3 · UART0 · AM335x       │
└─────────────────────────────────────────────┘
```

---

## Các thành phần sẽ xây dựng

### Boot & Entry

Kernel nhận quyền điều khiển từ U-Boot, tự setup page table, bật MMU, chuyển sang virtual address space rồi nhảy vào `kernel_main()`. Toàn bộ viết bằng assembly.

### Memory & MMU

Kernel và user space được tách biệt hoàn toàn bằng MMU. Mỗi process có page table riêng. Kernel không thể bị truy cập từ user space — nếu vi phạm sẽ bị catch bởi Data Abort handler.

### Exception & Interrupt

Vector table cài thủ công. Mọi sự kiện đều đi qua đây: syscall (SVC), timer interrupt (IRQ), memory fault (Data Abort), illegal instruction (Undefined). Không có đường tắt nào vào kernel.

**Exception-to-kernel-stack flow (theo pattern Linux ARM):**

Khi exception xảy ra, CPU tự chuyển sang exception mode (IRQ/SVC/ABT) và dùng stack riêng của mode đó. Nhưng exception stack là **shared** — nếu chạy logic phức tạp trên đó, preemption sẽ corrupt stack. Flow đúng:

```text
1. CPU chuyển sang exception mode (ví dụ IRQ mode)
2. Save vài registers tạm lên IRQ stack (trampoline)
3. Switch sang SVC mode NGAY LẬP TỨC
4. Lấy kernel stack của process hiện tại từ PCB
5. Save full context (r0-r12, lr, spsr) lên kernel stack per-process
6. Xử lý exception (timer tick, syscall, ...)
7. Restore context từ kernel stack per-process
8. Return về user mode
```

Exception stacks (IRQ/ABT/UND/FIQ) chỉ dùng làm **trampoline** vài instruction — toàn bộ xử lý thực tế diễn ra trên kernel stack per-process. Đây là lý do mỗi process cần kernel stack riêng.

### Process Management

3 process static, khởi tạo lúc boot. Mỗi process có:

- PCB lưu toàn bộ trạng thái CPU
- Page table riêng
- Kernel stack riêng
- Chạy ở User mode

**Process state machine:**

```text
         schedule()            yield / timer IRQ
  READY ──────────► RUNNING ──────────────► READY
                      │
                      │ read() chờ UART / sleep
                      ▼
                   BLOCKED ──► READY   (khi UART có data / timeout)
                      │
                      │ exit()
                      ▼
                    DEAD        (scheduler skip vĩnh viễn)
```

| State | Ý nghĩa |
| --- | --- |
| `READY` | Sẵn sàng chạy, nằm trong run queue |
| `RUNNING` | Đang chiếm CPU |
| `BLOCKED` | Chờ event (UART input), bị loại khỏi run queue |
| `DEAD` | Đã exit hoặc bị kill, scheduler không bao giờ pick |

**Tại sao cần BLOCKED:** Nếu không có BLOCKED, `read()` chỉ có thể polling — shell process burn 100% time slice khi chờ input. Với BLOCKED state, scheduler skip process đang chờ → các process khác được chạy đầy đủ. Khi UART nhận ký tự (RX interrupt), IRQ handler đánh thức process bằng cách chuyển state về READY.

### Scheduler

Preemptive round-robin. DMTimer2 fire interrupt mỗi 10ms — kernel lấy lại CPU, quyết định process nào chạy tiếp. Không process nào giữ CPU vô hạn.

### Context Switch

Viết bằng assembly. Save toàn bộ register của process hiện tại vào PCB, swap page table, restore register của process tiếp theo. CPU tiếp tục chạy như không có gì xảy ra — từ góc nhìn của process.

### Syscall Interface

User process không thể gọi kernel trực tiếp. Phải đi qua `svc #0` — CPU chuyển sang SVC mode, kernel kiểm tra arguments, thực thi, trả kết quả, trở về User mode.

**Danh sách syscall:**

| Syscall | Mô tả |
| --- | --- |
| `write` | In chuỗi ra UART |
| `read` | Đọc ký tự từ UART (blocking — process chuyển BLOCKED nếu chưa có data) |
| `exit` | Kết thúc process (state → DEAD) |
| `yield` | Nhường CPU tự nguyện |
| `getpid` | Lấy ID của process hiện tại |
| `ps` | Lấy danh sách process đang chạy |
| `meminfo` | Lấy thông tin memory |
| `kill` | Kill process theo pid (state → DEAD, scheduler skip) |

**Syscall pointer validation:**

Kernel không bao giờ deref pointer từ user space trực tiếp. Mọi syscall nhận pointer (ví dụ `write(buf, len)`) phải validate trước khi access:

```c
/* Kiểm tra pointer nằm trong user space, không trỏ vào kernel */
static inline int valid_user_ptr(const void *ptr, size_t len) {
    uintptr_t addr = (uintptr_t)ptr;
    return (addr >= USER_VIRT_BASE) &&
           (addr + len <= USER_VIRT_BASE + USER_REGION_SIZE) &&
           (addr + len > addr);  /* overflow check */
}
```

Nếu pointer không hợp lệ → syscall return lỗi, không crash kernel. Với section mapping 1MB, check đơn giản: VA phải nằm trong `0x40000000`–`0x40FFFFFF`. Đây là phiên bản đơn giản hóa của `copy_from_user()` / `copy_to_user()` trong Linux.

### UART / printk

Debug output và I/O duy nhất của hệ thống. Xây dựng đầu tiên trước mọi thứ khác.

---

## Userspace

Không dùng glibc. Tự viết những thứ tối thiểu để user process chạy được:

- **`crt0.S`** — entry point, setup stack, gọi `main()`, gọi `exit()`
- **Syscall wrappers** — C functions gọi kernel qua `svc #0`
- **Minimal libc** — `strlen`, `strcmp`, `memcpy`, `memset`
- **3 user program** — counter, runaway, shell (xem bên dưới)

---

## Hệ thống cuối cùng

Khi hoàn thành, RingNova sẽ boot trên BeagleBone Black thật với 3 process chạy đồng thời:

| Process | Chạy ở | Làm gì | Chứng minh |
| --- | --- | --- | --- |
| **counter** | User mode | In số đếm định kỳ, gọi `yield()` sau mỗi iteration | Cooperative yield + scheduler hoạt động |
| **runaway** | User mode | `while(1) { dummy_compute; }` — **không bao giờ yield** | Preemption thật — process hang không hang hệ thống |
| **shell** | User mode | Nhận lệnh từ UART, gọi syscall để tương tác | Interactive + kill + ps responsive |

Người dùng kết nối qua cáp serial và terminal — đúng paradigm Unix/Linux ban đầu.

**Shell commands:**

| Lệnh | Mô tả |
| --- | --- |
| `ps` | Hiện danh sách process và trạng thái |
| `meminfo` | Hiện thông tin memory |
| `kill <pid>` | Kill một process — test isolation |
| `crash` | Cố ý dereference NULL → trigger Data Abort, test fault isolation |
| `help` | Hiện danh sách lệnh |

---

## Ngoài scope

- Filesystem, VFS
- Networking stack
- `fork` / `exec`
- Signals, pipe, socket
- **IPC (shared memory / message passing)** — monolithic kernel không cần IPC là core; `shm_map` có thể thêm sau nếu cần, không thay đổi kernel architecture
- Custom bootloader
- POSIX compliance
- Display / HDMI
- Dynamic allocation (kmalloc/free), page allocator
- SMP / multi-core

---

## Demo

3 moment showstopper cho defense — mỗi moment prove 1 concept kernel cốt lõi:

| # | Demo | Chứng minh |
| --- | --- | --- |
| 1 | 3 process chạy đồng thời: counter in số, runaway `while(1)` không yield, shell response prompt | **Preemption thật** — process hang không freeze hệ thống, kernel cưỡng chế lấy CPU mỗi 10ms |
| 2 | Gõ `crash` trong shell → shell NULL-deref → Data Abort, shell DEAD, counter + runaway vẫn chạy | **Fault isolation** — MMU + exception handling cô lập lỗi, kernel không crash theo |
| 3 | Gõ `kill 1` → runaway DEAD, counter vẫn chạy. `ps` thấy state thay đổi | **Kernel kiểm soát** — `kill` + scheduler skip DEAD + `ps` expose state |
| 4 | Gõ `ps` và `meminfo` — liệt kê pid, state, pgd, kernel stack, user PA của 3 process | Kernel quản lý đầy đủ metadata process |

---

## Mục tiêu học thuật

Khi hoàn thành, dự án chứng minh hiểu thực tế 4 câu hỏi cốt lõi của OS:

1. **Kernel lấy quyền điều khiển hardware như thế nào?**
2. **Tại sao user process không thể đọc/ghi memory của kernel?**
3. **CPU chuyển giữa các process như thế nào?**
4. **Ai quyết định process nào chạy tiếp theo?**
