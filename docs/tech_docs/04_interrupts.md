# Chapter 04 — Interrupts: Hardware gõ cửa kernel

> MMU đã bật, exception handler đã bắt được fault. Nhưng kernel vẫn "điếc" — timer đếm
> mỗi 10 ms mà kernel không biết, UART có ký tự mà kernel không nghe thấy. CPU chỉ đọc
> instruction tuần tự, nếu không ai chủ động cắt ngang thì nó chạy mãi một vòng.
> Chapter này trang bị cho kernel khả năng "nghe": hardware gõ cửa → CPU dừng việc hiện
> tại → kernel xử lý → trả lại CPU cho code trước đó.

---

## Đã xây dựng đến đâu

Module có dấu ★ là **mới trong chapter này**.

```
┌──────────────────────────────────────────────────────┐
│                    User space                       │
│                                                      │
│                    (chưa có)                         │
└──────────────────────────────────────────────────────┘
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┌──────────────────────────────────────────────────────┐
│                   Kernel (SVC mode)                  │
│                                                      │
│   ┌────────────┐   ┌─────────────────────────┐       │
│   │   kmain    │──▶│   Exception Handler     │       │
│   │            │   │   vector · stubs · C    │       │
│   └────────────┘   └─────────────┬───────────┘       │
│          │                       │                   │
│          │                       ▼                   │
│          │       ┌──────────────────────────┐        │
│          │       │  ★ IRQ dispatch          │        │
│          │       │    irq_table[128]        │        │
│          │       │    irq_dispatch()        │        │
│          │       └────────────┬─────────────┘        │
│          │                    │                      │
│          │   ┌────────────────┴────────────────┐     │
│          ▼   ▼                                 ▼     │
│   ┌──────────────────┐          ┌──────────────────┐ │
│   │  ★ INTC driver   │          │  ★ Timer driver  │ │
│   │    GIC v1 (QEMU) │          │   SP804  (QEMU)  │ │
│   │    INTC  (BBB)   │          │   DMT2   (BBB)   │ │
│   └──────────────────┘          └──────────────────┘ │
│                                                      │
│   ┌────────────┐   ┌─────────────────────────┐       │
│   │    MMU     │   │   UART · Boot · start.S │       │
│   └────────────┘   └─────────────────────────┘       │
│                                                      │
│        MMU: ON   ·   IRQ: ★ ON (CPSR.I=0)            │
└──────────────────────────────────────────────────────┘
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                      Hardware
         CPU · RAM · UART · ★ INTC · ★ Timer (firing)
```

**Flow khởi động hiện tại — kernel linked ở VA cao `0xC0000000`:**

```mermaid
flowchart LR
    A[Reset] --> B[start.S<br/>stacks+BSS@PA]
    B --> E[mmu_init<br/>@PA — câm]
    E --> T["ldr pc, =_start_va<br/>trampoline"]
    T --> C[kmain<br/>@VA]
    C --> D[uart_init]
    D --> MP[mmu_print_status]
    MP --> F[exception_init]
    F --> G["★ irq_init<br/>(INTC reset,<br/>mask all)"]
    G --> H["★ timer_init<br/>(10 ms period)"]
    H --> I["★ irq_register<br/>+ irq_enable<br/>(per-line INTC unmask)"]
    I --> PI[process_init_all]
    PI --> DI[mmu_drop_identity]
    DI --> PFR["process_first_run<br/>(rfefd → USR,<br/>CPSR.I=0 atomic)"]

    style G fill:#ffe699,stroke:#e8a700,color:#000
    style H fill:#ffe699,stroke:#e8a700,color:#000
    style I fill:#ffe699,stroke:#e8a700,color:#000
```

Điểm mới của chapter này: INTC chip được cấu hình (mask all → per-line enable qua
`irq_register + irq_enable`), timer driver được start, vector table cài VBAR. CPU-level
`CPSR.I` vẫn mask trong suốt kmain — tránh timer IRQ preempt kmain và corrupt PCB đang
build. Khi `process_first_run` swap sang user mode đầu tiên qua `rfefd`, CPSR.I=0 được
restore atomic; từ đó mỗi exception entry tự động mask I=1 và rfefd restore lại — kernel
không bao giờ chạy với IRQ enabled trên stack có thể bị yank.

