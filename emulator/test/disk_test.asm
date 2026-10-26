; disk_test.asm -- the disk card's four ports (D66): the card's ID, an empty
; port (the pull-downs: 00), IDE channel A's master and slave, the second CF
; port written and read back, the interrupt status word, and a port's
; interrupt through its vector. Run with -d2 dkA.img -d2s dkB.img -d1
; dk1.img, where dkA.img's sector 0 starts "MA" and dkB.img's "SL".
; Self-checking: HALT with r1 = 0, or the failing case. On p32uemu -C and on
; the gate-level disk card (generators/sim_sys.py --disk) the two must agree
; instruction for instruction and cycle for cycle (the card's two wait
; states on every port access).

P1       = 0xFFFFEF10           ; port 1, the second CF adapter
P2       = 0xFFFFEF20           ; port 2, IDE channel A
P3       = 0xFFFFEF30           ; port 3, IDE channel B: empty
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
BUF      = 0x00110000           ; a sector buffer

        li    sp, 0x00180000
        li    r1, 0
        li    r6, TREC
        sw    r0, T_IRQS(r6)
        la    r4, handler
        mtcr  VBR, r4

        addi  r1, r1, 1         ; 1  the card: ID 2, revision 0
        lbu   r2, DKID(r0)
        li    r3, 2
        bne   r2, r3, fail
        lbu   r2, DKREV(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 2  an empty port reads 00: status, a register, the data
        lbu   r2, P3+CMD(r0)
        bnez  r2, fail
        lbu   r2, P3+COUNT(r0)
        bnez  r2, fail
        lhu   r2, P3(r0)
        bnez  r2, fail

        addi  r1, r1, 1         ; 3  IDE A's master: sector 0 starts "MA"
        li    r9, P2
        li    r10, 0xE0
        call  rdsec
        bnez  r2, fail
        li    r8, BUF
        lhu   r2, 0(r8)
        li    r3, 0x414D
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 4  its slave: "SL"
        li    r10, 0xF0
        call  rdsec
        bnez  r2, fail
        li    r8, BUF
        lhu   r2, 0(r8)
        li    r3, 0x4C53
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 5  CF 1: a sector written ...
        li    r9, P1
        li    r4, 0xE0
        sb    r4, HEAD(r9)
        li    r4, 1
        sb    r4, COUNT(r9)
        li    r4, 3
        sb    r4, LBA0(r9)
        sb    r0, LBA1(r9)
        sb    r0, LBA2(r9)
        li    r4, 0x30          ; WRITE SECTOR
        sb    r4, CMD(r9)
        call  drq
        bnez  r2, fail
        li    r7, 256
        li    r5, 0x1234
c5:     sh    r5, 0(r9)
        addi  r5, r5, 0x0101
        addi  r7, r7, -1
        bnez  r7, c5
c5w:    lbu   r2, ALT(r9)       ; done: not busy, no DRQ, no error
        andi  r3, r2, 0x80
        bnez  r3, c5w
        andi  r3, r2, 0x09
        bnez  r3, fail
        addi  r1, r1, 1         ; 6  ... and read back
        li    r10, 0xE0
        li    r4, 3
        call  rdsec_lba
        bnez  r2, fail
        li    r8, BUF
        li    r7, 256
        li    r5, 0x1234
c6:     lhu   r2, 0(r8)
        andi  r3, r5, 0xFFFF
        bne   r2, r3, fail
        addi  r5, r5, 0x0101
        addi  r8, r8, 2
        addi  r7, r7, -1
        bnez  r7, c6

        lbu   r2, P1+CMD(r0)    ; (every unit's interrupt acknowledged: its status read)
        li    r4, 0xF0
        sb    r4, P2+HEAD(r0)
        lbu   r2, P2+CMD(r0)
        li    r4, 0xE0
        sb    r4, P2+HEAD(r0)
        lbu   r2, P2+CMD(r0)
        lw    r2, DKISR(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 7  the status word: port 2 asking (bit 2), until its status is read
        li    r9, P2
        li    r4, 0xE0
        sb    r4, HEAD(r9)
        li    r4, 0xEC          ; IDENTIFY: INTRQ with the data ready
        sb    r4, CMD(r9)
        call  drq
        bnez  r2, fail
        lw    r2, DKISR(r0)
        li    r3, 4
        bne   r2, r3, fail
        lbu   r2, CMD(r9)       ; reading the status acknowledges it
        lw    r2, DKISR(r0)
        bnez  r2, fail
        call  drain

        addi  r1, r1, 1         ; 8  the interrupt: level 1, vector 62 (window 6, port 2)
        li    r4, 2
        mtcr  IMASK, r4
        li    r4, 0xEC
        sb    r4, CMD(r9)
        li    r4, 3             ; S | IE
        mtcr  STATUS, r4
c8:     lw    r2, T_IRQS(r6)
        beqz  r2, c8
        li    r4, 1
        mtcr  STATUS, r4
        mtcr  IMASK, r0
        lw    r2, T_CAUSE(r6)
        li    r3, 0x80006201
        bne   r2, r3, fail
        call  drain
        li    r1, 0
fail:   halt

; rdsec: sector 0 of the unit r10 (E0 master, F0 slave) of port r9 into
; BUF; rdsec_lba: sector r4. r2 = 0, or the status with ERR.
rdsec:  li    r4, 0
rdsec_lba:
        sb    r10, HEAD(r9)
        sb    r4, LBA0(r9)
        sb    r0, LBA1(r9)
        sb    r0, LBA2(r9)
        li    r4, 1
        sb    r4, COUNT(r9)
        li    r4, 0x20          ; READ SECTOR
        sb    r4, CMD(r9)
        push  r10
        call  drq
        pop   r10
        bnez  r2, rd_ret
        li    r8, BUF
        li    r7, 256
rd1:    lhu   r2, 0(r9)
        sh    r2, 0(r8)
        addi  r8, r8, 2
        addi  r7, r7, -1
        bnez  r7, rd1
        li    r2, 0
rd_ret: ret

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

; drain: read port r9's 256 data halfwords (a command's data) away
drain:  li    r7, 256
dr1:    lhu   r2, 0(r9)
        addi  r7, r7, -1
        bnez  r7, dr1
        ret

; the interrupt: CAUSE recorded, the port's status read (INTRQ off)
handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        bge   r12, r0, h_bad
        sw    r12, T_CAUSE(r13)
        lw    r12, T_IRQS(r13)
        addi  r12, r12, 1
        sw    r12, T_IRQS(r13)
        lbu   r12, P2+CMD(r0)
        pop   r13
        pop   r12
        rte
h_bad:  halt
