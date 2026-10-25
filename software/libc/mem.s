; mem.s -- memcpy and memset in hand-written assembly (docs/bench.md: the
; C versions were 2.6x slower). Shared by libc and the kernel (its Makefile
; assembles this file). The p32cc ABI: d in r1, s / c in r2, n in r3; the
; result (d) in r1; r1-r7 are scratch.
        .text
        .global memcpy, memset

; void *memcpy(void *d, const void *s, size_t n)
memcpy: mov   r7, r1                    ; the result
        xor   r4, r1, r2
        andi  r4, r4, 3
        bnez  r4, .Lcb                  ; not co-aligned: bytes
.Lca:   andi  r4, r1, 3                 ; bytes until d (and s) is aligned
        beqz  r4, .Lcw
        beqz  r3, .Lcx
        lbu   r4, 0(r2)
        sb    r4, 0(r1)
        addi  r1, r1, 1
        addi  r2, r2, 1
        addi  r3, r3, -1
        jmp   .Lca
.Lcw:   sltiu r4, r3, 16                ; 16 bytes at a time
        bnez  r4, .Lc4
        lw    r4, 0(r2)
        lw    r5, 4(r2)
        lw    r6, 8(r2)
        sw    r4, 0(r1)
        lw    r4, 12(r2)
        sw    r5, 4(r1)
        sw    r6, 8(r1)
        sw    r4, 12(r1)
        addi  r1, r1, 16
        addi  r2, r2, 16
        addi  r3, r3, -16
        jmp   .Lcw
.Lc4:   sltiu r4, r3, 4                 ; then words
        bnez  r4, .Lcb
        lw    r4, 0(r2)
        sw    r4, 0(r1)
        addi  r1, r1, 4
        addi  r2, r2, 4
        addi  r3, r3, -4
        jmp   .Lc4
.Lcb:   beqz  r3, .Lcx                  ; then (or only) bytes
        lbu   r4, 0(r2)
        sb    r4, 0(r1)
        addi  r1, r1, 1
        addi  r2, r2, 1
        addi  r3, r3, -1
        jmp   .Lcb
.Lcx:   mov   r1, r7
        ret

; void *memset(void *d, int c, size_t n)
memset: mov   r7, r1
        andi  r2, r2, 255
        slli  r4, r2, 8                 ; c in all four bytes
        or    r2, r2, r4
        slli  r4, r2, 16
        or    r2, r2, r4
.Lsa:   andi  r4, r1, 3                 ; bytes until d is aligned
        beqz  r4, .Lsw
        beqz  r3, .Lsx
        sb    r2, 0(r1)
        addi  r1, r1, 1
        addi  r3, r3, -1
        jmp   .Lsa
.Lsw:   sltiu r4, r3, 16
        bnez  r4, .Ls4
        sw    r2, 0(r1)
        sw    r2, 4(r1)
        sw    r2, 8(r1)
        sw    r2, 12(r1)
        addi  r1, r1, 16
        addi  r3, r3, -16
        jmp   .Lsw
.Ls4:   sltiu r4, r3, 4
        bnez  r4, .Lsb
        sw    r2, 0(r1)
        addi  r1, r1, 4
        addi  r3, r3, -4
        jmp   .Ls4
.Lsb:   beqz  r3, .Lsx
        sb    r2, 0(r1)
        addi  r1, r1, 1
        addi  r3, r3, -1
        jmp   .Lsb
.Lsx:   mov   r1, r7
        ret
