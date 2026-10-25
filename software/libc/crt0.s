; crt0.s -- P32W Unix program start-up (M2, D22; os-design.md 3.2). The kernel
; enters _start in user mode with sp pointing at argc, then argv[] and envp[]
; (each NULL-terminated): main(argc, argv, envp), then exit(). A program run
; with --help alone shows its manual page instead (help.c).
        .text
        .global _start
_start: lw    r1, 0(sp)                 ; argc
        addi  r2, sp, 4                 ; argv
        slli  r3, r1, 2
        add   r3, r3, r2
        addi  r3, r3, 4                 ; envp: after argv's NULL
        la    r4, environ
        sw    r3, 0(r4)
        addi  sp, sp, -12               ; --help as the only argument: the
        sw    r1, 0(sp)                 ; manual page instead (help.c)
        sw    r2, 4(sp)
        sw    r3, 8(sp)
        call  __help
        lw    r1, 0(sp)
        lw    r2, 4(sp)
        lw    r3, 8(sp)
        addi  sp, sp, 12
        call  main
        call  exit
.Lhang: beq   r0, r0, .Lhang
