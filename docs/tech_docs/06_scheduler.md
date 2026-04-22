# Chapter 06 — Scheduler + Context Switch: 3 process thật sự xen kẽ

> PCB đã dựng xong ở chapter 05, nhưng CPU vẫn chưa chạm vào process nào — kernel đang
> chạy trong `kmain` và sẽ đứng mãi ở `wfi` nếu không có ai ra lệnh cho nó đổi ngôi.
> Chapter này viết hai mảnh còn thiếu: một đoạn assembly biết tráo trạng thái CPU giữa
> hai PCB, và một scheduler round-robin quyết định ai được chạy tiếp. Kết quả cuối:
> counter, runaway, shell luân phiên mỗi 10 ms mà không process nào "ép" được cái khác.

---

## Đã xây dựng đến đâu

Module có dấu ★ là **mới trong chapter này**.

```
┌──────────────────────────────────────────────────────┐
│                     User space                       │
│                                                      │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│   │ counter  │  │ runaway  │  │ shell    │  ← stub  │
│   └──────────┘  └──────────┘  └──────────┘           │
└──────────────────────────────────────────────────────┘
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┌──────────────────────────────────────────────────────┐
│                    Kernel (SVC mode)                 │
│                                                      │
│   ┌──────────────────────────────────────────┐       │
│   │ ★ Scheduler                               │       │
│   │    schedule()  — round-robin              │       │
│   │    need_reschedule flag                   │       │
│   │    BLOCKED state + wake hook              │       │
│   └────────────┬─────────────────────────────┘       │
│                │                                     │
│   ┌────────────┴──────────────┐                     │
│   │ ★ context_switch(prev,next) (asm)            │   │
│   │    save r4-r11+lr → prev.kstack             │   │
│   │    save/restore banked SP_usr/LR_usr        │   │
│   │    swap TTBR0 + TLBIALL + ICIALLU           │   │
│   │    load next.kstack → pop r4-r11 + bx lr    │   │
│   └────────────┬──────────────────────────────┘      │
│                │                                     │
│   ┌────────────┴──────────────┐                     │
│   │ ★ ret_from_first_entry (asm trampoline)      │   │
│   │    drain IRQ-exit frame on first use         │   │
│   └───────────────────────────────────────────┘      │
│                                                      │
│   Exception Handler · IRQ dispatch · MMU · UART     │
└──────────────────────────────────────────────────────┘
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                      Hardware
                  CPU · RAM · Timer (IRQ fires)
```

**Flow khởi động:**

```mermaid
flowchart LR
    A[kmain boot] --> B[process_init_all<br/>3 PCB + initial frames]
    B --> C[run_boot_tests]
    C --> D[mmu_drop_identity]
    D --> E["★ timer_set_handler<br/>(scheduler_tick)"]
    E --> F["★ process_first_run<br/>context_switch(NULL, proc[0])"]
    F --> G["ret_from_first_entry<br/>→ rfefd → USR mode"]
    G --> H[counter runs]
    H -.timer IRQ.-> I["handle_irq → schedule()"]
    I --> J[context_switch → next]

    style E fill:#ffe699,stroke:#e8a700,color:#000
    style F fill:#ffe699,stroke:#e8a700,color:#000
```

Điểm mới: lần đầu tiên PC rời kernel, nhảy vào USR mode tại `0x40000000`. Từ đó mỗi
timer tick kéo kernel trở lại, `schedule()` chọn process kế, `context_switch()` tráo
kernel stack + banked registers + TTBR0 rồi `rfefd` vào USR của process kế.

---

## Nguyên lý

### Context switch = đổi đúng đủ trạng thái

CPU không có khái niệm "process". Với CPU, "process" chỉ là một bộ registers + page
table + kernel stack đang dùng. Tráo process = lưu bộ đó của A vào PCB A, nạp bộ của B
từ PCB B, rồi tiếp tục như thể chưa gián đoạn.

Trên ARMv7-A với User/Kernel split, bộ trạng thái cần tráo gồm:

- **GPRs r0–r12**: shared giữa mọi mode → đã được exception entry push lên kernel stack
  khi IRQ fire. Context switch không cần đụng r0–r12.
- **Callee-saved (r4–r11) + LR** của kernel code gọi `context_switch`: phải đẩy xuống
  prev's kernel stack để khi resume sau này, kernel chạy tiếp đúng chỗ.
- **Banked SP_usr, LR_usr**: banked theo mode, không nằm trên kernel stack — phải lưu
  vào PCB và restore khi switch vào.
