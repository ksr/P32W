; monitor.asm -- the P32W boot ROM monitor: the first program at reset.
; Talks on the console (UART 0, 9600 8N1). Commands (numbers in hex):
;   d ADDR [LEN]        dump memory, words (default 16 bytes)
;   m ADDR VALUE        store a word
;   g ADDR              call ADDR; the program may RET to the monitor (r1 shown)
;   l LBA COUNT ADDR    load COUNT sectors from the CompactFlash card to ADDR
;   b                   boot from the card: sector 0 = header ("P32B", sector
;                       count, load address, entry); sectors 1.. = the image
;   ?                   help        q   HALT
; An exception (bad address, ...) prints CAUSE, EPC and BADADDR and returns
; to the prompt. RAM use: the line buffer and stack at the top of RAM.
; Build: python3 assembler/p32asm.py software/monitor/monitor.asm -o monitor.bin
LINE     = 0x008FFC00           ; 128-byte line buffer (below 9 MiB: in the first memory card, D52)
STACK    = 0x008FFC00           ; the stack grows down from the buffer
MAGIC    = 0x42323350           ; "P32B" little-endian

reset:  li    sp, STACK
        la    r2, exception
        mtcr  VBR, r2
        li    r2, U0
        li    r3, 12            ; 9600 baud
        call  uart_init
        la    r2, banner
        call  uart_puts

prompt: li    sp, STACK
        la    r2, prompt_s
        call  uart_puts
        call  readline
        li    r9, LINE          ; r9: parse pointer
        call  skipsp
        lbu   r8, 0(r9)
        beqz  r8, prompt
        addi  r9, r9, 1
        li    r3, 'd'
        beq   r8, r3, cmd_dump
        li    r3, 'm'
        beq   r8, r3, cmd_mod
        li    r3, 'g'
        beq   r8, r3, cmd_go
        li    r3, 'l'
        beq   r8, r3, cmd_load
        li    r3, 'b'
        beq   r8, r3, cmd_boot
        li    r3, '?'
        beq   r8, r3, cmd_help
        li    r3, 'q'
        beq   r8, r3, cmd_quit
        la    r2, what_s
        call  uart_puts
        jmp   prompt

; ---------------------------------------------------------------- commands
cmd_help:
        la    r2, help_s
        call  uart_puts
        jmp   prompt

cmd_quit:
        li    r1, 0
        halt

cmd_dump:                       ; d ADDR [LEN]
        call  gethex
        beqz  r2, syntax
        srli  r7, r1, 2         ; r7: address, word aligned
        slli  r7, r7, 2
        call  gethex
        li    r6, 16
        beqz  r2, dump_line
        mov   r6, r1            ; r6: bytes left
dump_line:
        mov   r2, r7
        call  uart_puthex
        li    r2, ':'
        call  uart_putc
        li    r5, 4             ; words per line
dump_word:
        li    r2, ' '
        call  uart_putc
        lw    r2, 0(r7)
        call  uart_puthex
        addi  r7, r7, 4
        addi  r6, r6, -4
        ble   r6, r0, dump_end
        addi  r5, r5, -1
        bnez  r5, dump_word
        call  newline
        jmp   dump_line
dump_end:
        call  newline
        jmp   prompt

cmd_mod:                        ; m ADDR VALUE
        call  gethex
        beqz  r2, syntax
        srli  r7, r1, 2
        slli  r7, r7, 2
        call  gethex
        beqz  r2, syntax
        sw    r1, 0(r7)
        mov   r6, r1
        mov   r2, r7
        call  uart_puthex
        la    r2, eq_s
        call  uart_puts
        mov   r2, r6
        call  uart_puthex
        call  newline
        jmp   prompt

cmd_go:                         ; g ADDR
        call  gethex
        beqz  r2, syntax
        callr r1
        mov   r7, r1
        la    r2, ret_s
        call  uart_puts
        mov   r2, r7
        call  uart_puthex
        call  newline
        jmp   prompt

cmd_load:                       ; l LBA COUNT ADDR
        call  gethex
        beqz  r2, syntax
        mov   r7, r1            ; LBA
        call  gethex
        beqz  r2, syntax
        mov   r6, r1            ; count
        call  gethex
        beqz  r2, syntax
        mov   r5, r1            ; address
        call  disk_load
        jmp   prompt

cmd_boot:                       ; b
        call  cf_init
        bnez  r1, nodisk
        li    r2, 0
        li    r3, LINE - 1024   ; the header sector, below the line buffer
        call  cf_read
        bnez  r1, diskerr
        li    r3, LINE - 1024
        lw    r4, 0(r3)
        li    r2, MAGIC
        bne   r4, r2, noboot
        li    r7, 1             ; LBA 1
        lw    r6, 4(r3)         ; count
        lw    r5, 8(r3)         ; load address
        lw    r4, 12(r3)        ; entry
        push  r4
        call  disk_load
        pop   r4
        la    r2, entry_s
        call  uart_puts
        mov   r2, r4
        call  uart_puthex
        call  newline
        callr r4                ; the booted program may RET here
        mov   r7, r1
        la    r2, ret_s
        call  uart_puts
        mov   r2, r7
        call  uart_puthex
        call  newline
        jmp   prompt
