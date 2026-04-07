# Chapter 00 — Foundation: Từ C đến silicon

> Bạn viết C, biết OS là gì ở mức khái niệm. Nhưng bạn chưa biết C thật sự chạy thế nào
> trên hardware, chưa đọc được assembly, chưa biết CPU hoạt động ở mức register.
> Chapter này bổ sung đúng những thứ đó — đủ để đọc hiểu 10 chapter tiếp theo.
>
> Mỗi phần kết nối trực tiếp vào chapter sau. Không có kiến thức nào ở đây là "biết cho vui".

---

## 1. C không tự chạy được

Khi bạn viết:

```c
int x = 5;
int y = x + 3;
```

Bạn nghĩ gì xảy ra? Máy tính "hiểu" C rồi chạy?

Không. CPU không biết C là gì. Nó không biết `int` là gì, không biết `+` là gì, không biết
biến là gì. CPU chỉ biết làm một việc: **đọc instruction từ memory, decode, execute**.
Lặp lại. Mãi mãi.

Instruction là mã máy — chuỗi số mà CPU hiểu. Mỗi instruction làm đúng 1 việc đơn giản:
copy số vào register, cộng 2 register, đọc 1 ô nhớ. Không hơn.

Compiler (gcc, clang) là thứ dịch C thành instruction. Đoạn code trên, sau khi compile
cho ARM, trở thành thứ kiểu như:

```
mov   r0, #5        ← đặt số 5 vào register r0
add   r1, r0, #3    ← r1 = r0 + 3 = 8
```

Đó là tất cả. Không có phép thuật. `int x = 5` chỉ là "đặt số 5 vào 1 ô nhớ bên trong CPU".

### Register — ô nhớ bên trong CPU

Register là bộ nhớ nhỏ nhất, nhanh nhất — nằm ngay trong CPU, truy cập trong 1 chu kỳ xung nhịp.
ARM có 16 register chính (r0–r15), mỗi register chứa 32 bit (4 byte).

CPU làm mọi phép tính **trên register**, không trên RAM. Muốn tính `a + b` khi a và b
nằm trong RAM:

```
                    RAM                         CPU
              ┌─────────────┐            ┌──────────────┐
              │ a = 5       │──load──▶   │ r0 = 5       │
              │ b = 3       │──load──▶   │ r1 = 3       │
              │             │            │ add r2,r0,r1  │
              │ c = ?       │◀──store──  │ r2 = 8       │
              └─────────────┘            └──────────────┘
```

3 bước: load a vào r0, load b vào r1, cộng, store kết quả ngược lại RAM.
Đây là thứ **mỗi** phép tính C đều phải đi qua.

### Tại sao điều này quan trọng

Trên Linux, khi bạn viết C, compiler + OS + runtime lo hết phần bên dưới.
Bạn không cần biết register, instruction, hay memory layout.

Nhưng trên bare-metal — không có OS, không có runtime. **Bạn phải tự tạo ra
môi trường mà C cần để chạy được.** Đó là việc đầu tiên kernel phải làm
(→ Chapter 01).

---

## 2. Đọc ARM assembly — vừa đủ để theo dõi code

Phần 1 nói C compile thành instruction. Nhưng khi đọc kernel code, bạn sẽ gặp assembly
thật — không phải pseudocode. Phần này dạy đọc ARM assembly ở mức **đủ để hiểu `start.S`
và các đoạn assembly trong project**, không phải dạy viết.

### Pattern chung

Hầu hết ARM instruction theo dạng:

```
opcode   destination, source1, source2
```

Ví dụ:

```asm
add   r2, r0, r1      @ r2 = r0 + r1
sub   r3, r3, #1       @ r3 = r3 - 1
mov   r0, #5           @ r0 = 5
```

- `r0`, `r1`, ... `r12` — register đa dụng (Chapter 00.1 đã giới thiệu)
- `#5` — giá trị trực tiếp (immediate). Dấu `#` nghĩa là "con số", không phải register
- `@` — comment trong ARM assembly (giống `//` trong C)

### Đọc/ghi memory

```asm
ldr   r0, [r1]         @ r0 = *r1          (load: đọc từ memory)
str   r0, [r1]         @ *r1 = r0          (store: ghi vào memory)
ldr   r0, [r1, #4]     @ r0 = *(r1 + 4)   (offset 4 byte)
str   r0, [r1], #4     @ *r1 = r0; r1 += 4 (post-increment)
```

`[r1]` = address trong r1, tương tự `*ptr` trong C.
`[r1, #4]` = address r1 + 4, tương tự `*(ptr + 1)` với `uint32_t *ptr`.

`ldr r0, =_bss_start` — dạng đặc biệt: load **address** của symbol `_bss_start` vào r0.
Tương tự `r0 = &_bss_start` trong C.

### Nhảy (branch)

