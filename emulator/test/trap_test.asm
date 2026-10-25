; trap_test.asm -- self-checking test of stage A (docs/isa-system.md): traps,
; control registers, user/supervisor mode and the banked r15.
; r1 = current case number; a mismatch HALTs with r1 = that case (in user
; mode via SYSCALL r2 = 98, since HALT is privileged there). All pass: r1 = 0.
;
; The handler records CAUSE, EPC, BADADDR and STATUS of every trap in TREC,
; counts traps, and continues after the trapping instruction -- except
; SYSCALL with r2 = 99 (return to supervisor at `resume`) or r2 = 98 (fail).
; Since stage C, user addresses go through the TLB: the handler first
; identity-maps (virtual = physical) any page on a TLB miss, unrecorded, so
; user-mode behaviour is as in stage A.

TREC     = 0x00100000           ; trap record in RAM
T_CAUSE  = 0
T_EPC    = 4
T_BAD    = 8
T_STAT   = 12
T_COUNT  = 16
SSP_TOP  = 0x00500000           ; supervisor stack
USP_TOP  = 0x00400000           ; user stack
CON      = 0xFFFFFF00

        li    sp, SSP_TOP
        li    r1, 0
        li    r6, TREC
        sw    r0, T_COUNT(r6)

; ---------------------------------------------------------------- supervisor mode
        addi  r1, r1, 1         ; 1  reset state: STATUS = S only
        mfcr  r2, STATUS
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 2  VBR reads back as written; its low 2 bits are
        la    r4, handler       ;    ignored when a trap jumps there (D15)
        ori   r5, r4, 3
        mtcr  VBR, r5
        mfcr  r2, VBR
        bne   r2, r5, fail
        addi  r1, r1, 1         ; 3  SYSCALL: CAUSE = 5
        li    r2, 1
sc3:    syscall
        lw    r2, T_CAUSE(r6)
        li    r3, 5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  EPC = address of the SYSCALL itself
        lw    r2, T_EPC(r6)
        la    r3, sc3
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  STATUS in the handler: S = 1, PS = 1
        lw    r2, T_STAT(r6)
        li    r3, 5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 6  after RTE: S = PS = 1, PS unchanged
        mfcr  r2, STATUS
        li    r3, 5
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 7  BREAK: CAUSE = 6
        break
        lw    r2, T_CAUSE(r6)
        li    r3, 6
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 8  misaligned load: CAUSE = 3
        li    r4, 0x1234
        li    r5, 0x00100002
        lw    r4, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 3
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 9  BADADDR = the misaligned address
        lw    r2, T_BAD(r6)
        bne   r2, r5, fail
        addi  r1, r1, 1         ; 10 the faulting load had no effect
        li    r3, 0x1234
        bne   r4, r3, fail
        addi  r1, r1, 1         ; 11 reserved control register: ILLEGAL
        mfcr  r2, 12
        lw    r2, T_CAUSE(r6)
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 12 four traps so far
        lw    r2, T_COUNT(r6)
        li    r3, 4
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 13 SSP balanced after the traps
        li    r3, SSP_TOP
        bne   sp, r3, fail

; ---------------------------------------------------------------- to user mode
        addi  r1, r1, 1         ; 14 USP set and read back; r15 is still the SSP
        li    r4, USP_TOP
        mtcr  USP, r4
        mfcr  r2, USP
        bne   r2, r4, fail
        li    r3, SSP_TOP
        bne   sp, r3, fail
        la    r4, user
        mtcr  EPC, r4
        li    r4, 1             ; S = 1, PS = 0: RTE enters user mode at EPC
        mtcr  STATUS, r4
        rte

user:   addi  r1, r1, 1         ; 15 user mode sees the user SP
        li    r3, USP_TOP
        bne   sp, r3, ufail
        addi  r1, r1, 1         ; 16 SYSCALL from user mode: handler sees S = 1, PS = 0
        li    r2, 1
        syscall
        lw    r2, T_STAT(r6)
        li    r3, 1
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 17 user SP unchanged by the trap
        li    r3, USP_TOP
        bne   sp, r3, ufail
        addi  r1, r1, 1         ; 18 privileged instruction: PRIVILEGE
        li    r4, 0x5555