noboot: la    r2, noboot_s
        call  uart_puts
        jmp   prompt

; disk_load: r6 sectors from LBA r7 to address r5; prints a summary.
disk_load:
        push  r6
        push  r5
        call  cf_init
        bnez  r1, nodisk
disk_next:
        beqz  r6, disk_done
        mov   r2, r7
        mov   r3, r5
        call  cf_read
        bnez  r1, diskerr
        addi  r7, r7, 1
        addi  r5, r5, 512
        addi  r6, r6, -1
        jmp   disk_next
disk_done:
        la    r2, loaded_s
        call  uart_puts
        pop   r5
        pop   r6
        mov   r2, r6
        call  uart_puthex
        la    r2, sectors_s
        call  uart_puts
        mov   r2, r5
        call  uart_puthex
        call  newline
        ret
nodisk: la    r2, nodisk_s
        call  uart_puts
        jmp   prompt
diskerr:
        la    r2, diskerr_s
        call  uart_puts
        mov   r2, r1
        call  uart_puthex
        call  newline
        jmp   prompt

syntax: la    r2, syntax_s
        call  uart_puts
        jmp   prompt

; ---------------------------------------------------------------- exceptions
exception:                      ; VBR: print CAUSE, EPC, BADADDR; back to the prompt
        li    sp, STACK
        la    r2, exc_s
        call  uart_puts
        mfcr  r2, CAUSE
        call  uart_puthex
        la    r2, epc_s
        call  uart_puts
        mfcr  r2, EPC
        call  uart_puthex
        la    r2, bad_s
        call  uart_puts
        mfcr  r2, BADADDR
        call  uart_puthex
        call  newline
        jmp   prompt

; ---------------------------------------------------------------- input
; readline: a line into LINE (0-terminated), with echo and backspace.
readline:
        li    r7, LINE
        li    r6, 0             ; length
rl_next:
        call  uart_getc
        li    r3, 13
        beq   r1, r3, rl_end
        li    r3, 10
        beq   r1, r3, rl_end
        li    r3, 8
        beq   r1, r3, rl_bs
        li    r3, 127
        beq   r1, r3, rl_bs
        li    r3, 126
        bge   r6, r3, rl_next   ; full: ignore
        add   r3, r7, r6
        sb    r1, 0(r3)
        addi  r6, r6, 1
        mov   r2, r1
        call  uart_putc
        jmp   rl_next
rl_bs:  beqz  r6, rl_next
        addi  r6, r6, -1
        la    r2, bs_s
        call  uart_puts
        jmp   rl_next
rl_end: add   r3, r7, r6
        sb    r0, 0(r3)
        call  newline
        ret

; skipsp: advance r9 over spaces.
skipsp: lbu   r3, 0(r9)
        li    r4, ' '
        bne   r3, r4, skipsp_done
        addi  r9, r9, 1
        jmp   skipsp
skipsp_done:
        ret

; gethex: the next hex number at r9 -> r1; r2 = digits read (0: none).
gethex: call  skipsp
        li    r1, 0
        li    r2, 0
gh_next:
        lbu   r3, 0(r9)
        addi  r4, r3, -'0'
        sltiu r5, r4, 10
        bnez  r5, gh_digit
        ori   r3, r3, 0x20      ; lower case
        addi  r4, r3, -'a'
        sltiu r5, r4, 6
        beqz  r5, gh_done
        addi  r4, r4, 10
gh_digit:
        slli  r1, r1, 4
        or    r1, r1, r4
        addi  r2, r2, 1
        addi  r9, r9, 1
        jmp   gh_next
gh_done:
        ret

newline:
        li    r2, 13
        call  uart_putc
        li    r2, 10
        jmp   uart_putc         ; (tail call)

banner:   .asciz "\r\nP32W monitor -- ? for help\r\n"
prompt_s: .asciz "> "
what_s:   .asciz "?\r\n"
syntax_s: .asciz "syntax? (? for help)\r\n"
eq_s:     .asciz " = "
ret_s:    .asciz "returned "
entry_s:  .asciz "entry "
loaded_s: .asciz "loaded "
sectors_s: .asciz " sectors at "
nodisk_s: .asciz "no disk\r\n"
diskerr_s: .asciz "disk error "
noboot_s: .asciz "not a boot disk\r\n"
exc_s:    .asciz "exception, CAUSE "
epc_s:    .asciz " EPC "
bad_s:    .asciz " BADADDR "
bs_s:     .byte 8, 32, 8, 0       ; backspace, space, backspace
help_s:   .ascii "d ADDR [LEN]      dump memory\r\n"
          .ascii "m ADDR VALUE      store a word\r\n"
          .ascii "g ADDR            call a program\r\n"
          .ascii "l LBA COUNT ADDR  load sectors from the disk\r\n"
          .ascii "b                 boot from the disk\r\n"
          .asciz "q                 halt\r\n"
          .align 4
          .include "../lib/p32io.inc"