```asm
b     label            @ nhảy đến label (goto)
bl    kmain            @ nhảy đến kmain, lưu địa chỉ quay về vào LR
bx    lr               @ nhảy về địa chỉ trong LR (return)
blo   label            @ nhảy nếu kết quả cmp trước đó là "lower" (unsigned <)
```

`bl` = branch and link — **gọi function** trong assembly. Tương tự `kmain()` trong C,
nhưng CPU cũng lưu luôn chỗ quay về (vào LR).

### So sánh và thực thi có điều kiện

```asm
cmp   r0, r1           @ so sánh r0 với r1 (set flag, không lưu kết quả)
strlo r2, [r0], #4     @ store CHỈ KHI r0 < r1 (lo = lower, unsigned)
```

ARM cho phép **gắn điều kiện vào instruction**: `strlo` = `str` + `lo` (chỉ thực hiện
nếu điều kiện "lower" đúng). Đây là cách ARM làm vòng lặp mà không cần `if`.

### Các instruction đặc biệt sẽ gặp trong project

| Instruction | Ý nghĩa | Gặp ở |
|-------------|----------|-------|
| `cpsid if` | Disable IRQ và FIQ | Chapter 01: `start.S` dòng đầu |
| `cps #0x13` | Chuyển CPU sang SVC mode | Chapter 01: setup stacks |
| `mrs r0, cpsr` | Đọc CPSR register vào r0 | Chapter 01: `kmain` đọc mode |
| `msr cpsr, r0` | Ghi r0 vào CPSR | Chapter 02, 05: thay đổi mode |
| `mcr p15, ...` | Ghi vào coprocessor CP15 (MMU, cache) | Chapter 02: set VBAR, Chapter 03: enable MMU |
| `stmia r0, {r0-r12}` | Store nhiều register liên tiếp vào memory | Chapter 05: context switch |
| `ldmia r0, {r0-r12}` | Load nhiều register từ memory | Chapter 05: context switch |
| `svc #0` | Gọi Supervisor Call (syscall) | Chapter 07: user → kernel |

Không cần nhớ hết bây giờ. Khi gặp trong chapter sau, quay lại bảng này tra cứu.

---

## 3. Function call = thao tác trên stack

> Phần này dành cho bạn nào chưa rõ stack hoạt động thế nào ở mức hardware.
> Nếu bạn đã hiểu stack frame, SP, LR — bỏ qua, nhảy thẳng sang phần 4.

Khi C gọi function:

```c
int result = add(5, 3);
```

Chuyện gì thật sự xảy ra? CPU đang chạy từng instruction tuần tự — làm sao nó "nhảy"
vào function `add`, chạy xong rồi quay về đúng chỗ cũ?

### Vấn đề 1: Quay về đâu?

CPU có register đặc biệt tên **PC** (Program Counter) — nó chứa địa chỉ của instruction
tiếp theo. CPU chạy instruction tại PC, rồi PC tự tăng lên. Tuần tự.

Khi gọi function, CPU cần:
1. **Nhớ** địa chỉ quay về (instruction ngay sau lệnh gọi)
2. **Nhảy** PC đến đầu function
3. Khi function xong, **khôi phục** PC về địa chỉ đã nhớ

ARM dùng register **LR** (Link Register, r14) để lưu địa chỉ quay về:

```
bl  add       ← "branch and link": lưu địa chỉ dòng tiếp vào LR, nhảy đến add
              ← khi add xong, nó chạy "bx lr" = nhảy về địa chỉ trong LR
```

### Vấn đề 2: Function gọi function thì sao?

`main` gọi `foo`, `foo` gọi `bar`. Khi `foo` gọi `bar`, LR bị ghi đè — địa chỉ quay về
`main` mất. Cần chỗ lưu nhiều địa chỉ quay về, theo thứ tự.

Đây là lý do stack tồn tại.

### Stack — chồng đĩa

Stack là vùng RAM mà CPU dùng như chồng đĩa: **đặt lên trên, lấy từ trên xuống**
(Last In, First Out).

Register **SP** (Stack Pointer, r13) trỏ đến đỉnh stack. Mỗi function call:

```
Gọi foo():                              foo() return:
┌───────────────┐                       ┌───────────────┐
│               │ ◀── SP (đỉnh mới)    │               │
│  LR cũ       │     push LR           │               │ ◀── SP (trở lại)
│  biến local   │     cấp chỗ          │               │     pop, giải phóng
├───────────────┤                       ├───────────────┤
│  ... caller   │                       │  ... caller   │
└───────────────┘                       └───────────────┘
     ▲ stack mọc xuống                      ▲ stack co lại
```

Mỗi function call **push** một "khung" (stack frame) lên stack: LR (địa chỉ quay về),
biến local, argument. Khi return, **pop** khung đó — SP quay lại vị trí cũ, đĩa được lấy ra.

