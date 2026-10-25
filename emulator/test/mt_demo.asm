; mt_demo.asm -- four user tasks under the preemptive kernel (mtkernel.inc).
; Tasks A, B and C each compute the sum of i and of i*i for i = 1..60 in
; registers, printing their letter every 10 iterations, and check both sums
; at the end (printing X if wrong): a register save/restore bug in the
; scheduler would corrupt them. None of them ever yields; the timer alone
; switches between them, so their letters interleave. Task D breaks the
; rules (writes the console directly) and is killed; the others go on.
        .include "mtkernel.inc"

NTASKS = 4
M      = 60

task_entries:
        .word task_a, task_b, task_c, task_d

task_a: li    r10, 'A'
        jmp   worker
task_b: li    r10, 'B'
        jmp   worker
task_c: li    r10, 'C'
        jmp   worker
task_d: li    r4, '!'
d_bad:  sb    r4, CON(r0)       ; I/O page from user mode: killed
        li    r1, SYS_EXIT      ; not reached
        syscall

worker: li    r5, 0             ; sum of i
        li    r9, 0             ; sum of i*i
        li    r6, 1             ; i
        li    r11, M + 1
        li    r12, 10
        li    r13, 0            ; iterations since the last letter
w_loop: add   r5, r5, r6
        mul   r8, r6, r6
        add   r9, r9, r8
        addi  r13, r13, 1
        bne   r13, r12, w_next
        li    r13, 0
        li    r1, SYS_PUTC
        mov   r2, r10
        syscall
w_next: addi  r6, r6, 1
        bne   r6, r11, w_loop
        li    r3, M * (M + 1) / 2
        bne   r5, r3, w_bad
        li    r3, M * (M + 1) * (2 * M + 1) / 6
        bne   r9, r3, w_bad
        li    r1, SYS_EXIT
        syscall
w_bad:  li    r1, SYS_PUTC
        li    r2, 'X'
        syscall
        li    r1, SYS_EXIT
        syscall
