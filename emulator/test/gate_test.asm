; gate_test.asm -- the measuring gate (p32uemu -G, D85): bit 7 of the output
; port, and nothing else, starts and stops the count. Four counts: a loop; a
; longer loop with the port's other bits changing inside it; an idle wait for
; the timer (WAIT, 1,000 ticks, so 3,000 cycles at 3 MHz); and one still up at
; HALT. The other bits alone, before the first count, must start nothing.

PORT     = 0xFFFFFF21           ; the output port (a byte)
TCOUNT   = 0xFFFFFF10
TCTRL    = 0xFFFFFF18           ; bit 0 enable, bit 1 pending (write 1 = ack)
GATE     = 0x80

        li    r1, 0
        li    r4, 0x7F          ; every other bit: no count
        sb    r4, PORT(r0)
        sb    r0, PORT(r0)

        li    r4, GATE          ; 1: 1,000 x 2 + li + the store that lowers it = 2,002
        sb    r4, PORT(r0)
        li    r3, 1000
c1:     addi  r3, r3, -1
        bnez  r3, c1
        sb    r0, PORT(r0)

        li    r4, GATE          ; 2: 3,000 x 2 + li, li, sb + the lowering store = 6,004
        sb    r4, PORT(r0)
        li    r3, 3000
        li    r5, GATE | 1      ; (bit 7 stays up: the same count)
        sb    r5, PORT(r0)
c2:     addi  r3, r3, -1
        bnez  r3, c2
        sb    r0, PORT(r0)

        li    r4, 1000          ; 3: idle, until the timer's 1,000 ticks are up
        sw    r4, TCOUNT(r0)
        li    r4, 1
        sw    r4, TCTRL(r0)
        mtcr  IMASK, r4
        li    r4, GATE
        sb    r4, PORT(r0)
        wait
        sb    r0, PORT(r0)
        sw    r0, TCTRL(r0)
        mtcr  IMASK, r0

        li    r4, GATE          ; 4: left up: reported at the end
        sb    r4, PORT(r0)
        li    r3, 10
c4:     addi  r3, r3, -1
        bnez  r3, c4
        halt
