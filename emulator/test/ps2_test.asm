; ps2_test.asm -- the video card's PS/2 ports (D67): the keyboard's bytes
; as they arrive (each waited for: on the card a byte takes a frame on the
; wire), its vectored interrupt, the mouse's reset (FF: FA AA 00), a command
; with its data byte to the keyboard (ED 02: FA, FA). Self-checking: HALT
; with r1 = 0, or the number of the failing case. Run with -K KEYS (KEYS
; holding "aB") on the emulators, and on the gate-level card
; (generators/sim_sys.py --video --opt=-K --opt=KEYS); not cycle for cycle,
; the bytes' timing being the devices'.

KDATA    = 0xFFFFDF08
KSTAT    = 0xFFFFDF09           ; bit 0 FULL, 1 BUSY
MDATA    = 0xFFFFDF0C
MSTAT    = 0xFFFFDF0D
VIE      = 0xFFFFDF10
VISR     = 0xFFFFDFF4
TREC     = 0x00100000           ; the handler's record
T_CAUSE  = 0
T_BYTE   = 4
T_IRQS   = 8

        li    sp, 0x00180000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_IRQS(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  the mouse has nothing to say
        lbu   r2, MSTAT(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 2  the keyboard types "a": 1C, F0 1C
        li    r7, 0x1C
        call  key
        li    r7, 0xF0
        call  key
        li    r7, 0x1C
        call  key
        addi  r1, r1, 1         ; 3  the next byte (12, shift) by its interrupt: vector 50, level 4
        li    r4, 1
        sb    r4, VIE(r0)
        li    r4, 0x10
        mtcr  IMASK, r4
        li    r4, 3             ; S | IE
        mtcr  STATUS, r4
        li    r5, 200000
t3:     lw    r2, T_IRQS(r6)
        bnez  r2, t3d
        addi  r5, r5, -1
        bnez  r5, t3
        jmp   fail
t3d:    li    r4, 1             ; interrupts off again
        mtcr  STATUS, r4
        mtcr  IMASK, r0
        sb    r0, VIE(r0)
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80005004
        bne   r2, r3, fail
        lw    r2, T_BYTE(r6)
        li    r3, 0x12
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  the rest of "B": 32, F0 32, F0 12
        li    r7, 0x32
        call  key
        li    r7, 0xF0
        call  key
        li    r7, 0x32
        call  key
        li    r7, 0xF0
        call  key
        li    r7, 0x12
        call  key
        addi  r1, r1, 1         ; 5  the mouse, reset: FA AA 00
        li    r4, 0xFF
        sb    r4, MDATA(r0)
        li    r7, 0xFA
        call  mouse
        li    r7, 0xAA
        call  mouse
        li    r7, 0x00
        call  mouse
        addi  r1, r1, 1         ; 6  the keyboard's LEDs: ED, FA; 02, FA
        li    r4, 0xED
        sb    r4, KDATA(r0)
        li    r7, 0xFA
        call  key
        li    r4, 0x02
        sb    r4, KDATA(r0)
        li    r7, 0xFA
        call  key
        addi  r1, r1, 1         ; 7  nothing left: both ports empty and idle, no interrupt status
        lbu   r2, KSTAT(r0)
        bnez  r2, fail
        lbu   r2, MSTAT(r0)
        bnez  r2, fail
        lw    r2, VISR(r0)
        bnez  r2, fail

        li    r1, 0
fail:   halt

; key / mouse: wait for the port's next byte (FULL), which must be r7
key:    li    r5, 200000
k1:     lbu   r2, KSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, k2
        addi  r5, r5, -1
        bnez  r5, k1
        halt
k2:     lbu   r2, KDATA(r0)
        bne   r2, r7, kbad
        ret
kbad:   halt

mouse:  li    r5, 200000
m1:     lbu   r2, MSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, m2
        addi  r5, r5, -1
        bnez  r5, m1
        halt
m2:     lbu   r2, MDATA(r0)
        bne   r2, r7, kbad
        ret

; the handler: the keyboard's interrupt (CAUSE and the byte recorded: reading
; it takes the request away, and the interrupt is switched off); anything
; else stops the test
handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        bge   r12, r0, h_bad
        sw    r12, T_CAUSE(r13)
        lbu   r12, KDATA(r0)
        sw    r12, T_BYTE(r13)
        sb    r0, VIE(r0)       ; (one byte this way: the rest by polling)
        lw    r12, T_IRQS(r13)
        addi  r12, r12, 1
        sw    r12, T_IRQS(r13)
        pop   r13
        pop   r12
        rte
h_bad:  halt
