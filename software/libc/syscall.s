; syscall.s -- __syscall(n, a, b, c, d): the one way into the kernel (D22).
; The C calling convention puts n, a, b, c in r1-r4 and d on the stack;
; the kernel wants the number in r1 and the arguments in r2-r5 -- so only d
; moves. A result of -4095..-1 is an error: errno = -result, return -1.
        .text
        .global __syscall
__syscall:
        lw    r5, 4(sp)                 ; d (0(sp) is the return address)
        syscall
        li    r6, -4095
        bltu  r1, r6, .Lok
        sub   r6, r0, r1
        la    r7, errno
        sw    r6, 0(r7)
        li    r1, -1
.Lok:   ret
