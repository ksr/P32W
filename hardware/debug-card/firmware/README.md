# The debug card's firmware

The program in the debug card's Raspberry Pi Pico 2 (A1): the card's host link, USB to the Mac
([docs/debug-card.md](../../../docs/debug-card.md), [its theory](../../../docs/debug-card-theory.md); D135).
It turns one command a line from the Mac into reads and writes on the card's local bus, and waits on the
card where its sequences need it. On the Mac, `tools/dbgcard.py` speaks to it.

| File | What |
|---|---|
| `dbgcard/core.c`, `core.h` | the firmware: every command; nothing of the Pico in it |
| `dbgcard/hal.h` | what it needs of a board: a local-bus write and read, CRESN, waits, text to the host |
| `dbgcard/pico.c` | `hal.h` on the Pico 2: GP0-18, its USB serial port, its timer; `main()` |
| `dbgcard/host.c` | `hal.h` on the Mac, for the tests: the card's gate-level netlist plays the board |
| `dbgcard/slices.h` | which slice and bit each line is: written by `generators/card_debug.py --firmware`, do not edit |
| `CMakeLists.txt`, `pico_sdk_import.cmake` | the build, with the Raspberry Pi Pico SDK |

## Building and loading

The Raspberry Pi Pico SDK 2.x and its compiler (`arm-none-eabi-gcc`), and `cmake`:

    export PICO_SDK_PATH=~/pico-sdk
    cd hardware/debug-card/firmware
    cmake -S . -B build && cmake --build build          # -> build/dbgcard.uf2

Hold the Pico's BOOTSEL button while plugging it into the Mac: it shows as a disk, `RP2350`; copy
`dbgcard.uf2` onto it. It restarts as a USB serial port, `/dev/cu.usbmodem…`.

## Tested before any card exists

`make test` runs this very `core.c` against the card's gate-level netlist (`generators/sim_dbgfw.py`), the Mac's
tool with it, in the machines `sim_debug.py` builds: the debug and memory cards alone; the whole machine running
a program beside DMA; captures with triggers; the CPU card on XCLK. `pico.c` is compiled against the SDK's
declarations (`emulator/test/pico_stub`); it is the one file only a real Pico tries out.

## The local bus

GP0-7 data (through the 74LVC245), GP8-14 XA0-6, GP15 `-XWR`, GP16 `-XRD`, GP17 XDIR, GP18 CRESN; GP26 the
card's 5 V, halved (ADC0, D136) (theory section 3). The times `pico.c` gives each operation, which the card asks for and nothing specified before
(theory section 15, item 3):

| Step | Time | Why |
|---|---|---|
| XA, XD steady before `-XWR` falls | 30 ns | registers' set-up; the TIL311s want XD 50 ns before the strobe ends |
| `-XWR` low | **70 ns** | at least one oscillator period (41.7 ns): STEP's preset must span an oscillator edge; **under eight** (333 ns), or a STEP passes phase 3 twice and gives two CLK periods (`sim_dbgfw.py strobe` shows both) |
| XD held after `-XWR` rises | 50 ns | the TIL311s: 40 ns |
| `-XRD` low to XD read | 200 ns | the decoders, a 74HCT541, the 74LVC245 |

## The commands

One a line, numbers in hex, upper or lower case. Each reply is any data lines, then one line beginning `OK`
or `ERR`. `HELP` lists them.

