# The boot ROM monitor

**Status:** DESCRIBES THE PROGRAM AS BUILT (2026-10-26):
`software/monitor/monitor.asm` and the routines it includes from
`software/lib/p32io.inc`. Tested by `make test` (section 4f) on both
emulators against the recorded session in `emulator/test/monitor.expect`.

The monitor is the first program P32W runs: it is what the boot ROM
holds, so the CPU starts it at address 0 after every reset (see
[the boot sequence](boot.md)). It is deliberately small (under 2 KiB,
1,960 bytes today) and does only what is needed to look at a machine that
has just been built and to start a bigger program:

- talk on the console (UART 0, 9600 baud, 8N1);
- show and change memory a word at a time;
- call a program in memory;
- load sectors from the CompactFlash card, and boot from it;
- catch any exception and report it, instead of losing control.

It uses no interrupts, no MMU and no RAM beyond its own stack and line
buffer, so it works on a machine whose devices, interrupts or virtual
memory are not working yet: bring-up starts here.

## 1. Where it lives

**In the ROM.** The monitor's image is the boot ROM's contents: four
SST39SF020A flash chips on the memory card in window 0 (the card with its
ROM jumper open, D63), one chip per byte lane. Byte `k` of the image goes
into lane `k mod 4`'s chip at address `k / 4`. Programming them: split
`monitor.bin` into four lane files and write each chip in a programmer
(the memory card ties the flash chips' write line high, so they are not
written in place; D101).

**In RAM, while it runs:**

| Address | Use |
|---|---|
| `0x0000_0000` … `0x0000_07A7` | the monitor's code and strings (ROM) |
| `0x001F_FC00` … `0x001F_FC7F` | the line buffer: the command being typed (128 bytes) |
| below `0x001F_FC00` | the monitor's stack, growing down |
| `0x001F_F800` … `0x001F_F9FF` | `b`'s copy of the boot header sector |
| `0x0010_0000` … `0x001F_7FFF` | free for programs (the usual load address is `0x0010_0000`) |

Everything sits in the first 2 MiB bank, which every machine has (D60).
Before D60 the stack was at 9 MiB, and a machine with less memory could not
run the monitor. The ROM hides the first MiB of the RAM under it, so RAM
starts at `0x0010_0000`.

## 2. Using it

The monitor prints

```
P32W monitor -- ? for help
>
```

and reads a line: echoed as typed, Backspace (^H) or Delete removes a
character, Return ends the line, and anything past 126 characters is
ignored. **Numbers are hexadecimal**, without `0x`; upper or lower case.
Spaces separate them. An unknown command prints `?`, a missing number
`syntax? (? for help)`.

| Command | What it does |
|---|---|
| `d ADDR [LEN]` | **Dump** memory as words, four to a line, from `ADDR` rounded down to a multiple of 4; `LEN` bytes (default 16). Each word is a 32-bit load, so a dump of the I/O windows really reads the device registers. |
| `m ADDR VALUE` | **Store** the word `VALUE` at `ADDR` (rounded down to a multiple of 4) and print `ADDR = VALUE`. |
| `g ADDR` | **Call** the program at `ADDR` (below). When it returns, print `returned` and its r1. |
| `l LBA COUNT ADDR` | **Load** `COUNT` 512-byte sectors from the CompactFlash card, starting at sector `LBA`, into memory at `ADDR` (which must be a multiple of 2). Prints `loaded COUNT sectors at ADDR`. |
| `b` | **Boot** from the card (§3). |
| `?` | Help: the command list. |
| `q` | `HALT` with r1 = 0. In an emulator this ends the run; on the machine the CPU card's HALT LED lights and the clock stops until RESET. |

