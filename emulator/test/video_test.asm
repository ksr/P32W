; video_test.asm -- the video card, revision 2 (docs/video-card-acrtc.md;
; D103, D104) as p32io.h models it: an HD63484 ACRTC on a 16-bit bus and a
; PS/2 section, in a register page and nowhere else. Self-checking: HALT
; with r1 = 0, or with the number of the failing case. Run with -K KEYS
; (KEYS holding "aB"), -Km 09,05,FB, -K KEYS2 (holding "xy") and -V SCREEN:
; SCREEN's first line is then "HELLO P32W", SCREEN.pbm the picture with its
; top line drawn. The PS/2 section's firmware takes its time (D109): the
; cases wait for FULL before a byte is read and for BUSY to clear before a
; command is written, as a driver must.

VSTAT    = 0xFFFFDF00           ; halfword, read: the chip's status in bits 7-0
VAR      = 0xFFFFDF00           ; halfword, write: the chip's address register
VDATA    = 0xFFFFDF04           ; halfword: the register addressed; address 0: the FIFOs
PSTAT    = 0xFFFFDF08           ; byte, read: 1 FULL, 2 SRC (the mouse's), 4 BUSY, 8 TEXT, 0x10 the chip's IRQ
PCMD     = 0xFFFFDF08           ; byte, write: a command to the PS/2 section
PDATA    = 0xFFFFDF0C           ; byte, read: the PS/2 byte
VID      = 0xFFFFDFF0           ; card ID (3)
VREV     = 0xFFFFDFF1           ; revision (2)
VISR     = 0xFFFFDFF4           ; interrupt status, a word: bit 0 the PS/2 section, 1 the chip
OLDMEM   = 0xFE050000           ; where revisions 0 and 1 had their memory: nothing now
TREC     = 0x00100000           ; the handler's record: bus errors
T_BERR   = 0

        li    sp, 0x00500000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_BERR(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  the card: ID 3, revision 2; no memory on the bus (a bus error)
        lbu   r2, VID(r0)
        li    r3, 3
        bne   r2, r3, fail
        lbu   r2, VREV(r0)
        li    r3, 2
        bne   r2, r3, fail
        li    r9, OLDMEM
        lw    r2, 0(r9)
        lw    r2, T_BERR(r6)
        li    r3, 1
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 2  the chip after reset: status 23 (command end, FIFO ready, FIFO empty);
        lhu   r2, VSTAT(r0)     ;    CCR 8000 (ABT); the FIFOs shut: a word written is no command
        andi  r2, r2, 0xFF
        li    r3, 0x23
        bne   r2, r3, fail
        li    r4, 0x02
        sh    r4, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0x8000
        bne   r2, r3, fail
        sh    r0, VAR(r0)
        li    r4, 0x5800
        sh    r4, VDATA(r0)
        lhu   r2, VSTAT(r0)
        andi  r2, r2, 0xFF
        li    r3, 0x23
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 3  registers are words; r80 and up step the address register by 2
        li    r4, 0x82
        sh    r4, VAR(r0)
        li    r4, 0x630C
        sh    r4, VDATA(r0)     ; r82
        li    r4, 0x054F
        sh    r4, VDATA(r0)     ; r84
        li    r4, 0x82
        sh    r4, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0x630C
        bne   r2, r3, fail
        lhu   r2, VDATA(r0)
        li    r3, 0x054F
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 4  the start sequence; the screen cleared; a word written (WT), read back (RD)
        la    r10, regs
t4:     lhu   r4, 0(r10)
        beqz  r4, t4a
        lhu   r5, 2(r10)
        sh    r4, VAR(r0)
        sh    r5, VDATA(r0)
        addi  r10, r10, 4
        jmp   t4
t4a:    la    r10, setup
        li    r11, 41
        call  send
        la    r10, wtrd
        li    r11, 11
        call  send
        li    r5, 100000
t4b:    lhu   r2, VSTAT(r0)     ; the read FIFO has a word (RFR)
        andi  r2, r2, 0x04
        bnez  r2, t4c
        addi  r5, r5, -1
        bnez  r5, t4b
        jmp   fail
t4c:    sh    r0, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0xBEEF
        bne   r2, r3, fail
        lhu   r2, VSTAT(r0)     ; ... and now none
        andi  r2, r2, 0x04
        bnez  r2, fail
        la    r10, wtclr
        li    r11, 6
        call  send

        addi  r1, r1, 1         ; 5  the font from the EPROM, two masked copies; "HELLO P32W" on row 0:
        la    r10, font         ;    one move, then a graphic copy for each character
        li    r11, 27
        call  send
        la    r10, hello
t5:     lbu   r11, 0(r10)
        beqz  r11, t5a
        li    r7, 0xED00        ; AGCPY: Xs, Ys, 7, -15
        call  fifo
        andi  r7, r11, 31       ; Xs = (c & 31) x 8
        slli  r7, r7, 3
        call  fifo
        addi  r7, r11, -0x20    ; Ys = -1 - 16 x ((c - 0x20) >> 5)
        srli  r7, r7, 5
        slli  r7, r7, 4
        sub   r7, r0, r7
        addi  r7, r7, -1
        call  fifo
        li    r7, 7
        call  fifo
        li    r7, 0xFFF1
        call  fifo
        addi  r10, r10, 1
        jmp   t5
t5a:    call  idle

        addi  r1, r1, 1         ; 6  a line along the top of the screen (ALINE); word 0 reads FFFF
        la    r10, line
        li    r11, 11
        call  send
        li    r5, 100000
t6:     lhu   r2, VSTAT(r0)
        andi  r2, r2, 0x04
        bnez  r2, t6a
        addi  r5, r5, -1
        bnez  r5, t6
        jmp   fail
t6a:    sh    r0, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0xFFFF
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 7  the write FIFO: the chip busy clearing the band under the text, eight
        call  idle              ;    words wait and there is room for no more; then it is free again
        la    r10, band
        li    r11, 8
        call  send
        sh    r0, VAR(r0)
        li    r4, 0x0802        ; (WPR Pr02, four times: eight words)
        sh    r4, VDATA(r0)
        sh    r0, VDATA(r0)
        sh    r4, VDATA(r0)
        sh    r0, VDATA(r0)
        sh    r4, VDATA(r0)
        sh    r0, VDATA(r0)
        sh    r4, VDATA(r0)
        sh    r0, VDATA(r0)
        lhu   r2, VSTAT(r0)
        andi  r2, r2, 0x23
        bnez  r2, fail
        call  idle
        lhu   r2, VSTAT(r0)
        andi  r2, r2, 0x23
        li    r3, 0x23
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 8  the chip's interrupt: WEE in CCR -> level 4, the card's status bit 4,
        mfcr  r2, IPEND         ;    its interrupt status bit 1; off again
        andi  r2, r2, 0x10
        bnez  r2, fail
        li    r4, 0x02
        sh    r4, VAR(r0)
        li    r4, 0x0001
        sh    r4, VDATA(r0)
        addi  r2, r2, 0
        addi  r2, r2, 0
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        beqz  r2, fail
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 0x10
        beqz  r2, fail
        lw    r2, VISR(r0)
        li    r3, 2
        bne   r2, r3, fail
        li    r4, 0x02
        sh    r4, VAR(r0)
        sh    r0, VDATA(r0)
        addi  r2, r2, 0
        addi  r2, r2, 0
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, fail
        lw    r2, VISR(r0)
        bnez  r2, fail

        addi  r1, r1, 1         ; 9  the keyboard typed "aB": ASCII from the PS/2 section, FULL, SRC = 0. The
        call  pfull             ;    firmware has to have started (two seconds from a reset): wait for FULL.
        lbu   r2, PSTAT(r0)     ;    The byte read, FULL is clear: the firmware takes some 50 us to load the
        andi  r2, r2, 0x0F      ;    next one
        li    r3, 1
        bne   r2, r3, fail
        lbu   r2, PDATA(r0)
        li    r3, 0x61          ; 'a'
        bne   r2, r3, fail
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, fail

        addi  r1, r1, 1         ; 10 a command written over one not yet taken is lost: 'B' waits; 01 and, at
        call  pfull             ;    once, 02: BUSY, the firmware finds only the 02, and no interrupt is asked
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, fail
        call  pbusy
        li    r4, 0x01
        sb    r4, PCMD(r0)
        li    r4, 0x02
        sb    r4, PCMD(r0)
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 4
        beqz  r2, fail
        call  pbusy
        li    r5, 100
lost1:  mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, fail
        addi  r5, r5, -1
        bnez  r5, lost1

        addi  r1, r1, 1         ; 11 its interrupt: on (01) with a byte waiting -> level 4, interrupt status
        li    r4, 0x01          ;    bit 0; acknowledged (03): quiet though the byte still waits; off (02).
        call  pcmd              ;    A command is written once the last one is taken (BUSY clear), and has
        call  pbusy             ;    been taken when BUSY is clear again
        call  pirq
        lw    r2, VISR(r0)
        li    r3, 1
        bne   r2, r3, fail
        li    r4, 0x03
        call  pcmd
        call  pbusy
        call  pquiet
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 1
        beqz  r2, fail
        li    r4, 0x02
        call  pcmd
        call  pbusy
        lbu   r2, PDATA(r0)
        li    r3, 0x42          ; 'B'
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 12 the mouse's packet, whole: three bytes with SRC = 1
        li    r7, 0x09
        call  mbyte
        li    r7, 0x05
        call  mbyte
        li    r7, 0xFB
        call  mbyte

        addi  r1, r1, 1         ; 13 flush (06) empties the firmware's queue, not the card's latch: 'x' is
        call  pfull             ;    there, FULL, and still is after it, and is read; the 'y' behind it is
        li    r4, 0x06          ;    gone, nothing more comes
        call  pcmd
        call  pbusy
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 3
        li    r3, 1
        bne   r2, r3, fail
        lbu   r2, PDATA(r0)
        li    r3, 0x78          ; 'x'
        bne   r2, r3, fail
        li    r5, 500
flush1: lbu   r2, PSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, fail
        addi  r5, r5, -1
        bnez  r5, flush1

        addi  r1, r1, 1         ; 14 refused, each a bus error: a byte of the chip's status, a word of its
        lbu   r2, VSTAT(r0)     ;    data, a halfword of the PS/2 byte, a write to the PS/2 byte
        lw    r2, VDATA(r0)
        lhu   r2, PDATA(r0)
        sb    r4, PDATA(r0)
        lw    r2, T_BERR(r6)
        li    r3, 5
        bne   r2, r3, fail

        li    r1, 0
fail:   halt                    ; r1 = failing case number

; fifo: r7 to the chip's write FIFO once it has room (r2, r5 used)
fifo:   sh    r0, VAR(r0)
        li    r5, 2000000
fifo1:  lhu   r2, VSTAT(r0)
        andi  r2, r2, 0x02
        bnez  r2, fifo2
        addi  r5, r5, -1
        bnez  r5, fifo1
        halt
fifo2:  sh    r7, VDATA(r0)
        ret

; send: r11 words from r10 to the write FIFO
send:   lhu   r7, 0(r10)
        call  fifo
        addi  r10, r10, 2
        addi  r11, r11, -1
        bnez  r11, send
        ret

; idle: wait for the chip's command end
idle:   li    r5, 2000000
idle1:  lhu   r2, VSTAT(r0)
        andi  r2, r2, 0x20
        bnez  r2, idle2
        addi  r5, r5, -1
        bnez  r5, idle1
        halt
idle2:  ret

; mbyte: the next PS/2 byte, once it is there, must be the mouse's (FULL and SRC) and r7
mbyte:  li    r5, 4000000
mbyte1: lbu   r2, PSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, mbyte2
        addi  r5, r5, -1
        bnez  r5, mbyte1
        halt
mbyte2: lbu   r2, PSTAT(r0)
        andi  r2, r2, 3
        li    r3, 3
        bne   r2, r3, mbad
        lbu   r2, PDATA(r0)
        bne   r2, r7, mbad
        ret
mbad:   halt

; pfull: wait for FULL, a byte in the PS/2 section's latch (r2, r5 used). Long enough for the ATmega's start
pfull:  li    r5, 4000000
pfull1: lbu   r2, PSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, pfull2
        addi  r5, r5, -1
        bnez  r5, pfull1
        halt
pfull2: ret

; pbusy: wait until the firmware has taken the command written (BUSY clear; r2, r5 used)
pbusy:  li    r5, 4000000
pbusy1: lbu   r2, PSTAT(r0)
        andi  r2, r2, 4
        beqz  r2, pbusy2
        addi  r5, r5, -1
        bnez  r5, pbusy1
        halt
pbusy2: ret

; pcmd: the command r4 to the PS/2 section, once the last one is taken (r2, r5 used)
pcmd:   li    r5, 4000000
pcmd1:  lbu   r2, PSTAT(r0)
        andi  r2, r2, 4
        beqz  r2, pcmd2
        addi  r5, r5, -1
        bnez  r5, pcmd1
        halt
pcmd2:  sb    r4, PCMD(r0)
        ret

; pirq: wait for level 4 to ask (r2, r5 used); pquiet: for it to stop asking
pirq:   li    r5, 1000
pirq1:  mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, pirq2
        addi  r5, r5, -1
        bnez  r5, pirq1
        halt
pirq2:  ret
pquiet: li    r5, 1000
pquie1: mfcr  r2, IPEND
        andi  r2, r2, 0x10
        beqz  r2, pquie2
        addi  r5, r5, -1
        bnez  r5, pquie1
        halt
pquie2: ret

; a bus error: counted, and the instruction skipped
handler:
        push  r12
        push  r13
        mfcr  r12, CAUSE
        li    r13, 4
        bne   r12, r13, h_bad
        li    r13, TREC
        lw    r12, T_BERR(r13)
        addi  r12, r12, 1
        sw    r12, T_BERR(r13)
        mfcr  r12, EPC
        addi  r12, r12, 4
        mtcr  EPC, r12
        pop   r13
        pop   r12
        rte
h_bad:  halt

        .align 4
; the chip's registers: 640 x 480 at 60 Hz from 25.175 MHz, the Base screen 40 words a line from word 0
regs:   .half 0x02, 0x0000, 0x04, 0x8088, 0x06, 0x4000
        .half 0x82, 0x630C, 0x84, 0x054F, 0x86, 0x020D, 0x88, 0x2002, 0x8A, 0x01E0
        .half 0xCA, 0x0028, 0xCC, 0x0000, 0xCE, 0x0000
        .half 0x04, 0xC088, 0
; ORG (the origin: word 19,160, the screen's bottom-left); the colours; the pattern's pointers; the pattern
; RAM solid (WPTN, 16 words); the mask; the pointer to word 0; CLR 40 words x 480 lines
setup:  .half 0x0400, 0x4004, 0xAD80
        .half 0x0800, 0x0000, 0x0801, 0xFFFF
        .half 0x0805, 0x0000, 0x0806, 0x0000, 0x0807, 0xF0F0
        .half 0x1800, 16
        .half 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
        .half 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
        .half 0x0804, 0xFFFF
        .half 0x080C, 0x4000, 0x080D, 0x0000
        .half 0x5800, 0x0000, 0x0027, 0xFE21
; the pointer to word 19,163; WT BEEF; the pointer again; RD
wtrd:   .half 0x080C, 0x4004, 0x080D, 0xADB0, 0x4800, 0xBEEF
        .half 0x080C, 0x4004, 0x080D, 0xADB0, 0x4400
; ... and the word back to 0
wtclr:  .half 0x080C, 0x4004, 0x080D, 0xADB0, 0x4800, 0x0000
; the mask 00FF, the pointer to word 19,200 (the line under the screen), SCPY 16 words x 48 lines from the
; EPROM's even places (80000); the mask FF00, the pointer again, SCPY from its odd places (80780); the mask
; FFFF; AMOVE to row 0's first cell (0, 439)
font:   .half 0x0804, 0x00FF, 0x080C, 0x4004, 0x080D, 0xB000
        .half 0x7100, 0x0080, 0x0000, 0x000F, 0xFFD1
        .half 0x0804, 0xFF00, 0x080C, 0x4004, 0x080D, 0xB000
        .half 0x7100, 0x0080, 0x7800, 0x000F, 0xFFD1
        .half 0x0804, 0xFFFF
        .half 0x8000, 0x0000, 0x01B7
; AMOVE (0, 479); ALINE to (640, 479); the pointer to word 0; RD
line:   .half 0x8000, 0x0000, 0x01DF, 0x8800, 0x0280, 0x01DF
        .half 0x080C, 0x4000, 0x080D, 0x0000, 0x4400
; the pointer to word 17,600 (line 440); CLR 40 words x 40 lines
band:   .half 0x080C, 0x4004, 0x080D, 0x4C00, 0x5800, 0x0000, 0x0027, 0xFFD9
hello:  .asciz "HELLO P32W"
