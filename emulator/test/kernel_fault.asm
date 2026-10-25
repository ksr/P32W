; kernel_fault.asm -- a user-mode program that breaks the rules: after one
; legal write() it stores to the console directly. The kernel must stop it
; with "user fault, cause 2" (PRIVILEGE) and exit status 102.
        .include "kernel.inc"

user_main:
        li    r1, SYS_WRITE
        la    r2, msg
        li    r3, msg_end - msg
        syscall
        li    r4, '!'
bad:    sb    r4, CON(r0)       ; user mode may not touch the I/O page
        li    r1, SYS_EXIT      ; not reached
        li    r2, 0
        syscall

msg:    .ascii "about to touch the console directly"
msg_end:
