; diskdma_test.asm -- the disk card's DMA engine (D75): its registers, a
; sector read from IDE channel A into memory in two transfers while the CPU
; keeps using the card (ID, status word, an empty port, DMACTL: each access
; waits while the engine has the card's byte bus), the sector written to the
; second CF port from memory and read back, the engine's interrupt (status
; bit 4, vector window x 16 + 4), and a transfer stopped by a bus error.
; Run with -d2 dmA.img -d1 dm1.img, where halfword j of dmA.img's sector 1
; is 0x5A00 + j. Self-checking: HALT with r1 = 0, or the failing case.

P1       = 0xFFFFEF10           ; port 1, the second CF adapter
P2       = 0xFFFFEF20           ; port 2, IDE channel A
P3       = 0xFFFFEF30           ; port 3, IDE channel B: empty
DMAADR   = 0xFFFFEF40
DMACNT   = 0xFFFFEF44
DMACTL   = 0xFFFFEF48
DKID     = 0xFFFFEFF0
DKREV    = 0xFFFFEFF1
DKISR    = 0xFFFFEFF4
COUNT    = 2                    ; task file offsets
LBA0     = 3
LBA1     = 4
LBA2     = 5
HEAD     = 6
CMD      = 7                    ; status (read) / command (write)
ALT      = 14                   ; alternate status
TREC     = 0x00100000
T_CAUSE  = 0
T_IRQS   = 4
T_ISR    = 8
BUF      = 0x00110000           ; sector buffers
BUF2     = 0x00110400
NOWHERE  = 0x08000000           ; no card answers

        li    sp, 0x00180000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_IRQS(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  revision 1; the registers read back; idle
        lbu   r2, DKREV(r0)
        li    r3, 1
        bne   r2, r3, fail
        lw    r2, DMACTL(r0)
        bnez  r2, fail
        li    r4, 0x00123457
        sw    r4, DMAADR(r0)
        lw    r2, DMAADR(r0)
        li    r3, 0x00123454    ; (a word address)
        bne   r2, r3, fail
        li    r4, 0x00012345
        sw    r4, DMACNT(r0)
        lw    r2, DMACNT(r0)
        li    r3, 0x2345        ; (16 bits)
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 2  port 2's sector 1 by DMA, in two halves, the CPU using the card
        li    r9, P2
        li    r4, 1
        call  rdcmd
        bnez  r2, fail
        li    r4, BUF
        sw    r4, DMAADR(r0)
        li    r4, 64
        sw    r4, DMACNT(r0)
        li    r4, 0x09          ; RUN, port 2, drive -> memory
        sw    r4, DMACTL(r0)
        call  busy
        beqz  r11, fail         ; (the CPU ran while it moved)
        li    r3, 0x48          ; DONE, port 2
        bne   r2, r3, fail
        lw    r2, DMACNT(r0)
        bnez  r2, fail
        lw    r2, DMAADR(r0)
        li    r3, BUF + 256
        bne   r2, r3, fail
        li    r4, 64            ; the second half: DMAADR runs on
        sw    r4, DMACNT(r0)
        li    r4, 0x09
        sw    r4, DMACTL(r0)
        call  busy
        li    r3, 0x48
        bne   r2, r3, fail
        lbu   r2, ALT(r9)       ; the drive: its data all taken (no DRQ, no error)
        andi  r2, r2, 0x89
        bnez  r2, fail
        addi  r1, r1, 1         ; 3  the sector as the image has it
        li    r8, BUF
        call  check
        bnez  r2, fail

        addi  r1, r1, 1         ; 4  written to port 1's sector 2 from memory by DMA ...
        li    r9, P1
        li    r4, 0xE0
        sb    r4, HEAD(r9)
        li    r4, 1
        sb    r4, COUNT(r9)
        li    r4, 2
        sb    r4, LBA0(r9)
        sb    r0, LBA1(r9)
        sb    r0, LBA2(r9)
        li    r4, 0x30          ; WRITE SECTOR
        sb    r4, CMD(r9)
        call  drq
        bnez  r2, fail
        li    r4, BUF
        sw    r4, DMAADR(r0)
        li    r4, 128
        sw    r4, DMACNT(r0)
        li    r4, 0x07          ; RUN, memory -> drive, port 1
        sw    r4, DMACTL(r0)
        call  busy
        li    r3, 0x46          ; DONE, DIR, port 1
        bne   r2, r3, fail
c4w:    lbu   r2, ALT(r9)       ; the drive has it: not busy, no DRQ, no error
        andi  r3, r2, 0x80
        bnez  r3, c4w
        andi  r3, r2, 0x09
        bnez  r3, fail
        addi  r1, r1, 1         ; 5  ... and read back by DMA
        li    r4, 2
        call  rdcmd
        bnez  r2, fail
        li    r4, BUF2
        sw    r4, DMAADR(r0)
        li    r4, 128
        sw    r4, DMACNT(r0)
        li    r4, 0x05          ; RUN, port 1
        sw    r4, DMACTL(r0)
        call  busy
        li    r3, 0x44
        bne   r2, r3, fail
        li    r8, BUF2
        call  check
        bnez  r2, fail

        addi  r1, r1, 1         ; 6  the engine's interrupt: level 1, vector 64 (window 6, source 4)
        li    r9, P2
        li    r4, 1
        call  rdcmd
        bnez  r2, fail
        lbu   r2, CMD(r9)       ; (the ports' own interrupts acknowledged)
        lbu   r2, P1+CMD(r0)
        lw    r2, DKISR(r0)
        bnez  r2, fail
        li    r4, BUF2
        sw    r4, DMAADR(r0)
        li    r4, 128
        sw    r4, DMACNT(r0)
        li    r4, 2
        mtcr  IMASK, r4
        li    r4, 0x19          ; RUN, port 2, IE
        sw    r4, DMACTL(r0)
        li    r4, 3             ; S | IE
        mtcr  STATUS, r4
c6:     lw    r2, T_IRQS(r6)
        beqz  r2, c6
        li    r4, 1
        mtcr  STATUS, r4
        mtcr  IMASK, r0
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80006401
        bne   r2, r3, fail
        lw    r2, T_ISR(r6)     ; the status word said: the engine (bit 4)
        li    r3, 0x10
        bne   r2, r3, fail
        lw    r2, DKISR(r0)     ; acknowledged (DMACTL written)
        bnez  r2, fail

        addi  r1, r1, 1         ; 7  no card answers: ERR, nothing counted
        li    r4, NOWHERE
        sw    r4, DMAADR(r0)
        li    r4, 4
        sw    r4, DMACNT(r0)
        li    r4, 0x07          ; RUN, memory -> drive, port 1 (no command: the bus read fails first)
        sw    r4, DMACTL(r0)
        call  busy
        li    r3, 0x86          ; ERR, DIR, port 1
        bne   r2, r3, fail
        lw    r2, DMACNT(r0)
        li    r3, 4
        bne   r2, r3, fail
        lw    r2, DMAADR(r0)
        li    r3, NOWHERE
        bne   r2, r3, fail
        sw    r0, DMACTL(r0)    ; (cleared)
        lw    r2, DMACTL(r0)
        bnez  r2, fail
        li    r1, 0
fail:   halt

; rdcmd: READ SECTOR r4 (master) on port r9, then wait for its data (r2 = 0)
rdcmd:  li    r2, 0xE0
        sb    r2, HEAD(r9)
        sb    r4, LBA0(r9)
        sb    r0, LBA1(r9)
        sb    r0, LBA2(r9)
        li    r2, 1
        sb    r2, COUNT(r9)
        li    r2, 0x20          ; READ SECTOR
        sb    r2, CMD(r9)
        ; fall into drq

; drq: wait for port r9 not busy with DRQ (r2 = 0), or ERR (r2 = the status)
drq:    lbu   r2, ALT(r9)
        andi  r3, r2, 0x80
        bnez  r3, drq
        andi  r3, r2, 0x01
        bnez  r3, drq_e
        andi  r3, r2, 0x08
        beqz  r3, drq
        li    r2, 0
drq_e:  ret

; busy: while the engine runs, use the card (its ID, the status word, the
; empty port 3's status, DMACTL), counting the turns in r11; r2 = DMACTL at
; the end
busy:   li    r11, 0
bz1:    lbu   r2, DKID(r0)
        li    r3, 2
        bne   r2, r3, fail
        lw    r2, DKISR(r0)
        lbu   r2, P3+CMD(r0)
        bnez  r2, fail
        lw    r2, DMACTL(r0)
        andi  r3, r2, 1
        beqz  r3, bz2
        addi  r11, r11, 1
        jmp   bz1
bz2:    ret

; check: the 128 words at r8 are the sector's (halfword j: 0x5A00 + j); r2 = 0
check:  li    r5, 0x5A015A00
        li    r10, 0x00020002
        li    r7, 128
ck1:    lw    r2, 0(r8)
        bne   r2, r5, ck_e
        add   r5, r5, r10
        addi  r8, r8, 4
        addi  r7, r7, -1
        bnez  r7, ck1
        li    r2, 0
ck_e:   ret

; the interrupt: CAUSE and the status word recorded, the engine
; acknowledged (DMACTL written)
handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        bge   r12, r0, h_bad
        sw    r12, T_CAUSE(r13)
        lw    r12, DKISR(r0)
        sw    r12, T_ISR(r13)
        sw    r0, DMACTL(r0)
        lw    r12, T_IRQS(r13)
        addi  r12, r12, 1
        sw    r12, T_IRQS(r13)
        pop   r13
        pop   r12
        rte
h_bad:  halt
