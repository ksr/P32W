# software/

Programs for the P32W itself.

| | |
|---|---|
| [cmd/](cmd/) | **the P32W Unix commands** (C): `init`, `sh`, `ls`, `cat`, `cp`, `mv`, `rm`, `ln`, `mkdir`, `rmdir`, `pwd`, `grep`, `wc`, `sort`, `uniq`, `head`, `tail`, `tee`, `ps`, `kill`, `chmod`, `chown`, `date`, `stty`, `halt`, ... |
| [libc/](libc/), [include/](include/) | **the C library** (M2, D22): strings, ctype, malloc, stdio / printf, stdlib, setjmp, system calls, signals, directories, time |
| [sa/crt0_sa.s](sa/crt0_sa.s) | the stand-alone runtime: C programs on the bare machine (the compiler's tests) |
| [root.manifest](root.manifest) | the root filesystem: what `make disk` puts where |
| [tests/](tests/) | scripted P32W Unix sessions (`run_session.sh`, in `make test`) |
| [monitor/monitor.asm](monitor/monitor.asm) | **the boot ROM monitor** — the first program at reset: console on UART 0 (9600 8N1); dump and modify memory, call a program, load sectors from the CompactFlash card, boot from it; catches exceptions and reports them |
| [lib/p32io.inc](lib/p32io.inc) | UART, CompactFlash and port routines (the programmer's guide's) |

`make` builds the library and the commands; `make disk` builds
`build/disk.img`: the kernel (`kernel/`) in the boot area and the root
filesystem (`tools/p32fs.py build`). Boot it with the monitor's `b`.

The monitor and the library are tested by `make test` on both emulators: `emulator/test/lib_test.asm`
exercises the library; a scripted monitor session boots a program from a disk
image and must match `emulator/test/monitor.expect` exactly.

## The monitor

```
python3 assembler/p32asm.py software/monitor/monitor.asm -o monitor.bin
python3 emulator/test/mkbootdisk.py program.bin disk.img     # a bootable disk
emulator/p32emu -d disk.img monitor.bin                     # try it
```

| Command | |
|---|---|
| `d ADDR [LEN]` | dump memory as words (hex; default 16 bytes) |
| `m ADDR VALUE` | store a word |
| `g ADDR` | call a program; it may `RET` to the monitor, which shows its `r1` |
| `l LBA COUNT ADDR` | load sectors from the card |
| `b` | boot: sector 0 holds `"P32B"`, the sector count, the load address and the entry point; sectors 1.. hold the program |
| `?` / `q` | help / `HALT` |

An exception (a bad address, an illegal instruction in a called program)
prints `CAUSE`, `EPC` and `BADADDR` and returns to the prompt: the monitor
sets `VBR` to its own handler. It keeps its line buffer and stack at
`0x001F_FC00` down, at the top of the first 2 MiB bank (which every
machine has, D60), so programs have `0x0010_0000`–`0x001F_7FFF`.

On the hardware the monitor's image is what goes into the boot ROM: split
into byte lanes for the four flash chips (or written in place by the debug
card's host link with the PROG jumper fitted).
