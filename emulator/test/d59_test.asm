; d59_test.asm -- self-checking test of the D59 instructions: indexed loads
; and stores (rs + rt * scale), post-increment loads and stores (the access at
; rs + off, then rs += inc) and LI32 (a 32-bit constant in the next word).
; r1 = number of the current case; a mismatch HALTs with r1 = that case
; (the exit status); all pass: HALT with r1 = 0.
; Registers: r2 = result, r3 = expected, r4 / r5 = operands, r6 = the data
; (in RAM).

        li    sp, 0x00500000
        li    r1, 0
        li    r6, 0x00400000    ; the data, in RAM
        li    r4, 0x01234567
        sw    r4, 0(r6)
        li    r4, 0x89ABCDEF
        sw    r4, 4(r6)
        sw    r0, 16(r6)
        sw    r0, 20(r6)

; ---------------------------------------------------------------- indexed
        addi  r1, r1, 1         ; 1  LWX, scale 1: a byte offset
        li    r4, 4
        lwx   r2, r6, r4
        li    r3, 0x89ABCDEF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 2  LWX, scale 4: a word index
        li    r4, 1
        lwx   r2, r6, r4, 4
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 3  LBX sign-extends
        li    r4, 7
        lbx   r2, r6, r4
        li    r3, 0xFFFFFF89
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  LBUX zero-extends
        lbux  r2, r6, r4
        li    r3, 0x89
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  LHX, scale 2, sign-extends
        li    r4, 3
        lhx   r2, r6, r4, 2
        li    r3, 0xFFFF89AB
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 6  LHUX zero-extends
        lhux  r2, r6, r4, 2
        li    r3, 0x89AB
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 7  SWX, scale 8, then read back
        li    r4, 2
        li    r5, 0x11223344
        swx   r5, r6, r4, 8
        lw    r2, 16(r6)
        bne   r2, r5, fail
        addi  r1, r1, 1         ; 8  SBX stores one byte
        li    r4, 17
        li    r5, 0xAA
        sbx   r5, r6, r4
        lw    r2, 16(r6)
        li    r3, 0x1122AA44
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 9  SHX, scale 2, stores one halfword
        li    r4, 9
        li    r5, 0xBEEF
        shx   r5, r6, r4, 2
        lw    r2, 16(r6)
        li    r3, 0xBEEFAA44
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 10 the index r0: the base alone
        lwx   r2, r6, r0, 8
        li    r3, 0x01234567
        bne   r2, r3, fail

; ---------------------------------------------------------------- post-increment
        addi  r1, r1, 1         ; 11 LWP: the value, then rs += 4
        mov   r5, r6
        lwp   r2, (r5), 4
        li    r3, 0x01234567
        bne   r2, r3, fail
        addi  r4, r6, 4
        bne   r5, r4, fail
        addi  r1, r1, 1         ; 12 LWP with an offset and a negative step
        lwp   r2, -4(r5), -8
        bne   r2, r3, fail
        addi  r4, r6, -4
        bne   r5, r4, fail
        addi  r1, r1, 1         ; 13 LBP sign-extends, rs += 1
        addi  r5, r6, 7
        lbp   r2, (r5), 1
        li    r3, 0xFFFFFF89
        bne   r2, r3, fail
        addi  r4, r6, 8
        bne   r5, r4, fail
        addi  r1, r1, 1         ; 14 LBUP zero-extends
        addi  r5, r6, 7
        lbup  r2, (r5), 1
        li    r3, 0x89
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 15 LHP sign-extends, rs += 2
        addi  r5, r6, 6
        lhp   r2, (r5), 2
        li    r3, 0xFFFF89AB
        bne   r2, r3, fail
        addi  r4, r6, 8
        bne   r5, r4, fail
        addi  r1, r1, 1         ; 16 LHUP zero-extends
        addi  r5, r6, 6
        lhup  r2, (r5), 2
        li    r3, 0x89AB
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 17 SWP: stored, then rs += 4
        addi  r5, r6, 20
        li    r4, 0x55667788
        swp   r4, (r5), 4
        lw    r2, 20(r6)
        bne   r2, r4, fail
        addi  r3, r6, 24
        bne   r5, r3, fail
        addi  r1, r1, 1         ; 18 SBP with an offset
        li    r4, 0x99
        sbp   r4, -1(r5), -1    ; byte 23
        lw    r2, 20(r6)
        li    r3, 0x99667788
        bne   r2, r3, fail
        addi  r3, r6, 23
        bne   r5, r3, fail
        addi  r1, r1, 1         ; 19 SHP
        addi  r5, r6, 20
        li    r4, 0x1234
        shp   r4, (r5), 2
        lw    r2, 20(r6)
        li    r3, 0x99661234
        bne   r2, r3, fail
        addi  r3, r6, 22
        bne   r5, r3, fail
        addi  r1, r1, 1         ; 20 LWP with rd = rs: rs + inc is written last
        mov   r5, r6
        lwp   r5, (r5), 4
        addi  r3, r6, 4
        bne   r5, r3, fail
        addi  r1, r1, 1         ; 21 post-increment of r0: r0 stays 0
        lwp   r2, (r0), 4
        mov   r3, r0
        bne   r3, r0, fail

; ---------------------------------------------------------------- LI32
        addi  r1, r1, 1         ; 22 LI32
        li32  r2, 0x12345678
        li    r3, 0x1234
        srli  r4, r2, 16
        bne   r4, r3, fail
        andi  r4, r2, 0xFFFF
        li    r3, 0x5678
        bne   r4, r3, fail
        addi  r1, r1, 1         ; 23 LI32 of a negative value
        li32  r2, -2
        addi  r2, r2, 2
        bne   r2, r0, fail
        addi  r1, r1, 1         ; 24 LI32 then a taken branch right after it
        li32  r2, 7
        beq   r0, r0, t24
        jmp   fail
t24:    li    r3, 7
        bne   r2, r3, fail

; ---------------------------------------------------------------- faults
; A faulting access has no effect: neither rd nor rs changes, EPC is the
; instruction (so RTE could re-execute it). The handler counts in r7 and
; skips the instruction.
        la    r4, handler
        mtcr  VBR, r4
        li    r7, 0
        addi  r1, r1, 1         ; 25 LWP misaligned: rd and rs unchanged
        addi  r5, r6, 0
        li    r2, 77
        lwp   r2, 1(r5), 4
f25:    li    r3, 1
        bne   r7, r3, fail
        bne   r5, r6, fail
        li    r3, 77
        bne   r2, r3, fail
        la    r3, f25 - 4
        bne   r8, r3, fail      ; (r8 = EPC)
        addi  r1, r1, 1         ; 26 SWP misaligned: no store, rs unchanged
        li    r4, 0x12345678
        swp   r4, 2(r5), -4
        li    r3, 2
        bne   r7, r3, fail
        bne   r5, r6, fail
        lw    r2, 0(r6)
        li    r3, 0x01234567
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 27 LWX misaligned: rd unchanged; the cause
        li    r4, 3
        li    r2, 77
        lwx   r2, r6, r4
        li    r3, 3
        bne   r7, r3, fail
        li    r3, 77
        bne   r2, r3, fail
        li    r3, 3             ; MISALIGNED
        bne   r9, r3, fail      ; (r9 = CAUSE)

        li    r1, 0
        halt
fail:   halt

handler:
        addi  r7, r7, 1
        mfcr  r8, EPC
        mfcr  r9, CAUSE
        addi  r10, r8, 4
        mtcr  EPC, r10
        rte
