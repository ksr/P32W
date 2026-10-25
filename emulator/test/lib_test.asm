; lib_test.asm -- software/lib/p32io.inc on the emulated I/O card: UART
; setup and output, hex output, console input, CF read / write. Run with
; -d disk.img (8 sectors, byte i of sector n = (7n + i) & 0xFF) and the input
; "xyz". Prints a fixed transcript; HALT r1 = 0, or the failing step.
BUF      = 0x00100000

        li    sp, 0x00500000
        li    r1, 1
        li    r2, U0
        li    r3, 12
        call  uart_init         ; 9600 8N1
        li    r2, U1
        li    r3, 1
        call  uart_init         ; 115200 8N1
        la    r2, hello
        call  uart_puts
        li    r2, 0xCAFE0123
        call  uart_puthex
        li    r2, '\n'
        call  uart_putc
        li    r1, 2             ; echo three characters, upper case
        li    r9, 3
echo:   call  uart_getc
        addi  r2, r1, -32
        call  uart_putc
        addi  r9, r9, -1
        bnez  r9, echo
        call  uart_poll         ; input exhausted: -1
        li    r3, -1
        li    r8, 2
        bne   r1, r3, fail8
        li    r2, '\n'
        call  uart_putc
        li    r8, 3             ; the disk
        call  cf_init
        bnez  r1, fail8
        li    r8, 4
        li    r2, 3
        li    r3, BUF
        call  cf_read
        bnez  r1, fail8
        li    r3, BUF
        lbu   r2, 100(r3)
        li    r4, (21 + 100) & 0xFF
        bne   r2, r4, fail8
        li    r8, 5             ; write sector 6: bytes 0..255, 0..255
        li    r3, BUF
        li    r4, 0
fill:   sb    r4, 0(r3)
        addi  r3, r3, 1
        addi  r4, r4, 1
        slti  r5, r4, 512
        bnez  r5, fill
        li    r2, 6
        li    r3, BUF
        call  cf_write
        bnez  r1, fail8
        li    r8, 6
        li    r3, BUF + 1024    ; read it back elsewhere
        li    r2, 6
        call  cf_read
        bnez  r1, fail8
        li    r3, BUF + 1024
        lbu   r2, 300(r3)
        li    r4, 300 & 0xFF
        bne   r2, r4, fail8
        la    r2, done
        call  uart_puts
        li    r1, 0
        halt
fail8:  mov   r1, r8
        halt

hello:  .asciz "p32io library: "
done:   .asciz "disk ok\n"
        .align 4
        .include "../../software/lib/p32io.inc"
