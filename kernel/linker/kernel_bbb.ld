/*
 * Linker Script - Ngôn ngữ của địa chỉ 
 */

/*
 * symbol _start nó là 1 tên đại diện cho địa chỉ tại đó thôi. 
 * Nó điền giá trị vào field e_entry trong ELF header. 
 * Nếu ko có ENTRY(..). e_entry để mặc định là 0x0
 * Nó chỉ hỗ trợ cho các thành phần nào đọc file ELF thôi.
 * Ví dụ: QEMU đọc elf để chạy. nó thấy e_entry = _start thì con trỏ PC sẽ nhảy đến đây. 
 * Ví dụ: GDB đọc elf để debug. nó thấy e_entry = _start thì con trỏ PC sẽ nhảy đến đây để đặt breakpoint.

 * Khi build thì elf được tạo ra chứa (nếu đọc thì toàn là hex) vì thế objdump là tool để dịch ngược lại từ elf sang assembly để đọc được. 
 * trong elf có nhiều phần khác nhau ví dụ: elf header, segment header table, .init, .text, .rodata, .data, .bss, .symtab, .debug, .line, .strtab
 * bin sẽ có các thành phần trong elf: .text, .rodata, .data, .bss nhưng sẽ được chuẩn hóa thành dãy byte code
 * để debug: nó phải áp dụng nhiều section khác nhau:
 * symtab chứa thông tin về symbol (ofset, địa chỉ) tra strtab để lấy tên hàm ra nhưng lại ko biết dòng nào
 * .debug: source file, chứa tên biến local, kiểu dữ liệu + .line biết dòng source
 * .text     → bytes thực tế để disassemble

 * Vậy khi objdump mình cần quan tâm đến các section nào:
 * Section Header Table. Xem .text, .data, .bss, .stack nằm đúng địa chỉ chưa.
 * .symtab + .strtab. Xem _start, stack tops, _bss_start đúng địa chỉ chưa.
 * .text + .symtab. Xem instructions thực tế CPU sẽ chạy.
 */
ENTRY(_start) 

MEMORY 
{
    /* 
     * Sau khi Booloader hoàn thành phần setup (trong đó có khởi tạo DRAM)
     * Có DRAM để chạy các ct lớn rồi nhưng chưa thể vào kernel ngay cần setup mt
     */

    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 512M /**/
}

SECTIONS
{
    .text :
    {
        _text_start = .;    /* vị trí con trỏ hiện tại: là 0x80000000*/
        *(.text.start)     /* _start — must be first    |  tìm file .o nào có section .text.start, kéo bytes đó vào đây.      */
                            /*kích thước con trỏ tự tăng lên*/
        *(.text*)        
        *(.rodata*)
        . = ALIGN(4);
        _text_end = .;      /* vị trí con trỏ kết thúc */
    } > RAM          /*đặt section .text này vào vùng nhớ tên RAM đã khai báo ở trên*/

    /* vùng nhớ biến global đã được khởi tạo*/
    .data :
    {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM

    /* vùng nhớ biến global chưa được khởi tạo */
    .bss (NOLOAD) :     /*NOLOAD ở đây nghĩa là ko load dữ liệu vào bộ nhớ*/
    {                   /*nhưng vùng bss các biến global ko gán giá trị sẽ có giá trị là 0 mà*/
                        /*Lí do là bên _start nó sẽ zero bss phân vùng này đi nên giá trị trong vùng này là 0  */
        . = ALIGN(8);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > RAM 

    .stack (NOLOAD) : 
    {
        . = ALIGN(8);
        
        /* FIQ stack — 512 B */
        . = . + 0x200; 
        PROVIDE(_fig_stack_top = .);
    }
}