- **SP_svc** (kernel stack pointer): mỗi process một cái; tráo SP = tráo "góc nhìn"
  kernel của process này sang process kế.
- **TTBR0**: address space base. Process khác page table khác.
- **TLB**: cache translations phụ thuộc TTBR0. Sau khi đổi TTBR0 phải invalidate.
- **I-cache**: cũng phụ thuộc VA. Đổi TTBR0 mà không flush I-cache → instruction cũ
  bám lại.

### Scheduler = ai chạy tiếp?

Context switch chỉ biết cách đổi, không biết đổi sang ai. Đó là việc của scheduler.

**Round-robin** là policy đơn giản nhất: duyệt PCB array theo thứ tự, chọn process
`READY` kế tiếp, switch sang nó. Đủ dùng cho 3 process tĩnh — công bằng, không starvation.

**Preemptive** nghĩa là process không có quyền từ chối. Timer IRQ fire → kernel lấy
CPU → scheduler quyết → switch. Process đang chạy vòng lặp vô hạn (runaway) vẫn bị
tước CPU mỗi 10 ms.

### BLOCKED state — không chạy nhưng chưa chết

`READY / RUNNING / BLOCKED / DEAD`. Scheduler skip `BLOCKED` và `DEAD`. `BLOCKED` là
trạng thái tạm — process đang đợi sự kiện (ví dụ UART RX trong chapter 09). Khi sự
kiện đến, một hook từ kernel (UART IRQ handler) chuyển nó về `READY`, lần schedule
kế tiếp pick lên lại.

---

## Bối cảnh

```
Trạng thái trước chapter 06:
- MMU       : ON, 3 process có proc_pgd riêng (chapter 05)
- PCB       : 3 cái, initial kernel stack frame đã prebuild
- Timer IRQ : hoạt động, tick_count tăng đều (chapter 04)
- current   : = &processes[0]
- PC        : trong kmain, đứng ở for(;;) wfi
```

Scheduler chưa có. Context switch chưa có. Không có cách nào chuyển PC sang
`0x40000000` (user_entry). Kernel chỉ idle.

---

## Vấn đề

1. **Không có cách vào user mode lần đầu.** `kmain` chạy ở SVC mode. Để vào USR mode
   cần `rfefd` hoặc `ldmfd sp!, {..., pc}^` — nhưng phải có stack frame đúng định
   dạng đã push sẵn. PCB đã có initial frame (chapter 05), nhưng chưa có đoạn code
   assembly thực hiện cú nhảy.

2. **Không có cách switch giữa 2 process.** Mỗi process có kernel stack riêng,
   banked SP_usr riêng, TTBR0 riêng. Tráo đồng bộ tất cả trong đúng thứ tự không
   thể viết bằng C thuần (phải chạm banked registers).

3. **Format frame cho first entry khác format IRQ-exit.** Initial frame có 14 words
   `{r0-r12, user_pc}` khớp với `msr spsr; ldmfd {..., pc}^`. IRQ exit dùng srsdb +
   rfefd với format 16 words `{r0-r12, svc_lr, lr_irq, spsr_irq}`. Hai đường thoát
   khác nhau → code phức tạp, dễ lệch.

4. **Scheduler cần hook vào timer IRQ.** Handler timer chỉ bump counter (chapter 04).
   Cần flag `need_reschedule` để tail của `handle_irq` gọi `schedule()` đúng lúc —
   không phải trong IRQ handler (IRQ stack nhỏ, chưa save đủ context).

---

## Thiết kế

### Một định dạng frame duy nhất

Thay vì 2 đường thoát (first entry vs IRQ exit), làm **initial frame theo cùng
format IRQ-exit**. 25 words tổng trên kernel stack của process:

```
Top (low addr)  ┌─────────────────────┐
                │ r4  = 0             │ \
                │ r5  = 0             │  |
                │ r6  = 0             │  |  9-word kernel-resume frame.
                │ r7  = 0             │  |  context_switch epilogue:
                │ r8  = 0             │  |    ldmfd sp!, {r4-r11, lr}
                │ r9  = 0             │  |    bx lr
                │ r10 = 0             │  |
                │ r11 = 0             │  |
                │ lr  = ret_from_     │ /   first-entry trampoline
                │       first_entry   │
                ├─────────────────────┤
                │ r0  = 0             │ \
                │ r1  = 0             │  |
                │ ...                 │  |  16-word IRQ-exit frame.
                │ r12 = 0             │  |  ret_from_first_entry:
                │ svc_lr = 0          │  |    ldmfd sp!, {r0-r12, lr}
                │ user_pc  = 0x40000000│  |    rfefd sp!
                │ user_cpsr = 0x10    │ /
                └─────────────────────┘
Bottom (high)   ← kstack_base + kstack_size
                ← ctx.sp_svc points at r4 slot
```

