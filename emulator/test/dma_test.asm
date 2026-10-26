; dma_test.asm -- for the debug card's DMA (generators/sim_debug.py dma, D64):
; poll a word until a bus master writes it, then HALT with it (status: its
; low byte). The CPU keeps using the bus meanwhile: the arbiter must hand
; slot 0 its cycle between the CPU's, holding the CPU (STALL) if need be.
FLAG     = 0x00180000

        li    r3, FLAG
        sw    r0, 0(r3)
poll:   lw    r2, 0(r3)
        beqz  r2, poll
        mov   r1, r2
        halt
