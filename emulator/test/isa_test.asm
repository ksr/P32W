; isa_test.asm -- self-checking test of every ISA v0 instruction.
; r1 = number of the current case. On a mismatch the program jumps to fail,
; which HALTs with r1 = that case number (the emulator's exit status).
; All cases pass: HALT with r1 = 0. Expected values follow docs/isa-v0.md.
; Registers: r2 = result, r3 = expected, r4/r5 = operands, r6 = RAM base.

        li    sp, 0x00500000
        li    r1, 0

; ---------------------------------------------------------------- R-type ALU
        addi  r1, r1, 1         ; 1  ADD
        li    r4, 5
        li    r5, 7
        add   r2, r4, r5
        li    r3, 12
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 2  ADD wraps modulo 2^32
        li    r4, 0xFFFFFFFF
        li    r5, 1
        add   r2, r4, r5
        bne   r2, r0, fail
        addi  r1, r1, 1         ; 3  SUB
        li    r4, 5
        li    r5, 7
        sub   r2, r4, r5
        li    r3, 0xFFFFFFFE
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  AND
        li    r4, 0xF0F0F0F0
        li    r5, 0xFF00FF00
        and   r2, r4, r5
        li    r3, 0xF000F000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  OR
        li    r4, 0xF0F0F0F0
        li    r5, 0x0F0F0000
        or    r2, r4, r5
        li    r3, 0xFFFFF0F0
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 6  XOR
        li    r4, 0xFFFF0000
        li    r5, 0x0FF00FF0
        xor   r2, r4, r5
        li    r3, 0xF00F0FF0
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 7  NOR
        li    r4, 0xF0F0F0F0
        li    r5, 0x0F0F0000
        nor   r2, r4, r5
        li    r3, 0x00000F0F
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 8  NOT (pseudo: NOR rd,rs,r0)
        li    r4, 0x12345678
        not   r2, r4
        li    r3, 0xEDCBA987
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 9  SLL
        li    r4, 1
        li    r5, 31
        sll   r2, r4, r5
        li    r3, 0x80000000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 10 SLL uses only the low 5 bits of the amount
        li    r4, 1
        li    r5, 33
        sll   r2, r4, r5
        li    r3, 2
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 11 SRL zero-fills
        li    r4, 0x80000000
        li    r5, 4
        srl   r2, r4, r5
        li    r3, 0x08000000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 12 SRA sign-fills
        li    r4, 0x80000000
        li    r5, 4
        sra   r2, r4, r5
        li    r3, 0xF8000000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 13 SLT signed: -1 < 1
        li    r4, -1
        li    r5, 1
        slt   r2, r4, r5
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 14 SLTU unsigned: 0xFFFFFFFF < 1 is false
        sltu  r2, r4, r5
        bne   r2, r0, fail

; ---------------------------------------------------------------- immediates
        addi  r1, r1, 1         ; 15 ADDI with a negative immediate
        li    r4, 10
        addi  r2, r4, -3
        li    r3, 7
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 16 ANDI zero-extends
        li    r4, 0xFFFFFFFF
        andi  r2, r4, 0x8001
        li    r3, 0x00008001
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 17 ORI zero-extends
        ori   r2, r0, 0xFFFF
        li    r3, 0x0000FFFF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 18 XORI zero-extends
        li    r4, 0xFFFFFFFF
        xori  r2, r4, 0xFFFF
        li    r3, 0xFFFF0000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 19 SLLI
        li    r4, 3
        slli  r2, r4, 4
        li    r3, 48
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 20 SRLI
        li    r4, 0xF0000000
        srli  r2, r4, 28
        li    r3, 0xF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 21 SRAI
        srai  r2, r4, 28
        li    r3, 0xFFFFFFFF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 22 SLTI signed: -5 < -4
        li    r4, -5
        slti  r2, r4, -4
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 23 SLTIU sign-extends, then compares unsigned
        li    r4, 5
        sltiu r2, r4, -1        ; 5 < 0xFFFFFFFF
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 24 LUI
        lui   r2, 0x1234
        li    r3, 0x12340000
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 25 LI two-word form equals LUI + ORI
        li    r2, 0x12345678
        lui   r3, 0x1234
        ori   r3, r3, 0x5678
        bne   r2, r3, fail

; ---------------------------------------------------------------- multiply / divide
        addi  r1, r1, 1         ; 26 MUL
        li    r4, 7
        li    r5, -3
        mul   r2, r4, r5
        li    r3, -21
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 27 MULH signed: -2 * 3 = -6, high word all ones
        li    r4, -2
        li    r5, 3
        mulh  r2, r4, r5
        li    r3, 0xFFFFFFFF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 28 MULHU: 0xFFFFFFFF^2 = 0xFFFFFFFE_00000001
        li    r4, 0xFFFFFFFF
        mulhu r2, r4, r4
        li    r3, 0xFFFFFFFE
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 29 DIV rounds toward zero: -7 / 2 = -3
        li    r4, -7
        li    r5, 2
        div   r2, r4, r5
        li    r3, -3
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 30 REM takes the dividend's sign: -7 % 2 = -1
        rem   r2, r4, r5
        li    r3, -1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 31 DIVU
        li    r4, 0xFFFFFFFE
        divu  r2, r4, r5
        li    r3, 0x7FFFFFFF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 32 REMU
        li    r4, 7
        li    r5, 3
        remu  r2, r4, r5
        li    r3, 1
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 33 DIV by zero -> all ones
        li    r4, 5
        div   r2, r4, r0
        li    r3, 0xFFFFFFFF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 34 REM by zero -> dividend
        rem   r2, r4, r0
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 35 DIV overflow: -2^31 / -1 = -2^31
        li    r4, 0x80000000
        li    r5, -1
        div   r2, r4, r5
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 36 REM overflow -> 0
        rem   r2, r4, r5
        bne   r2, r0, fail
        addi  r1, r1, 1         ; 37 DIVU by zero -> all ones
        divu  r2, r4, r0
        li    r3, 0xFFFFFFFF
        bne   r2, r3, fail

; ---------------------------------------------------------------- r0
        addi  r1, r1, 1         ; 38 writes to r0 are discarded
        addi  r0, r0, 5
        add   r2, r0, r0
        bne   r2, r0, fail
        li    r3, 0
        bne   r0, r3, fail

; ---------------------------------------------------------------- loads / stores
        li    r6, 0x00100000    ; RAM base
        addi  r1, r1, 1         ; 39 SW / LW
        li    r4, 0x11223344
        sw    r4, 0(r6)
        lw    r2, 0(r6)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 40 little-endian: lowest byte at lowest address
        lbu   r2, 0(r6)
        li    r3, 0x44
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 41
        lbu   r2, 3(r6)
        li    r3, 0x11
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 42 LHU
        lhu   r2, 2(r6)
        li    r3, 0x1122
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 43 SB / LB sign-extends
        sw    r0, 4(r6)
        li    r4, 0x80
        sb    r4, 4(r6)
        lb    r2, 4(r6)
        li    r3, 0xFFFFFF80
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 44 LBU zero-extends
        lbu   r2, 4(r6)
        li    r3, 0x80
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 45 SH / LH sign-extends
        li    r4, 0x8001
        sh    r4, 6(r6)
        lh    r2, 6(r6)
        li    r3, 0xFFFF8001
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 46 LHU
        lhu   r2, 6(r6)
        li    r3, 0x8001
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 47 byte/halfword stores touch only their lanes
        lw    r2, 4(r6)
        li    r3, 0x80010080
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 48 negative offset
        addi  r7, r6, 8
        lw    r2, -4(r7)
        bne   r2, r3, fail

; ---------------------------------------------------------------- branches
        li    r4, -1
        li    r5, 1
        addi  r1, r1, 1         ; 49 BEQ taken / not taken
        beq   r5, r5, ok49
        jmp   fail
ok49:   beq   r4, r5, fail
        addi  r1, r1, 1         ; 50 BNE
        bne   r4, r5, ok50
        jmp   fail
ok50:   bne   r5, r5, fail
        addi  r1, r1, 1         ; 51 BLT signed
        blt   r4, r5, ok51
        jmp   fail
ok51:   blt   r5, r4, fail
        blt   r5, r5, fail
        addi  r1, r1, 1         ; 52 BGE signed (including equal)
        bge   r5, r4, ok52
        jmp   fail
ok52:   bge   r5, r5, ok52b
        jmp   fail
ok52b:  bge   r4, r5, fail
        addi  r1, r1, 1         ; 53 BLTU unsigned: 1 < 0xFFFFFFFF
        bltu  r5, r4, ok53
        jmp   fail
ok53:   bltu  r4, r5, fail
        addi  r1, r1, 1         ; 54 BGEU unsigned
        bgeu  r4, r5, ok54
        jmp   fail
ok54:   bgeu  r5, r4, fail
        addi  r1, r1, 1         ; 55 pseudo BGT / BLE / BEQZ / BNEZ
        bgt   r5, r4, ok55a
        jmp   fail
ok55a:  ble   r4, r5, ok55b
        jmp   fail
ok55b:  beqz  r0, ok55c
        jmp   fail
ok55c:  bnez  r5, ok55d
        jmp   fail
ok55d:  bnez  r0, fail
        addi  r1, r1, 1         ; 56 backward branch: sum 1..10 = 55
        li    r2, 0
        li    r4, 10
sum:    add   r2, r2, r4
        addi  r4, r4, -1
        bnez  r4, sum
        li    r3, 55
        bne   r2, r3, fail

; ---------------------------------------------------------------- calls / stack
        addi  r1, r1, 1         ; 57 CALL / RET restore SP
        mov   r8, sp
        li    r2, 0
        call  sub_set
        li    r3, 0x55
        bne   r2, r3, fail
        bne   sp, r8, fail
        addi  r1, r1, 1         ; 58 CALL pushes the address of the next instruction
        call  sub_ra
after58:
        la    r3, after58
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 59 full-descending: the push lands at SP - 4
        addi  r3, r8, -4
        bne   r9, r3, fail
        addi  r1, r1, 1         ; 60 CALLR
        li    r2, 0
        la    r7, sub_set
        callr r7
        li    r3, 0x55
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 61 JR
        la    r7, ok61
        jr    r7
        jmp   fail
ok61:   addi  r1, r1, 1         ; 62 PUSH / POP
        li    r4, 0xCAFEF00D
        push  r4
        addi  r3, r8, -4
        bne   sp, r3, fail
        pop   r2
        bne   r2, r4, fail
        bne   sp, r8, fail
        addi  r1, r1, 1         ; 63 nested calls
        li    r2, 0
        call  sub_outer
        li    r3, 0x56
        bne   r2, r3, fail
        bne   sp, r8, fail

        addi  r1, r1, 1         ; 64 SWAP exchanges a register with memory (D51)
        li    r6, 0x00100000
        li    r4, 0xA5A5A5A5
        sw    r4, 8(r6)
        li    r2, 0x5A5A5A5A
        swap  r2, 8(r6)
        bne   r2, r4, fail
        lw    r3, 8(r6)
        li    r4, 0x5A5A5A5A
        bne   r3, r4, fail
        addi  r1, r1, 1         ; 65 SWAP into r0: the word is written, r0 stays 0
        swap  r0, 8(r6)
        add   r2, r0, r0
        li    r3, 0
        bne   r2, r3, fail
        lw    r3, 8(r6)
        bne   r3, r0, fail

; ---------------------------------------------------------------- done
        li    r1, 0
        halt
fail:   halt                    ; r1 = failing case number

sub_set:
        li    r2, 0x55
        ret
sub_ra: lw    r2, 0(sp)         ; return address as pushed by CALL
        mov   r9, sp            ; SP inside the call
        ret
sub_outer:
        call  sub_set
        addi  r2, r2, 1
        ret
