; vmkernel.inc -- protected multitasking kernel with virtual memory (stage C,
; docs/isa-system.md). Include it FIRST: it starts at address 0 (reset).
;
; The program supplies:
;   NPROCS        number of processes (1..8)
;   proc_entries  .word table of their entry points
;   UCODE_START   start of the user code (page-aligned, after the kernel)
;   UCODE_END     end of the user code
; Each process runs in USER mode in its own address space:
;   UCODE_START..UCODE_END   program code, shared, read + execute (ROM)
;   0x0040_0000              one private data page, read/write, zero-filled
;   0x007F_F000              one private stack page, read/write; SP starts
;                            at 0x0080_0000
; Every other user address is unmapped. Processes use the SAME virtual
; addresses for DIFFERENT physical memory, and cannot see each other or the
; kernel. The timer preempts every QUANTUM instructions (round robin).
;
; Page tables: one linear table per process covering user space 0..16 MiB
; (4096 entries in TLBLO format: PFN | X W V). The TLB refill handler is a
; fast path that saves only two registers.
;
; System calls: number in r1, arguments in r2/r3, result in r1.
;   0 exit()            end this process; HALT (status 0) after the last one
;   1 putc(ch)          write one character
;   3 write(buf, len)   write len bytes from the caller's memory; returns len,
;                       or -1 if any byte of buf is not mapped (nothing written)
;   4 yield()           give up the rest of this time slice
; Any other trap kills the process with a report:
;   "kernel: process N killed, cause C, pc P, addr A"
; and the others go on. A trap from the kernel itself is a panic.

CON       = 0xFFFFFF00
TCOUNT    = 0xFFFFFF10
TRELOAD   = 0xFFFFFF14
TCTRL     = 0xFFFFFF18
SSP_TOP   = 0x00500000          ; kernel stack: top of RAM (D50)
QUANTUM   = 500                 ; time slice in timer ticks (= instructions)
SYS_EXIT  = 0
SYS_PUTC  = 1
SYS_WRITE = 3
SYS_YIELD = 4
UVA_DATA  = 0x00400000
UVA_STACK = 0x007FF000
UVA_STACK_TOP = 0x00800000
PTE_V     = 1
PTE_W     = 2
PTE_X     = 4

KVARS     = 0x00100000          ; kernel variables (physical)
K_CUR     = 0                   ;   index of the running process
K_CURPCB  = 4                   ;   its record
K_ALIVE   = 8                   ;   processes not yet ended
K_CURPT   = 12                  ;   its page table
K_NEXTFRAME = 16                ;   next free physical page
PCBS      = 0x00100100          ; process records
PCB_SIZE  = 80
T_EPC     = 0                   ;   resume address; r1..r14 at 4 * n
T_USP     = 60
T_STATE   = 64                  ;   1 = live
T_PT      = 68                  ;   page table address
T_ASID    = 72
PTABLES   = 0x00110000          ; page tables, 16 KiB each
FRAMES    = 0x00200000          ; free physical pages from here

; ---------------------------------------------------------------- boot
v_boot: li    sp, SSP_TOP
        call  k_tlbinval
        la    r1, v_trap
        mtcr  VBR, r1
        li    r1, KVARS
        li    r2, FRAMES
        sw    r2, K_NEXTFRAME(r1)
        li    r9, 0             ; process index
vb_proc:
        li    r2, PCB_SIZE      ; r8 = its record
        mul   r8, r9, r2
        li    r2, PCBS
        add   r8, r8, r2
        slli  r7, r9, 14        ; r7 = its page table
        li    r2, PTABLES
        add   r7, r7, r2
        sw    r7, T_PT(r8)
        addi  r2, r9, 1         ; ASID = index + 1 (0 is never used)
        sw    r2, T_ASID(r8)
        li    r2, 4             ; saved r1..r14 = 0
vb_clr: add   r3, r8, r2
        sw    r0, 0(r3)
        addi  r2, r2, 4
        li    r3, T_USP
        bne   r2, r3, vb_clr
        la    r2, proc_entries
        slli  r3, r9, 2
        add   r2, r2, r3
        lw    r2, 0(r2)
        sw    r2, T_EPC(r8)
        li    r2, UVA_STACK_TOP
        sw    r2, T_USP(r8)
        li    r2, 1
        sw    r2, T_STATE(r8)
        li    r2, 0             ; page table: everything unmapped
        li    r3, 4096 * 4
vb_pt:  add   r4, r7, r2
        sw    r0, 0(r4)
        addi  r2, r2, 4
        bne   r2, r3, vb_pt
        li    r2, UCODE_START >> 12     ; code: shared ROM pages, read + execute
        li    r3, (UCODE_END + 0xFFF) >> 12
