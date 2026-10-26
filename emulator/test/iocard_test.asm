; iocard_test.asm -- the I/O card's devices at the hardware's timing (D65):
; the timer as the card counts (1 MHz: a tick every third 3 MHz cycle, p32uemu
; -C), its interrupt and vector, the interrupt status word, the clock's
; registers, the USB controllers, and the accesses the card must refuse.
; Self-checking: HALT with r1 = 0, or the number of the failing case. Run on
; p32uemu -C, and on the gate-level I/O card (generators/sim_sys.py --io):
; the two must agree instruction for instruction.

TCOUNT   = 0xFFFFFF10
TRELOAD  = 0xFFFFFF14
TCTRL    = 0xFFFFFF18           ; bit 0 enable, bit 1 pending (write 1 = ack)
RTCIDX   = 0xFFFFFF40
RTCDAT   = 0xFFFFFF41
USB0D    = 0xFFFFFF50
USB0C    = 0xFFFFFF51
USB1D    = 0xFFFFFF54
USB1C    = 0xFFFFFF55
IOISR    = 0xFFFFFFF4
TREC     = 0x00100000           ; the handler's record: CAUSE, EPC, interrupts, bus errors
T_CAUSE  = 0
T_EPC    = 4
T_IRQS   = 8
T_BERR   = 12

        li    sp, 0x00180000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_IRQS(r6)
        sw    r0, T_BERR(r6)
        la    r4, handler
        mtcr  VBR, r4

