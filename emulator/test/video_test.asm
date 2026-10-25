; video_test.asm -- the video card (D50; docs/architecture.md 4a)
; as p32io.h models it: its ID, the text and overlay RAM, the VGA blanking
; status, the PS/2 keyboard and mouse. Self-checking: HALT with r1 = 0, or
; with the number of the failing case. Run with -K KEYS (KEYS holding "aB")
; and -V SCREEN: SCREEN's first line is then "HELLO P32W", SCREEN.pbm the
; overlay with its top line drawn.

VCTL     = 0xFFFFDF00           ; control: 1 TEXT, 2 OVERLAY, 4 XOR, 8 CURSOR
VSTAT    = 0xFFFFDF01           ; status: 1 VBLANK, 2 DE
VROW     = 0xFFFFDF02
VCOL     = 0xFFFFDF03
KDATA    = 0xFFFFDF08
KSTAT    = 0xFFFFDF09
MDATA    = 0xFFFFDF0C
MSTAT    = 0xFFFFDF0D
VIE      = 0xFFFFDF10
VID      = 0xFFFFDFF0           ; card ID (3)
VISR     = 0xFFFFDFF4
TEXT     = 0xFE050000           ; 25 x 80 cells: character, attribute
OVERLAY  = 0xFE058000           ; 480 lines x 80 bytes

        li    sp, 0x00500000
        li    r1, 0

        addi  r1, r1, 1         ; 1  the card's ID is 3; reset: everything off
        lbu   r2, VID(r0)
        li    r3, 3
        bne   r2, r3, fail
        lbu   r2, VCTL(r0)
        bnez  r2, fail
        addi  r1, r1, 1         ; 2  text: "HELLO P32W" in row 0, attribute 0
        la    r10, hello
        li    r9, TEXT
t2:     lbu   r11, 0(r10)
        beqz  r11, t2done
        sh    r11, 0(r9)        ; (a halfword: the character and attribute 0)
        addi  r10, r10, 1
        addi  r9, r9, 2
        jmp   t2
t2done: li    r9, TEXT
        lbu   r2, 0(r9)
        li    r3, 0x48          ; 'H'
        bne   r2, r3, fail
        lw    r2, 0(r9)         ; a word: 'H', 0, 'E', 0
        li    r3, 0x00450048
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 3  the overlay's top line, in words
        li    r9, OVERLAY
        li    r4, -1
        li    r5, 20
t3:     sw    r4, 0(r9)
        addi  r9, r9, 4
        addi  r5, r5, -1
        bnez  r5, t3
        li    r9, OVERLAY
        lbu   r2, 79(r9)
        li    r3, 0xFF
        bne   r2, r3, fail
        lbu   r2, 80(r9)        ; (line 1 untouched)
        bnez  r2, fail
        addi  r1, r1, 1         ; 4  control and cursor read back
        li    r4, 3
        sb    r4, VCTL(r0)
        li    r4, 24
        sb    r4, VROW(r0)
        li    r4, 79
        sb    r4, VCOL(r0)
        lbu   r2, VCTL(r0)
        li    r3, 3
        bne   r2, r3, fail
        lbu   r2, VCOL(r0)
        li    r3, 79
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 5  VBLANK comes and goes (a frame is 16.7 ms)
        li    r6, 100000
t5a:    lbu   r2, VSTAT(r0)     ; wait for blanking
        andi  r2, r2, 1
        addi  r6, r6, -1
        beqz  r6, fail
        beqz  r2, t5a
t5b:    lbu   r2, VSTAT(r0)     ; then for the picture again
        andi  r2, r2, 1
        addi  r6, r6, -1
        beqz  r6, fail
        bnez  r2, t5b
        addi  r1, r1, 1         ; 6  the keyboard types "a": 1C, F0 1C
        lbu   r2, KSTAT(r0)
        andi  r2, r2, 1
        beqz  r2, fail
        li    r7, 0x1C
        call  key
        li    r7, 0xF0
        call  key
        li    r7, 0x1C
        call  key
        addi  r1, r1, 1         ; 7  "B": shift 12, 32, F0 32, F0 12
        li    r7, 0x12
        call  key
        li    r7, 0x32
        call  key
        addi  r1, r1, 1         ; 8  its interrupt: level 4, status bit 0, when enabled
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, fail
        li    r4, 1
        sb    r4, VIE(r0)
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        beqz  r2, fail
        lw    r2, VISR(r0)
        li    r3, 1
        bne   r2, r3, fail
        li    r7, 0xF0          ;    the rest of "B"; then nothing left, no interrupt
        call  key
        li    r7, 0x32
        call  key
        li    r7, 0xF0
        call  key
        li    r7, 0x12
        call  key
        lbu   r2, KSTAT(r0)
        bnez  r2, fail
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, fail
        addi  r1, r1, 1         ; 9  the mouse, reset (FF): FA AA 00
        li    r4, 0xFF
        sb    r4, MDATA(r0)
        lbu   r2, MSTAT(r0)
        beqz  r2, fail
        lbu   r2, MDATA(r0)
        li    r3, 0xFA
        bne   r2, r3, fail
        lbu   r2, MDATA(r0)
        li    r3, 0xAA
        bne   r2, r3, fail
        lbu   r2, MDATA(r0)
        bnez  r2, fail
        lbu   r2, MSTAT(r0)
        bnez  r2, fail

        li    r1, 0
fail:   halt                    ; r1 = failing case number

; key: the next keyboard byte must be r7
key:    lbu   r2, KDATA(r0)
        bne   r2, r7, keybad
        ret
keybad: halt

hello:  .asciz "HELLO P32W"