vb_code:
        slli  r4, r2, 12
        ori   r4, r4, PTE_V | PTE_X
        slli  r5, r2, 2
        add   r5, r5, r7
        sw    r4, 0(r5)
        addi  r2, r2, 1
        bne   r2, r3, vb_code
        li    r10, UVA_DATA     ; private data page
        call  vb_map_rw
        li    r10, UVA_STACK    ; private stack page
        call  vb_map_rw
        addi  r9, r9, 1
        li    r2, NPROCS
        bne   r9, r2, vb_proc
        li    r1, KVARS
        li    r2, NPROCS
        sw    r2, K_ALIVE(r1)
        addi  r2, r2, -1        ; "current" = the last, so the scheduler picks 0
        sw    r2, K_CUR(r1)
        li    r2, QUANTUM
        sw    r2, TRELOAD(r0)
        li    r2, 1
        mtcr  IMASK, r2         ; timer interrupt
        li    r2, 9             ; S = 1, PS = 0, PIE = 1: RTE enters user mode
        mtcr  STATUS, r2        ;   with interrupts enabled
        jmp   v_schedule

; vb_map_rw: map user page r10 to a fresh zero-filled physical page,
; read/write, in page table r7. Clobbers r2-r5.
vb_map_rw:
        li    r2, KVARS
        lw    r3, K_NEXTFRAME(r2)
        li    r4, 4096
        add   r4, r3, r4
        sw    r4, K_NEXTFRAME(r2)
        mov   r5, r3
vm_zero:
        sw    r0, 0(r5)
        addi  r5, r5, 4
        bne   r5, r4, vm_zero
        ori   r3, r3, PTE_V | PTE_W
        srli  r5, r10, 12
        slli  r5, r5, 2
        add   r5, r5, r7
        sw    r3, 0(r5)
        ret

; ---------------------------------------------------------------- trap entry
; Fast path: a TLB miss (causes 8-10) on a mapped page is refilled from the
; current page table with only r12/r13 saved; RTE re-executes the access.
v_trap: push  r12
        push  r13
        mfcr  r12, CAUSE
        addi  r12, r12, -8
        sltiu r12, r12, 3
        beqz  r12, v_slow
        mfcr  r12, BADADDR
        srli  r12, r12, 12      ; VPN
        sltiu r13, r12, 4096    ; covered by the table (below 16 MiB)?
        beqz  r13, v_slow
        slli  r12, r12, 2
        li    r13, KVARS
        lw    r13, K_CURPT(r13)
        add   r12, r12, r13
        lw    r12, 0(r12)       ; page-table entry = TLBLO
        andi  r13, r12, PTE_V
        beqz  r13, v_slow       ; not mapped: the slow path kills the process
        mtcr  TLBLO, r12
        tlbw                    ; TLBHI (VPN, I/D half, ASID) was set by the miss
        pop   r13
        pop   r12
        rte
v_slow: pop   r13
        pop   r12

; Full path: save the whole process state; then every register is free.
        push  r14
        li    r14, KVARS
        lw    r14, K_CURPCB(r14)
        sw    r1, 4(r14)
        sw    r2, 8(r14)
        sw    r3, 12(r14)
        sw    r4, 16(r14)
        sw    r5, 20(r14)
        sw    r6, 24(r14)
        sw    r7, 28(r14)
        sw    r8, 32(r14)
        sw    r9, 36(r14)
        sw    r10, 40(r14)
        sw    r11, 44(r14)
        sw    r12, 48(r14)
        sw    r13, 52(r14)
        pop   r1
        sw    r1, 56(r14)
        mfcr  r1, EPC
        sw    r1, T_EPC(r14)
        mfcr  r1, USP
        sw    r1, T_USP(r14)
        mfcr  r1, STATUS
        andi  r1, r1, 4         ; PS = 1: the kernel itself trapped
        bnez  r1, v_panic
        mfcr  r1, CAUSE
        blt   r1, r0, v_irq
        li    r2, 5
        bne   r1, r2, v_kill
        lw    r1, T_EPC(r14)    ; SYSCALL: resume after it
        addi  r1, r1, 4
        sw    r1, T_EPC(r14)
        lw    r1, 4(r14)        ; number
        lw    r2, 8(r14)        ; arguments
        lw    r3, 12(r14)
        beqz  r1, v_exit
        li    r4, SYS_PUTC
        beq   r1, r4, v_putc
        li    r4, SYS_WRITE
        beq   r1, r4, v_write
        li    r4, SYS_YIELD
        beq   r1, r4, v_schedule
        li    r4, -1
        sw    r4, 4(r14)
        jmp   v_resume

v_putc: sb    r2, CON(r0)
        sw    r0, 4(r14)
        jmp   v_resume

; write(buf = r2, len = r3): check every byte is mapped in the caller's
; page table, then copy through the translation. Never trusts buf as a
; physical address (closes the stage A hole).
v_write:
        li    r4, KVARS
        lw    r4, K_CURPT(r4)
        add   r6, r2, r3        ; end
        bltu  r6, r2, vw_bad    ; wrapped around
        mov   r5, r2
