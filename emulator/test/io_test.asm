; io_test.asm -- the I/O card (D50): both UARTs, the ports and the CompactFlash
; disk, as p32io.h models them. Self-checking: HALT with r1 = 0, or with the
; number of the failing case. Run with: -p 0x5A -a aux.out -d disk.img, where
; disk.img holds 8 sectors, byte i of sector n = (7 * n + i) & 0xFF.

U0       = 0xFFFFFF00           ; UART 0 (console) registers 0-7
U0IER    = 0xFFFFFF01
U0IIR    = 0xFFFFFF02
U0LCR    = 0xFFFFFF03
U0LSR    = 0xFFFFFF05
U0SCR    = 0xFFFFFF07
U1       = 0xFFFFFF08           ; UART 1 data
PIN      = 0xFFFFFF20           ; input port
POUT     = 0xFFFFFF21           ; output port
CFDATA   = 0xFFFFEF00           ; CompactFlash task file: the disk card's port 0 (D50)
CFFEAT   = 0xFFFFEF01
CFCOUNT  = 0xFFFFEF02
CFLBA0   = 0xFFFFEF03
CFLBA1   = 0xFFFFEF04
CFLBA2   = 0xFFFFEF05
CFHEAD   = 0xFFFFEF06
CFCMD    = 0xFFFFEF07           ; status when read
BUF      = 0x00100000           ; sector buffer in RAM

        li    sp, 0x00500000
        li    r1, 0

; ---------------------------------------------------------------- UART 0
        addi  r1, r1, 1         ; 1  LSR: THRE + TEMT, no data (stdin empty)
        lbu   r2, U0LSR(r0)
        li    r3, 0x60
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 2  scratch register
        li    r4, 0xA5
        sb    r4, U0SCR(r0)
        lbu   r2, U0SCR(r0)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 3  divisor latch (DLAB), 9600 baud at 1.8432 MHz
        li    r4, 0x80
        sb    r4, U0LCR(r0)
        li    r4, 12
        sb    r4, U0(r0)        ; DLL
        sb    r0, U0IER(r0)     ; DLM
        lbu   r2, U0(r0)
        bne   r2, r4, fail
        li    r4, 0x03          ; 8N1, DLAB off
        sb    r4, U0LCR(r0)
        lbu   r2, U0LCR(r0)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 4  IIR with no interrupt enabled: 01
        lbu   r2, U0IIR(r0)
        li    r3, 0x01
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  enabling THRE interrupts raises IRQ2 at once
        li    r4, 0x02
        sb    r4, U0IER(r0)
        mfcr  r2, IPEND
        andi  r2, r2, 4
        beqz  r2, fail
        addi  r1, r1, 1         ; 6  IIR reports THRE (02) once, then nothing (01)
        lbu   r2, U0IIR(r0)
        li    r3, 0x02
        bne   r2, r3, fail
        lbu   r2, U0IIR(r0)
        li    r3, 0x01
        bne   r2, r3, fail
        mfcr  r2, IPEND
        andi  r2, r2, 4
        bnez  r2, fail
        sb    r0, U0IER(r0)

; ---------------------------------------------------------------- UART 1, ports
        addi  r1, r1, 1         ; 7  UART 1 output (checked in aux.out)
        la    r10, auxmsg
        li    r9, U1
        call  puts
        addi  r1, r1, 1         ; 8  input port reads -p 0x5A
        lbu   r2, PIN(r0)
        li    r3, 0x5A
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 9  output port reads back
        li    r4, 0x3C
        sb    r4, POUT(r0)
        lbu   r2, POUT(r0)
        bne   r2, r4, fail

