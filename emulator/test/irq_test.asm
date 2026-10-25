; irq_test.asm -- self-checking test of stage B (docs/isa-system.md): IPEND,
; IMASK, the timer device, interrupt delivery and timing, WAIT.
; r1 = current case; a mismatch HALTs with r1 = that case. All pass: r1 = 0.
; The handler records CAUSE, EPC and r7, counts interrupts, acknowledges and
; stops the timer, and returns. An unexpected exception HALTs (r1 = case).

TREC     = 0x00100000
T_CAUSE  = 0
T_EPC    = 4
T_COUNT  = 8
T_R7     = 12
SSP_TOP  = 0x00500000
TCOUNT   = 0xFFFFFF10           ; timer registers (isa-system.md B4)
TRELOAD  = 0xFFFFFF14
TCTRL    = 0xFFFFFF18           ; bit 0 enable, bit 1 pending (write 1 = ack)

        li    sp, SSP_TOP
        li    r1, 0
        li    r6, TREC
        sw    r0, T_COUNT(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  IMASK resets to 0
        mfcr  r2, IMASK
        bnez  r2, fail
        addi  r1, r1, 1         ; 2  IPEND is 0 at reset
        mfcr  r2, IPEND
        bnez  r2, fail
        addi  r1, r1, 1         ; 3  the timer ticks once per instruction
        li    r4, 100
        sw    r4, TRELOAD(r0)
        sw    r4, TCOUNT(r0)
        li    r4, 1
        sw    r4, TCTRL(r0)     ; enable: counting starts with the next instruction
        lw    r2, TCOUNT(r0)    ; this instruction's tick: 100 -> 99
        li    r3, 99
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  reaching 0 asserts IRQ0 (IPEND bit 0)
        li    r4, 3
        sw    r4, TCOUNT(r0)
        nop                     ; 3 -> 2
        nop                     ; 2 -> 1
        nop                     ; 1 -> 0: pending, reload
        mfcr  r2, IPEND
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  TCTRL shows enable + pending
        lw    r2, TCTRL(r0)
        li    r3, 3
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 6  IMASK set but IE off: no interrupt taken
        li    r4, 1
        mtcr  IMASK, r4
        nop
        lw    r2, T_COUNT(r6)
        bnez  r2, fail
        addi  r1, r1, 1         ; 7  acknowledging clears the pending line
        li    r4, 2             ; ack, and disable
        sw    r4, TCTRL(r0)
        mfcr  r2, IPEND
        bnez  r2, fail

        addi  r1, r1, 1         ; 8  exact timing: count 5 -> interrupt after the
        li    r4, 1000          ;    5th ADDI, before the 6th
        sw    r4, TRELOAD(r0)
        li    r4, 5
        sw    r4, TCOUNT(r0)
        li    r7, 0
        li    r4, 3             ; S | IE
        mtcr  STATUS, r4
        li    r4, 1
        sw    r4, TCTRL(r0)     ; enable
        addi  r7, r7, 1         ; tick 5 -> 4
        addi  r7, r7, 1         ; 4 -> 3
        addi  r7, r7, 1         ; 3 -> 2
        addi  r7, r7, 1         ; 2 -> 1
        addi  r7, r7, 1         ; 1 -> 0: IRQ0 asserted
i6:     addi  r7, r7, 1         ; interrupt taken before this one
        addi  r7, r7, 1
        addi  r7, r7, 1
        addi  r7, r7, 1
        addi  r7, r7, 1
        lw    r2, T_R7(r6)      ; r7 seen by the handler
        li    r3, 5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 9  EPC = the next, unexecuted instruction
        lw    r2, T_EPC(r6)
        la    r3, i6
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 10 CAUSE = bit 31 | IRQ0
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80000000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 11 the interrupted code then ran to completion
        li    r3, 10
        bne   r7, r3, fail
        addi  r1, r1, 1         ; 12 exactly one interrupt
        lw    r2, T_COUNT(r6)
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 13 RTE restored IE
        mfcr  r2, STATUS
        andi  r2, r2, 2
        beqz  r2, fail

        addi  r1, r1, 1         ; 14 WAIT with IE off returns once the timer fires,
        li    r4, 1             ;    without taking an interrupt
        mtcr  STATUS, r4
        li    r4, 50
        sw    r4, TCOUNT(r0)
        li    r4, 1
        sw    r4, TCTRL(r0)
        wait
        mfcr  r2, IPEND
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 15
        lw    r2, T_COUNT(r6)
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 16 WAIT with IE on: the interrupt is taken right
        li    r4, 2             ;    after WAIT, EPC = the instruction after it
        sw    r4, TCTRL(r0)     ; ack + disable
        li    r4, 50
        sw    r4, TCOUNT(r0)
        li    r4, 3
        mtcr  STATUS, r4
        li    r4, 1
        sw    r4, TCTRL(r0)
        wait
a16:    nop
        lw    r2, T_EPC(r6)
        la    r3, a16
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 17 two interrupts in all
        lw    r2, T_COUNT(r6)
        li    r3, 2
        bne   r2, r3, fail

        li    r1, 0
        halt
fail:   halt

handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        sw    r12, T_CAUSE(r13)
        mfcr  r12, EPC
        sw    r12, T_EPC(r13)
        sw    r7, T_R7(r13)
        lw    r12, T_COUNT(r13)
        addi  r12, r12, 1
        sw    r12, T_COUNT(r13)
        li    r12, 2            ; acknowledge and stop the timer
        sw    r12, TCTRL(r0)
        mfcr  r12, CAUSE
        blt   r12, r0, h_ret    ; bit 31 set: an interrupt, as expected
        halt                    ; unexpected exception: r1 = case
h_ret:  pop   r13
        pop   r12
        rte
