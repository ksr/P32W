; p32io.inc -- I/O card routines: UARTs, CompactFlash, ports (D50).
; Register map: docs/programmers-guide.md, docs/architecture.md.
; Convention (the guide's): arguments in r2-r5, result in r1, return with RET;
; these routines clobber r10-r14 only. Supervisor mode (the I/O page traps
; in user mode). Tested by emulator/test/lib_test.asm on both emulators.

U0       = 0xFFFFFF00           ; UART 0 (console): registers 0-7
U1       = 0xFFFFFF08           ; UART 1
UTHR     = 0                    ; register offsets: data (THR / RBR), DLL with DLAB
UIER     = 1                    ;   interrupt enable, DLM with DLAB
UFCR     = 2                    ;   FIFO control (write) / interrupt ID (read)
ULCR     = 3                    ;   line control: 0x03 = 8N1, bit 7 = DLAB
ULSR     = 5                    ;   line status: bit 0 data ready, bit 5 THR empty
PORTIN   = 0xFFFFFF20
PORTOUT  = 0xFFFFFF21
CFDATA   = 0xFFFFEF00           ; CompactFlash task file: the disk card's port 0 (D50; window 6, D52)
CFERR    = 0xFFFFEF01           ;   error (read) / features (write)
CFCOUNT  = 0xFFFFEF02
CFLBA0   = 0xFFFFEF03
CFLBA1   = 0xFFFFEF04
CFLBA2   = 0xFFFFEF05
CFHEAD   = 0xFFFFEF06           ;   0xE0 | LBA 27:24 (LBA mode, drive 0)
CFCMD    = 0xFFFFEF07           ;   command (write) / status (read)
CF_BSY   = 0x80
CF_DRQ   = 0x08
CF_ERR   = 0x01

; uart_init: UART r2 (U0 or U1 address) to 8N1 with divisor r3
; (1.8432 MHz clock: 12 = 9600 baud, 1 = 115200), FIFOs on, interrupts off.
uart_init:
        li    r10, 0x80
        sb    r10, ULCR(r2)     ; DLAB on
        sb    r3, UTHR(r2)      ; DLL
        srli  r10, r3, 8
        sb    r10, UIER(r2)     ; DLM
        li    r10, 0x03
        sb    r10, ULCR(r2)     ; 8N1, DLAB off
        li    r10, 0x07
        sb    r10, UFCR(r2)     ; FIFOs on and cleared
        sb    r0, UIER(r2)      ; no interrupts
        ret

; uart_putc: character r2 to the console (UART 0), waiting for room.
uart_putc:
        lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 0x20
        beqz  r10, uart_putc
        sb    r2, U0+UTHR(r0)
        ret

; uart_getc: the next console character in r1, waiting for it.
uart_getc:
        lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 0x01
        beqz  r10, uart_getc
        lbu   r1, U0+UTHR(r0)
        ret

; uart_poll: r1 = the next console character, or -1 if none is waiting.
uart_poll:
        li    r1, -1
        lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 0x01
        beqz  r10, uart_poll_done
        lbu   r1, U0+UTHR(r0)
uart_poll_done:
        ret

; uart_puts: the 0-terminated string at r2 to the console.
uart_puts:
        mov   r13, r2
uart_puts_next:
        lbu   r12, 0(r13)
        beqz  r12, uart_puts_done
uart_puts_wait:
        lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 0x20
        beqz  r10, uart_puts_wait
        sb    r12, U0+UTHR(r0)
        addi  r13, r13, 1
        jmp   uart_puts_next
uart_puts_done:
        ret

; uart_puthex: r2 as 8 hex digits on the console.
uart_puthex:
        li    r13, 28           ; shift for the next digit
uart_puthex_next:
        srl   r12, r2, r13
        andi  r12, r12, 15
        addi  r12, r12, '0'
        slti  r11, r12, '9' + 1
        bnez  r11, uart_puthex_wait
        addi  r12, r12, 'A' - '9' - 1
uart_puthex_wait:
        lbu   r10, U0+ULSR(r0)
        andi  r10, r10, 0x20
        beqz  r10, uart_puthex_wait
        sb    r12, U0+UTHR(r0)
        addi  r13, r13, -4
        bge   r13, r0, uart_puthex_next
        ret

; cf_wait: wait while the card is busy; r1 = its status.
cf_wait:
        lbu   r1, CFCMD(r0)
        andi  r10, r1, CF_BSY
        bnez  r10, cf_wait
        ret

; cf_init: a card there? 16-bit transfers (D26: the card's own default;
; SET FEATURES 81h undoes an 8-bit mode left by earlier software -- a card
; that never had one may refuse it, which is fine). r1 = 0 if a card
; answered, else -1.
cf_init:
        lbu   r10, CFCMD(r0)
        andi  r10, r10, 0x40    ; DRDY: nothing there reads 0 (pulled-down adapter)
        beqz  r10, cf_init_none
        li    r10, 0x81
        sb    r10, CFERR(r0)    ; features: no 8-bit transfers
        li    r10, 0xEF
        sb    r10, CFCMD(r0)    ; SET FEATURES
cf_init_busy:
        lbu   r10, CFCMD(r0)
        andi  r11, r10, CF_BSY
        bnez  r11, cf_init_busy
        li    r1, 0
        ret
cf_init_none:
        li    r1, -1
        ret

; cf_read: sector r2 (LBA) into the 512 bytes at r3 (halfword-aligned).
; r1 = 0, or the status with ERR set.
cf_read:
        mov   r14, r3
        li    r10, 0x20         ; READ SECTOR
        jmp   cf_xfer
; cf_write: the 512 bytes at r3 (halfword-aligned) to sector r2. r1 = 0,
; or the error status.
cf_write:
        mov   r14, r3
        li    r10, 0x30         ; WRITE SECTOR
cf_xfer:
        li    r11, 1
        sb    r11, CFCOUNT(r0)
        sb    r2, CFLBA0(r0)
        srli  r11, r2, 8
        sb    r11, CFLBA1(r0)
        srli  r11, r2, 16
        sb    r11, CFLBA2(r0)
        srli  r11, r2, 24
        andi  r11, r11, 0x0F
        ori   r11, r11, 0xE0
        sb    r11, CFHEAD(r0)
        sb    r10, CFCMD(r0)
        mov   r13, r10          ; remember the direction
cf_xfer_drq:
        lbu   r1, CFCMD(r0)     ; wait for DRQ (or an error)
        andi  r11, r1, CF_ERR
        bnez  r11, cf_xfer_done
        andi  r11, r1, CF_BSY
        bnez  r11, cf_xfer_drq
        andi  r11, r1, CF_DRQ
        beqz  r11, cf_xfer_drq
        li    r12, 256              ; 256 halfwords: the data register is 16 bits (D26)
        li    r11, 0x20
        bne   r13, r11, cf_xfer_out
cf_xfer_in:
        lhu   r11, CFDATA(r0)
        sh    r11, 0(r14)
        addi  r14, r14, 2
        addi  r12, r12, -1
        bnez  r12, cf_xfer_in
        jmp   cf_xfer_end
cf_xfer_out:
        lhu   r11, 0(r14)
        sh    r11, CFDATA(r0)
        addi  r14, r14, 2
        addi  r12, r12, -1
        bnez  r12, cf_xfer_out
cf_xfer_end:
        lbu   r1, CFCMD(r0)     ; wait until done; ERR?
        andi  r11, r1, CF_BSY
        bnez  r11, cf_xfer_end
        andi  r11, r1, CF_ERR
        beqz  r11, cf_xfer_ok
cf_xfer_done:
        ret
cf_xfer_ok:
        li    r1, 0
        ret
