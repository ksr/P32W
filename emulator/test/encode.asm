; encode.asm -- exact encodings checked word for word by run_tests.sh.
        add   r1, r2, r3        ; 10123000 (isa-v0.md example)
        addi  r1, r0, 42        ; 2010002A (isa-v0.md example)
        lb    r1, -256(r0)      ; 4010FF00
        sb    r11, 0xFFFFFF00(r0) ; 48B0FF00 (top-32K address as offset)
here:   beq   r0, r0, here      ; 5000FFFF (offset -1: target = PC+4-4)
        jmp   next              ; 60000000 (offset 0)
next:   call  next              ; 61FFFFFF
        jr    r5                ; 62050000
        ret                     ; 64000000
        halt                    ; 01000000
        li    r1, 5             ; 20100005           (ADDI, 1 word)
        li    r1, -1            ; 2010FFFF           (ADDI, 1 word)
        li    r1, 0xFFFF        ; 2310FFFF           (ORI, 1 word)
        li    r1, 0x12340000    ; 2F101234           (LUI, 1 word)
        li    r1, 0x12345678    ; 2E010000 12345678  (LI32, D59)
        li    r2, later         ; 2E020000 00000005  (forward ref: always LI32)
        not   r3, r4            ; 1A340000
        sll   r1, r2, r3        ; 15123000
        srai  r1, r2, 31        ; 2712001F
        lui   r7, 0xABCD        ; 2F70ABCD
        nor   r1, r2, r3        ; 1A123000
        mulhu r1, r2, r3        ; 32123000
        sw    r9, -4(sp)        ; 4A9FFFFC
        syscall                 ; 70000000
        rte                     ; 71000000
        mfcr  r3, EPC           ; 72310000
        mtcr  VBR, r5           ; 73540000
        break                   ; 74000000
        mfcr  r1, 5             ; 72150000 (control register by number = USP)
        wait                    ; 75000000
        mtcr  IMASK, r2         ; 73270000
        tlbw                    ; 76000000
        mtcr  TLBHI, r4         ; 73490000
        mfcr  r1, ASID          ; 72180000
        lwx   r1, r2, r3        ; 82123000 (D59: r2 + r3)
        swx   r4, r5, r6, 8     ; 8A456003 (r5 + r6 * 8)
        lwp   r1, -4(r2), 4     ; 9212FC04 (at r2 - 4, then r2 += 4)
        sbp   r3, (sp), -1      ; 983F00FF
        li32  r9, 0xDEADBEEF    ; 2E090000 DEADBEEF
later = 5
