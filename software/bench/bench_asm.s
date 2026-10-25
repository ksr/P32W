; bench_asm.s -- the benchmark routines in hand-written P32W assembly (the
; ABI of p32cc: arguments in r1-r4, the result in r1, r1-r7 free to use).
; Each is what a person would write: pointers walked in registers, loops
; unrolled where it is natural, no frame for a leaf.
        .text
        .global b_strlen, b_wcopy, b_sum, b_crc, b_isort, b_count, b_hcopy

; int b_strlen(const char *s)
b_strlen:
        mov   r2, r1
.Ls:    lbu   r3, 0(r2)
        beqz  r3, .Lsd
        lbu   r3, 1(r2)
        beqz  r3, .Ls1
        lbu   r3, 2(r2)
        beqz  r3, .Ls2
        lbu   r3, 3(r2)
        beqz  r3, .Ls3
        addi  r2, r2, 4
        jmp   .Ls
.Ls3:   addi  r2, r2, 1
.Ls2:   addi  r2, r2, 1
.Ls1:   addi  r2, r2, 1
.Lsd:   sub   r1, r2, r1
        ret

; void b_wcopy(unsigned *d, const unsigned *s, int n) -- n a multiple of 4
b_wcopy:
        beqz  r3, .Lwd
.Lw:    lw    r4, 0(r2)
        lw    r5, 4(r2)
        lw    r6, 8(r2)
        lw    r7, 12(r2)
        sw    r4, 0(r1)
        sw    r5, 4(r1)
        sw    r6, 8(r1)
        sw    r7, 12(r1)
        addi  r1, r1, 16
        addi  r2, r2, 16
        addi  r3, r3, -4
        bnez  r3, .Lw
.Lwd:   ret

; unsigned b_sum(const unsigned char *p, int n) -- n a multiple of 4
b_sum:
        li    r4, 0
        add   r2, r1, r2              ; end
.Lu:    lbu   r3, 0(r1)
        lbu   r5, 1(r1)
        lbu   r6, 2(r1)
        lbu   r7, 3(r1)
        add   r4, r4, r3
        add   r4, r4, r5
        add   r4, r4, r6
        add   r4, r4, r7
        addi  r1, r1, 4
        bne   r1, r2, .Lu
        mov   r1, r4
        ret

; unsigned b_crc(const unsigned char *p, int n, const unsigned *t)
b_crc:
        li    r4, -1
        add   r2, r1, r2              ; end
        beq   r1, r2, .Lcd
.Lc:    lbu   r5, 0(r1)
        xor   r5, r5, r4
        andi  r5, r5, 255
        slli  r5, r5, 2
        add   r5, r5, r3
        lw    r5, 0(r5)
        srli  r4, r4, 8
        xor   r4, r4, r5
        addi  r1, r1, 1
        bne   r1, r2, .Lc
.Lcd:   not   r1, r4
        ret

; void b_isort(int *a, int n)
b_isort:
        slli  r2, r2, 2
        add   r2, r1, r2              ; end
        addi  r3, r1, 4               ; i
.Li:    bgeu  r3, r2, .Lid
        lw    r4, 0(r3)               ; v
        mov   r5, r3                  ; the hole
.Lj:    beq   r5, r1, .Ljd
        lw    r6, -4(r5)
        bge   r4, r6, .Ljd            ; a[j] <= v: stop
        sw    r6, 0(r5)
        addi  r5, r5, -4
        jmp   .Lj
.Ljd:   sw    r4, 0(r5)
        addi  r3, r3, 4
        jmp   .Li
.Lid:   ret

; int b_count(const unsigned char *p, int n, int c) -- n a multiple of 4
b_count:
        li    r4, 0
        add   r2, r1, r2
.Lk:    lbu   r5, 0(r1)
        lbu   r6, 1(r1)
        xor   r5, r5, r3
        sltiu r5, r5, 1
        add   r4, r4, r5
        xor   r6, r6, r3
        sltiu r6, r6, 1
        add   r4, r4, r6
        lbu   r5, 2(r1)
        lbu   r6, 3(r1)
        xor   r5, r5, r3
        sltiu r5, r5, 1
        add   r4, r4, r5
        xor   r6, r6, r3
        sltiu r6, r6, 1
        add   r4, r4, r6
        addi  r1, r1, 4
        bne   r1, r2, .Lk
        mov   r1, r4
        ret

; void b_hcopy(unsigned short *d, const unsigned short *s, int n) -- n a multiple of 4
b_hcopy:
        beqz  r3, .Lhd
.Lh:    lhu   r4, 0(r2)
        lhu   r5, 2(r2)
        lhu   r6, 4(r2)
        lhu   r7, 6(r2)
        sh    r4, 0(r1)
        sh    r5, 2(r1)
        sh    r6, 4(r1)
        sh    r7, 6(r1)
        addi  r1, r1, 8
        addi  r2, r2, 8
        addi  r3, r3, -4
        bnez  r3, .Lh
.Lhd:   ret