; ---------------------------------------------------------------- the timer's registers
        addi  r1, r1, 1         ; 1  TRELOAD reads back
        li    r4, 0x12345678
        sw    r4, TRELOAD(r0)
        lw    r2, TRELOAD(r0)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 2  TCOUNT, the timer stopped, reads back
        li    r4, 0x00ABCDEF
        sw    r4, TCOUNT(r0)
        lw    r2, TCOUNT(r0)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 3  TCTRL: stopped, nothing pending
        lw    r2, TCTRL(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 4  enabled, it counts down
        li    r4, 300
        sw    r4, TCOUNT(r0)
        li    r4, 1
        sw    r4, TCTRL(r0)
        li    r5, 300
c4:     lw    r2, TCOUNT(r0)
        beq   r2, r5, c4
        bgeu  r2, r5, fail      ; (down, not up)
        addi  r1, r1, 1         ; 5  TCTRL: enabled
        lw    r2, TCTRL(r0)
        andi  r2, r2, 1
        beqz  r2, fail

; ---------------------------------------------------------------- expiry, pending, the interrupt status
        addi  r1, r1, 1         ; 6  at 0 the timer reloads and sets pending
        li    r4, 2             ; (ack, stop)
        sw    r4, TCTRL(r0)
        li    r4, 1000
        sw    r4, TRELOAD(r0)
        li    r4, 4
        sw    r4, TCOUNT(r0)
        li    r4, 1
        sw    r4, TCTRL(r0)
c6:     lw    r2, TCTRL(r0)
        andi  r2, r2, 2
        beqz  r2, c6
        lw    r2, TCOUNT(r0)    ; reloaded: at most 1000, well above 4
        li    r3, 900
        bltu  r2, r3, fail
        addi  r1, r1, 1         ; 7  the interrupt status word: bit 0, the timer
        lw    r2, IOISR(r0)
        andi  r2, r2, 1
        beqz  r2, fail
        addi  r1, r1, 1         ; 8  IPEND: level 0
        mfcr  r2, IPEND
        andi  r2, r2, 1
        beqz  r2, fail
        addi  r1, r1, 1         ; 9  acknowledged: pending gone, still counting
        li    r4, 3
        sw    r4, TCTRL(r0)
        lw    r2, TCTRL(r0)
        li    r3, 1
        bne   r2, r3, fail
        lw    r2, IOISR(r0)
        andi  r2, r2, 1
        bnez  r2, fail

; ---------------------------------------------------------------- the vectored interrupt
        addi  r1, r1, 1         ; 10 the timer's interrupt, taken from WAIT
        li    r4, 2
        sw    r4, TCTRL(r0)     ; ack, stop
        li    r4, 60
        sw    r4, TCOUNT(r0)
        li    r4, 1
        mtcr  IMASK, r4
        li    r4, 3             ; S | IE
        mtcr  STATUS, r4
        li    r4, 1
        sw    r4, TCTRL(r0)
        wait
        nop
        lw    r2, T_IRQS(r6)
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 11 CAUSE: an interrupt, vector 70 (window 7, device 0), level 0
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80007000
        bne   r2, r3, fail
        li    r4, 1             ; interrupts off again
        mtcr  STATUS, r4
        mtcr  IMASK, r0

; ---------------------------------------------------------------- the real-time clock
        addi  r1, r1, 1         ; 12 its battery-backed RAM: two bytes, each read back
        li    r4, 0x0E
        sb    r4, RTCIDX(r0)
        li    r4, 0x5A
        sb    r4, RTCDAT(r0)
        li    r4, 0x0F
        sb    r4, RTCIDX(r0)
        li    r4, 0xA5
        sb    r4, RTCDAT(r0)
        li    r4, 0x0E
        sb    r4, RTCIDX(r0)
        lbu   r2, RTCDAT(r0)
        li    r3, 0x5A
        bne   r2, r3, fail
        li    r4, 0x0F
        sb    r4, RTCIDX(r0)
        lbu   r2, RTCDAT(r0)
        li    r3, 0xA5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 13 register D: the battery is good
        li    r4, 0x0D
        sb    r4, RTCIDX(r0)
        lbu   r2, RTCDAT(r0)
        li    r3, 0x80
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 14 the register number reads FF
        lbu   r2, RTCIDX(r0)
        li    r3, 0xFF
        bne   r2, r3, fail

; ---------------------------------------------------------------- the USB controllers
        addi  r1, r1, 1         ; 15 USB 0: CHECK_EXIST answers the byte inverted
        li    r4, 0x06
        sb    r4, USB0C(r0)
        li    r4, 0x3C
        sb    r4, USB0D(r0)
        lbu   r2, USB0D(r0)
        li    r3, 0xC3
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 16 USB 1, the same
        li    r4, 0x06
        sb    r4, USB1C(r0)
        li    r4, 0x81
        sb    r4, USB1D(r0)
        lbu   r2, USB1D(r0)
        li    r3, 0x7E
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 17 their status: no interrupt (bit 7 high)
        lbu   r2, USB0C(r0)
        li    r3, 0x80
        bne   r2, r3, fail
        lbu   r2, USB1C(r0)
        bne   r2, r3, fail

; ---------------------------------------------------------------- refused: bus errors
        addi  r1, r1, 1         ; 18 a word read of a UART register
        lw    r2, 0xFFFFFF00(r0)
        addi  r1, r1, 1         ; 19 a byte read of the timer
        lbu   r2, TCOUNT(r0)
        addi  r1, r1, 1         ; 20 a byte where nothing is (FF56)
        lbu   r2, 0xFFFFFF56(r0)
        addi  r1, r1, 1         ; 21 a byte of the interrupt status word
        lbu   r2, IOISR(r0)
        addi  r1, r1, 1         ; 22 a halfword of a UART
        lhu   r2, 0xFFFFFF00(r0)
        addi  r1, r1, 1         ; 23 five bus errors, each one counted
        lw    r2, T_BERR(r6)
        li    r3, 5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 24 a byte write to the ID register is taken and ignored
        li    r4, 0x77
        sb    r4, 0xFFFFFFF0(r0)
        lbu   r2, 0xFFFFFFF0(r0)
        li    r3, 1
        bne   r2, r3, fail
        lw    r2, T_BERR(r6)
        li    r3, 5
        bne   r2, r3, fail

        li    r4, 2
        sw    r4, TCTRL(r0)     ; the timer stopped
        li    r1, 0
fail:   halt

; the handler: an interrupt (the timer: recorded, acknowledged, stopped) or
; a bus error (counted, the access skipped); anything else stops the test
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
        mfcr  r12, EPC
        sw    r12, T_EPC(r13)
        lw    r12, T_IRQS(r13)
        addi  r12, r12, 1
        sw    r12, T_IRQS(r13)
        li    r12, 2
        sw    r12, TCTRL(r0)
h_ret:  pop   r13
        pop   r12
        rte
h_bad:  halt
