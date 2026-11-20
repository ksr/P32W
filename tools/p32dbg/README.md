# tools/p32dbg — the P32W debugger

`p32dbg` (D97) runs P32W in either emulator and shows it in a browser tab: the
video card's screen, live, with its keyboard and its mouse; the serial console; the
registers, the code, memory and breakpoints; and, in the microcode-level
emulator, the CPU's insides cycle by cycle. The machine stays in C: started
with `-dbg`, `p32emu` or `p32uemu` runs a small debug server on 127.0.0.1
(`emulator/p32dbg.h`), and this package (Python, the standard library only)
drives it, from the page or from commands. Its design follows YACC1-D's
`y1dbg`.

```
python3 tools/p32dbg                     the browser UI: open http://127.0.0.1:8832/
python3 tools/p32dbg -u                  the same with p32uemu: the microcode, cycle by cycle
python3 tools/p32dbg -d copy.img         another disk (else software/build/disk.img, as tools/p32w)
python3 tools/p32dbg --no-disk           the monitor alone
python3 tools/p32dbg -c "s 10; r; d"     commands, their output on stdout, then exit
python3 tools/p32dbg --script FILE       commands from a file (then the UI, with --ui web)
```

Options: `-u` (p32uemu), `-d IMG` (the disk; `P32W_DISK` too; it is written as
P32W writes it, so try things on a copy), `--no-disk`, `--emu-arg=ARG` (any
other emulator option, e.g. `--emu-arg=-M --emu-arg=2` for two memory cards),
`-c "cmd; cmd"` (`\;` keeps a semicolon in a command), `--script FILE`,
`--ui web|none`, `--port N` (default 8832; 0, any free one), `--timeout S`
(how long a scripted `c`, `until` or `waitfor` may run, default 60 s; then
paused), `--log FILE` (the emulator's stderr). The emulator ends when the
debugger does, however it ends.

**In a codespace** (`.devcontainer/README.md`): start it in the terminal;
GitHub forwards port 8832 and opens the page in a new tab, at a private
address only you can reach.

## The page

- **Run, Pause, Step, Over, µStep, Reset, Quit** at the top, with the state
  (green running, red stopped, grey halted), why it stopped, the PC with its
  symbol and the counts. It starts stopped, at the reset ("start", PC 0): Run
  starts it, and the monitor's `>` appears on the console. Keys typed on the
  console while it is stopped wait until it runs.
- **Screen.** The video card's picture as its chip has drawn it
  (docs/video-card-acrtc.md): 640 × 480 dots, one bit each; black until a
  program starts the chip. P32W Unix's console is on lines 40–439: 80 × 25
  characters of 8 × 16 dots. Click it and type: each key goes
  to the PS/2 keyboard as its set-2 scan codes, the make code on the way down
  and F0 and the code on the way up (arrows and the like with E0), so shift and
  ctrl work as on the real keyboard; the card's PS/2 section makes ASCII of them. With the card fitted (the debugger fits
  it), the screen is P32W Unix's `/dev/tty4`, with a login of its own: vi and
  top run there (D53).
  **The mouse** over the screen is the card's PS/2 mouse (D143): what it moves
  by goes to the mouse, a dot of the picture for a step, and its buttons with
  it (left, right, middle), so wm's pointer follows it and flight's stick. The
  page's own pointer is hidden while a program has the mouse: the one to
  watch is the machine's. The two are apart if the machine's was not where
  the page's came onto the screen: run it into a corner and they agree.
  What the mouse moves while the machine is stopped is dropped.
