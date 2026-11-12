# emulator/

The two P32W emulators. The full user guide and theory of operation is
[../docs/emulators.md](../docs/emulators.md); this page is a summary.

`p32emu.c` — the **instruction-level reference emulator** (D7). It decodes
each instruction and performs its effect directly: ISA v0 with stage A
traps and privilege, stage B interrupts and timer, and stage C virtual
memory ([../docs/isa-system.md](../docs/isa-system.md)). It is the golden
model that the microcode-level emulator, the FPGA ALU, the gate-level
model and the hardware are checked against, and the fast one for running
P32W Unix: 23–163 million instructions a second on an M3 Pro (emulators.md
§1). Emulated time runs at 1 MHz: one timer tick per instruction.

`p32uemu.c` — the **microcode-level emulator** (D14). It runs the real
control-store image from `../microcode/p32ucode.py` one microcycle at a
time over a card-level model (register SRAM with the r15 bank rule, PC
counter, ALU latches, MAR/MDR with byte lanes, split TLB, sequencer). It
knows no instruction semantics. `make test` runs programs on both and
requires identical state at every instruction (`-s` traces), identical
output and identical exit status. It boots P32W Unix too, 2 to 8 times
more slowly.

`p32io.h` — **the devices**, one model included by both emulators (D50,
D52): the I/O card (four TL16C550 UARTs, the timer, the input and output
ports, the DS12C887 real-time clock (D23), two USB ports and the network
coprocessor), the disk card (two CompactFlash sockets and two IDE channels,
True-IDE task files, 16-bit data transfers, D26) and the video card (D53; its HD63484 chip in `p32acrtc.h`, D103),
with the cards' bus rules (byte registers answer byte accesses only, the
timer word accesses only; anything else is BUSERROR).

`p32opc.h` is generated — never edit it; edit `microcode/p32isa.py`.

## Build and run

```sh
make            # generates p32opc.h, the microcode image and p32uc.h; builds both emulators
make test       # the whole project's tests (see below)
./p32emu  [-t] [-m] [-s FILE] [-l N] [I/O options] image.bin
./p32uemu [-u ../microcode/p32ucode.bin] [-s FILE] [-l N] [-c] [I/O options] image.bin
```

Both emulators:

- `-l N` — stop after N instructions (default 100000000; no limit when the
  console is a terminal, so an interactive session runs until ^] or
  `halt`; 0 = no limit). A stop prints `STOP: instruction limit (N)
  reached` on stderr.
- `-s FILE` — write the architectural state at every instruction start
  (the same format from both).

`p32emu` only: `-t` traces every instruction (address, mode S/U, word,
disassembly) and every trap to stderr; `-m` prints TLB statistics at exit,
with the miss counts of larger and 2-way "what if" TLBs (emulators.md §3);
`-U IMG` plugs a USB drive holding IMG into the I/O card's first CH376S
(D43; `FF50`/`FF51`, IRQ6), `-U1 IMG` into the second (`FF54`/`FF55`),
and `-Uout N` pulls it out after N sectors have moved; `-M N` fits N
memory cards (1, the default, to 4; 16 MiB each, in windows 0 to N − 1,
D52): above them RAM is a BUSERROR, so the kernel's memory probe is tested
as the hardware will test it — in `p32uemu` too; `-k FILE` writes a kernel profile at exit, the instructions run at each
kernel address and in user mode, which `tools/kprof.py FILE` totals by
function (from `kernel/unix.map`; static functions count under the one
before them).

Both emulators: `-N` starts the I/O card's network coprocessor (D45;
`FF52`/`FF53`, IRQ7): the mailbox protocol of docs/net-mailbox.md, carried out with the
Mac's own sockets, so P32W reaches the real network. 10.0.2.2 is the Mac;
P32W's listening ports below 1024 appear on the Mac 8000 higher (`-Nport N`
changes that); TLS goes through `tools/p32tls.py`, trusting the Mac's
certificates or `-Nca FILE`. `test/net_peer.py` runs `test/net_model.c`
against servers of its own.

**The long tests on the hardware models:** `sh test/hwtests.sh
[MICROPYTHON_TESTS_DIR]` runs every recorded P32W Unix session on `p32uemu`,
the full set of programs at gate level, and, given the release's `tests`
directory, MicroPython's 645 tests on `p32uemu` against the host reference
build (`tools/mptests.py`). It takes about 45 minutes (2026-10-17: 24 of 24
passed); `make test` runs the quick part of the same checks.

