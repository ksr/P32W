; fib.asm -- print the Fibonacci numbers F(0)..F(47), one per line, in
; decimal: every one that fits in 32 bits unsigned. F(48) = 4807526976 would
; overflow; the carry is detected with an unsigned compare (D5: no flags).
; HALT with r1 = 0.
CON     = 0xFFFFFF00            ; console data register

        li    sp, 0x00500000    ; top of RAM; full-descending stack (D6)
        li    r2, 0             ; a = F(n)
        li    r3, 1             ; b = F(n+1)
loop:   mov   r10, r2
        call  putdec            ; print a
        add   r4, r2, r3        ; next = a + b
        bltu  r4, r3, last      ; wrapped: a + b < b means carry out of bit 31
        mov   r2, r3
        mov   r3, r4
        jmp   loop
last:   mov   r10, r3           ; b = F(47) still fits; print it and stop
        call  putdec
        li    r1, 0
        halt

; putdec: print r10 as an unsigned decimal number and a newline.
; Digits are produced least significant first (REMU/DIVU by 10) into a
; 12-byte buffer on the stack, then printed forwards. Clobbers r10-r14.
putdec: addi  sp, sp, -12       ; buffer; keeps SP word-aligned
        addi  r14, sp, 12       ; r14 = one past the end of the buffer
        mov   r12, r14          ; r12 = next free byte, filling backwards
        li    r13, 10
pd_div: remu  r11, r10, r13     ; lowest digit
        divu  r10, r10, r13
        addi  r11, r11, '0'
        addi  r12, r12, -1
        sb    r11, 0(r12)
        bnez  r10, pd_div       ; at least one digit, so 0 prints as "0"
pd_out: lbu   r11, 0(r12)
        sb    r11, CON(r0)
        addi  r12, r12, 1
        bne   r12, r14, pd_out
        li    r11, '\n'
        sb    r11, CON(r0)
        addi  sp, sp, 12
        ret