up18:   mfcr  r4, STATUS
        lw    r2, T_CAUSE(r6)
        li    r3, 2
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 19 EPC points at it; r4 unchanged
        lw    r2, T_EPC(r6)
        la    r3, up18
        bne   r2, r3, ufail
        li    r3, 0x5555
        bne   r4, r3, ufail
        addi  r1, r1, 1         ; 20 HALT is privileged
        halt
        lw    r2, T_CAUSE(r6)
        li    r3, 2
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 21 I/O page access: PRIVILEGE, BADADDR = address
        lbu   r4, CON(r0)
        lw    r2, T_CAUSE(r6)
        li    r3, 2
        bne   r2, r3, ufail
        lw    r2, T_BAD(r6)
        li    r3, CON
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 22 r4 unchanged
        li    r3, 0x5555
        bne   r4, r3, ufail
        addi  r1, r1, 1         ; 23 CALL with an unmapped SP: BUSERROR ...
        mov   r9, sp
        li    sp, 0x02000000
        call  user
        mov   r8, sp
        mov   sp, r9
        lw    r2, T_CAUSE(r6)
        li    r3, 4
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 24 ... SP unchanged (commit after access) ...
        li    r3, 0x02000000
        bne   r8, r3, ufail
        addi  r1, r1, 1         ; 25 ... and BADADDR = the push address
        lw    r2, T_BAD(r6)
        li    r3, 0x01FFFFFC
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 26 ordinary CALL/RET on the user stack
        call  usub
        li    r3, 0x77
        bne   r2, r3, ufail
        li    r2, 99            ; back to supervisor mode at `resume`
        syscall
ufail:  li    r2, 98            ; handler HALTs with r1 = failing case
        syscall
usub:   li    r2, 0x77
        ret

; ---------------------------------------------------------------- back in supervisor
resume: addi  r1, r1, 1         ; 27 supervisor mode again
        mfcr  r2, STATUS
        andi  r2, r2, 1
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 28 r15 is the SSP again
        li    r3, SSP_TOP
        bne   sp, r3, fail
        addi  r1, r1, 1         ; 29 ten traps in all
        lw    r2, T_COUNT(r6)
        li    r3, 10
        bne   r2, r3, fail
        li    r1, 0
        halt
fail:   halt

; ---------------------------------------------------------------- handler
handler:
        push  r12               ; on the supervisor stack, whatever the user SP
        push  r13
        mfcr  r12, CAUSE        ; TLB miss (8-10): identity-map and re-execute
        addi  r12, r12, -8
        sltiu r12, r12, 3
        beqz  r12, h_rec
        mfcr  r12, BADADDR
        srli  r12, r12, 12
        slli  r12, r12, 12
        ori   r12, r12, 7       ; V | W | X
        mtcr  TLBLO, r12
        tlbw
        pop   r13
        pop   r12
        rte
h_rec:  li    r13, TREC
        mfcr  r12, CAUSE
        sw    r12, T_CAUSE(r13)
        mfcr  r12, EPC
        sw    r12, T_EPC(r13)
        mfcr  r12, BADADDR
        sw    r12, T_BAD(r13)
        mfcr  r12, STATUS
        sw    r12, T_STAT(r13)
        lw    r12, T_COUNT(r13)
        addi  r12, r12, 1
        sw    r12, T_COUNT(r13)
        mfcr  r12, CAUSE
        li    r13, 5
        bne   r12, r13, h_skip  ; not a SYSCALL
        li    r13, 99
        beq   r2, r13, h_resume
        li    r13, 98
        beq   r2, r13, h_fail
h_skip: mfcr  r12, EPC          ; continue after the trapping instruction
        addi  r12, r12, 4
        mtcr  EPC, r12
        pop   r13
        pop   r12
        rte
h_resume:
        pop   r13
        pop   r12
        jmp   resume
h_fail: halt                    ; supervisor mode: r1 = failing user case