**A session** (the test's own, `emulator/test/monitor.expect`), booting a
one-sector program that prints `BOOTED` and returns `0x55`:

```
P32W monitor -- ? for help
> b
loaded 00000001 sectors at 00100000
entry 00100000
BOOTED
returned 00000055
> d 100000 8
00100000: 20200042 4820FF00
> g 100000
BOOTED
returned 00000055
> l 0 1 200000
loaded 00000001 sectors at 00200000
> d 200000 10
00200000: 42323350 00000001 00100000 00100000
> q
```

The last dump is the card's sector 0, the boot header: `"P32B"`, one
sector, load address and entry `0x0010_0000`.

## 3. Booting: `b`

The card's sector 0 is the **boot header**, five little-endian words:

| Word | Meaning |
|---|---|
| 0 | magic `0x4232_3350`, the bytes `P32B` |
| 1 | the number of sectors to load |
| 2 | where to load them |
| 3 | where to start |
| 4 | (P32W Unix's disks: the filesystem's first sector; the monitor ignores it) |

`b` checks for a card (`no disk` if the CF port is empty) and reads
sector 0. If word 0 is not the magic it prints `not a boot disk`.
Otherwise it loads word 1's sectors from sector 1 onwards to word 2's
address, prints `loaded …` and `entry …`, and **calls** word 3's address.
A disk error at any point prints `disk error` and the card's status byte,
then returns to the prompt.

To make a bootable card from any program:

```
python3 emulator/test/mkbootdisk.py program.bin disk.img [LOAD [ENTRY]]
```

(load and entry default to `100000`). P32W Unix's disk is made by
`tools/p32fs.py`, which puts the kernel in the boot area the same way.

## 4. Programs the monitor calls

`g` and `b` both use `CALLR`. The program starts with:

- **supervisor mode, interrupts off** (STATUS as after reset), IMASK 0;
- **`sp` = the monitor's stack**, with the return address on top (P32W's
  `CALL` pushes it, D6). A program that returns with `RET` comes back to
  the monitor, which prints its r1. It may use the stack freely below that.
- **`VBR` = the monitor's exception handler**, until the program sets its
  own (the kernel does, at once).
- the console UART initialised (9600 8N1, FIFOs on, interrupts off).

Registers are not preserved for it: the monitor needs nothing back except
`sp` (restored with the return) and r1. A program that never returns (the
kernel) simply takes over the machine.

## 5. Exceptions

The monitor sets `VBR` to its handler before anything else. Any exception
while it, or a program it called, is running (an address nothing answers,
which is a BUSERROR; a misaligned word; an illegal instruction; `BREAK`)
ends up there. The handler resets the stack, prints

```
exception, CAUSE 00000004 EPC 00100010 BADADDR 0FFFFFF0
```

(`isa-system.md` lists the causes: 4 is a bus error) and returns to the
prompt. A program that crashes therefore leaves the monitor running, and
its memory can be dumped afterwards. Interrupts stay off throughout, so an
interrupt cannot reach the handler.

## 6. How it works

The monitor is one assembly file of about 330 lines, plus the shared
device routines it includes:

- **The command loop** (`prompt`): reset `sp`, print `> `, `readline` into
  the buffer, skip spaces, dispatch on the first character. Each command
  parses its numbers with `gethex` (r1 the value, r2 the number of digits:
  0 means none), does its work and jumps back to `prompt`. Jumping back,
  rather than returning, resets the stack every time, so a command that
  fails part way leaves nothing behind.
- **The console** (`p32io.inc`): `uart_init`, `uart_putc`, `uart_getc`,
  `uart_puts`, `uart_puthex`. All of them poll the line status register
  (LSR bit 5 THR empty, bit 0 data ready) and clobber only r10–r14, by the
  programmer's guide's convention: arguments in r2–r5, the result in r1.
- **The disk** (`p32io.inc`): `cf_init` (is a card there; turn off 8-bit
  mode) and `cf_read` (one sector: the LBA into the task file, READ SECTOR
  20h, wait for DRQ, 256 halfword reads of the 16-bit data register, D26;
  wait for BSY to clear, check ERR). `disk_load` loops over the sectors.
- **Devices it touches:** UART 0 (`0xFFFF_FF00`) and the disk card's port 0
  (`0xFFFF_EF00`) at their default windows (D52). It needs no other card,
  and no interrupt, timer or MMU.

**Building and trying it:**

```
python3 assembler/p32asm.py software/monitor/monitor.asm -o monitor.bin
emulator/p32emu -d software/build/disk.img monitor.bin
```

**Possible later additions** (not built): a write-sector command, a
download over the serial line (Kermit or S-records) for a machine with no
card yet, an autoboot after a few seconds without a key.
