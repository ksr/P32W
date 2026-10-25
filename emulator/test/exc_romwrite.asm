; expected: EXCEPTION write to ROM
        sw    r0, 0x100(r0)
        halt