; ---------------------------------------------------------------- CompactFlash
        addi  r1, r1, 1         ; 10 ready: DRDY + DSC
        lbu   r2, CFCMD(r0)
        li    r3, 0x50
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 11 SET FEATURES 01h: 8-bit transfers
        li    r4, 0x01
        sb    r4, CFFEAT(r0)
        li    r4, 0xEF
        sb    r4, CFCMD(r0)
        mfcr  r2, IPEND         ; INTRQ (IRQ1) is up after the command
        andi  r2, r2, 2
        beqz  r2, fail
        lbu   r2, CFCMD(r0)     ; reading status acknowledges it
        li    r3, 0x50
        bne   r2, r3, fail
        mfcr  r2, IPEND
        andi  r2, r2, 2
        bnez  r2, fail
        addi  r1, r1, 1         ; 12 IDENTIFY: DRQ, word 0 = 848Ah (CompactFlash)
        li    r4, 0xEC
        sb    r4, CFCMD(r0)
        lbu   r2, CFCMD(r0)
        li    r3, 0x58
        bne   r2, r3, fail
        li    r10, BUF
        call  cfread
        li    r10, BUF
        lhu   r2, 0(r10)
        li    r3, 0x848A
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 13 READ SECTOR 3 (LBA): bytes 0 and 511
        li    r4, 3
        call  cfsetup
        li    r4, 0x20
        sb    r4, CFCMD(r0)
        li    r10, BUF
        call  cfread
        li    r10, BUF
        lbu   r2, 0(r10)
        li    r3, 21            ; 7 * 3 + 0
        bne   r2, r3, fail
        lbu   r2, 511(r10)
        li    r3, 20            ; (21 + 511) & 0xFF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 14 transfer done: DRQ gone
        lbu   r2, CFCMD(r0)
        li    r3, 0x50
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 15 WRITE SECTOR 5: byte i = i ^ 55h
        li    r4, 5
        call  cfsetup
        li    r4, 0x30
        sb    r4, CFCMD(r0)
        lbu   r2, CFCMD(r0)
        li    r3, 0x58
        bne   r2, r3, fail
        li    r11, 0
cw:     xori  r12, r11, 0x55
        sb    r12, CFDATA(r0)
        addi  r11, r11, 1
        li    r3, 512
        bne   r11, r3, cw
        lbu   r2, CFCMD(r0)
        li    r3, 0x50
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 16 read sector 5 back
        li    r4, 5
        call  cfsetup
        li    r4, 0x20
        sb    r4, CFCMD(r0)
        li    r10, BUF
        call  cfread
        li    r10, BUF
        lbu   r2, 10(r10)
        li    r3, 0x5F          ; 10 ^ 55h
        bne   r2, r3, fail
        lbu   r2, 300(r10)
        li    r3, 0x79          ; 300 & 0xFF = 2Ch, ^ 55h
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 17 an unknown command aborts: ERR, error = ABRT
        li    r4, 0x99
        sb    r4, CFCMD(r0)
        lbu   r2, CFCMD(r0)
        li    r3, 0x51
        bne   r2, r3, fail
        lbu   r2, CFFEAT(r0)    ; error register
        li    r3, 0x04
        bne   r2, r3, fail

; ---------------------------------------------------------------- done
        la    r10, okmsg
        li    r9, U0
        call  puts
        li    r1, 0
fail:   halt                    ; r1 = failing case number

; puts: the 0-terminated string at r10 to the UART data register at r9.
puts:   lbu   r11, 0(r10)
        beqz  r11, puts_done
        sb    r11, 0(r9)
        addi  r10, r10, 1
        jmp   puts
puts_done:
        ret

; cfsetup: one sector at LBA r4 (< 256), LBA mode, drive 0.
cfsetup:
        li    r5, 1
        sb    r5, CFCOUNT(r0)
        sb    r4, CFLBA0(r0)
        sb    r0, CFLBA1(r0)
        sb    r0, CFLBA2(r0)
        li    r5, 0xE0
        sb    r5, CFHEAD(r0)
        ret

; cfread: 512 bytes from the CF data register to r10.
cfread: li    r11, 512
cr:     lbu   r12, CFDATA(r0)
        sb    r12, 0(r10)
        addi  r10, r10, 1
        addi  r11, r11, -1
        bnez  r11, cr
        ret

auxmsg: .asciz "aux port\n"
okmsg:  .asciz "io ok\n"
