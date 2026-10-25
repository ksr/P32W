; sigtramp.s -- P32W C library (D22): signal handlers run through here. The
; kernel saved the interrupted state on the user stack and enters with
; r1 = the signal, r2 = the handler; the handler returns, sigreturn restores.
        .text
        .global __sigtramp
__sigtramp:
        callr r2
        li    r1, 66                    ; SYS_sigreturn
        syscall