Khi resume process:
- **First time**: context_switch pops 9-word kernel-resume → lr = `ret_from_first_entry`
  → bx lr → trampoline chạy ldmfd + rfefd → USR mode.
- **Preempted trước đó bởi IRQ**: kernel-resume đã được push bởi chính context_switch
  lần trước → pop → bx lr → trả về `handle_irq` post-bl-schedule → exception_entry_irq
  tail làm ldmfd + rfefd.

Cùng bộ đôi `ldmfd + rfefd` cho cả hai trường hợp. Trampoline
`ret_from_first_entry` chỉ thêm 2 instruction.

### `context_switch(prev, next)` — bidirectional, load-only nếu `prev == NULL`

Một function duy nhất cho mọi trường hợp:

```asm
context_switch:
    cmp     r0, #0                @ prev NULL → skip save
    beq     .Lload

    push    {r4-r11, lr}          @ 9 words onto prev's kernel stack
    str     sp, [r0, #CTX_SP_SVC_OFFSET]

    cps     #0x1F                 @ SYS mode shares USR bank
    str     sp, [r0, #CTX_SP_USR_OFFSET]
    str     lr, [r0, #CTX_LR_USR_OFFSET]
    cps     #0x13

.Lload:
    ldr     r2, [r1, #PROC_PGD_PA_OFFSET]
    orr     r2, r2, #0x4A         @ TTBR0 walk attrs
    mcr     p15, 0, r2, c2, c0, 0
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0 @ TLBIALL
    mcr     p15, 0, r2, c7, c5, 0 @ ICIALLU (I-cache is VA-tagged)
    dsb
    isb

    cps     #0x1F
    ldr     sp, [r1, #CTX_SP_USR_OFFSET]
    ldr     lr, [r1, #CTX_LR_USR_OFFSET]
    cps     #0x13

    ldr     sp, [r1, #CTX_SP_SVC_OFFSET]
    pop     {r4-r11, lr}
    bx      lr
```

**`prev == NULL` = bootstrap từ kmain**: không có gì để save (kmain sẽ không
resume); chỉ load next. `kmain` gọi qua wrapper `process_first_run(&processes[0])`.

Offset của `CTX_SP_SVC_OFFSET` v.v. bắt buộc phải match struct layout. Header
[kernel/include/proc.h](../../kernel/include/proc.h) định nghĩa cả macro **và**
dùng `_Static_assert` + `offsetof()` để compiler bắt lệch tại build time.

### `schedule()` — nhìn flag, switch nếu cần

```c
void schedule(void) {
    if (!need_reschedule || !current)
        return;
    need_reschedule = 0;

    process_t *prev = current;
    for (vòng quanh ring bắt đầu từ prev->pid + 1) {
        process_t *cand = ...;
        if (cand == prev) skip;
        if (cand->state != READY && cand->state != RUNNING) skip;

        if (prev->state == TASK_RUNNING)   /* chỉ demote RUNNING */
            prev->state = TASK_READY;
        cand->state = TASK_RUNNING;
        current = cand;
        context_switch(prev, cand);
        return;
    }
    /* không có process nào khác — giữ current */
}
```

Hai chi tiết:

- **Chỉ demote `RUNNING`** — không overwrite `DEAD` hay `BLOCKED` khi switch-out.
  Thiếu điều kiện này, scheduler sẽ "hồi sinh" process đã chết, chạy lại code crash.
- **Skip BLOCKED ngay trong vòng chọn** — BLOCKED process không thuộc run queue
  đến khi được wake.

### Hook vào IRQ và SVC tail

`scheduler_tick()` gắn vào timer callback:

```c
timer_set_handler(scheduler_tick);   /* set need_reschedule = 1 */
```

Và `handle_irq` + `handle_svc` đều gọi `schedule()` ở tail:

```c
void handle_irq(void)  { irq_dispatch(); schedule(); }
void handle_svc(ctx)   { syscall_dispatch(ctx); schedule(); }
```

