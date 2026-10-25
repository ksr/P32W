; vm_test.asm -- self-checking test of stage C (docs/isa-system.md): the split
; I/D TLB, translation, permissions, misses, ASIDs, the 2 GiB user limit.
; TLB entries are written by hand (no page tables). User code lives in page
; VPN 4 (0x4000), identity-mapped in the I-TLB, execute-only.
; r1 = current case; a mismatch HALTs with r1 = that case (from user mode
; via SYSCALL r2 = 98). All pass: r1 = 0.
;
; The handler records CAUSE, EPC, BADADDR and TLBHI, counts traps, and
; continues after the faulting instruction -- or, for a fault on an
; instruction fetch, at the address in r9.

TREC     = 0x00100000           ; trap record (physical; read-only to the user)
T_CAUSE  = 0
T_EPC    = 4
T_BAD    = 8
T_TLBHI  = 12
T_COUNT  = 16
SSP_TOP  = 0x00500000
V = 1
W = 2
X = 4
I = 0x100                       ; TLBHI: I-TLB entry

        li    sp, SSP_TOP
        call  k_tlbinval_local
        la    r4, handler
        mtcr  VBR, r4
        li    r6, TREC
        sw    r0, T_COUNT(r6)
        li    r4, 0xCAFE0001    ; data seen through the translation
        li    r5, 0x00200000
        sw    r4, 0(r5)
        li    r4, 0x44          ; data in the page that shares a slot with the code
        li    r5, 0x00203000
        sw    r4, 0(r5)
        li    r4, 1
        mtcr  ASID, r4

; TLB entries for ASID 1                         (VPN -> index = VPN mod 64)
        li    r4, 0x00004000 | I | 1            ; I: code page 4 -> 4, execute only
        li    r5, 0x00004000 | V | X
        call  tlbset
        li    r4, 0x00011000 | I | 1            ; I: page 0x11 valid but not executable
        li    r5, 0x00201000 | V
        call  tlbset
        li    r4, 0x00010000 | 1                ; D: page 0x10 -> 0x200000, read/write
        li    r5, 0x00200000 | V | W
        call  tlbset
        li    r4, 0x00011000 | 1                ; D: page 0x11 -> 0x201000, read-only
        li    r5, 0x00201000 | V
        call  tlbset
        li    r4, 0x00012000 | 2                ; D: page 0x12, but for ASID 2
        li    r5, 0x00201000 | V | W
        call  tlbset
        li    r4, 0x0001F000 | 1                ; D: stack page 0x1F -> 0x202000
        li    r5, 0x00202000 | V | W
        call  tlbset
        li    r4, 0x00044000 | 1                ; D: page 0x44 -> 0x203000: index 4,
        li    r5, 0x00203000 | V                ;    the same slot as the code page
        call  tlbset
        li    r4, 0x00100000 | 1                ; D: the trap record, read-only
        li    r5, 0x00100000 | V
        call  tlbset

        li    r4, 0x00020000    ; user stack top (page 0x1F)
        mtcr  USP, r4
        li    r4, user
        mtcr  EPC, r4
        li    r4, 1             ; S = 1, PS = 0: RTE enters user mode
        mtcr  STATUS, r4
        li    r1, 0
        rte

; tlbset: write TLB entry TLBHI = r4, TLBLO = r5
tlbset: mtcr  TLBHI, r4
        mtcr  TLBLO, r5
        tlbw
        ret

; ---------------------------------------------------------------- back in supervisor
resume: addi  r1, r1, 1         ; 16 the user's store landed at the physical page
        li    r5, 0x00200004
        lw    r2, 0(r5)
        li    r3, 0x5A5A5A5A
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 17 TLBHI keeps only VPN, I and ASID bits
        li    r4, 0xFFFFFFFF
        mtcr  TLBHI, r4
        mfcr  r2, TLBHI
        li    r3, 0xFFFFF1FF
        bne   r2, r3, fail
        addi  r1, r1, 1         ; 18 TLBLO keeps only PFN, X, W, V
        mtcr  TLBLO, r4
        mfcr  r2, TLBLO
        li    r3, 0xFFFFF007
        bne   r2, r3, fail
        li    r1, 0
        halt
fail:   halt

; ---------------------------------------------------------------- handler
handler:
        push  r12
        push  r13
        li    r13, TREC
        mfcr  r12, CAUSE
        sw    r12, T_CAUSE(r13)
        mfcr  r12, EPC
        sw    r12, T_EPC(r13)
        mfcr  r12, BADADDR
        sw    r12, T_BAD(r13)
        mfcr  r12, TLBHI
        sw    r12, T_TLBHI(r13)
        lw    r12, T_COUNT(r13)
        addi  r12, r12, 1
        sw    r12, T_COUNT(r13)
        mfcr  r12, CAUSE
        li    r13, 5
        bne   r12, r13, h_notsc
        li    r13, 99
        beq   r2, r13, h_resume
        li    r13, 98
        beq   r2, r13, h_fail
        jmp   h_next
