; vidcard_test.asm -- the video card, revision 2 (docs/video-card-acrtc.md;
; D103, D106, D107), for the gate-level run: generators/sim_sys.py --video
; against p32uemu -C, instruction for instruction and cycle for cycle. It is
; video_test.asm's short relative: what goes through the card's own logic
; (the chip's four-clock cycle, its registers and FIFO, the PS/2 latches and
; flags, both interrupts TAKEN, each with its vector from the acknowledge
; cycle, bus errors) and none of the drawing, which is the chip's model's.
; Self-checking: HALT with r1 = 0, or with the number of the failing case.
; Run with -K KEYS, KEYS holding "ab".

VSTAT    = 0xFFFFDF00           ; halfword, read: the chip's status in bits 7-0
VAR      = 0xFFFFDF00           ; halfword, write: the chip's address register
VDATA    = 0xFFFFDF04           ; halfword: the register addressed; address 0: the FIFOs
PSTAT    = 0xFFFFDF08           ; byte, read: 1 FULL, 2 SRC, 4 BUSY, 8 TEXT, 0x10 the chip's IRQ
PCMD     = 0xFFFFDF08           ; byte, write: a command to the PS/2 section (two clocks)
PDATA    = 0xFFFFDF0C           ; byte, read: the PS/2 byte
VID      = 0xFFFFDFF0           ; card ID (3)
VREV     = 0xFFFFDFF1           ; revision (2)
VISR     = 0xFFFFDFF4           ; interrupt status, a word: bit 0 the PS/2 section, 1 the chip
TREC     = 0x00100000           ; the handler's record
T_CAUSE  = 0                    ; the last interrupt's CAUSE
T_IRQS   = 4                    ; interrupts taken
T_BERR   = 8                    ; bus errors
T_BYTE   = 12                   ; the PS/2 byte the handler read

        li    sp, 0x00500000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_CAUSE(r6)
        sw    r0, T_IRQS(r6)
        sw    r0, T_BERR(r6)
        sw    r0, T_BYTE(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  the card: ID 3, revision 2, nothing asking
        lbu   r2, VID(r0)
        li    r3, 3
        bne   r2, r3, fail
        lbu   r2, VREV(r0)
        li    r3, 2
        bne   r2, r3, fail
        lw    r2, VISR(r0)
        bnez  r2, fail

        addi  r1, r1, 1         ; 2  the chip after reset: status 23, CCR 8000 (ABT)
        lhu   r2, VSTAT(r0)
        andi  r2, r2, 0xFF
        li    r3, 0x23
        bne   r2, r3, fail
        li    r4, 0x02
        sh    r4, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0x8000
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 3  registers are words; r80 and up step the address register by 2
        li    r4, 0x82
        sh    r4, VAR(r0)
        li    r4, 0x630C
        sh    r4, VDATA(r0)     ; r82
        li    r4, 0x054F
        sh    r4, VDATA(r0)     ; r84
        li    r4, 0x82
        sh    r4, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0x630C
        bne   r2, r3, fail
        lhu   r2, VDATA(r0)
        li    r3, 0x054F
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 4  ABT off; a command through the write FIFO (ORG, three words): the chip is
        li    r4, 0x02          ;    busy, then at its end again
        sh    r4, VAR(r0)
        sh    r0, VDATA(r0)
        sh    r0, VAR(r0)
        li    r4, 0x0400
        sh    r4, VDATA(r0)
        li    r4, 0x4004
        sh    r4, VDATA(r0)
        li    r4, 0xAD80
        sh    r4, VDATA(r0)
        li    r5, 20000
c4:     lhu   r2, VSTAT(r0)
        andi  r2, r2, 0x20
        bnez  r2, c4done
        addi  r5, r5, -1
        bnez  r5, c4
        jmp   fail
c4done:

        addi  r1, r1, 1         ; 5  the chip's interrupt, taken: write FIFO empty enabled in CCR -> level 4, the
        li    r4, 0x10          ;    acknowledge cycle's vector 51 (window 5, device 1)
        mtcr  IMASK, r4
        li    r4, 3             ; S | IE
        mtcr  STATUS, r4
        li    r4, 0x02
        sh    r4, VAR(r0)
        li    r4, 0x0001
        sh    r4, VDATA(r0)
        nop
        nop
        nop
        nop
        lw    r2, T_IRQS(r6)
        li    r3, 1
        bne   r2, r3, fail
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80005104
        bne   r2, r3, fail
        lw    r2, VISR(r0)      ;    the handler turned the enable off: nothing asks
        bnez  r2, fail

        addi  r1, r1, 1         ; 6  the keyboard typed "ab": 'a' waits, from the keyboard; read, and 'b' waits
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 0x0F
        li    r3, 1
        bne   r2, r3, fail
        lbu   r2, PDATA(r0)
        li    r3, 0x61
        bne   r2, r3, fail
        nop
        nop
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 0x0F
        li    r3, 1
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 7  the PS/2 section's interrupt, taken: on (command 01) with 'b' waiting -> level
        li    r4, 0x01          ;    4, vector 50; the handler reads the byte and turns it off (02)
        sb    r4, PCMD(r0)
        nop
        nop
        nop
        nop
        lw    r2, T_IRQS(r6)
        li    r3, 2
        bne   r2, r3, fail
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80005004
        bne   r2, r3, fail
        lw    r2, T_BYTE(r6)
        li    r3, 0x62
        bne   r2, r3, fail
        li    r4, 1             ; interrupts off again
        mtcr  STATUS, r4
        mtcr  IMASK, r0
        lbu   r2, PSTAT(r0)     ;    nothing waits, nothing is busy, nothing asks
        andi  r2, r2, 0x1F
        bnez  r2, fail
        lw    r2, VISR(r0)
        bnez  r2, fail

        addi  r1, r1, 1         ; 8  refused, each a bus error: a byte of the chip's status, a write to the PS/2
        lbu   r2, VSTAT(r0)     ;    byte, a halfword of the card status
        sb    r4, PDATA(r0)
        lhu   r2, PSTAT(r0)
        lw    r2, T_BERR(r6)
        li    r3, 3
        bne   r2, r3, fail

        li    r1, 0
fail:   halt                    ; r1 = failing case number

; the handler: an interrupt (the chip's: its enables off; the PS/2 section's: the byte read, its interrupt
; off), or a bus error (counted, the access skipped); anything else stops the test
handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        blt   r12, r0, h_irq
        li    r13, 4
        bne   r12, r13, h_bad
        li    r13, TREC
        lw    r12, T_BERR(r13)
        addi  r12, r12, 1
        sw    r12, T_BERR(r13)
        mfcr  r12, EPC
        addi  r12, r12, 4
        mtcr  EPC, r12
        jmp   h_ret
h_irq:  sw    r12, T_CAUSE(r13)
        lw    r12, T_IRQS(r13)
        addi  r12, r12, 1
        sw    r12, T_IRQS(r13)
        lw    r12, T_CAUSE(r13)
        andi  r12, r12, 0x100
        beqz  r12, h_ps2
        li    r12, 0x02         ; the chip: CCR's enables off
        sh    r12, VAR(r0)
        sh    r0, VDATA(r0)
        jmp   h_ret
h_ps2:  lbu   r12, PDATA(r0)
        sw    r12, T_BYTE(r13)
        li    r12, 0x02
        sb    r12, PCMD(r0)
h_ret:  pop   r13
        pop   r12
        rte
h_bad:  halt