**Lưu ý về thứ tự:** `mmu_init` chạy từ `start.S` (trước `kmain`), không phải từ `kmain` —
vì `uart_printf` dùng VA string literal, không hoạt động pre-MMU. Log MMU được `kmain`
phát lại sau khi UART sẵn sàng, qua `mmu_print_status()`. Xem Chapter 03 cho chi tiết.

---

## Nguyên lý

### Nếu không có cơ chế cắt ngang, CPU chạy một chương trình mãi mãi

CPU là máy thực thi tuần tự: đọc instruction tại PC, decode, execute, tăng PC. Không có
khái niệm "ngoại cảnh". Nếu đang chạy vòng lặp `while (1) { x++; }` thì nó chạy mãi — không
bao giờ tự kiểm tra "timer đã đến chưa" hay "UART có data mới chưa". CPU không có giác quan.

Hệ quả: nếu kernel muốn biết có chuyện gì ngoài vùng CPU đang execute, nó phải chủ động
đi hỏi. Cách ngây thơ là **polling**:

```c
while (1) {
    if (timer_expired()) handle_tick();
    if (uart_has_data())  handle_char();
    if (gpio_changed())   handle_button();
    /* ... */
}
```

Polling đúng nhưng lãng phí: 99.99% thời gian CPU chỉ đọc register để xác nhận "chưa có gì".
Và tệ hơn: nếu vòng lặp polling bị một tác vụ chậm chặn lại, các sự kiện khác bị bỏ lỡ.

### Giải pháp: để hardware tự gõ cửa

Thay vì CPU đi hỏi, để **hardware tự thông báo** khi có sự kiện. Mỗi thiết bị có một đường
tín hiệu vật lý nối về CPU. Khi thiết bị có chuyện cần báo, nó kích hoạt đường đó. CPU nhận
được tín hiệu → dừng instruction hiện tại → nhảy vào handler → xong thì quay lại.

Đường tín hiệu đó gọi là **interrupt line**. Hành động CPU dừng để xử lý gọi là **interrupt
request (IRQ)**. Đây vẫn là cùng một cơ chế exception của Chapter 02 — chỉ khác ở nguồn
kích hoạt: thay vì code sai (Data Abort, Undefined), nguồn là **chân vật lý** kéo lên.

### Một CPU, nhiều thiết bị — cần bộ ghép

Cortex-A8 chỉ có **1 chân nIRQ** vật lý. Nhưng hệ thống có nhiều nguồn cần báo: timer,
UART, GPIO, DMA, Ethernet, ... Không thể nối 10 chân vào 1 chân. Giải pháp: đặt một chip
trung gian — **interrupt controller** (INTC hay PIC) — gom tất cả line lại, multiplex
thành 1 đường duy nhất vào CPU.

```
┌───────┐ line 4                                    nIRQ
│ Timer │─────────┐                                  │
└───────┘         │                                  ▼
┌───────┐ line 44 │    ┌──────────────────┐       ┌─────┐
│ UART  │─────────┼───▶│   INTC / PIC     │──────▶│ CPU │
└───────┘         │    │  ┌────────────┐  │       └─────┘
┌───────┐ line 27 │    │  │ mask bits  │  │
│ GPIO  │─────────┘    │  └────────────┘  │
└───────┘              │  ┌────────────┐  │
                       │  │ active id  │  │ ◀── CPU đọc để biết ai gọi
                       │  └────────────┘  │
                       └──────────────────┘
```

Vai trò của INTC:
1. **Multiplex**: gom nhiều line thành 1 đường nIRQ duy nhất
2. **Mask**: cho phép kernel enable/disable từng line riêng biệt
3. **Identify**: sau khi báo, INTC giữ số hiệu của line đang kích hoạt để CPU đọc
4. **Priority**: khi nhiều line cùng gọi, INTC chọn line ưu tiên cao nhất (ta chưa dùng,
   disable priority bằng threshold = max)
