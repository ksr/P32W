; mtkernel.inc -- preemptive round-robin multitasking kernel (stage B,
; docs/isa-system.md). Include it FIRST: it starts at address 0 (reset).
;
; The program supplies:
;   NTASKS        number of tasks (1..8)
;   task_entries  .word table of the tasks' entry points
; Each task runs in USER mode with its own stack and its own saved registers.
; The timer interrupts every QUANTUM instructions; the kernel saves the
; running task's state into its task record and resumes the next live task.
; Tasks never need to yield: preemption is by the timer alone.
;
; System calls: number in r1, argument in r2, result in r1; other registers
; are preserved.
;   0 exit()      end this task; when the last task ends, HALT with status 0
;   1 putc(ch)    write one character
;   4 yield()     give up the rest of this time slice
; Any other trap from a task kills that task (with a report); the others go
; on. A trap from the kernel itself is a panic (HALT, status 100 + cause).
;
; Stage-B limit: tasks share one address space. Since stage C, user
; addresses go through the TLB; this kernel identity-maps (virtual =
; physical) any page below 16 MiB on a miss, so tasks can still overwrite
; each other's memory. vmkernel.inc gives each process its own space.

CON       = 0xFFFFFF00
TCOUNT    = 0xFFFFFF10
TRELOAD   = 0xFFFFFF14
TCTRL     = 0xFFFFFF18
SSP_TOP   = 0x00500000          ; kernel stack: top of RAM (D50)
USTACK_TOP = 0x00400000         ; task i stack top = USTACK_TOP - i * 64K
QUANTUM   = 50                  ; time slice, in timer ticks (= instructions)
SYS_EXIT  = 0
SYS_PUTC  = 1
SYS_YIELD = 4

KVARS     = 0x00100000          ; kernel variables
K_CUR     = 0                   ;   index of the running task
K_CURTCB  = 4                   ;   address of its task record
K_ALIVE   = 8                   ;   number of tasks not yet ended
TCBS      = 0x00100100          ; task records, TCB_SIZE bytes each
TCB_SIZE  = 80
T_EPC     = 0                   ;   resume address
                                ;   r1..r14 at 4 * n
T_USP     = 60                  ;   user SP (r15)
T_STATE   = 64                  ;   1 = live, 0 = ended

; ---------------------------------------------------------------- boot
m_boot: li    sp, SSP_TOP
        call  k_tlbinval
        la    r1, m_trap
        mtcr  VBR, r1
        li    r2, 0             ; task index
        li    r3, TCBS          ; its record
        la    r4, task_entries
        li    r5, USTACK_TOP
        li    r6, NTASKS
mb_task:
        li    r7, 4             ; clear saved r1..r14 (RAM is undefined in hardware)
mb_clr: add   r8, r3, r7
        sw    r0, 0(r8)
        addi  r7, r7, 4
        li    r8, T_USP
        bne   r7, r8, mb_clr
        lw    r7, 0(r4)
        sw    r7, T_EPC(r3)
        sw    r5, T_USP(r3)
        li    r7, 1
        sw    r7, T_STATE(r3)
        addi  r3, r3, TCB_SIZE
        addi  r4, r4, 4
        li    r7, 0x10000
        sub   r5, r5, r7
        addi  r2, r2, 1
        bne   r2, r6, mb_task
        li    r1, KVARS
        sw    r0, K_CUR(r1)
        li    r2, TCBS
        sw    r2, K_CURTCB(r1)
        li    r2, NTASKS
        sw    r2, K_ALIVE(r1)
        li    r2, QUANTUM       ; timer: interrupt every QUANTUM ticks
        sw    r2, TRELOAD(r0)
        sw    r2, TCOUNT(r0)
        li    r2, 1
        mtcr  IMASK, r2         ; IRQ0
        sw    r2, TCTRL(r0)     ; start the timer
        li    r2, 9             ; S = 1, PS = 0, PIE = 1: RTE enters user mode
        mtcr  STATUS, r2        ;   with interrupts enabled
        li    r14, TCBS
        jmp   m_resume

