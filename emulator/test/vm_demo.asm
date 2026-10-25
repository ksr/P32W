; vm_demo.asm -- five protected processes under vmkernel.inc.
;
; A and B run the same code: each stores its own letter at the SAME virtual
; address (0x400000, its private data page), lets the timer switch to the
; other process, then reads it back and prints it -- 5 times. If the address
; spaces leaked, they would print each other's letters. Both also print a
; line through write() from their data page.
; C asks write() to print kernel memory (a kernel address passed as a user
; pointer): it must get -1, prints "C:EFAULT", then writes to its own code
; (read-only) and is killed (PROTECT).
; D reads a page it does not have (TLBMISS_R) and is killed.
; E jumps into its data page (not executable) and is killed (PROTECT).
        .include "vmkernel.inc"

NPROCS = 5

        .org  0x8000
UCODE_START = .
proc_entries:
        .word proc_a, proc_b, proc_c, proc_d, proc_e

proc_a: li    r10, 'A'
        jmp   twin
proc_b: li    r10, 'B'
        jmp   twin

twin:   li    r8, UVA_DATA
        li    r9, 5             ; rounds
tw_loop:
        sb    r10, 0(r8)        ; my letter at the shared virtual address
        li    r4, 300           ; outlast a time slice: the other process runs
tw_spin:
        addi  r4, r4, -1
        bnez  r4, tw_spin
        lbu   r2, 0(r8)         ; still mine?
        li    r1, SYS_PUTC
        syscall
        addi  r9, r9, -1
        bnez  r9, tw_loop
        sb    r10, 16(r8)       ; build "<letter>:ok\n" in the data page
        li    r4, ':'
        sb    r4, 17(r8)
        li    r4, 'o'
        sb    r4, 18(r8)
        li    r4, 'k'
        sb    r4, 19(r8)
        li    r4, '\n'
        sb    r4, 20(r8)
        li    r1, SYS_WRITE
        addi  r2, r8, 16
        li    r3, 5
        syscall
        li    r1, SYS_EXIT
        syscall

proc_c: li    r1, SYS_WRITE     ; a kernel address as a user pointer
        li    r2, KVARS
        li    r3, 4
        syscall
        li    r4, -1
        bne   r1, r4, c_bad
        li    r1, SYS_WRITE
        la    r2, msg_efault    ; (code pages are readable)
        li    r3, 9
        syscall
        jmp   c_next
c_bad:  li    r1, SYS_PUTC
        li    r2, '?'
        syscall
c_next: la    r4, proc_c
c_store: sw   r0, 0(r4)         ; write own code: PROTECT, killed
        li    r1, SYS_EXIT
        syscall

proc_d: li    r4, 0x00500000    ; not mapped for anyone
d_load: lw    r2, 0(r4)         ; TLBMISS_R, killed
        li    r1, SYS_EXIT
        syscall

proc_e: li    r4, UVA_DATA      ; data page: no execute permission
        jr    r4                ; PROTECT on the fetch, killed

msg_efault:
        .ascii "C:EFAULT\n"
        .align 4
UCODE_END = .
