; bus_test.asm -- for generators/sim_backplane.py: a program that moves every
; line of the backplane it can, with every slot filled (the CPU, MMU / data,
; debug and four memory cards, the I/O, disk and video cards). Not timing: the
; board's lines, each through a card. The I/O window is 7, the disk card's 6,
; the video card's 5; memory cards in windows 0 to 3 (the first with the ROM).
; Run with -d1 IMAGE whose halfword j of sector 1 is 0x5A00 + j. HALT with
; r1 = 0, or the failing case. Up to dma: (case 9) it is compared with p32uemu
; -C state by state; from there it checks itself (a DMA engine's timing is its
; own).

TCOUNT   = 0xFFFFFF10
TRELOAD  = 0xFFFFFF14
TCTRL    = 0xFFFFFF18
IOID     = 0xFFFFFFF0
DKID     = 0xFFFFEFF0
P1       = 0xFFFFEF10
DMAADR   = 0xFFFFEF40
DMACNT   = 0xFFFFEF44
DMACTL   = 0xFFFFEF48
VID      = 0xFFFFDFF0
VAR      = 0xFFFFDF00
VDATA    = 0xFFFFDF04
TREC     = 0x00100000
T_CAUSE  = 0
T_COUNT  = 4
BUF      = 0x00110000

        li    sp, 0x00180000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_CAUSE(r6)
        sw    r0, T_COUNT(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  D: a one walked, and A2-A19 with them
        li    r5, 0x00100100
        li    r4, 1
        li    r7, 4
c1:     add   r8, r5, r7
        sw    r4, 0(r8)
        lw    r2, 0(r8)
        bne   r2, r4, fail
        add   r4, r4, r4
        add   r7, r7, r7
        li    r3, 0x00080000
        bne   r7, r3, c1d
        li    r7, 4
c1d:    bnez  r4, c1
        addi  r1, r1, 1         ; 2  byte lanes, halfwords, signed and not
        li    r5, 0x00100100
        li    r4, 0x80FF7F01
        sw    r4, 0(r5)
        li    r3, 0xA5
        sb    r3, 1(r5)
        lb    r2, 1(r5)
        li    r3, 0xFFFFFFA5
        bne   r2, r3, fail
        sb    r3, 2(r5)
        sb    r3, 3(r5)
        lbu   r2, 0(r5)
        li    r3, 1
        bne   r2, r3, fail
        lh    r2, 2(r5)
        li    r3, 0xFFFFA5A5
        bne   r2, r3, fail
        sh    r0, 2(r5)
        lhu   r2, 0(r5)
        li    r3, 0xA501
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 3  the other memory cards: A24, A25; the first card's banks: A20-A23
        li    r5, 0x01000004
        li    r7, 0x02000008
        li    r8, 0x03000010
        li    r9, 0x00200000
        li    r10, 0x00400000
        li    r11, 0x00800000
        sw    r5, 0(r5)
        sw    r7, 0(r7)
        sw    r8, 0(r8)
        sw    r9, 0(r9)
        sw    r10, 0(r10)
        sw    r11, 0(r11)
        lw    r2, 0(r5)
        bne   r2, r5, fail
        lw    r2, 0(r7)
        bne   r2, r7, fail
        lw    r2, 0(r8)
        bne   r2, r8, fail
        lw    r2, 0(r9)
        bne   r2, r9, fail
        lw    r2, 0(r10)
        bne   r2, r10, fail
        lw    r2, 0(r11)
        bne   r2, r11, fail
        addi  r1, r1, 1         ; 4  bus errors: A26 to A31, each alone (nobody answers), CAUSE 4
        li    r5, 0x04000000
        li    r7, 0
c4:     lw    r2, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 4
        bne   r2, r3, fail
        addi  r7, r7, 1
        lw    r2, T_COUNT(r6)
        bne   r2, r7, fail
        add   r5, r5, r5
        bnez  r5, c4
        lw    r2, 0x101(r0)     ; (misaligned: the fault codes' other lines)
        sb    r0, 0x40(r0)      ; (the ROM takes no write)
        addi  r1, r1, 1         ; 5  the I/O, disk and video cards' IDs; a register of the video chip
        lbu   r2, IOID(r0)
        li    r3, 1
        bne   r2, r3, fail
        lbu   r2, DKID(r0)
        li    r3, 2
        bne   r2, r3, fail
        lbu   r2, VID(r0)
        li    r3, 3
        bne   r2, r3, fail
        li    r4, 0x82
        sh    r4, VAR(r0)
        li    r4, 0x630C
        sh    r4, VDATA(r0)
        li    r4, 0x82
        sh    r4, VAR(r0)
        lhu   r2, VDATA(r0)
        li    r3, 0x630C
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 6  an interrupt: level 0, its vector by the acknowledge cycle
        li    r4, 1000
        sw    r4, TRELOAD(r0)
        li    r4, 20
        sw    r4, TCOUNT(r0)
        li    r4, 1
        mtcr  IMASK, r4
        li    r4, 3
        mtcr  STATUS, r4
        li    r4, 1
        sw    r4, TCTRL(r0)
        wait
        nop
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80007000
        bne   r2, r3, fail
        li    r4, 1
        mtcr  STATUS, r4
        mtcr  IMASK, r0
        addi  r1, r1, 1         ; 7  the atomic exchange: -LOCK
        li    r5, 0x00100200
        li    r4, 0x12345678
        sw    r4, 0(r5)
        li    r2, 0x0FEDCBA9
        swap  r2, 0(r5)
        bne   r2, r4, fail
        addi  r1, r1, 1         ; 8  a TLB entry, user mode through it, back by SYSCALL; a miss
        li    r4, 1
        mtcr  ASID, r4
        la    r5, user
        li    r3, 0xFFFFF000
        and   r5, r5, r3
        ori   r4, r5, 0x101     ; I, ASID 1
        mtcr  TLBHI, r4
        ori   r4, r5, 5         ; V | X
        mtcr  TLBLO, r4
        tlbw
        la    r4, user
        mtcr  EPC, r4
        li    r4, 1
        mtcr  STATUS, r4
        la    r9, resume
        rte
user:   lw    r2, 0x100(r0)     ; not in the D-TLB
        syscall
resume: lw    r2, T_CAUSE(r6)
        li    r3, 5
        bne   r2, r3, fail

dma:    addi  r1, r1, 1         ; 9  a read by the disk card's DMA, the CPU using the bus meanwhile
        li    r9, P1
        li    r2, 0xE0
        sb    r2, 6(r9)
        li    r2, 1
        sb    r2, 3(r9)
        sb    r0, 4(r9)
        sb    r0, 5(r9)
        sb    r2, 2(r9)
        li    r2, 0x20          ; READ SECTOR
        sb    r2, 7(r9)
c9d:    lbu   r2, 14(r9)
        andi  r2, r2, 0x08
        beqz  r2, c9d
        li    r4, BUF
        sw    r4, DMAADR(r0)
        li    r4, 4
        sw    r4, DMACNT(r0)
        li    r4, 0x05          ; RUN, port 1, the drive to memory
        sw    r4, DMACTL(r0)
c9:     lw    r2, DMACTL(r0)
        andi  r3, r2, 1
        bnez  r3, c9
        li    r3, 0x44
        bne   r2, r3, fail
        li    r5, BUF
        lw    r2, 0(r5)
        li    r3, 0x5A015A00
        bne   r2, r3, fail
        lw    r2, 12(r5)
        li    r3, 0x5A075A06
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 10 and a bus error under it: nobody at 0x08000000
        li    r4, 0x08000000
        sw    r4, DMAADR(r0)
        li    r4, 4
        sw    r4, DMACNT(r0)
        li    r4, 0x07
        sw    r4, DMACTL(r0)
c10:    lw    r2, DMACTL(r0)
        andi  r3, r2, 1
        bnez  r3, c10
        li    r3, 0x86
        bne   r2, r3, fail
        sw    r0, DMACTL(r0)
        lbu   r2, 7(r9)         ; (the drive's own interrupt acknowledged)
        li    r1, 0
fail:   halt

handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        sw    r12, T_CAUSE(r13)
        blt   r12, r0, h_irq
        li    r13, 5
        beq   r12, r13, h_sys
        li    r13, TREC
        lw    r12, T_COUNT(r13)
        addi  r12, r12, 1
        sw    r12, T_COUNT(r13)
        mfcr  r12, EPC
        addi  r12, r12, 4
        mtcr  EPC, r12
        jmp   h_ret
h_sys:  mtcr  EPC, r9
        li    r12, 5            ; S = 1, PS = 1
        mfcr  r13, STATUS
        ori   r13, r13, 4
        mtcr  STATUS, r13
        jmp   h_ret
h_irq:  li    r12, 2
        sw    r12, TCTRL(r0)
h_ret:  pop   r13
        pop   r12
        rte
