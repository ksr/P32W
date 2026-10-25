; echo.asm -- console input test: copy stdin to stdout in upper case until
; the receive-ready bit stays clear (end of input), then HALT with r1 = 0.
CON     = 0xFFFFFF00            ; data
CONST   = 0xFFFFFF05            ; UART 0 line status: bit 0 = receive data ready (D50)

loop:   lbu   r2, CONST(r0)
        andi  r2, r2, 1
        beqz  r2, done
        lbu   r3, CON(r0)
        li    r4, 'a'
        bltu  r3, r4, out       ; below 'a': unchanged
        li    r4, 'z'
        bgtu  r3, r4, out       ; above 'z': unchanged
        addi  r3, r3, -32       ; to upper case
out:    sb    r3, CON(r0)
        jmp   loop
done:   li    r1, 0
        halt