5. **Acknowledge (EOI)**: sau khi handler xong, kernel phải báo INTC "đã xong" để line
   này được unlatch, sẵn sàng gọi lần sau

### Timer — đồng hồ kernel nghe nhịp

Trong tất cả nguồn interrupt, **timer là đặc biệt nhất**. Nó không phải phản ứng với sự kiện
bên ngoài — nó **tự tạo sự kiện** theo chu kỳ cố định. Lập trình `timer_init(10000)` thì cứ
10 ms timer fire một lần, không cần ai kích hoạt.

Vì sao timer quan trọng đến vậy:

- **Thời gian**: kernel đếm tick → biết đã qua bao lâu, cho `sleep()`, `timeout`, `uptime`
- **Preemption** (Chapter 06): mỗi tick timer là cơ hội để scheduler **cưỡng chế** lấy CPU
  lại từ process đang chạy. Không có timer → process không yield thì kernel bất lực
- **Watchdog**: nếu một action không xong trong N tick, coi như chết

Không có timer interrupt = không có kernel thực sự, chỉ có thư viện chạy trong process user.

---

## Bối cảnh

```
Trạng thái CPU lúc vào Chapter 04:
- MMU       : ON — peripheral addresses mapped strongly-ordered + XN
- IRQ/FIQ   : masked (CPSR.I = 1) từ lúc reset
- VBAR      : đã cài, 8 vector entry đầy đủ handler
- IRQ stub  : exception_entry_irq lưu context → handle_irq → panic + halt
              (kernel chưa muốn nhận IRQ, nếu có thì coi là bug)
- INTC      : chưa chạm — register ở trạng thái reset hardware
- Timer     : chưa chạm — không fire
```

Hardware peripheral đã nằm trong bản đồ VA (Chapter 03), có thể đọc/ghi register. Vector
table đã có slot cho IRQ (Chapter 02). Mọi thứ sẵn sàng để kernel "học nghe".

---

## Vấn đề

1. **Kernel không có cách đo thời gian** — busy loop `for (i=0; i<N; i++)` phụ thuộc tốc độ
   CPU, compiler optimization, cache state. Không có nguồn tick độc lập → không có khái niệm
   "10 ms đã qua".

2. **Không preempt được** — giả sử đã có 3 process, process 0 rơi vào vòng lặp vô hạn.
   Chỉ có timer interrupt mới cắt ngang được nó. Không có timer → Chapter 06 scheduler
   không tồn tại.

3. **I/O phải polling** — shell (Chapter 10) cần blocking read từ UART. Không có UART RX
   interrupt → phải polling `LSR & RXDA` liên tục → burn 100% CPU.

4. **IRQ stub hiện tại panic** — nếu có hardware nào đó (ví dụ lỗi config) kích hoạt IRQ
   line, kernel halt thay vì xử lý. Chặn mọi khả năng chạy thật với hardware.

---

## Thiết kế

### Hai platform, hai INTC khác nhau — cùng một interface

QEMU `realview-pb-a8` và BeagleBone Black dùng hai bộ interrupt controller khác biệt:

| Platform | Controller | Base | Số line | Đặc điểm |
|----------|-----------|------|---------|-----------|
| QEMU | ARM GIC v1 | CPU @ `0x1E000000`, Dist @ `0x1E001000` | 96 | Distributor + CPU interface tách đôi; IAR/EOIR protocol |
| BBB  | AM335x INTC | `0x48200000` | 128 | 4 bank × 32 bit; NEWIRQAGR single-write EOI |

**Lưu ý về QEMU**: machine `realview-pb-a8` wire up **GIC v1** (không phải PL190 VIC
như variant `realview-eb`). SP804 TIMER0_1 tương ứng với **SPI #4**; trên GIC SPI offset
là 32 → IRQ ID = **36**. Trên BBB, DMTIMER2 là IRQ **68**.

Vì hai backend khác nhau, driver che giấu phía sau một API thống nhất:

```c
void     intc_init(void);
void     intc_enable_line(uint32_t irq);
void     intc_disable_line(uint32_t irq);
uint32_t intc_get_active(void);
void     intc_eoi(uint32_t irq);
```

Code kernel (dispatch layer, tests) chỉ nhìn thấy API này. Thay platform → thay backend,
không động vào phần trên.

### Timer tương tự — DMTimer2 phức tạp hơn SP804 nhiều

| Platform | Timer | Clock | Setup cần |
|----------|-------|-------|-----------|
| QEMU | SP804 Dual (Timer0) | 1 MHz | Load + ctrl register, 3 dòng code |
| BBB  | DMTIMER2 | 24 MHz | CM_PER clock gate → soft reset → posted mode → TLDR/TCRR → IRQENABLE → TCLR |

DMTIMER2 trên AM335x yêu cầu **clock module (CM_PER) phải enable trước**. Nếu không,
module chết im lặng — register đọc ra toàn 0, write không có tác dụng. Thứ tự bắt buộc:

```
CM_PER_L4LS_CLKSTCTRL = SW_WKUP
CM_PER_TIMER2_CLKCTRL = MODULEMODE_ENABLE
poll IDLEST == FUNC  ← chờ clock domain thật sự awake
... rồi mới chạm TIMER2_BASE
```

Ngoài ra DMTIMER2 dùng **posted write** (`TSICR.POSTED = 1`): writes return về CPU ngay, nhưng
register thật được cập nhật một clock sau. Nếu CPU write rồi read lại ngay → có thể đọc giá
trị cũ. Phải poll `TWPS` trước write kế tiếp. SP804 trên QEMU không có chuyện này.

Cả hai driver expose cùng API:

```c
void     timer_init(uint32_t period_us);
void     timer_set_handler(timer_handler_t fn);
void     timer_irq(void);              /* đăng ký với irq_register */
uint32_t timer_get_ticks(void);
```

### Dispatch layer — một bảng, một hàm

Kernel không hỏi INTC về ai đang gọi — INTC trả lời. Dispatcher chỉ cần:

```c
irq_handler_t irq_table[MAX_IRQS];

void irq_dispatch(void) {
    uint32_t n = intc_get_active();       /* hỏi INTC: ai gọi? */
    if (n < MAX_IRQS && irq_table[n])
        irq_table[n]();                    /* gọi handler đã đăng ký */
    intc_eoi(n);                           /* báo INTC: xong */
}
```

Mỗi driver cần nhận IRQ gọi `irq_register(IRQ_X, my_handler)` lúc init. Runtime không cần
switch-case, không cần biết về driver nào — chỉ lookup bảng.

### EOI AFTER handler, không trước

Nếu EOI trước khi handler chạy xong, INTC coi line đã rảnh. Nếu cùng line fire thêm lần nữa
giữa chừng, CPU có thể nhận IRQ mới đè lên handler cũ → nested → stack corrupt (RingNova
không hỗ trợ nested IRQ). Thứ tự đúng: **handler chạy xong → EOI → quay về code bị ngắt**.

Ngoại lệ: spurious IRQ (INTC báo "không có ai gọi" — thường do race). Dispatcher **vẫn phải
gọi EOI** cho ID spurious, nếu không INTC giữ trạng thái pending và không nhận line mới.

### IRQ entry — SVC stack từ đầu, IRQ stack chỉ là trampoline

ARMv7 khi nhận IRQ:
- Chuyển sang IRQ mode
- `LR_irq = interrupted_PC + 4`
- `SPSR_irq = CPSR` (của mode bị ngắt)
- Nhảy đến VBAR + 0x18

Vấn đề: IRQ mode có SP riêng (banked), stack rất nhỏ (1 KB). Không thể chạy logic C đầy đủ
trên đó. Solution:

```asm
exception_entry_irq:
    sub     lr, lr, #4              /* LR = instruction bị ngắt          */
    srsdb   sp!, #0x13              /* push {LR_irq, SPSR_irq} lên SVC stk */
    cps     #0x13                   /* switch sang SVC mode               */
    stmfd   sp!, {r0-r12, lr}       /* save GPRs trên SVC stack           */
    bl      handle_irq
    ldmfd   sp!, {r0-r12, lr}       /* restore GPRs                        */
    rfefd   sp!                     /* pop {PC, CPSR} → return + unmask    */
```

Hai instruction quan trọng:

- **`srsdb sp!, #0x13`** (Store Return State): push `{LR_irq, SPSR_irq}` xuống stack của
  **mode 0x13 (SVC)**, không phải stack hiện tại (IRQ). Đây là điểm mấu chốt — context
  lưu ngay vào SVC stack trước khi switch mode, không cần copy.
- **`rfefd sp!`** (Return From Exception): pop `{PC, CPSR}` — restore cả return address
  lẫn processor status register (bao gồm mode bit). Atomic, không cần manage SPSR bằng tay.

Kết quả: IRQ stack chỉ dùng làm chỗ CPU "đặt chân" trong 1 chu kỳ trước khi srsdb/cps
chuyển đi. Toàn bộ handler C chạy trên SVC stack — stack lớn 8 KB, đủ chỗ cho nested call.

### Không re-enable IRQ trong handler

ARMv7 tự động set `CPSR.I=1` khi vào IRQ mode. Kernel không gọi `cpsie i` lại trong handler.
Lý do:
- Single-core, 1 IRQ stack per CPU — nested IRQ đè stack
- Handler phải ngắn gọn, 10 ms là deadline — không để dồn

Nếu handler cần chạy lâu (ví dụ filesystem), hoãn lại bottom-half (deferred work). RingNova
chưa có nhu cầu — handler timer chỉ bump counter.

---

## Cách hoạt động

### Một chu kỳ interrupt từ hardware đến trả CPU

```mermaid
sequenceDiagram
    participant HW as Timer HW
    participant INTC as INTC
    participant CPU as CPU (SVC)
    participant H as handle_irq + dispatch
    participant DRV as timer_irq

    Note over CPU: đang chạy kmain
    HW->>INTC: line 36 (QEMU) / 68 (BBB) = 1
    INTC->>CPU: nIRQ = 1
    CPU->>CPU: chuyển IRQ mode<br/>LR_irq = PC+4<br/>SPSR_irq = CPSR cũ
    CPU->>CPU: jump VBAR + 0x18
    Note over CPU: entry stub
    CPU->>CPU: srsdb → SVC stack<br/>cps #0x13<br/>stmfd GPRs
    CPU->>H: bl handle_irq
    H->>INTC: intc_get_active()
    INTC-->>H: irq = 36/68
    H->>DRV: timer_irq()
    DRV->>HW: ack interrupt flag
    DRV->>H: tick_count++
    H->>INTC: intc_eoi(irq)
    H-->>CPU: return
    CPU->>CPU: ldmfd GPRs<br/>rfefd → pop {PC, CPSR}
    Note over CPU: quay lại kmain,<br/>CPSR = trước IRQ
```

### Vì sao spurious vẫn phải EOI

```
                       ┌─────────────────────────┐
                       │  INTC state machine      │
                       │                          │
  Timer pulse ────▶    │   line latched           │
                       │        │                 │
                       │        ▼                 │
                       │   signal nIRQ to CPU     │
                       │        │                 │
                       │        ▼                 │
                       │   CPU reads IAR/SIR_IRQ  │◀── active ID returned
                       │        │                 │
                       │        ▼                 │
         (chờ EOI)     │   line → "in-service"    │  ← nếu không EOI, stuck ở đây
                       │        │                 │       không line nào qua được
                       │        ▼ (khi nhận EOI)  │
                       │   line unlatched         │
                       │        │                 │
                       └────────┴─────────────────┘
```

Spurious = CPU đọc IAR thấy "không có ai" (ID=1023 trên GIC, bit spurious=1 trên AM335x).
Trạng thái "in-service" có thể đã được claim → phải EOI để unlatch → không thì mọi line
tiếp theo đều bị block.

---

## Implementation

### Files

