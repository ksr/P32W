; expected: EXCEPTION load from unmapped address
        li    r2, 0x02000000
        lw    r3, 0(r2)
        halt
