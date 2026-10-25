; expected: EXCEPTION misaligned load
        li    r2, 0x00100002
        lw    r3, 0(r2)
        halt
