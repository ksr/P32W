; kernel.inc -- minimal stage-A kernel (docs/isa-system.md).
;
; Include it FIRST: it starts at address 0 (reset). It sets up the supervisor
; stack and trap handler, then enters the program's `user_main` in USER mode
; with its own stack. The user program reaches the console only through
; system calls; any other trap from user mode kills it with a report.
;
; System calls (ABI, isa-system.md section 8): number in r1, arguments in
; r2-r5, result in r1. Other registers are preserved.
;   0 exit(status)       stop the machine; exit status = status
;   1 putc(ch)           write one character
;   2 getc() -> ch | -1  read one character, -1 at end of input
;   3 write(buf, len)    write len bytes from buf; returns len
;
; Stage-A limits: no isolation. Since stage C, user addresses go through the
; TLB; this kernel simply identity-maps (virtual = physical) any page below
; 16 MiB on a TLB miss, read/write/execute -- so a user program can still
; overwrite any RAM, and write() does not check buf. vmkernel.inc is the
; protected version.

CON      = 0xFFFFFF00
CONST    = 0xFFFFFF05           ; UART 0 line status, bit 0 = data ready (D50)
SSP_TOP  = 0x00500000           ; supervisor stack: top of RAM (4 MiB, D50)
USP_TOP  = 0x00400000           ; user stack
SYS_EXIT  = 0
SYS_PUTC  = 1
SYS_GETC  = 2
SYS_WRITE = 3

k_boot: li    sp, SSP_TOP
        call  k_tlbinval
        la    r10, k_trap
        mtcr  VBR, r10
        li    r10, USP_TOP
        mtcr  USP, r10
        la    r10, user_main
        mtcr  EPC, r10
        li    r10, 1            ; S = 1, PS = 0: RTE enters user mode at EPC
        mtcr  STATUS, r10
        li    r10, 0
        rte

; ---------------------------------------------------------------- trap handler
k_trap: addi  sp, sp, -20       ; save the registers the kernel uses
        sw    r10, 0(sp)
        sw    r11, 4(sp)
        sw    r12, 8(sp)
        sw    r13, 12(sp)
        sw    r14, 16(sp)
        mfcr  r10, CAUSE
        addi  r11, r10, -8      ; TLB miss (causes 8-10)?
        sltiu r11, r11, 3
        bnez  r11, k_miss
        li    r11, 5            ; SYSCALL
        bne   r10, r11, k_fault
        mfcr  r10, EPC          ; continue after the SYSCALL
        addi  r10, r10, 4
        mtcr  EPC, r10
        beqz  r1, k_exit        ; SYS_EXIT = 0
        li    r11, SYS_PUTC
        beq   r1, r11, k_putc
        li    r11, SYS_GETC
        beq   r1, r11, k_getc
        li    r11, SYS_WRITE
        beq   r1, r11, k_write
        li    r1, -1            ; unknown system call
k_ret:  lw    r10, 0(sp)
        lw    r11, 4(sp)
        lw    r12, 8(sp)
        lw    r13, 12(sp)
        lw    r14, 16(sp)
        addi  sp, sp, 20
        rte

; TLB miss: identity-map the page (below 16 MiB), then re-execute.
k_miss: mfcr  r11, BADADDR
        srli  r12, r11, 24
        bnez  r12, k_fault
        srli  r11, r11, 12
        slli  r11, r11, 12
        ori   r11, r11, 7       ; V | W | X
        mtcr  TLBLO, r11
        tlbw                    ; TLBHI was set by the miss
        jmp   k_ret

k_exit: mov   r1, r2            ; HALT reports r1 as the exit status
        halt

k_putc: sb    r2, CON(r0)
        li    r1, 0
        jmp   k_ret

k_getc: lbu   r10, CONST(r0)
        andi  r10, r10, 1
        beqz  r10, k_eof
        lbu   r1, CON(r0)
        jmp   k_ret
k_eof:  li    r1, -1
        jmp   k_ret

k_write:
        mov   r10, r2
        add   r11, r2, r3
kw_loop:
        beq   r10, r11, kw_done
        lbu   r12, 0(r10)
        sb    r12, CON(r0)
        addi  r10, r10, 1
        jmp   kw_loop
kw_done:
        mov   r1, r3
        jmp   k_ret

; Any other trap: report it and stop with exit status 100 + cause.
; "user fault" if it came from user mode, "kernel panic" if from the kernel.
k_fault:
        mfcr  r13, STATUS
        andi  r13, r13, 4       ; PS: trapped from supervisor mode?
        la    r10, k_msg_user
        beqz  r13, kf_who
        la    r10, k_msg_panic
kf_who: call  k_puts
        mfcr  r10, CAUSE
        call  k_putdec
        la    r10, k_msg_pc
        call  k_puts
        mfcr  r10, EPC
        call  k_puthex
        la    r10, k_msg_addr
        call  k_puts
        mfcr  r10, BADADDR
        call  k_puthex
        li    r10, '\n'
        sb    r10, CON(r0)
        mfcr  r1, CAUSE
        addi  r1, r1, 100
        halt

        .include "klib.inc"

k_msg_user:  .asciz "\nkernel: user fault, cause "
k_msg_panic: .asciz "\nkernel: PANIC, cause "
k_msg_pc:    .asciz ", pc "
k_msg_addr:  .asciz ", addr "
        .align 4