`p32uemu` only: `-u FILE` the control-store image; `-c` ticks the timer once
per microcycle, as the hardware will, instead of once per instruction (the
default, which matches p32emu).

The I/O options (both):

| Option | |
|---|---|
| `-d IMG` | a CompactFlash disk image (raw 512-byte sectors, read and written) in the disk card's first CF socket |
| `-d1 IMG`; `-d2`, `-d2s`; `-d3`, `-d3s` | the second CF socket; IDE A's master and slave; IDE B's |
| `-a OUT`, `-A IN` | UART 1's output and input as files (UART 0 is stdin/stdout) |
| `-T PORT` | UART 1 on a TCP port of localhost: a second terminal (`tools/p32term.py PORT`, or `nc localhost PORT`) |
| `-a2 -A2 -T2`, `-a3 -A3 -T3` | the same for UARTs 2 and 3 |
| `-V FILE` | the video card's screen at exit: its 640 × 480 dots to FILE.pbm, the console's 25 rows of text (read from the dots) to FILE |
| `-K FILE` | the PS/2 keyboard types FILE's text |
| `-Km B0,B1,B2` | the PS/2 mouse sends this packet (hex) |
| `-p N` | the value the input port reads (default 0) |
| `-w` | for scripts: feed the console's input a line at a time, each after a prompt (`# $ > login: Password:` and the others in p32io.h) |
| `-P STR` | a prompt for `-w` (the first `-P` replaces the defaults) |
| `-R SECS` | the real-time clock starts at SECS (seconds since 1970, UTC) and follows emulated time; without it, it starts at the host's time |

Exit status: `r1 & 0xFF` on `HALT`; 255 on an unhandled exception, the
instruction limit, or a usage or load error.

## Behaviour worth knowing

- **Memory map** (D52): 1 MiB ROM at 0, RAM from `0x0010_0000` on 1 to 4
  memory cards of 16 MiB (`-M`), the
  device registers in the top 32 KiB of the address space (the video card, revision 2, has no memory on the bus: D103). User and supervisor modes with a banked
  r15; exceptions trap through `VBR`; interrupts are taken between
  instructions; a software-managed split TLB (64 + 64 entries,
  direct-mapped, ASID-tagged, D12/D13) translates every user-mode access,
  and supervisor mode is untranslated.
- **Time:** the timer ticks once per instruction, so interrupt timing is
  exactly repeatable (D11). `WAIT` skips straight to the timer's expiry, and
  the HALT line reports the skipped ticks as "idle ticks". The real-time
  clock follows emulated time (one microsecond per tick).
- **No handler installed:** while `VBR` = 0 an exception or interrupt stops
  the emulator with a register dump (D10). A `WAIT` that can never wake up
  stops it too.

## Tests (`test/run_tests.sh`, run by `make test`)

The script is the whole project's regression suite. It runs about 130
checks in these groups (numbered as in the script):

| § | What |
|---|---|
| 1–5 | the ISA: exact encodings, every instruction (self-checking), console I/O, Fibonacci and primes against Python references, stage A traps, stage B interrupts and a pre-emptive kernel, stage C virtual memory and protected processes, the I/O card and its library, the boot monitor, exceptions stopping the emulator |
| 6 | the hardware models: the microcode emulator against the reference on every program, the FPGA ALU's Verilog, the instruction reference up to date |
| 7 | the bus definition ([../docs/bus.md](../docs/bus.md)): consistent with the microcode, every radial line two-ended, the document up to date |
| 12–15 | the toolchain on the host: assembler errors, relocatable objects, the linker and archives, p32cc's test programs against the host's C compiler, the disk-image tool and its fsck |
| 16 | P32W Unix in p32emu: recorded sessions (`software/tests/session*.in` and `.expect`) covering boot, the shell, users, signals, history, the manual, make, BASIC, SQL, administration; the native toolchain (`cc_test.sh`: the same assembly as p32cc for every C file, and self-hosting byte for byte); the system's own tests on P32W (`/usr/tests`) |
| 17 | the archivers (tar, gzip, zip) against the host's |

The sessions boot a private image (`software/tests/run_session.sh`), never
`software/build/disk.img`.