- **Console, UART 0.** The serial console: the monitor's `>`, the kernel's
  messages, `/dev/tty0`. Click it and type (Enter is CR, Backspace DEL, the
  arrows ESC [ A..D); a paste is typed too. It draws lines, not a terminal's
  screen: for full-screen programs use the screen.
- **Commands.** The command language below, with its output (↑ and ↓ recall).
- **Registers**, those changed since the last stop in yellow.
- **Code** around the PC, with the symbols of the monitor and the kernel; click
  a line to set or clear a breakpoint there.
- **CPU internals** (`-u`): the last microcycle — its microaddress (SEQ:OPCODE:
  STEP:COND, named: `ADD step 2`, `FETCH`, `TRAP step 1`), the microword and
  every field by name (from `microcode/p32ucode.py`), a one-line summary of
  what it did, the A, B and R buses, the ALU's latches, function and EQ / LT /
  LTU, MAR, MDR, IR, the next microaddress, and the 32 physical registers
  (r0..r14, USP, EPC, CAUSE, BADADDR, VBR, the microcode's T0..T7, the r0 sink,
  SSP).
- **Memory**: 256 bytes at an address kept as written (`sp`, `r4+10`,
  `ttread`), so it follows the register; as a load sees it, or physical.

## The command language

```
s [N]            step N instructions (default 1)       n           step over a CALL or CALLR (else s)
us [N]           step N microcycles (p32uemu)          c           continue: to a breakpoint or HALT
pause | stop     pause a run                           until ADDR  run to ADDR
reset            the reset (memory, disks, the picture in the video chip's RAM keep their contents)
b ADDR | bd ADDR | bd all | bl                         breakpoints (the next instruction is at ADDR)
r                registers                             set R=V [R=V...]   (r1..r15, sp, pc, status, epc, ...)
m [ADDR] [LEN]   memory (hex, LEN default 128; as a load sees it)     pm ADDR [LEN]  physical memory
poke ADDR BB...  write bytes                           d [ADDR] [N]  disassemble N (default 16; at PC)
trace [N]        the last N instructions run (default 20)
u                the microcycle last run (p32uemu): microaddress, the word's fields, the buses
screen           the video card's console as text, read from the picture's dots
key TEXT         type on UART 0, the serial console (\r Enter, \e Esc, \xHH a byte, \\ a backslash)
kbd TEXT         type on the PS/2 keyboard: the screen's console, /dev/tty4 (the same escapes)
mouse DX DY [B]  the PS/2 mouse moves DX steps right and DY up (decimal) with the buttons B down (1 left, 2 right, 4 middle)
console          UART 0's output since the last console
waitfor TEXT [S] run until UART 0 prints TEXT (at most S seconds; for scripts)
sym              where the symbols come from           sleep S     wait S seconds (a run runs on)
help | quit
```

An address is hex (`F06A`, `$F06A`, `0xF06A`), a symbol (`uart_puts`, `swtch`),
`r0`..`r15`, `sp`, `pc`, or one of them plus or minus a hex offset
(`ttread+10`). Counts are decimal.

- **Instruction boundaries.** The machine stops between instructions (after
  `us`, between microcycles), at the instruction the next fetch reads, after a
  pending interrupt has been taken. A breakpoint fires when the next
  instruction is at its address; `c` from a breakpoint runs that instruction
  first. A step counts instructions begun: after `reset`, `s 1` runs the first.
- **Addresses** are virtual in user mode, through the TLB as a load sees them
  (the D-TLB, else the I-TLB), and physical in supervisor mode, as the CPU's
  are. `??` marks memory nothing answers for, and device registers: the
  debugger never reads one, since a read can take its data.
- **HALT** halts; `reset` and then `c` or `s` start again from PC 0 (the
  register file keeps its contents, as the hardware's SRAM does).
- **Symbols**: the monitor's labels (assembled again with a listing) and the
  kernel's (`kernel/unix.map` and its objects, statics too, as `tools/uprof.py`).
- **In scripts** `c`, `until` and `n` wait for the stop (at most `--timeout`);
  in the page they return at once and the page follows the state.

## Files

| File | What it is |
|---|---|
| `emulator/p32dbg.h` | the debug server, in both emulators: the protocol, one JSON line a command |
| `emu.py` | starts the emulator with `-dbg 0`, reads its port, connects |
| `debugger.py` | symbols, addresses, the disassembler (from `microcode/p32isa.py`), the microword's fields (from `microcode/p32ucode.py`), the screen as text |
| `shell.py` | the command language |
| `web.py`, `web/index.html` | the browser UI: a local HTTP server (the standard library) and the page |
| `__main__.py` | the command line |
| `tests/` | the tests `make test` runs (section 22): a session of commands in both emulators, recorded, and the browser side (`web_test.py`) |
