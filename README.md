# RingNova

> Một nhân hệ điều hành tối giản, xây dựng từ đầu trên kiến trúc ARMv7-A.

---

## 1. Giới thiệu

**RingNova** là dự án xây dựng một OS kernel từ đầu (from scratch) trên nền tảng ARM Cortex-A8, cụ thể là bo mạch BeagleBone Black. Không phải một bản clone hay port của Linux, mà là một cài đặt độc lập của các cơ chế cốt lõi mà một hệ điều hành hiện đại thực sự cần có.

Dự án không hướng đến việc tạo ra một OS hoàn chỉnh. Mục tiêu duy nhất là **hiểu sâu bằng cách tự xây dựng** — từ exception vector, MMU, context switch, cho đến preemptive scheduling — tất cả đều viết tay, không dựa vào framework hay thư viện có sẵn.

---

## 2. Nền tảng

| | |
|---|---|
| **Phần cứng** | BeagleBone Black (AM335x, Cortex-A8) |
| **Kiến trúc** | ARMv7-A |
| **Môi trường phát triển** | QEMU → BeagleBone Black |
| **Bootloader** | U-Boot (vendor) |
| **Toolchain** | `arm-none-eabi-gcc`, GNU Binutils, GDB |

---

## 3. Kiến trúc & Thành phần

RingNova được tổ chức xung quanh 4 trục kỹ thuật cốt lõi. Các trục này không độc lập — chúng phụ thuộc nhau theo đúng thứ tự xây dựng: phải có exception model trước thì kernel mới tiếp nhận được quyền điều khiển; phải có kernel ổn định thì mới bật MMU được; phải có MMU thì isolation giữa các process mới là thật.

Ngoài 4 trục chính, hệ thống còn có một số thành phần hạ tầng phải được xây dựng sớm — cụ thể là debug output và kernel allocator — vì toàn bộ các thành phần còn lại đều phụ thuộc vào chúng.

---

### 3.1. Privilege & Exception

Kernel thiết lập ranh giới cứng giữa User mode và Kernel mode theo mô hình đặc quyền của ARM. Mọi yêu cầu từ user space vào kernel — dù là system call, interrupt hay fault — đều đi qua exception vector table được cài đặt thủ công. Không có đường tắt nào.

Khi exception xảy ra, CPU chuyển sang exception mode tương ứng (IRQ/SVC/ABT) nhưng exception stack chỉ dùng làm **trampoline** — save vài registers rồi switch sang SVC mode và kernel stack per-process ngay lập tức. Toàn bộ xử lý thực tế diễn ra trên kernel stack per-process, theo đúng pattern Linux ARM.

**Thành phần liên quan:** Exception vector table, SVC handler, IRQ handler, trap handler.

---

### 3.2. Virtual Memory

MMU được bật và cấu hình từ đầu, không dùng flat memory model. Kernel ánh xạ tại địa chỉ cao (`0xC0000000+`) và hoàn toàn không nhìn thấy từ user space. Mỗi process có page table riêng; ARMv7 hỗ trợ TTBR0 cho user space và TTBR1 cho kernel space, cho phép tách biệt hai vùng mà không cần flush toàn bộ TLB khi context switch.

**Thành phần liên quan:** MMU initialization, page table management, TTBR0/TTBR1 configuration, identity map boot stage.

---

### 3.3. Process & Context Switch

Mỗi process được đại diện bởi một PCB lưu toàn bộ trạng thái: register set, kernel stack pointer, page table base và execution state. Context switch được viết bằng assembly — lưu đầy đủ r0–r15 và CPSR, sau đó swap TTBR0 trước khi khôi phục trạng thái của process tiếp theo.

Mỗi process có 4 trạng thái:

| State | Ý nghĩa |
|---|---|
| `READY` | Sẵn sàng chạy, nằm trong run queue |
| `RUNNING` | Đang chiếm CPU |
| `BLOCKED` | Chờ event (UART input), bị loại khỏi run queue |
| `DEAD` | Đã exit hoặc bị kill, scheduler không bao giờ pick |

**Thành phần liên quan:** PCB, kernel stack per-process (8 KB), context switch routine (assembly).

---

### 3.4. Preemptive Scheduling

Scheduler được kích hoạt bởi timer hardware (DMTIMER2), không phải bởi sự tự nguyện của process. Mỗi khi timer IRQ phát sinh, scheduler quyết định process nào chạy tiếp theo theo cơ chế round-robin. Không có process nào giữ CPU vô thời hạn. Isolation là thật — một process lỗi không ảnh hưởng đến process khác.

**Thành phần liên quan:** Timer driver (DMTIMER2), IRQ handler, round-robin scheduler.

---

### 3.5. Hạ tầng hỗ trợ

Các thành phần dưới đây không thuộc 4 trục chính nhưng là điều kiện cần để toàn bộ hệ thống hoạt động và kiểm thử được.

| # | Thành phần | Vai trò |
|---|---|---|
| 1 | **UART / printk** | Debug output và kernel panic — xây dựng đầu tiên, trước mọi thứ khác |
| 2 | **System calls** | Interface từ user space vào kernel qua SVC: `write`, `read`, `exit`, `yield`, `getpid`, `ps`, `meminfo`, `kill`. Mọi pointer từ user space được validate trước khi kernel deref |
| 3 | **Minimal libc** | Tự viết — đủ để user process chạy được, không dùng glibc |
| 4 | **Minimal shell** | Môi trường tương tác qua UART để kiểm thử toàn bộ hệ thống |

---

## 4. Phạm vi dự án

RingNova được giới hạn có chủ đích. Các thành phần dưới đây nằm ngoài phạm vi:

- Filesystem, VFS, mount
- Networking stack
- `fork` / `exec`
- POSIX compliance
- Signals, pipe, socket
- **IPC (shared memory / message passing)** — monolithic kernel không cần IPC là core
- Display / HDMI
- Dynamic allocation (kmalloc), page allocator
- SMP / multi-core

---

## 5. Quan hệ với Linux và GNU

Về tư tưởng thiết kế kernel — high address split, per-process page table, SVC syscall, preemptive scheduling trên ARM — RingNova giải quyết cùng bài toán theo cùng hướng với Linux. Toolchain sử dụng là GNU (`gcc`, `ld`, `gdb`). Libc được tự viết như một bản thay thế tối giản cho glibc.

> Kernel tư tưởng giống Linux. Toolchain là GNU. Code là của riêng mình.

---

## 6. Kết quả kỳ vọng

Khi hoàn thành, RingNova sẽ khởi động trên BeagleBone Black thật, chạy 2–3 user process cô lập theo cơ chế preemptive scheduling, tiếp nhận system call từ user space, và cung cấp một minimal shell để tương tác — toàn bộ trên một kernel được viết từng dòng từ đầu.
