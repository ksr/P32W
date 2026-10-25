; crt0_sa.s -- the stand-alone C runtime (M2, D22): runs a C program on the
; bare machine, in supervisor mode, from the boot ROM image at address 0
; (the emulators: p32emu prog.bin). Link it first:
;     p32ld --format bin --text 0 --data 0x00100000 crt0_sa.o prog.o libc.a
; It copies .data from ROM to RAM, clears .bss, sets the stack at the top of
; RAM, and installs a tiny trap handler that serves the system calls a
; test program needs -- through the real SYSCALL instruction, so the C
; library is the one the kernel will run:
;   exit(status)        HALT with the status (the emulator's exit status)
;   write(fd, buf, n)   to UART 0 (fds 1 and 2)
;   read(fd, buf, n)    from UART 0, up to a newline; 0 (end of file) after
;                       a long silence -- the emulator's end of input
;   sbrk(incr)          from _end upwards
; Anything else returns -ENOSYS (38). Any other trap prints its cause and EPC
; and halts with status 255.

U0      = 0xFFFFFF00
ULSR    = 5

        .text
        .global _start
_start: li    sp, 0x00900000            ; 9 MiB: inside the first memory card (D52)
        la    r1, _data_load            ; .data: ROM -> RAM
        la    r2, _data
        la    r3, _edata
.Lcopy: bgeu  r2, r3, .Lclear
        lw    r4, 0(r1)
        sw    r4, 0(r2)
        addi  r1, r1, 4
        addi  r2, r2, 4
        beq   r0, r0, .Lcopy
.Lclear:la    r2, _bss                  ; .bss: zero
        la    r3, _end
.Lz:    bgeu  r2, r3, .Lgo
        sw    r0, 0(r2)
        addi  r2, r2, 4
        beq   r0, r0, .Lz
.Lgo:   la    r1, _end
        la    r2, .Lbrk
        sw    r1, 0(r2)
        la    r1, __sa_trap
        mtcr  VBR, r1
        li    r1, 0                     ; main(0, 0)
        li    r2, 0
        call  main
        call  exit                      ; exit(main's result)
        halt

; ---------------------------------------------------------------- traps
__sa_trap:
        addi  sp, sp, -24
        sw    r10, 0(sp)
        sw    r11, 4(sp)
        sw    r12, 8(sp)
        sw    r13, 12(sp)
        sw    r2, 16(sp)
        sw    r3, 20(sp)
        mfcr  r10, CAUSE
        li    r11, 5                    ; SYSCALL
        bne   r10, r11, .Lfatal
        li    r11, 1
        beq   r1, r11, .Lexit
        li    r11, 4
        beq   r1, r11, .Lwrite
        li    r11, 3
        beq   r1, r11, .Lread
        li    r11, 17
        beq   r1, r11, .Lsbrk
        li    r1, -38                   ; ENOSYS
        beq   r0, r0, .Ldone
.Lexit: mov   r1, r2
        halt
.Lwrite:mov   r1, r4                    ; the result: all n bytes
.Lw1:   beqz  r4, .Ldone
.Lw2:   lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 0x20
        beqz  r10, .Lw2
        lbu   r10, 0(r3)
        sb    r10, U0(r0)
        addi  r3, r3, 1
        addi  r4, r4, -1
        beq   r0, r0, .Lw1
.Lread: li    r1, 0                     ; bytes read
.Lr1:   beq   r1, r4, .Ldone
        li    r12, 0x200000             ; patience: then end of file
.Lr2:   lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 1
        bnez  r10, .Lr3
        addi  r12, r12, -1
        bnez  r12, .Lr2
        beq   r0, r0, .Ldone
.Lr3:   lbu   r10, U0(r0)
        sb    r10, 0(r3)
        addi  r3, r3, 1
        addi  r1, r1, 1
        li    r11, 10
        bne   r10, r11, .Lr1
        beq   r0, r0, .Ldone
.Lsbrk: la    r11, .Lbrk
        lw    r1, 0(r11)                ; the old break
        add   r12, r1, r2
        sw    r12, 0(r11)
.Ldone: mfcr  r10, EPC
        addi  r10, r10, 4
        mtcr  EPC, r10
        lw    r10, 0(sp)
        lw    r11, 4(sp)
        lw    r12, 8(sp)
        lw    r13, 12(sp)
        lw    r2, 16(sp)
        lw    r3, 20(sp)
        addi  sp, sp, 24
        rte
.Lfatal:la    r3, .Lmsg                 ; "trap " CAUSE " at " EPC
        call  .Lputs
        mov   r2, r10
        call  .Lhex
        la    r3, .Lmsg2
        call  .Lputs
        mfcr  r2, EPC
        call  .Lhex
        li    r2, 10
        call  .Lputc
        li    r1, 255
        halt
.Lputs: lbu   r2, 0(r3)
        beqz  r2, .Lret
        call  .Lputc
        addi  r3, r3, 1
        beq   r0, r0, .Lputs
.Lret:  ret
.Lputc: lbu   r11, U0+ULSR(r0)
        andi  r11, r11, 0x20
        beqz  r11, .Lputc
        sb    r2, U0(r0)
        ret
.Lhex:  mov   r12, r2                   ; 8 hex digits of r2
        li    r13, 8
.Lh1:   srli  r2, r12, 28
        slli  r12, r12, 4
        li    r11, 10
        blt   r2, r11, .Lh2
        addi  r2, r2, 39                ; 'a' - '0' - 10
.Lh2:   addi  r2, r2, 48
        call  .Lputc
        addi  r13, r13, -1
        bnez  r13, .Lh1
        ret

        .data
.Lbrk:  .word 0
.Lmsg:  .asciz "\ntrap "
.Lmsg2: .asciz " at "
