; start.s -- P32W Unix kernel entry, traps, context switch (M3, D22).
;
; The monitor's `b` loads the kernel at 0x0010_0000 and calls _start, in
; supervisor mode with interrupts off. The kernel runs untranslated
; (supervisor addresses are physical, isa-system.md C2).
;
; Every trap and interrupt enters at trapvec on the supervisor stack -- the
; current process's kernel stack, because trapret leaves sp at its top when
; it returns to user mode. A TLB miss on a mapped user page is refilled
; there and then (two registers saved, one load from the current page
; table); anything else saves a struct trapframe (proc.h) and calls trap().

        .text
        .global _start
_start: la    sp, bootstack_top
        la    r1, _bss                  ; clear .bss
        la    r2, _end
.Lz:    bgeu  r1, r2, .Lzd
        sw    r0, 0(r1)
        addi  r1, r1, 4
        beq   r0, r0, .Lz
.Lzd:   li    r1, 1                     ; S = 1, interrupts off
        mtcr  STATUS, r1
        mtcr  IMASK, r0
        la    r1, trapvec
        mtcr  VBR, r1
        call  kmain
        li    r1, 1
        halt

; ---------------------------------------------------------------- traps
        .global trapvec
trapvec:
        addi  sp, sp, -8
        sw    r12, 0(sp)
        sw    r13, 4(sp)
        mfcr  r12, CAUSE
        addi  r12, r12, -8
        sltiu r12, r12, 3               ; a TLB miss (causes 8-10)?
        beqz  r12, .Lslow
        mfcr  r12, BADADDR
        srli  r12, r12, 12              ; VPN
        sltiu r13, r12, 2048            ; inside the 8 MiB user window? (NPTE, param.h)
        beqz  r13, .Lslow
        slli  r12, r12, 2
        la    r13, curpt
        lw    r13, 0(r13)
        add   r12, r12, r13
        lw    r12, 0(r12)               ; the PTE, in TLBLO format
        andi  r13, r12, 1
        beqz  r13, .Lslow               ; not mapped: trap() decides
        mtcr  TLBLO, r12
        tlbw                            ; TLBHI was set by the miss
        la    r13, ntlbrefill           ; counted (the scheduler charges each process its share)
        lw    r12, 0(r13)
        addi  r12, r12, 1
        sw    r12, 0(r13)
        lw    r12, 0(sp)
        lw    r13, 4(sp)
        addi  sp, sp, 8
        rte
.Lslow: lw    r12, 0(sp)
        lw    r13, 4(sp)
        addi  sp, sp, 8
        addi  sp, sp, -80               ; struct trapframe
        sw    r1, 4(sp)
        sw    r2, 8(sp)
        sw    r3, 12(sp)
        sw    r4, 16(sp)
        sw    r5, 20(sp)
        sw    r6, 24(sp)
        sw    r7, 28(sp)
        sw    r8, 32(sp)
        sw    r9, 36(sp)
        sw    r10, 40(sp)
        sw    r11, 44(sp)
        sw    r12, 48(sp)
        sw    r13, 52(sp)
        sw    r14, 56(sp)
        mfcr  r1, USP
        sw    r1, 60(sp)
        mfcr  r1, EPC
        sw    r1, 64(sp)
        mfcr  r1, STATUS
        sw    r1, 68(sp)
        mfcr  r1, CAUSE
        sw    r1, 72(sp)
        mfcr  r1, BADADDR
        sw    r1, 76(sp)
        mov   r1, sp
        call  trap

; trapret: sp points at a trapframe; restore it and return from the trap.
        .global trapret
trapret:
        li    r1, 1                     ; interrupts off while restoring
        mtcr  STATUS, r1
        lw    r1, 64(sp)
        mtcr  EPC, r1
        lw    r1, 60(sp)
        mtcr  USP, r1
        lw    r1, 68(sp)                ; S = 1, IE = 0, PS / PIE: where RTE goes
        mtcr  STATUS, r1
        lw    r1, 4(sp)
        lw    r2, 8(sp)
        lw    r3, 12(sp)
        lw    r4, 16(sp)
        lw    r5, 20(sp)
        lw    r6, 24(sp)
        lw    r7, 28(sp)
        lw    r8, 32(sp)
        lw    r9, 36(sp)
        lw    r10, 40(sp)
        lw    r11, 44(sp)
        lw    r12, 48(sp)
        lw    r13, 52(sp)
        lw    r14, 56(sp)
        addi  sp, sp, 80
        rte

; ---------------------------------------------------------------- swtch
; swtch(struct context *old, struct context *new): save the callee-saved
; registers, sp and the return address in old; resume new.
        .global swtch
swtch:  sw    r8, 0(r1)
        sw    r9, 4(r1)
        sw    r10, 8(r1)
        sw    r11, 12(r1)
        sw    r12, 16(r1)
        sw    r13, 20(r1)
        sw    r14, 24(r1)
        lw    r3, 0(sp)                 ; where swtch was called from
        addi  r4, sp, 4
        sw    r4, 28(r1)
        sw    r3, 32(r1)
        lw    r8, 0(r2)
        lw    r9, 4(r2)
        lw    r10, 8(r2)
        lw    r11, 12(r2)
        lw    r12, 16(r2)
        lw    r13, 20(r2)
        lw    r14, 24(r2)
        lw    sp, 28(r2)
        lw    r3, 32(r2)
        jr    r3

; ---------------------------------------------------------------- the privileged instructions
        .global mfcr_status, mtcr_status, mfcr_ipend, mtcr_imask, mfcr_imask, mtcr_asid
        .global tlbwrite, tlbflush, waitirq, halt, splhi, splx, spl0, mfcr_tlbhi
mfcr_status:
        mfcr  r1, STATUS
        ret
mtcr_status:
        mtcr  STATUS, r1
        ret
mfcr_ipend:
        mfcr  r1, IPEND
        ret
mtcr_imask:
        mtcr  IMASK, r1
        ret
mfcr_imask:
        mfcr  r1, IMASK
        ret
mtcr_asid:
        mtcr  ASID, r1
        ret
mfcr_tlbhi:
        mfcr  r1, TLBHI
        ret
tlbwrite:                               ; tlbwrite(hi, lo)
        mtcr  TLBHI, r1
        mtcr  TLBLO, r2
        tlbw
        ret
tlbflush:                               ; every entry of both halves invalid
        mtcr  TLBLO, r0
        li    r1, 0
        li    r3, 64 << 12
.Lf:    mtcr  TLBHI, r1                 ; D-TLB entry
        tlbw
        ori   r2, r1, 0x100             ; I-TLB entry (I = 1)
        mtcr  TLBHI, r2
        tlbw
        li    r2, 0x1000
        add   r1, r1, r2
        bne   r1, r3, .Lf
        ret
waitirq:
        wait
        ret
halt:   halt
        .global reset
reset:  li    r1, 1                     ; supervisor, interrupts off, as after reset
        mtcr  STATUS, r1
        mtcr  IMASK, r0
        mtcr  VBR, r0
        jr    r0                        ; PC = 0: the monitor
splhi:  mfcr  r1, STATUS                ; returns the old STATUS
        li    r2, -3
        and   r2, r1, r2
        mtcr  STATUS, r2
        ret
splx:   mfcr  r2, STATUS                ; splx(old): restore IE
        andi  r1, r1, 2
        li    r3, -3
        and   r2, r2, r3
        or    r2, r2, r1
        mtcr  STATUS, r2
        ret
spl0:   mfcr  r1, STATUS
        ori   r1, r1, 2
        mtcr  STATUS, r1
        ret

        .bss
        .align 4
        .space 4096
bootstack_top:
