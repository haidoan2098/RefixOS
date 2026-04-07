    .section .text.start, "ax"  ; Đây là vùng chứa mã của start, vùng này phải được đặt đầu tiên, 
                                ; để khi bootloader chạy xong thì nhảy vào để đoạn setup này chạy trước 
                                ; Ghi section này ở đầu thì toàn bộ code ở dưới sẽ nằm trong section này 
                                ; "ax" có nghĩa là section này có thể được thực thi (executable) và có thể 
                                ; được ghi (allocatable) vào bộ nhớ. 
                                
    .align  4 ; Căn chỉnh địa chỉ của đoạn mã này lên bội số của 4, vì ARM là kiến trúc 32-bit, mỗi lệnh có kích thước 4 byte.


; label trong assembly dùng để đại diện 1 vùng nhớ, CPU nó vẫn thấy là địa chỉ thôi 
; có label thì lập trình dễ kiểm soát hơn thay vì địa chỉ thô, thay vì nhớ địa chỉ thì nhớ label là được
; ngoài ra việc có label cũng giúp cho việc debug dễ hơn, có thể đặt breakpoint vào label đó để xem CPU chạy đến đó thì làm gì tiếp theo.
; Nếu không có label thì cũng được, nhưng có thì dễ đọc hơn, vì nó cho biết đây là điểm bắt đầu của chương trình.
; Có thể đưa lệnh dưới lên luôn cũng được, ko cần lable, nhưng có label cho dễ debug, trong môi trường lệnh ko có GUI thì nhớ label để đặt breakpoint dễ hơn.

_start:   ; Đoạn code setup sẽ chạy từ đây, bên bootloader thì nó nhảy vào 0x80000000 
          ; label _start là có hay ko có cũng được, nhưng có thì dễ đọc hơn, vì nó cho biết đây là điểm bắt đầu của chương trình.
          ; Có thể đưa lệnh dưới lên luôn cũng được, ko cần lable
          ; có label cho dễ debug, trong môi trường lệnh ko có GUI thì nhớ label để đặt breakpoint dễ hơn.
    cpsid   if      // Tắt IRQ và FIQ của CPU 
    
    ; CPU ARM có nhiều mode, mỗi mode cần 1 stack riêng, chỉ được truy cập khi đang ở 
    ; mode đó, nên phải chuyển sang từng mode để thiết lập stack pointer (SP) cho từng mode.
    
    cps     #0x11  // Chuyển sang FIQ mode 
    ldr    sp, =_fiq_stack_top  // Thiết lập SP cho FIQ mode, địa chỉ này được định nghĩa trong linker script, nó là địa chỉ vật lý của stack top cho FIQ mode.
    