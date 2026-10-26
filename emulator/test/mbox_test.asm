; mbox_test.asm -- the network coprocessor's mailbox (D45, docs/net-mailbox.md)
; through the I/O card's latches and flags (D65): READY, a HELLO request
; byte by byte (each after TXE), its reply byte by byte (each after RXF),
; IEN written and read back. Run with -N. Self-checking: HALT with r1 = 0,
; or the failing case. The hardware's flags follow the coprocessor's own
; pace, so only the result is compared, not the instruction trace.

NDATA    = 0xFFFFFF52
NSTAT    = 0xFFFFFF53           ; read: status; write: control
RXF      = 0x01
TXE      = 0x02
IEN      = 0x08
READY    = 0x80

        li    sp, 0x00180000
        li    r1, 0
        addi  r1, r1, 1         ; 1  the coprocessor is running
        li    r5, 1000
c1:     lbu   r2, NSTAT(r0)
        andi  r2, r2, READY
        bnez  r2, c1ok
        addi  r5, r5, -1
        bnez  r5, c1
        jmp   fail
c1ok:   addi  r1, r1, 1         ; 2  HELLO: command 01, socket 0, no payload
        li    r4, 0x01
        call  put
        li    r4, 0
        call  put
        call  put
        call  put
        addi  r1, r1, 1         ; 3  the reply: status 0, socket 0, length 8
        call  get
        bnez  r2, fail
        call  get
        bnez  r2, fail
        call  get
        li    r3, 8
        bne   r2, r3, fail
        call  get
        bnez  r2, fail
        addi  r1, r1, 1         ; 4  the payload: "P32NET 1"
        la    r6, hello
        li    r7, 8
c4:     call  get
        lbu   r3, 0(r6)
        bne   r2, r3, fail
        addi  r6, r6, 1
        addi  r7, r7, -1
        bnez  r7, c4
        addi  r1, r1, 1         ; 5  nothing more: RXF clear
        lbu   r2, NSTAT(r0)
        andi  r2, r2, RXF
        bnez  r2, fail
        addi  r1, r1, 1         ; 6  IEN, written, reads back
        li    r4, IEN
        sb    r4, NSTAT(r0)
        lbu   r2, NSTAT(r0)
        andi  r2, r2, IEN
        beqz  r2, fail
        sb    r0, NSTAT(r0)
        lbu   r2, NSTAT(r0)
        andi  r2, r2, IEN
        bnez  r2, fail
        li    r1, 0
fail:   halt

; put: r4 into NDATA once TXE says the request latch is empty
put:    lbu   r2, NSTAT(r0)
        andi  r2, r2, TXE
        beqz  r2, put
        sb    r4, NDATA(r0)
        ret
; get: the next reply byte into r2, once RXF says one is there
get:    lbu   r2, NSTAT(r0)
        andi  r2, r2, RXF
        beqz  r2, get
        lbu   r2, NDATA(r0)
        ret

hello:  .ascii "P32NET 1"