h_notsc:
        li    r13, 8            ; fetch miss, or PROTECT on a fetch (EPC = BADADDR):
        beq   r12, r13, h_fetch ;   continue at r9
        li    r13, 11
        bne   r12, r13, h_next
        mfcr  r12, EPC
        mfcr  r13, BADADDR
        bne   r12, r13, h_next
h_fetch:
        mtcr  EPC, r9
        jmp   h_ret
h_next: mfcr  r12, EPC          ; continue after the faulting instruction
        addi  r12, r12, 4
        mtcr  EPC, r12
h_ret:  pop   r13
        pop   r12
        rte
h_resume:
        pop   r13
        pop   r12
        jmp   resume
h_fail: halt

; local copy of the TLB invalidation (no kernel library in this test)
CON = 0xFFFFFF00
        .include "klib.inc"
k_tlbinval_local:
        jmp   k_tlbinval

; ---------------------------------------------------------------- user code
        .org  0x4000
user:   li    r6, TREC          ; (r6 is the user's own copy)
        addi  r1, r1, 1         ; 1  load through the translation: VA 0x10000 -> PA 0x200000
        li    r5, 0x00010000
        lw    r2, 0(r5)
        li    r3, 0xCAFE0001
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 2  store through it, read back
        li    r4, 0x5A5A5A5A
        sw    r4, 4(r5)
        lw    r2, 4(r5)
        bne   r2, r4, ufail
        addi  r1, r1, 1         ; 3  store to a read-only page: PROTECT, BADADDR = VA
        li    r5, 0x00011000
        sw    r4, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 11
        bne   r2, r3, ufail
        lw    r2, T_BAD(r6)
        bne   r2, r5, ufail
        addi  r1, r1, 1         ; 4  TLBHI = VPN | I=0 (data) | ASID 1
        lw    r2, T_TLBHI(r6)
        li    r3, 0x00011001
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 5  loading from the read-only page is allowed
        lw    r7, T_COUNT(r6)
        lw    r2, 0(r5)
        lw    r3, T_COUNT(r6)
        bne   r3, r7, ufail
        addi  r1, r1, 1         ; 6  load from an unmapped page: TLBMISS_R
        li    r5, 0x00030000
        lw    r2, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 9
        bne   r2, r3, ufail
        lw    r2, T_TLBHI(r6)
        li    r3, 0x00030001
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 7  store to it: TLBMISS_W
        sw    r0, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 10
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 8  an entry for another ASID does not match
        li    r5, 0x00012000
        lw    r2, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 9
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 9  jump to a page with no I-TLB entry: TLBMISS_X,
        la    r9, after9        ;    EPC = BADADDR = target, TLBHI.I = 1
        li    r4, 0x00010000
        jr    r4
after9: lw    r2, T_CAUSE(r6)
        li    r3, 8
        bne   r2, r3, ufail
        lw    r2, T_EPC(r6)
        bne   r2, r4, ufail
        lw    r2, T_TLBHI(r6)
        li    r3, 0x00010101
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 10 jump to a valid but non-executable page: PROTECT
        la    r9, after10
        li    r4, 0x00011000
        jr    r4
after10:
        lw    r2, T_CAUSE(r6)
        li    r3, 11
        bne   r2, r3, ufail
        lw    r2, T_TLBHI(r6)
        li    r3, 0x00011101
        bne   r2, r3, ufail
        addi  r1, r1, 1         ; 11 the upper 2 GiB: PRIVILEGE, BADADDR = address
        li    r5, 0x80000000
        lw    r2, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 2
        bne   r2, r3, ufail
        lw    r2, T_BAD(r6)
        bne   r2, r5, ufail
        addi  r1, r1, 1         ; 12 code page and data page in the same slot (index 4)
        lw    r7, T_COUNT(r6)   ;    coexist: no trap, correct data (D13)
        li    r5, 0x00044000
        lw    r2, 0(r5)
        li    r3, 0x44
        bne   r2, r3, ufail
        lw    r3, T_COUNT(r6)
        bne   r3, r7, ufail
        addi  r1, r1, 1         ; 13 the stack works through the translation
        li    r4, 0x13579BDF
        push  r4
        pop   r2
        bne   r2, r4, ufail
        addi  r1, r1, 1         ; 14 SWAP on a read-only page: PROTECT (the store),
        li    r5, 0x00011000    ;    and neither the word nor the register changes (D51)
        lw    r8, 0(r5)
        li    r4, 0x600DF00D
        swap  r4, 0(r5)
        lw    r2, T_CAUSE(r6)
        li    r3, 11
        bne   r2, r3, ufail
        li    r3, 0x600DF00D
        bne   r4, r3, ufail
        lw    r2, 0(r5)
        bne   r2, r8, ufail
        addi  r1, r1, 1         ; 15 SWAP through the translation exchanges the two
        li    r5, 0x00010000
        li    r4, 0x11112222
        sw    r4, 8(r5)
        li    r4, 0x33334444
        swap  r4, 8(r5)
        li    r3, 0x11112222
        bne   r4, r3, ufail
        lw    r2, 8(r5)
        li    r3, 0x33334444
        bne   r2, r3, ufail
        li    r2, 99            ; back to supervisor mode
        syscall
ufail:  li    r2, 98
        syscall
