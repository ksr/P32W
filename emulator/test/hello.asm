; hello.asm -- smoke test: print a line on the console, HALT with r1 = 0.
CON     = 0xFFFFFF00            ; console data register (isa-v0.md section 5)

        li    sp, 0x00500000    ; top of RAM; full-descending stack (D6)
        la    r10, msg
        call  puts
        li    r1, 0
        halt

; puts: print the 0-terminated string at r10. Clobbers r10, r11.
puts:   lbu   r11, 0(r10)
        beqz  r11, puts_done
        sb    r11, CON(r0)
        addi  r10, r10, 1
        jmp   puts
puts_done:
        ret

msg:    .asciz "P32W lives! instruction-level reference emulator\n"