Gọi sâu bao nhiêu cấp cũng được — mỗi cấp push 1 khung, return thì pop ngược lại.

### Không có stack thì sao?

C compiler **giả định** stack đã có sẵn. Mỗi lời gọi function đều sinh ra instruction
push/pop trên SP. Nếu SP chưa được set, hoặc trỏ vào vùng memory không hợp lệ:

- Push đầu tiên ghi vào địa chỉ rác → **ghi đè data ngẫu nhiên** hoặc **crash**
- Function return đọc LR từ địa chỉ rác → nhảy đến instruction ngẫu nhiên → crash

Không có triệu chứng rõ ràng. Không có error message. Chỉ có hành vi sai hoặc treo.

Trên Linux, OS tạo stack cho mỗi program trước khi chạy. Trên bare-metal — **không ai làm điều đó**.
Việc đầu tiên của boot code là set SP cho đúng vùng RAM (→ Chapter 01, `start.S` dòng đầu tiên
sau mask IRQ).

---

## 4. Memory chỉ là bus address

Khi C viết:

```c
*ptr = 42;
```

CPU gửi address lên **memory bus**, kèm data (42) và tín hiệu "write". Bất kỳ thứ gì
nằm ở address đó sẽ nhận data.

CPU **không biết và không quan tâm** cái gì ở address đó.

### Cùng instruction, khác address = khác hoàn toàn

```
Ghi vào 0x80000000 → ghi vào RAM     → lưu data bình thường
Ghi vào 0x44E09000 → ghi vào UART    → gửi ký tự ra serial port
Ghi vào 0x48040000 → ghi vào Timer   → cấu hình timer hardware
```

Tất cả đều là `str r0, [r1]` (store register vào address). CPU không phân biệt.
Sự khác biệt là **ai ngồi ở address đó** — RAM chip hay hardware controller.

```
         CPU
          │
          │  address + data
          ▼
    ┌─────────────┐
    │  Memory Bus  │
    └──┬──────┬───┘
       │      │
       ▼      ▼
    ┌─────┐ ┌──────┐
    │ RAM │ │ UART │  ← cùng bus, khác address range
    └─────┘ └──────┘
```

Cách truy cập hardware qua memory address gọi là **MMIO** (Memory-Mapped I/O).
Trên ARM, hầu hết mọi hardware đều dùng MMIO — không có instruction đặc biệt cho I/O.

### Hệ quả cho kernel developer

Điều khiển hardware = **đọc/ghi đúng address, đúng giá trị, đúng thứ tự**. Không cần API,
không cần library. Chỉ cần biết:

- Address nào (tra trong datasheet/TRM)
- Ghi giá trị gì (tra register description)
- Thứ tự nào (một số hardware yêu cầu ghi register A trước B)

Ví dụ, gửi ký tự 'A' ra UART trên BeagleBone Black:

```c
/* UART0 transmit register nằm ở address 0x44E09000 + 0x00 */
*((volatile uint32_t *)0x44E09000) = 'A';
```

Một dòng C. Không có driver framework, không có HAL. Đây là bản chất của bare-metal
(→ Chapter 01, UART driver).

### Tại sao `volatile`?

Compiler thông minh — nó tối ưu code. Nếu thấy bạn ghi vào 1 address mà không đọc lại,
nó có thể bỏ lệnh ghi đi ("dead store elimination"). Với RAM, điều này hợp lý.
Với hardware register — **thảm họa**: bạn muốn gửi ký tự ra UART mà compiler xóa mất lệnh ghi.

`volatile` nói với compiler: "address này có side effect — đừng tối ưu, đừng bỏ, đừng sắp xếp lại".

---

## 5. CPU mode và privilege

Nếu mọi code đều truy cập mọi address, thì bất kỳ chương trình nào cũng có thể:
- Ghi đè kernel → crash toàn hệ thống
- Disable interrupt → không ai dừng được nó
- Đọc dữ liệu chương trình khác → vi phạm bảo mật

CPU cần cách phân biệt **code đáng tin** và **code không đáng tin**.

### ARM giải quyết bằng CPU mode

ARM có nhiều mode, mỗi mode có mức đặc quyền khác nhau:

```
┌─────────────────────────────────────────────────────────┐
│                    Privileged modes                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │   SVC    │ │   IRQ    │ │   ABT    │ │   UND    │   │
│  │ Kernel   │ │ Interrupt│ │  Fault   │ │ Unknown  │   │
│  │ chạy đây │ │ handler  │ │ handler  │ │ handler  │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
│  ┌──────────┐                                            │
│  │   FIQ    │  ← interrupt nhanh, ưu tiên cao           │
│  └──────────┘                                            │
├─────────────────────────────────────────────────────────┤
│                   Unprivileged mode                       │
│  ┌──────────────────────────────────────────────────┐   │
│  │                   User mode                       │   │
│  │  Process chạy đây — không thể disable interrupt,  │   │
│  │  không thể truy cập kernel memory, không thể      │   │
│  │  chạm hardware register.                          │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

- **User mode:** ít quyền nhất. User program chạy ở đây. Không truy cập hardware,
  không disable interrupt, không thay đổi page table.
- **SVC mode (Supervisor):** toàn quyền. Kernel chạy ở đây.
- **IRQ/FIQ mode:** CPU **tự động** chuyển sang khi nhận interrupt từ hardware.
- **ABT mode (Abort):** CPU tự chuyển khi truy cập memory sai (Data Abort, Prefetch Abort).
- **UND mode (Undefined):** CPU tự chuyển khi gặp instruction không nhận ra.

### Banked registers — mỗi mode có SP riêng

Đây là chi tiết quan trọng: khi CPU chuyển mode, **một số register tự động đổi sang bản copy riêng
của mode đó**. Ít nhất SP (stack pointer), một số mode có thêm LR.

```
                User mode         IRQ mode          SVC mode
              ┌───────────┐    ┌───────────┐    ┌───────────┐
  r0 – r12   │  chung    │    │  chung    │    │  chung    │
              ├───────────┤    ├───────────┤    ├───────────┤
  SP (r13)    │ SP_usr    │    │ SP_irq    │    │ SP_svc    │  ← khác nhau!
  LR (r14)    │ LR_usr    │    │ LR_irq    │    │ LR_svc    │  ← khác nhau!
              └───────────┘    └───────────┘    └───────────┘
```

Nghĩa là: khi interrupt xảy ra, CPU chuyển sang IRQ mode, SP **tự động** trở thành SP_irq —
stack pointer khác hoàn toàn. Code đang chạy trên User stack không bị ảnh hưởng.

Đây là lý do boot code phải **setup SP riêng cho từng mode** (→ Chapter 01, `start.S`:
chuyển vào FIQ mode → set SP, chuyển IRQ mode → set SP, ...).

### CPU chuyển mode tự động

Code không "xin" chuyển mode. **Hardware cưỡng chế**:

```mermaid
flowchart LR
    A["User mode<br/>(process đang chạy)"] -->|"Timer interrupt"| B["IRQ mode<br/>(CPU tự chuyển)"]
    A -->|"Truy cập memory sai"| C["ABT mode<br/>(CPU tự chuyển)"]
    A -->|"Gọi SVC instruction"| D["SVC mode<br/>(CPU tự chuyển)"]
    A -->|"Instruction lạ"| E["UND mode<br/>(CPU tự chuyển)"]
```

Sự kiện này gọi là **exception**. Mỗi loại exception đưa CPU vào mode tương ứng.
User program không thể tự nâng quyền — chỉ có exception mới chuyển được mode.
Đây là nền tảng bảo mật của toàn bộ hệ thống.

Cơ chế exception sẽ được giải thích chi tiết ở → Chapter 02.

### CPSR — CPU biết mình đang ở mode nào

CPU lưu mode hiện tại trong register **CPSR** (Current Program Status Register).
Bạn không cần nhớ layout chi tiết — chỉ cần biết CPSR chứa 3 thứ quan trọng:

- **MODE bits [4:0]** — mode hiện tại (0x13 = SVC, 0x12 = IRQ, 0x10 = User, ...)
- **I bit [7]** — nếu = 1, IRQ bị mask (CPU không nhận interrupt)
- **F bit [6]** — nếu = 1, FIQ bị mask

Boot code bắt đầu bằng `cpsid if` — set I=1, F=1 — **mask tất cả interrupt** trước khi
làm bất cứ thứ gì (→ Chapter 01, dòng đầu tiên của `start.S`).

Chi tiết CPSR bit field sẽ được giải thích khi cần ở chapter sau.

---

## Liên kết

Chapter này không có code. Nó cung cấp mental model để đọc hiểu 10 chapter tiếp theo:

| Khái niệm | Phần | Dùng ở chapter |
|------------|------|----------------|
| Instruction, register | 1 | 01 (boot assembly), 02 (exception handler) |
| ARM assembly syntax | 2 | 01 (`start.S`), 02 (handler asm), 05 (context switch) |
| Stack, SP, function call | 3 | 01 (setup stacks), 05 (context switch) |
| Memory bus, MMIO, volatile | 4 | 01 (UART driver), 04 (timer/INTC driver) |
| CPU mode, privilege, banked SP | 5 | 01 (mode switching), 02 (exception), 05 (User vs SVC), 07 (syscall) |
| CPSR, IRQ mask | 5 | 01 (cpsid), 04 (interrupt enable), 06 (scheduler atomic) |

**Tiếp theo: Chapter 01 — Boot: Từ power-on đến UART output →**
