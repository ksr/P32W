; ps2boot_test.asm -- the video card's PS/2 section as a reset leaves it
; (docs/video-card-acrtc.md 4.6; D109): the ATmega328P is two seconds
; starting, and the emulators' model of its firmware has that. For the
; emulators only (the gate-level machine's ATmega is ready from the reset:
; it cannot run two seconds). Self-checking: HALT with r1 = 0, or with the
; number of the failing case. Run with -K KEYS, KEYS holding "k"; the
; machine's 1 MHz timer measures (a tick an instruction in p32emu, a
; microsecond with p32uemu -C).

PSTAT    = 0xFFFFDF08           ; byte, read: 1 FULL, 2 SRC, 4 BUSY
PCMD     = 0xFFFFDF08           ; byte, write: a command
PDATA    = 0xFFFFDF0C           ; byte, read: the PS/2 byte
TCOUNT   = 0xFFFFFF10           ; the timer: counts down, 1 MHz
TRELOAD  = 0xFFFFFF14
TCTRL    = 0xFFFFFF18
T0       = 0x7FFFFFFF

        li    sp, 0x00500000
        li    r1, 0
        li    r4, T0
        sw    r4, TRELOAD(r0)
        sw    r4, TCOUNT(r0)
        li    r4, 1
        sw    r4, TCTRL(r0)

        addi  r1, r1, 1         ; 1  after the reset: nothing waits, nothing is busy, nothing asks
        lbu   r2, PSTAT(r0)
        andi  r2, r2, 7
        bnez  r2, fail
        mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, fail

        addi  r1, r1, 1         ; 2  a command written now (01, interrupts on) is not taken: the bootloader
        li    r4, 0x01          ;    has the ATmega for about a second. BUSY all that time; then the sketch's
        sb    r4, PCMD(r0)      ;    setup() reads the latch and drops what it finds: BUSY clear between 1.0
        lbu   r2, PSTAT(r0)     ;    and 1.3 s after the reset
        andi  r2, r2, 4
        beqz  r2, fail
        call  pbusy
        call  now
        li    r3, 1000000
        blt   r2, r3, fail
        li    r3, 1300000
        bge   r2, r3, fail

        addi  r1, r1, 1         ; 3  the keyboard's and the mouse's resets take the firmware another second:
        lbu   r2, PSTAT(r0)     ;    the key typed long ago is in the latch between 2.0 and 2.3 s after the
        andi  r2, r2, 1         ;    reset, not before
        bnez  r2, fail
        call  pfull
        call  now
        li    r3, 2000000
        blt   r2, r3, fail
        li    r3, 2300000
        bge   r2, r3, fail

        addi  r1, r1, 1         ; 4  the 01 was dropped: FULL, and nothing asks. Written now it is taken, and
        mfcr  r2, IPEND         ;    level 4 asks
        andi  r2, r2, 0x10
        bnez  r2, fail
        li    r4, 0x01
        sb    r4, PCMD(r0)
        call  pbusy
        li    r5, 1000
ask1:   mfcr  r2, IPEND
        andi  r2, r2, 0x10
        bnez  r2, ask2
        addi  r5, r5, -1
        bnez  r5, ask1
        halt
ask2:   lbu   r2, PDATA(r0)
        li    r3, 0x6B          ; 'k'
        bne   r2, r3, fail

        addi  r1, r1, 1         ; 5  a command that talks to a device keeps the firmware some milliseconds, and
        li    r4, 0x12          ;    the next command waits in the latch that long: the keyboard's LEDs (two
        sb    r4, PCMD(r0)      ;    commands to the keyboard), then 02; BUSY clears twice, the second time
        call  pbusy             ;    3 ms or more after the first
        call  now
        mov   r8, r2
        li    r4, 0x02
        sb    r4, PCMD(r0)
        call  pbusy
        call  now
        sub   r2, r2, r8
        li    r3, 3000
        blt   r2, r3, fail
        li    r3, 12000
        bge   r2, r3, fail

        li    r1, 0
fail:   halt                    ; r1 = failing case number

; now: microseconds since the test began, in r2
now:    lw    r2, TCOUNT(r0)
        li    r3, T0
        sub   r2, r3, r2
        ret

; pfull: wait for FULL (r2, r5 used); pbusy: for BUSY to clear
pfull:  li    r5, 4000000
pfull1: lbu   r2, PSTAT(r0)
        andi  r2, r2, 1
        bnez  r2, pfull2
        addi  r5, r5, -1
        bnez  r5, pfull1
        halt
pfull2: ret
pbusy:  li    r5, 4000000
pbusy1: lbu   r2, PSTAT(r0)
        andi  r2, r2, 4
        beqz  r2, pbusy2
        addi  r5, r5, -1
        bnez  r5, pbusy1
        halt
pbusy2: ret