Gọi trong tail, **sau khi** toàn bộ IRQ/SVC frame đã settle trên SVC stack. Tại điểm
đó context_switch tráo được SP an toàn.

---

## Cách hoạt động

### Bootstrap — process_first_run

```mermaid
sequenceDiagram
    participant km as kmain
    participant pfr as process_first_run
    participant cs as context_switch
    participant rf as ret_from_first_entry
    participant P0 as proc[0] USR mode

    km->>pfr: process_first_run(&processes[0])
    pfr->>cs: context_switch(NULL, &processes[0])
    cs->>cs: skip save (prev NULL)
    cs->>cs: TTBR0 = proc[0].pgd_pa<br/>TLBIALL + ICIALLU + DSB + ISB
    cs->>cs: load SP_usr, LR_usr banked
    cs->>cs: sp = proc[0].ctx.sp_svc<br/>(pointing at 9-word frame)
    cs->>rf: pop r4-r11+lr → lr=ret_from_first_entry<br/>bx lr
    rf->>rf: ldmfd sp!, {r0-r12, lr}<br/>(drain IRQ-exit frame)
    rf->>P0: rfefd sp! → PC=0x40000000 CPSR=0x10
```

### Preemption loop

```mermaid
sequenceDiagram
    participant U as user A running
    participant EA as exception_entry_irq
    participant HI as handle_irq
    participant S as schedule
    participant CS as context_switch
    participant UB as user B

    U-->>EA: timer IRQ
    EA->>EA: srsdb → SVC stack<br/>stmfd r0-r12+lr
    EA->>HI: bl handle_irq
    HI->>HI: irq_dispatch<br/>timer_irq → scheduler_tick
    HI->>S: schedule()
    S->>CS: context_switch(A, B)
    CS->>CS: push r4-r11+lr onto A.kstack<br/>save A.sp_svc/sp_usr/lr_usr
    CS->>CS: TTBR0 = B.pgd_pa<br/>TLBIALL + ICIALLU
    CS->>CS: load B's banked regs<br/>sp = B.ctx.sp_svc<br/>pop r4-r11+lr
    CS-->>HI: bx lr → B's saved return point
    HI-->>EA: ret (on B's stack)
    EA->>EA: ldmfd r0-r12+lr<br/>rfefd sp!
    EA->>UB: USR mode at B's saved PC/CPSR
```