| File | Nội dung |
|------|----------|
| [kernel/include/drivers/intc.h](../../kernel/include/drivers/intc.h) | `struct intc_ops` contract + `irq_*` dispatch API |
| [kernel/include/drivers/timer.h](../../kernel/include/drivers/timer.h) | `struct timer_ops` contract + `timer_*` API |
| [kernel/drivers/intc/intc_core.c](../../kernel/drivers/intc/intc_core.c) | Generic IRQ table + dispatch + `intc_*` thin wrappers |
| [kernel/drivers/intc/gicv1.c](../../kernel/drivers/intc/gicv1.c) | `struct intc_ops gicv1_ops` — QEMU realview-pb-a8 |
| [kernel/drivers/intc/am335x_intc.c](../../kernel/drivers/intc/am335x_intc.c) | `struct intc_ops am335x_intc_ops` — BBB |
| [kernel/drivers/timer/timer_core.c](../../kernel/drivers/timer/timer_core.c) | tick_count + user_handler + dispatch |
| [kernel/drivers/timer/sp804.c](../../kernel/drivers/timer/sp804.c) | `struct timer_ops sp804_ops` — QEMU |
| [kernel/drivers/timer/dmtimer.c](../../kernel/drivers/timer/dmtimer.c) | `struct timer_ops dmtimer_ops` — BBB |
| [kernel/arch/arm/exception/exception_entry.S](../../kernel/arch/arm/exception/exception_entry.S) | `exception_entry_irq` (srsdb/rfefd → SVC stack) |
| [kernel/arch/arm/exception/exception_handlers.c](../../kernel/arch/arm/exception/exception_handlers.c) | `handle_irq` → `irq_dispatch()` |
| [kernel/platform/qemu/board.c](../../kernel/platform/qemu/board.c) / [bbb/board.c](../../kernel/platform/bbb/board.c) | Khai báo `intc_device` / `timer_device` và bind ops + địa chỉ |
| [kernel/platform/qemu/board.h](../../kernel/platform/qemu/board.h) / [bbb/board.h](../../kernel/platform/bbb/board.h) | `GIC_*_BASE` / `INTC_BASE`, `TIMER*_BASE`, `IRQ_TIMER`, `TIMER_CLK_HZ` |

### Điểm chính

**Backend chọn tại link time** — `kernel/platform/<board>/platform.mk` liệt kê driver cụ thể. Cho QEMU: `gicv1.c + sp804.c`; cho BBB: `am335x_intc.c + dmtimer.c`. Makefile chỉ compile driver board đang dùng, zero `#ifdef` còn sót trong kernel.

Kernel core gọi `irq_register(IRQ_TIMER, timer_irq)` xuyên qua subsystem layer — `intc_core` dispatch xuống `active_intc->ops->enable_line()`. Driver PL011 không biết mình đang chạy trên QEMU hay RPi; biết base address là đủ, lấy từ `dev->base` do `board.c` set.

**GIC init trên QEMU** — điểm dễ sai là thứ tự bật distributor vs CPU interface:

```c
REG32(GIC_DIST_BASE + GICD_CTLR) = 0;           /* disable khi đang config */
/* ... mask + priority + target ... */
REG32(GIC_DIST_BASE + GICD_CTLR) = 1;           /* enable distributor       */

REG32(GIC_CPU_BASE + GICC_PMR)   = 0xF0U;       /* accept priority ≤ 0xF0  */
REG32(GIC_CPU_BASE + GICC_CTLR)  = 1;           /* enable CPU interface     */
```

PMR=0xF0 (không phải 0xFF) — GIC v1 chỉ xài 4 bit cao của priority, cần mask lớn hơn mọi
priority driver dùng (0xA0 trong `intc_init`).

**DMTIMER2 reload formula** — timer đếm ngược từ TLDR đến 0xFFFFFFFF (wrap), nên reload
không phải period mà là `0 - period_count`:

```c
uint32_t count  = period_us * (TIMER_CLK_HZ / 1000000U);  /* BBB: * 24 */
uint32_t reload = (uint32_t)(0U - count);                  /* = -count  */
```

