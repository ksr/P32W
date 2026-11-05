; str.s -- strlen, strcmp, strncmp and strchr in hand-written assembly, as
; memcpy and memset are in mem.s: every program spends its time in these, and
; the compiler's C versions took about three times the instructions (D91).
; The p32cc ABI: arguments in r1, r2, r3; the result in r1; r1-r7 are scratch.
        .text
        .global strlen, strcmp, strncmp, strchr

; size_t strlen(const char *s)
strlen: mov   r2, r1
.Lln:   lbu   r3, 0(r2)
        addi  r2, r2, 1
        bnez  r3, .Lln
        sub   r1, r2, r1
        addi  r1, r1, -1
        ret

; int strcmp(const char *a, const char *b)
strcmp: lbu   r3, 0(r1)
        lbu   r4, 0(r2)
        addi  r1, r1, 1
        addi  r2, r2, 1
        bne   r3, r4, .Lcd
        bnez  r3, strcmp
        li    r1, 0
        ret
.Lcd:   sub   r1, r3, r4
        ret

; int strncmp(const char *a, const char *b, size_t n)
strncmp:
        beqz  r3, .Lnz
.Lnl:   lbu   r4, 0(r1)
        lbu   r5, 0(r2)
        bne   r4, r5, .Lnd
        beqz  r4, .Lnz
        addi  r1, r1, 1
        addi  r2, r2, 1
        addi  r3, r3, -1
        bnez  r3, .Lnl
.Lnz:   li    r1, 0
        ret
.Lnd:   sub   r1, r4, r5
        ret

; char *strchr(const char *s, int c)
strchr: andi  r2, r2, 255
.Lfl:   lbu   r3, 0(r1)
        beq   r3, r2, .Lfx
        addi  r1, r1, 1
        bnez  r3, .Lfl
        li    r1, 0
.Lfx:   ret