; ---------------------------------------------------------------- trap entry
; Save the whole task state into its record first; then every register is
; free for the kernel.
m_trap: push  r14               ; supervisor stack (banked r15)
        li    r14, KVARS
        lw    r14, K_CURTCB(r14)
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
        sw    r1, 56(r14)       ; the task's r14
        mfcr  r1, EPC
        sw    r1, T_EPC(r14)
        mfcr  r1, USP
        sw    r1, T_USP(r14)
        mfcr  r1, STATUS
        andi  r1, r1, 4         ; PS = 1: the kernel itself trapped
        bnez  r1, m_panic
        mfcr  r1, CAUSE
        blt   r1, r0, m_irq     ; bit 31: interrupt
        addi  r2, r1, -8        ; TLB miss (causes 8-10): identity-map the
        sltiu r2, r2, 3         ;   page below 16 MiB, then re-execute
        beqz  r2, m_notmiss
        mfcr  r2, BADADDR
        srli  r3, r2, 24
        bnez  r3, m_kill
        srli  r2, r2, 12
        slli  r2, r2, 12
        ori   r2, r2, 7         ; V | W | X
        mtcr  TLBLO, r2
        tlbw
        jmp   m_resume
m_notmiss:
        li    r2, 5
        bne   r1, r2, m_kill    ; not a SYSCALL: kill the task
        lw    r1, T_EPC(r14)    ; resume after the SYSCALL
        addi  r1, r1, 4
        sw    r1, T_EPC(r14)
        lw    r1, 4(r14)        ; number
        lw    r2, 8(r14)        ; argument
        beqz  r1, m_exit
        li    r3, SYS_PUTC
        beq   r1, r3, m_putc
        li    r3, SYS_YIELD
        beq   r1, r3, m_schedule
        li    r3, -1            ; unknown call
        sw    r3, 4(r14)
        jmp   m_resume

m_putc: sb    r2, CON(r0)
        sw    r0, 4(r14)        ; result 0
        jmp   m_resume

m_irq:  li    r1, 3             ; acknowledge IRQ0, keep the timer running
        sw    r1, TCTRL(r0)
        jmp   m_schedule

m_kill: la    r10, m_msg_kill   ; "kernel: task N killed, cause C, pc P"
        call  k_puts
        li    r1, KVARS
        lw    r10, K_CUR(r1)
        addi  r10, r10, '0'
        sb    r10, CON(r0)
        la    r10, m_msg_cause
        call  k_puts
        mfcr  r10, CAUSE
        call  k_putdec
        la    r10, m_msg_pc
        call  k_puts
        mfcr  r10, EPC
        call  k_puthex
        li    r10, '\n'
        sb    r10, CON(r0)
        li    r1, KVARS
        lw    r14, K_CURTCB(r1)
                                ; fall through: the task ends
m_exit: sw    r0, T_STATE(r14)
        li    r1, KVARS
        lw    r2, K_ALIVE(r1)
        addi  r2, r2, -1
        sw    r2, K_ALIVE(r1)
        bnez  r2, m_schedule
        li    r1, 0             ; last task ended
        halt

m_panic:
        la    r10, m_msg_panic
        call  k_puts
        mfcr  r10, CAUSE
        call  k_putdec
        la    r10, m_msg_pc
        call  k_puts
        mfcr  r10, EPC
        call  k_puthex
        li    r10, '\n'
        sb    r10, CON(r0)
        mfcr  r1, CAUSE
        addi  r1, r1, 100
        halt

; ---------------------------------------------------------------- scheduler
; Round robin: the next live task after the current one (possibly itself).
m_schedule:
        li    r1, KVARS
        lw    r2, K_CUR(r1)
        li    r3, NTASKS
ms_next:
        addi  r2, r2, 1
        bne   r2, r3, ms_try
        li    r2, 0
ms_try: li    r4, TCB_SIZE
        mul   r5, r2, r4
        li    r4, TCBS
        add   r5, r5, r4
        lw    r6, T_STATE(r5)
        beqz  r6, ms_next       ; ended: try the next (K_ALIVE > 0 here)
        sw    r2, K_CUR(r1)
        sw    r5, K_CURTCB(r1)
        mov   r14, r5
        li    r1, QUANTUM       ; a full time slice for the task being dispatched:
        sw    r1, TCOUNT(r0)    ;   the timer kept ticking through the kernel's own
        li    r1, 3             ;   instructions, so without this a slow switch
        sw    r1, TCTRL(r0)     ;   could leave a task no time at all (ack + run)

; Resume the task whose record is in r14.
m_resume:
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
        rte                     ; to user mode, IE = PIE = 1

        .include "klib.inc"

m_msg_kill:  .asciz "\nkernel: task "
m_msg_cause: .asciz " killed, cause "
m_msg_pc:    .asciz ", pc "
m_msg_panic: .asciz "\nkernel: PANIC, cause "
        .align 4