| Command | What |
|---|---|
| `ID`, `HELP` | the firmware; the commands |
| `PROBE` | is the card there, with power? (its 5 V first, then a reset; a capture in the SRAMs is lost) |
| `RESET` | CRESN low and back: every control register 0, a CPU card back on its own oscillator |
| `ST` | the status register decoded, CTRL0, CTRL1, the capture address, MRES |
| `CARDS` | which cards are in (`-CPUIN`, `-MMUIN`, D102), PWROK, the card's 5 V (`vcc=4.98V`, D136) |
| `LIVE [slices]` | the lines as they stand, through the latches at 12 MHz (so with no CLK too) |
| `RD a [n]` | n words read (DRIVE): beside a CPU by DMA, the arbiter's grant; alone, by GO on the card's clock |
| `WR a w …`, `WRB a b`, `WRH a h` | words, a byte, a halfword written (byte lane n = D[8n+7:8n], D4) |
| `TRIG n ADDR a [mask]` | pattern n (0-7): an address, compared where the mask has a 1 (default `FFFFFFFC`) |
| `TRIG n RANGE a b` | pattern n: a to b, within one 128 KiB block or whole blocks (two patterns join any two) |
| `TRIG n OFF`, `TRIG CLR`, `TRIG INIT` | a pattern off; all off; both trigger RAMs written whole now |
| `ARM [12M] [n]` | a capture (LISTEN): one sample a CLK, or 12 MHz; n samples kept after the trigger (1) |
| `STOP`, `CAP` | trigger now and wait for the end; the capture's state |
| `DUMP [slices] [first] [count]` | the samples, a line each: its address, a byte for each slice asked for |
| `ALONE ON|OFF` | the card makes CLK (DRIVE, no CPU card: D102); the system lines driven, released; the local link driven idle, J3 first, then LR as 0, so nothing floats |
| `CLK STEP [n]`, `CLK RUN`, `CLK STOP` | alone: CLK a period at a time, or 3 MHz |
| `RES ON|OFF`, `FAST ON|OFF` | alone: `-RES`, `-FAST` low, or let go |
| `XCLK ON|OFF [FORCE]` | the CPU card on the card's XCLK (D76): only with `-RES` low (`MRES ON`, or hold its RESET) |
| `MRES ON|OFF` | the machine held in reset (DRIVE; D136): `PWROK` pulled low, and the CPU card's `-RES` follows at its clock's next edge (on XCLK, at the next `XSTEP`); the reply says how both stand. No DMA meanwhile: `-RES` holds the arbiter |
| `XSTEP [n]`, `XRUN`, `XSTOP` | on XCLK: n of the CPU's eight phases; 1.5 MHz; stopped |
| `IRQ v` | `-IRQ7-0`: a 1 pulls that line low (DRIVE) |
| `OD ack wait lock` | `-ACK`, `-WAIT`, `-LOCK` pulled low by a 1 (DRIVE) |
| `DRV k v`, `GRP v` | a drive register (slices 0-18); the groups GD GBUS GSYS GLR GJ3 (bits 0-4), as CTRL0; alone with the MMU / data card in, J3 stays driven and LR is refused while J3 puts that card on R (below) |
| `DISP v`, `DISP AUTO` | the status display: the host's byte, or the firmware's own (below) |
| `W xa v`, `R xa` | one local-bus write or read, as it is |
| `TRIG ZERO` | for the simulator only, whose trigger RAMs start at 0 |

**The status display** (D100), while the host has not written it: the high digit what the card is doing
(0 idle, 1 armed, 2 capturing, 3 a capture done, 4 alone, 5 the CPU card on XCLK), the low digit what the last
command ran into (0 nothing, 1 no answer from the card, 2 a time-out, 3 no `-ACK`, 4 refused, 5 a mistyped command).

## What it does for the card's sake (D135, D136)

- **At power-on it drives CRESN low** before anything else (an A2-stepping RP2350's pin can drift against a weak
  pull-down, erratum E9: the card's R3 is 4.7k since D136, 10k before), and drives nothing high until the card's
  5 V is up and the card has answered: an unpowered card's inputs would take current from it.
- **The card's 5 V** (D136): R8 and R9 halve it onto GP26 (ADC0). Before every command the firmware reads it,
  and below 4.0 V it drives nothing, lets go of the local bus if it held it, and forgets what the card held (the
  drive registers and the trigger RAMs come up as anything); with power back, it probes and resets the card.
- **The probe:** with CRESN high, ARM leaves a 1 in the status (ARMQ, or a running capture), which CRESN low
  clears; only a powered card does both.
- **MRES** (D136) is TSAH's bit 7, beside the trigger's setup address: every write of TSAH carries it, and
  `RESET` (CRESN) clears it.
- **DMA's snapshot** (theory section 10) shows in the slices only once the sample clock has fallen after SNAP
  (WQ covers the last write). Stepping the card's clock, which stops high, the firmware gives one more period;
  on XCLK, eight phases.
- **A capture's last write** stays open while the sample clock stands high (the machine stepped on XCLK), and
  the capture address does not count meanwhile. Before it reads the capture's end, or reads back, the firmware
  switches the sample clock to 12 MHz for a moment: T12 runs, the write ends. On XCLK, `STOP` steps the machine
  until the capture is done.
- **ARM** is given with SETUP set: an early trigger while ARM's strobe still holds ARMQ's preset would outlast
  ARMCLRN and start a second capture over the first (`sim_dbgfw.py strobe`).
- **XCLK** is changed only while `-RES` is low. `-RES` is the CPU card's GAL PANEL register, clocked by the
  clock it has: stopped on XCLK, it shows the button only after an edge, so `XCLK OFF` gives one first.
- **A DMA cycle beside a CPU** is refused at once with no MMU / data card in (no arbiter, no grant), and a GO
  that never ends is cancelled by a reset after 20 ms, the settings put back.
- **The local link alone.** With the CPU card out nothing else drives J3, which has no pull-ups: `ALONE ON`
  drives it idle (RSRC, RDST and MEM none, `-WSTB` high), then LR as 0. With the MMU / data card in, J3 stays
  driven (`GRP` without GJ3 is refused), and LR is refused while J3's RSRC puts one of that card's registers on
  R (MEM, MAR, BADADDR; by CRHW, ASID, TLBHI, TLBLO): the two would fight. To read what it puts on R, let LR go
  first (`GRP 14`), then set RSRC; to drive R again, RSRC to 0 first. Which settings put it on R is in
  `slices.h`, from `card_mmu.py`'s decoding; `sim_dbgfw.py mmu` tries every one against its netlist.
