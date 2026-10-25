; swap_dma.asm -- SWAP against a DMA master (the bus lock, D51, D54), for
; p32uemu -D 0x00200000:PERIOD. The DMA master adds 1 to the word over and
; over; this program takes the word with SWAP, 0 in, again and again, and
; totals what it took. Every addition is then either in the total (r1 at
; HALT) or still in the word -- unless one landed between a SWAP's read and
; its write, which only -LOCK prevents. run_tests.sh checks
; r1 + the word = the additions (and that -Dnolock loses some).

WORD     = 0x00200000

        li    r6, WORD          ; (RAM starts at 0: the word too)
        li    r1, 0             ; the total
        li    r5, 3000          ; swaps
loop:   li    r2, 0
        swap  r2, 0(r6)         ; r2 = the word, the word = 0: one bus tenure
        add   r1, r1, r2
        addi  r5, r5, -1
        bnez  r5, loop
        halt