vw_chk: beq   r5, r6, vw_copy
        mov   r10, r5
        call  v_xlate
        bnez  r11, vw_bad
        addi  r5, r5, 1
        jmp   vw_chk
vw_copy:
        mov   r5, r2
vw_out: beq   r5, r6, vw_done
        mov   r10, r5
        call  v_xlate
        lbu   r10, 0(r10)
        sb    r10, CON(r0)
        addi  r5, r5, 1
        jmp   vw_out
vw_done:
        sw    r3, 4(r14)        ; result: len
        jmp   v_resume
vw_bad: li    r1, -1
        sw    r1, 4(r14)
        jmp   v_resume

; v_xlate: translate user address r10 through page table r4.
; Out: r11 = 0 and r10 = physical address, or r11 = 1 if not mapped.
; Clobbers r12.
v_xlate:
        srli  r11, r10, 12
        sltiu r12, r11, 4096
        beqz  r12, vx_bad
        slli  r11, r11, 2
        add   r11, r11, r4
        lw    r11, 0(r11)
        andi  r12, r11, PTE_V
        beqz  r12, vx_bad
        srli  r11, r11, 12
        slli  r11, r11, 12
        andi  r12, r10, 0xFFF
        or    r10, r11, r12
        li    r11, 0
        ret
vx_bad: li    r11, 1
        ret

v_irq:  li    r1, 3             ; acknowledge the timer, keep it running
        sw    r1, TCTRL(r0)
        jmp   v_schedule

v_kill: la    r10, v_msg_kill   ; "kernel: process N killed, cause C, pc P, addr A"
        call  k_puts
        li    r1, KVARS
        lw    r10, K_CUR(r1)
        call  k_putdec
        la    r10, v_msg_cause
        call  k_puts
        mfcr  r10, CAUSE
        call  k_putdec
        la    r10, v_msg_pc
        call  k_puts
        mfcr  r10, EPC
        call  k_puthex
        la    r10, v_msg_addr
        call  k_puts
        mfcr  r10, BADADDR
        call  k_puthex
        li    r10, '\n'
        sb    r10, CON(r0)
        li    r1, KVARS
        lw    r14, K_CURPCB(r1)
v_exit: sw    r0, T_STATE(r14)  ; the process ends
        li    r1, KVARS
        lw    r2, K_ALIVE(r1)
        addi  r2, r2, -1
        sw    r2, K_ALIVE(r1)
        bnez  r2, v_schedule
        li    r1, 0
        halt

v_panic:
        la    r10, v_msg_panic
        call  k_puts
        mfcr  r10, CAUSE
        call  k_putdec
        la    r10, v_msg_pc
        call  k_puts
        mfcr  r10, EPC
        call  k_puthex
        li    r10, '\n'
        sb    r10, CON(r0)
        mfcr  r1, CAUSE
        addi  r1, r1, 100
        halt

; ---------------------------------------------------------------- scheduler
v_schedule:
        li    r1, KVARS
        lw    r2, K_CUR(r1)
        li    r3, NPROCS
vs_next:
        addi  r2, r2, 1
        bne   r2, r3, vs_try
        li    r2, 0
vs_try: li    r4, PCB_SIZE
        mul   r5, r2, r4
        li    r4, PCBS
        add   r5, r5, r4
        lw    r6, T_STATE(r5)
        beqz  r6, vs_next
        sw    r2, K_CUR(r1)
        sw    r5, K_CURPCB(r1)
        lw    r6, T_PT(r5)      ; switch address space: page table + ASID
        sw    r6, K_CURPT(r1)
        lw    r6, T_ASID(r5)
        mtcr  ASID, r6
        mov   r14, r5
        li    r1, QUANTUM       ; a full time slice for the process
        sw    r1, TCOUNT(r0)
        li    r1, 3             ; ack + run
        sw    r1, TCTRL(r0)

v_resume:
        lw    r1, T_EPC(r14)
        mtcr  EPC, r1
        lw    r1, T_USP(r14)
        mtcr  USP, r1
        lw    r1, 4(r14)
        lw    r2, 8(r14)
        lw    r3, 12(r14)
        lw    r4, 16(r14)
        lw    r5, 20(r14)
        lw    r6, 24(r14)
        lw    r7, 28(r14)
        lw    r8, 32(r14)
        lw    r9, 36(r14)
        lw    r10, 40(r14)
        lw    r11, 44(r14)
        lw    r12, 48(r14)
        lw    r13, 52(r14)
        lw    r14, 56(r14)
        rte

        .include "klib.inc"

v_msg_kill:  .asciz "kernel: process "
v_msg_cause: .asciz " killed, cause "
v_msg_pc:    .asciz ", pc "
v_msg_addr:  .asciz ", addr "
v_msg_panic: .asciz "kernel: PANIC, cause "
        .align 4
