; boot_payload.asm -- a tiny program booted by the monitor's "b" command (the
; monitor test): position-independent, so it runs wherever the header loads
; it. Prints "BOOTED", returns 0x55 to the monitor.
        li    r2, 'B'
        sb    r2, 0xFFFFFF00(r0)
        li    r2, 'O'
        sb    r2, 0xFFFFFF00(r0)
        sb    r2, 0xFFFFFF00(r0)
        li    r2, 'T'
        sb    r2, 0xFFFFFF00(r0)
        li    r2, 'E'
        sb    r2, 0xFFFFFF00(r0)
        li    r2, 'D'
        sb    r2, 0xFFFFFF00(r0)
        li    r2, 13
        sb    r2, 0xFFFFFF00(r0)
        li    r2, 10
        sb    r2, 0xFFFFFF00(r0)
        li    r1, 0x55
        ret
