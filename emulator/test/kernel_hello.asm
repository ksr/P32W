; kernel_hello.asm -- a user-mode program on the stage-A kernel. It writes a
; greeting with write(), counts 0-9 with putc(), echoes its input in upper
; case with getc()/putc(), and exits with status 0 through exit().
        .include "kernel.inc"

user_main:
        li    r1, SYS_WRITE
        la    r2, greeting
        li    r3, greeting_end - greeting
        syscall
        li    r4, '0'           ; count 0..9 with putc
        li    r5, '9' + 1
digits: li    r1, SYS_PUTC
        mov   r2, r4
        syscall
        addi  r4, r4, 1
        bne   r4, r5, digits
        li    r1, SYS_PUTC
        li    r2, '\n'
        syscall
echo:   li    r1, SYS_GETC      ; echo input in upper case until end of input
        syscall
        li    r3, -1
        beq   r1, r3, done
        mov   r2, r1
        li    r3, 'a'
        bltu  r2, r3, put
        li    r3, 'z'
        bgtu  r2, r3, put
        addi  r2, r2, -32
put:    li    r1, SYS_PUTC
        syscall
        jmp   echo
done:   li    r1, SYS_EXIT
        li    r2, 0
        syscall

greeting:     .ascii "Hello from user mode!\n"
greeting_end:
