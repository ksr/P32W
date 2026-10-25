; setjmp.s -- P32W C library (M2, D22). jmp_buf: r8-r14, sp, return address.
        .text
        .global setjmp, longjmp
setjmp:                                 ; setjmp(env): r1 = env; 0(sp) = return address
        sw    r8, 0(r1)
        sw    r9, 4(r1)
        sw    r10, 8(r1)
        sw    r11, 12(r1)
        sw    r12, 16(r1)
        sw    r13, 20(r1)
        sw    r14, 24(r1)
        addi  r2, sp, 4                 ; sp after setjmp returns
        sw    r2, 28(r1)
        lw    r2, 0(sp)
        sw    r2, 32(r1)
        li    r1, 0
        ret
longjmp:                                ; longjmp(env, val): r1 = env, r2 = val
        lw    r8, 0(r1)
        lw    r9, 4(r1)
        lw    r10, 8(r1)
        lw    r11, 12(r1)
        lw    r12, 16(r1)
        lw    r13, 20(r1)
        lw    r14, 24(r1)
        lw    sp, 28(r1)
        lw    r3, 32(r1)
        mov   r1, r2
        bnez  r1, .Lgo
        li    r1, 1
.Lgo:   jr    r3