Khi user A sau này được chọn lại: load A.sp_svc → pop kernel-resume → lr trỏ về
đúng chỗ (sau `bl schedule` trong A's `handle_irq`) → handle_irq return →
exception_entry_irq tail drain A's IRQ frame → A resume.

### BLOCKED / wake

```mermaid
sequenceDiagram
    participant S as shell sys_read
    participant SC as schedule
    participant CX as context_switch
    participant C as counter
    participant U as UART IRQ handler

    S->>SC: scheduler_block_on_input()
    SC->>SC: shell.state = BLOCKED<br/>blocked_reader = shell<br/>need_reschedule = 1
    SC->>CX: context_switch(shell, next=counter)
    CX->>C: counter resumes
    Note over C: counter runs a while
    U->>U: UART char arrives
    U->>U: push to ring buffer
    U->>SC: scheduler_wake_reader()
    SC->>SC: shell.state = READY<br/>blocked_reader = NULL<br/>need_reschedule = 1
    Note over U: schedule() at handle_irq tail
    SC->>CX: context_switch(counter, shell)
    CX->>S: shell resumes from block_on_input
```

Shell resume ngay sau dòng `schedule();` trong `scheduler_block_on_input`. Nó retry
loop `uart_rx_pop()`, giờ có data, copy vào user buf, return.

---

## Implementation

### Files

| File | Vai trò |
|---|---|
| [kernel/arch/arm/proc/context_switch.S](../../kernel/arch/arm/proc/context_switch.S) | Bidirectional `context_switch(prev, next)` với offset `.equ` khớp struct |
| [kernel/arch/arm/exception/exception_entry.S](../../kernel/arch/arm/exception/exception_entry.S) | `ret_from_first_entry` trampoline ở cuối file |
| [kernel/sched/scheduler.c](../../kernel/sched/scheduler.c) | `schedule()`, `scheduler_tick`, `scheduler_block_on_input`, `scheduler_wake_reader` |
| [kernel/include/scheduler.h](../../kernel/include/scheduler.h) | Public API |
| [kernel/include/proc.h](../../kernel/include/proc.h) | `process_t` + offset macros + `_Static_assert` guard |
| [kernel/proc/process.c](../../kernel/proc/process.c) | `process_build_initial_frame` dựng 25-word stack |
| [kernel/arch/arm/exception/exception_handlers.c](../../kernel/arch/arm/exception/exception_handlers.c) | `handle_irq`/`handle_svc` tail gọi `schedule()` |
| [kernel/main.c](../../kernel/main.c) | `process_first_run(&processes[0])` thay cho idle loop |

### Điểm chính

**Struct offsets phải khớp asm.** `proc.h` có:

```c
#define CTX_SP_SVC_OFFSET       52
#define CTX_SP_USR_OFFSET       64
#define CTX_LR_USR_OFFSET       68
#define PROC_PGD_PA_OFFSET      88

_Static_assert(offsetof(proc_context_t, sp_svc) == CTX_SP_SVC_OFFSET,
               "ctx.sp_svc offset drifted");
/* ... */
```

Thêm field vào struct → offset lệch → compiler fail build → không thể ship asm sai.

**ICIALLU sau TTBR0 swap.** I-cache trên Cortex-A8 là VIPT với tag theo PA, nhưng
index theo VA. Sau khi TTBR0 đổi, VA cũ có thể ánh xạ sang PA mới; nếu không
invalidate, CPU có thể fetch instruction từ cache cũ. Lần đầu bỏ quên dòng này,
user process đầu tiên chạy code từ process mới ngay lần tráo đầu tiên, crash âm
thầm vì bytes ở PA mới chưa ra memory (D-cache cũng chưa clean — xem chapter 08).

**D-cache clean trong process_init_all.** Kernel ghi user image qua high-VA alias
→ bytes nằm trong D-cache. User I-fetch đọc qua chính VA đó nhưng đi L1 I-cache,
không thấy được D-cache chưa flush. `process_init_all` gọi `icache_sync()` cho
mỗi image sau `kmemcpy`: DCCMVAU từng cache line + DSB + ICIALLU + DSB + ISB.

**Linker `.text` phải ALIGN(16) cuối.** `.user_binaries` section có yêu cầu
alignment 16 byte. Nếu `.text` kết ở biên 4 byte, VMA của `.user_binaries` nhảy
lên biên 16 (tự ALIGN) nhưng LMA không đồng bộ → bytes load vào RAM lệch 4 byte
so với chỗ kernel nghĩ. Symbol `_counter_img_start` trỏ đúng VMA nhưng đọc ra
bytes của offset `+4` trong image. User chạy instruction lệch → `bl main` rơi
vào `bl sys_exit`. Fix: ép `.text` kết ở `ALIGN(16)`.

---

## Testing

Quan sát chính: 3 process luân phiên. Boot log in `[SCHED]` từng lần tráo (rate
limit 6 dòng để không flood), sau đó user code output xen kẽ.

**Smoke test bootstrap:** counter in `[pid 0] count=N` định kỳ chứng minh user
mode hoạt động. Thiếu → có thể là frame layout sai hoặc ICIALLU sót.

**Smoke test preemption:** runaway là vòng lặp vô hạn không `sys_yield`. Nếu
counter vẫn tăng → timer IRQ cắt được runaway → preemption thật.

**Smoke test resume:** chạy đủ lâu để cycle pid 2 → 0 → 1 → 2 ít nhất một lần.
Lần thứ 2 switch TO pid 0 là resume (không còn first entry). Nếu crash ở bước
này thì kernel-resume frame của pid 0 hỏng hoặc save-side của context_switch sai.

Tất cả verify được qua một lần boot 5-10 giây.

---

## Liên kết

### Dependencies

- **Chapter 03 — MMU**: `proc_pgd[i]` + TTBR0 swap nền tảng của context_switch.
- **Chapter 04 — Interrupts**: timer IRQ là nguồn preemption.
- **Chapter 05 — Process**: PCB + initial kernel stack frame.
- **CLAUDE.md**: "Exception stacks chỉ dùng làm trampoline" + "Không re-enable IRQ
  trong IRQ handler" — cả hai đều phản ánh trong thiết kế trên.

### Tiếp theo

**Chapter 07 — Syscall →** Process giờ chạy ở USR mode nhưng không làm được gì có
ích: không I/O, không cách trả lời kernel. Chapter sau mở cửa `svc #0`: user đặt
số syscall vào `r7`, args vào `r0-r3`, SVC trap vào SVC mode, kernel dispatch qua
bảng function pointer, kết quả trả qua `r0`.
