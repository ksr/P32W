; card_test.asm -- the cards (D50, D52) as p32io.h models them: the card
; ID registers, UARTs 2 and 3 and the shared interrupt levels. Self-checking:
; HALT with r1 = 0, or with the number of the failing case. Run with:
; -a2 two.out -A2 IN2 -a3 three.out -A3 IN3, where IN2 holds "x" and IN3 "y".

U2       = 0xFFFFFF30           ; UART 2 registers 0-7
U2IER    = 0xFFFFFF31
U2LSR    = 0xFFFFFF35
U2SCR    = 0xFFFFFF37
U3       = 0xFFFFFF38           ; UART 3
U3IER    = 0xFFFFFF39
U3LSR    = 0xFFFFFF3D
U3SCR    = 0xFFFFFF3F
U0SCR    = 0xFFFFFF07
IOID     = 0xFFFFFFF0           ; the I/O card, window 7: ID, revision, interrupt status
IOREV    = 0xFFFFFFF1
IOISR    = 0xFFFFFFF4
DISKID   = 0xFFFFEFF0           ; the disk card, window 6
DISKISR  = 0xFFFFEFF4

        li    sp, 0x00500000
        li    r1, 0

; ---------------------------------------------------------------- card IDs
        addi  r1, r1, 1         ; 1  the I/O card: ID 1, revision 0
        lbu   r2, IOID(r0)
        li    r3, 1
        bne   r2, r3, fail
        lbu   r2, IOREV(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 2  the disk card: ID 2, nothing asking
        lbu   r2, DISKID(r0)
        li    r3, 2
        bne   r2, r3, fail
        lw    r2, DISKISR(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 3  a write to an ID register is taken and ignored
        li    r4, 0x77
        sb    r4, IOID(r0)
        lbu   r2, IOID(r0)
        li    r3, 1
        bne   r2, r3, fail

; ---------------------------------------------------------------- UARTs 2 and 3
        addi  r1, r1, 1         ; 4  each UART has its own scratch register
        li    r4, 0x22
        sb    r4, U2SCR(r0)
        li    r4, 0x33
        sb    r4, U3SCR(r0)
        li    r4, 0x11
        sb    r4, U0SCR(r0)
        lbu   r2, U2SCR(r0)
        li    r3, 0x22
        bne   r2, r3, fail
        lbu   r2, U3SCR(r0)
        li    r3, 0x33
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  output (checked in the -a2 / -a3 files)
        la    r10, msg2
        li    r9, U2
        call  puts
        la    r10, msg3
        li    r9, U3
        call  puts
        addi  r1, r1, 1         ; 6  input waiting: LSR data ready on both
        lbu   r2, U2LSR(r0)
        andi  r2, r2, 1
        beqz  r2, fail
        lbu   r2, U3LSR(r0)
        andi  r2, r2, 1
        beqz  r2, fail
        addi  r1, r1, 1         ; 7  nothing enabled: no interrupt, status 0
        mfcr  r2, IPEND
        andi  r2, r2, 0x0C
        bnez  r2, fail
        lw    r2, IOISR(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 8  UART 2's receive interrupt is level 2 (shared with
        li    r4, 1             ;    UART 0), status bit 3
        sb    r4, U2IER(r0)
        mfcr  r2, IPEND
        andi  r2, r2, 0x0C
        li    r3, 0x04
        bne   r2, r3, fail
        lw    r2, IOISR(r0)
        li    r3, 0x08
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 9  UART 3's is level 3 (with UART 1), status bit 4
        li    r4, 1
        sb    r4, U3IER(r0)
        mfcr  r2, IPEND
        andi  r2, r2, 0x0C
        li    r3, 0x0C
        bne   r2, r3, fail
        lw    r2, IOISR(r0)
        li    r3, 0x18
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 10 reading the bytes ends both
        lbu   r2, U2(r0)
        li    r3, 0x78          ; 'x'
        bne   r2, r3, fail
        lbu   r2, U3(r0)
        li    r3, 0x79          ; 'y'
        bne   r2, r3, fail
        mfcr  r2, IPEND
        andi  r2, r2, 0x0C
        bnez  r2, fail
        lw    r2, IOISR(r0)
        bnez  r2, fail

        la    r10, okmsg
        li    r9, 0xFFFFFF00
        call  puts
        li    r1, 0
fail:   halt                    ; r1 = failing case number

; puts: the 0-terminated string at r10 to the UART data register at r9.
puts:   lbu   r11, 0(r10)
        beqz  r11, puts_done
        sb    r11, 0(r9)
        addi  r10, r10, 1
        jmp   puts
puts_done:
        ret

msg2:   .asciz "two\n"
msg3:   .asciz "three\n"
okmsg:  .asciz "cards ok\n"
