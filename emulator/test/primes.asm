; primes.asm -- print every prime below N with the Sieve of Eratosthenes, one
; per line in decimal, then a summary line "168 primes below 1000".
; HALT with r1 = 0.
CON     = 0xFFFFFF00            ; console data register
N       = 1000                  ; find primes in 2 .. N-1
SIEVE   = 0x00100000            ; RAM: one byte per number, 0 = prime, 1 = composite

        li    sp, 0x00500000    ; top of RAM; full-descending stack (D6)
        li    r6, SIEVE
        li    r7, N
        li    r8, 1

; RAM is zero at reset in the emulator but undefined in hardware: clear it.
        li    r2, 0
clear:  add   r3, r6, r2
        sb    r0, 0(r3)
        addi  r2, r2, 1
        bne   r2, r7, clear

; Sieve: for each p with p*p < N that is still marked prime, mark
; p*p, p*p+p, p*p+2p, ... as composite (smaller multiples are already marked).
        li    r2, 2             ; p
outer:  mul   r3, r2, r2        ; r3 = p*p
        bgeu  r3, r7, report    ; p*p >= N: every composite is marked
        add   r4, r6, r2
        lbu   r5, 0(r4)
        bnez  r5, next          ; p is composite: its multiples are marked already
mark:   add   r4, r6, r3
        sb    r8, 0(r4)
        add   r3, r3, r2
        bltu  r3, r7, mark
next:   addi  r2, r2, 1
        jmp   outer

; Print the numbers still marked prime, and count them in r9.
report: li    r2, 2
        li    r9, 0
ploop:  add   r4, r6, r2
        lbu   r5, 0(r4)
        bnez  r5, pnext
        mov   r10, r2
        call  putdec
        call  putnl
        addi  r9, r9, 1
pnext:  addi  r2, r2, 1
        bne   r2, r7, ploop

; Summary: "<count> primes below <N>"
        mov   r10, r9
        call  putdec
        la    r10, below
        call  puts
        li    r10, N
        call  putdec
        call  putnl
        li    r1, 0
        halt

below:  .asciz " primes below "
        .align 4

; putdec: print r10 as an unsigned decimal number. Digits are produced least
; significant first (REMU/DIVU by 10) into a 12-byte stack buffer, then
; printed forwards. Clobbers r10-r14.
putdec: addi  sp, sp, -12
        addi  r14, sp, 12       ; one past the end of the buffer
        mov   r12, r14          ; next free byte, filling backwards
        li    r13, 10
pd_div: remu  r11, r10, r13
        divu  r10, r10, r13
        addi  r11, r11, '0'
        addi  r12, r12, -1
        sb    r11, 0(r12)
        bnez  r10, pd_div       ; at least one digit, so 0 prints as "0"
pd_out: lbu   r11, 0(r12)
        sb    r11, CON(r0)
        addi  r12, r12, 1
        bne   r12, r14, pd_out
        addi  sp, sp, 12
        ret

; puts: print the 0-terminated string at r10. Clobbers r10, r11.
puts:   lbu   r11, 0(r10)
        beqz  r11, ps_done
        sb    r11, CON(r0)
        addi  r10, r10, 1
        jmp   puts
ps_done:
        ret

; putnl: print a newline. Clobbers r11.
putnl:  li    r11, '\n'
        sb    r11, CON(r0)
        ret
