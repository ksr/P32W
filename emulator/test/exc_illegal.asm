; expected: EXCEPTION illegal instruction (opcode 0x00 -- zeroed memory)
        nop
        .word 0
