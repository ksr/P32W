; exc_ioword.asm -- a word load from a UART (byte) register: the I/O card's
; byte bus answers byte accesses only, so nothing ACKs -> BUSERROR (D50).
        lw    r2, 0xFFFFFF00(r0)
        halt
