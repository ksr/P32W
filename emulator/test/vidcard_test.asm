; vidcard_test.asm -- the video card at the hardware's timing (D67): its RAM
; in every width and lane (each access waits for the card's bus slots), the
; registers, the status register as the raster moves, the accesses the card
; must refuse; then a screen to look at (text with each attribute, the
; cursor, the overlay ORed over it). Self-checking: HALT with r1 = 0, or the
; number of the failing case. Run on p32uemu -C -V SCREEN, and on the
; gate-level video card (generators/sim_sys.py --video --frame PNG): the two
; must agree instruction for instruction and cycle for cycle, and the frame
; the card draws after HALT must be the picture its RAM holds.

VCTL     = 0xFFFFDF00           ; control: 1 TEXT, 2 OVERLAY, 4 XOR, 8 CURSOR
VSTAT    = 0xFFFFDF01           ; status: 1 VBLANK, 2 DE
VROW     = 0xFFFFDF02
VCOL     = 0xFFFFDF03
KSTAT    = 0xFFFFDF09
VIE      = 0xFFFFDF10
VID      = 0xFFFFDFF0           ; card ID (3)
VISR     = 0xFFFFDFF4
TEXT     = 0xFE050000           ; 25 x 80 cells: character, attribute
OVERLAY  = 0xFE051000           ; 480 lines x 80 bytes (D67)
SCRATCH  = 0xFE050FA0           ; (past the text's 4,000 bytes)
TREC     = 0x00100000           ; the handler's record: bus errors
T_BERR   = 0

        li    sp, 0x00180000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_BERR(r6)
        la    r4, handler
        mtcr  VBR, r4

; ---------------------------------------------------------------- after reset
        addi  r1, r1, 1         ; 1  ID 3, revision 0, everything off, no interrupt
        lbu   r2, VID(r0)
        li    r3, 3
        bne   r2, r3, fail
        lbu   r2, VID+1(r0)
        bnez  r2, fail
        lbu   r2, VCTL(r0)
        bnez  r2, fail
        lw    r2, VISR(r0)
        bnez  r2, fail
        lbu   r2, KSTAT(r0)     ; (nothing typed)
        bnez  r2, fail

; ---------------------------------------------------------------- the RAM, every lane
        addi  r1, r1, 1         ; 2  a word, read back as bytes
        li    r9, SCRATCH
        li    r4, 0x11223344
        sw    r4, 0(r9)
        lbu   r2, 0(r9)
        li    r3, 0x44
        bne   r2, r3, fail
        lbu   r2, 1(r9)
        li    r3, 0x33
        bne   r2, r3, fail
        lbu   r2, 2(r9)
        li    r3, 0x22
        bne   r2, r3, fail
        lbu   r2, 3(r9)
        li    r3, 0x11
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 3  a halfword in the high half, a byte in lane 1
        li    r4, 0xA5C3
        sh    r4, 2(r9)
        li    r4, 0x5A
        sb    r4, 1(r9)
        lw    r2, 0(r9)
        li    r3, 0xA5C35A44
        bne   r2, r3, fail
        lhu   r2, 0(r9)
        li    r3, 0x5A44
        bne   r2, r3, fail
        lhu   r2, 2(r9)
        li    r3, 0xA5C3
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  sixteen words, back to back, then read back
        li    r9, SCRATCH+16
        li    r4, 0x01020304
        li    r5, 16
t4w:    sw    r4, 0(r9)
        addi  r4, r4, 0x0101
        addi  r9, r9, 4
        addi  r5, r5, -1
        bnez  r5, t4w
        li    r9, SCRATCH+16
        li    r4, 0x01020304
        li    r5, 16
t4r:    lw    r2, 0(r9)
        bne   r2, r4, fail
        addi  r4, r4, 0x0101
        addi  r9, r9, 4
        addi  r5, r5, -1
        bnez  r5, t4r

; ---------------------------------------------------------------- the registers
        addi  r1, r1, 1         ; 5  control, cursor and enables read back
        li    r4, 0x0B          ; TEXT, OVERLAY, CURSOR (OR)
        sb    r4, VCTL(r0)
        li    r4, 1
        sb    r4, VROW(r0)
        li    r4, 5
        sb    r4, VCOL(r0)
        li    r4, 3
        sb    r4, VIE(r0)
        lbu   r2, VCTL(r0)
        li    r3, 0x0B
        bne   r2, r3, fail
        lbu   r2, VROW(r0)
        li    r3, 1
        bne   r2, r3, fail
        lbu   r2, VCOL(r0)
        li    r3, 5
        bne   r2, r3, fail
        lbu   r2, VIE(r0)
        li    r3, 3
        bne   r2, r3, fail
        sb    r0, VIE(r0)
        addi  r1, r1, 1         ; 6  the status register, three times (the raster's place: the trace)
        lbu   r12, VSTAT(r0)
        lbu   r13, VSTAT(r0)
        nop
        nop
        lbu   r14, VSTAT(r0)
        andi  r2, r12, 0xFC
        bnez  r2, fail

; ---------------------------------------------------------------- refused: bus errors
        addi  r1, r1, 1         ; 7  a word of the control register
        lw    r2, VCTL(r0)
        addi  r1, r1, 1         ; 8  a halfword of the control register
        lhu   r2, VCTL(r0)
        addi  r1, r1, 1         ; 9  a byte where nothing is (DF05)
        lbu   r2, 0xFFFFDF05(r0)
        addi  r1, r1, 1         ; 10 a byte of the interrupt status word
        lbu   r2, VISR(r0)
        addi  r1, r1, 1         ; 11 a byte below the register page (D000)
        sb    r4, 0xFFFFD000(r0)
        addi  r1, r1, 1         ; 12 five bus errors, each one counted
        lw    r2, T_BERR(r6)
        li    r3, 5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 13 writes to the status and the ID are taken and ignored
        li    r4, 0x77
        sb    r4, VSTAT(r0)
        sb    r4, VID(r0)
        lbu   r2, VID(r0)
        li    r3, 3
        bne   r2, r3, fail
        lw    r2, T_BERR(r6)
        li    r3, 5
        bne   r2, r3, fail

; ---------------------------------------------------------------- a screen
        addi  r1, r1, 1         ; 14 row 0: "HELLO P32W", H plain, E inverse, L underline, L bright, O blink
        la    r10, hello
        la    r11, attrs
        li    r9, TEXT
t14:    lbu   r4, 0(r10)
        beqz  r4, t14d
        lbu   r5, 0(r11)
        sb    r4, 0(r9)
        sb    r5, 1(r9)
        addi  r10, r10, 1
        addi  r11, r11, 1
        addi  r9, r9, 2
        jmp   t14
t14d:   li    r9, TEXT
        lw    r2, 0(r9)
        li    r3, 0x01450048    ; 'H', 0, 'E', 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 15 row 12: the alphabet (halfwords); row 24, column 79: '#'
        li    r9, TEXT+2*(12*80+20)
        li    r4, 0x61          ; 'a'
t15:    sh    r4, 0(r9)
        addi  r9, r9, 2
        addi  r4, r4, 1
        li    r5, 0x7B
        bne   r4, r5, t15
        li    r4, 0x23
        li    r9, TEXT+2*(24*80+79)
        sh    r4, 0(r9)
        lhu   r2, 0(r9)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 16 the overlay: its top and bottom lines, a column at x = 320
        li    r9, OVERLAY
        li    r10, OVERLAY+479*80
        li    r4, -1
        li    r5, 20
t16:    sw    r4, 0(r9)
        sw    r4, 0(r10)
        addi  r9, r9, 4
        addi  r10, r10, 4
        addi  r5, r5, -1
        bnez  r5, t16
        li    r9, OVERLAY+40
        li    r4, 0x80
        li    r5, 480
t16b:   sb    r4, 0(r9)
        addi  r9, r9, 80
        addi  r5, r5, -1
        bnez  r5, t16b
        li    r9, OVERLAY
        lw    r2, 76(r9)
        li    r3, -1
        bne   r2, r3, fail
        lbu   r2, 80+40(r9)
        li    r3, 0x80
        bne   r2, r3, fail
        lbu   r2, 80+41(r9)
        bnez  r2, fail

        li    r1, 0
fail:   halt

; the handler: a bus error (counted, the access skipped); anything else stops the test
handler:
        push  r12
        push  r13
        mfcr  r12, CAUSE
        li    r13, 4
        bne   r12, r13, h_bad
        li    r13, TREC
        lw    r12, T_BERR(r13)
        addi  r12, r12, 1
        sw    r12, T_BERR(r13)
        mfcr  r12, EPC
        addi  r12, r12, 4
        mtcr  EPC, r12
        pop   r13
        pop   r12
        rte
h_bad:  halt

hello:  .asciz "HELLO P32W"
attrs:  .byte 0, 1, 2, 4, 8, 0, 0, 0, 0, 0
