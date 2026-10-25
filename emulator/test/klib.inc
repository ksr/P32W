; klib.inc -- console helpers shared by the kernels (supervisor mode only:
; they write the console directly). Included by kernel.inc and mtkernel.inc,
; which define CON.

; k_puts: print the 0-terminated string at r10. Clobbers r10, r11.
k_puts: lbu   r11, 0(r10)
        beqz  r11, kp_done
        sb    r11, CON(r0)
        addi  r10, r10, 1
        jmp   k_puts
kp_done:
        ret

; k_puthex: print r10 as 8 hex digits. Clobbers r10-r12, r14.
k_puthex:
        li    r12, 8
kh_loop:
        srli  r11, r10, 28
        addi  r11, r11, '0'
        li    r14, '9' + 1
        bltu  r11, r14, kh_out
        addi  r11, r11, 'A' - '9' - 1
kh_out: sb    r11, CON(r0)
        slli  r10, r10, 4
        addi  r12, r12, -1
        bnez  r12, kh_loop
        ret

; k_putdec: print r10 as an unsigned decimal number. Clobbers r10-r14.
k_putdec:
        addi  sp, sp, -12       ; digit buffer on the supervisor stack
        addi  r14, sp, 12
        mov   r12, r14
        li    r13, 10
kd_div: remu  r11, r10, r13
        divu  r10, r10, r13
        addi  r11, r11, '0'
        addi  r12, r12, -1
        sb    r11, 0(r12)
        bnez  r10, kd_div
kd_out: lbu   r11, 0(r12)
        sb    r11, CON(r0)
        addi  r12, r12, 1
        bne   r12, r14, kd_out
        addi  sp, sp, 12
        ret

; k_tlbinval: invalidate all 128 TLB entries, 64 in the D-TLB and 64 in the
; I-TLB (contents are undefined at reset, isa-system.md C4). Clobbers r10, r11.
k_tlbinval:
        mtcr  TLBLO, r0         ; V = 0
        li    r10, 0
ki_loop:
        mtcr  TLBHI, r10        ; D-TLB entry: index = VPN bits 5:0, I = 0
        tlbw
        ori   r11, r10, 0x100   ; same index in the I-TLB (I = 1)
        mtcr  TLBHI, r11
        tlbw
        li    r11, 0x1000
        add   r10, r10, r11
        li    r11, 64 * 0x1000
        bne   r10, r11, ki_loop
        ret