Với 10 ms @ 24 MHz: count = 240000, reload = 0xFFFB15A1.

**IRQ entry rewrite** — thay thế logic cũ (halt sau handler) bằng srsdb/rfefd. Diff ngắn
nhưng thay đổi toàn bộ luồng: trước đây context ở IRQ stack, giờ ở SVC stack.

**Wire trong kmain** (sau `exception_init`):

```c
irq_init();                              /* INTC reset, mask all       */
timer_init(10000);                       /* 10 ms period               */
irq_register(IRQ_TIMER, timer_irq);      /* đăng ký callback           */
irq_enable(IRQ_TIMER);                   /* unmask line trong INTC     */
irq_register(IRQ_UART0, uart_rx_irq);
irq_enable(IRQ_UART0);
```

CPU-level unmask không gọi ở đây — `rfefd` trong `ret_from_first_entry` restore
USR CPSR với I=0 atomic khi process đầu tiên vào user mode. Enabling IRQs sớm
hơn race với kmain: timer preempt → schedule() save kmain's SVC state vào
`processes[0].ctx`, đè initial frame → context_switch kế tiếp load SP sai.

---

## Testing

6 boot self-tests chạy tự động (T6 là mới):

| Test | Kiểm tra | Fail nghĩa là |
|------|----------|----------------|
| T1–T5 | (Chapter 03) MMU + exception | — |
| **T6** | Spin đọc `timer_get_ticks()`, break khi ≥ 5 | Timer không fire / INTC không route / dispatch lỗi |

T6 spin đến khi thấy ≥ 5 tick thay vì busy-wait cố định, để khỏi nhạy với tốc độ QEMU
(QEMU TCG chạy guest nhanh hơn/chậm hơn tùy host). Cap 200 triệu vòng — nếu quá mà vẫn 0
tick → fail sớm thay vì treo.

**Kết quả trên QEMU:**

```
[INTC]  GIC v1 dist=0x1e001000 cpu=0x1e000000 lines=96
[TIMER] SP804 @ 0x10011000 period=10000us reload=10000
[IRQ]   CPU IRQ enabled (CPSR.I=0)
...
[TEST] [PASS] T6 timer ticks: 5 observed
[TEST] ========== 6 passed, 0 failed ==========
```

**Chưa test:**
- BBB hardware (chỉ build clean, chưa flash SD card)
- Nested IRQ — RingNova không hỗ trợ, CLAUDE.md cấm re-enable IRQ trong handler
- Long-running handler — scheduler (Chapter 06) mới cần đo latency

---

## Liên kết

### Files trong code

| File | Vai trò |
|------|---------|
| [kernel/drivers/intc/](../../kernel/drivers/intc/) | INTC driver + dispatch |
| [kernel/drivers/timer/](../../kernel/drivers/timer/) | Timer driver |
| [kernel/include/drivers/intc.h](../../kernel/include/drivers/intc.h) | Subsystem contract + IRQ dispatch API |
| [kernel/arch/arm/exception/exception_entry.S](../../kernel/arch/arm/exception/exception_entry.S) | IRQ entry (srsdb/rfefd) |
| [kernel/arch/arm/exception/exception_handlers.c](../../kernel/arch/arm/exception/exception_handlers.c) | `handle_irq` dispatch |
| [kernel/main.c](../../kernel/main.c) | Wire + T6 |

### Dependencies

- **Chapter 02 — Exceptions**: VBAR + IRQ vector slot đã có
- **Chapter 03 — MMU**: peripheral addresses mapped strongly-ordered, access được
- **CLAUDE.md**: "Exception stacks chỉ dùng làm trampoline" + "Không re-enable IRQ trong IRQ handler"

### Tiếp theo

**Chapter 05 — Process →** kernel đã nghe được timer. Giờ cần cấu trúc để lưu trạng thái
nhiều chương trình: PCB (Process Control Block), context switch assembly, User mode entry.
Handler timer hiện tại chỉ bump counter — Chapter 06 sẽ biến nó thành `scheduler_tick` đầu
tiên.
