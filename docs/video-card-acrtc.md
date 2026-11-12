# The video card, revision 2: an HD63484 ACRTC (D103)

**Status:** DESIGN NOTE, WITH CHOICES FOR KEN (2026-11-12). No netlist, no
emulator model, no driver: §7 lists what Ken decides first. It replaces the
6845 description, [revision 1](video-card-6845.md) (D98), which never got
past its description. [Revision 0](video-card.md) (D67) is still what the
emulators and the kernel model, until this card's software is done (§9).
D103; D50, D52, D53, D57, D72, D98.

The card is YACC1-D's video card V3, which is not being built for the YACC1
(`~/Developer/YACC1-D/docs/cards/video-v3.md`; its section 13 sorts the
design for another bus). **The memory side and the dot path are taken as
they are**: they are simulated gate for gate and timed with the parts'
data-sheet numbers. **The bus side is new**, for P32W's bus, and is what
this note designs.

## 1. What the card is

- **Hitachi's HD63484P8** (the ACRTC, 8 MHz grade, 64-pin DIP; two are in
  hand) draws **VGA, 640 × 480 at 60 Hz, one bit a dot**, from a 25.175 MHz
  oscillator. Its clock, 2CLK, is the dot clock / 4: 6.294 MHz, 158.9 ns.
- **The chip draws, not the CPU.** The frame buffer (two 128K × 8 RAMs:
  128K words, six screens) is on the chip's own memory bus. The CPU sends
  commands: line, rectangle, fill, copy, 38 in all, at about a microsecond a
  dot. **Nothing of the picture is on P32W's bus.**
- **Text** two ways, as the YACC1 note's section 8: **B**, glyphs copied in
  the bitmap by the chip, with no text hardware; **A**, a character screen
  of 80 × 30 laid over the graphics, in eight more chips.
- **Two PS/2 ports** through an ATmega328P with YACC1-D's firmware, as D98
  decided.
- **The console** (D53), `/dev/tty4`, as before.

| | Revision 0 (D67) | Revision 1 (D98) | **Revision 2** |
|---|---|---|---|
| The raster | counters and GALs | a 6845 | the ACRTC |
| Graphics | 640 × 480, the CPU writes bytes | 640 × 400, the CPU writes bytes, two waits | 640 × 480, the chip draws from commands |
| Text | cells in RAM on the bus | cells in dual-port RAM on the bus | drawn by the chip (B), or a character screen in the chip's memory (A) |
| On the bus | 64 KiB of RAM and a register page | the same | **a register page only** |
| ICs | 88, 18 GALs | about 53, 6 GALs | **about 58, 5 GALs** (§4.7) |

## 2. What is kept, and where it is

All in `~/Developer/YACC1-D` (branch `main` unless said):

| What | Where |
|---|---|
| The design: the memory cycle and every equation (10b), text method A (10c), the simulation and its margins (10d) | `docs/cards/video-v3.md` |
| The circuit: parts, pins, nets | `hardware/cards/video/kicad/v3.0/v30_netlist.py` |
| The simulation and the timing check: eight corners, set-up, hold, runt pulses, two outputs on a net | `tests/video/v30_netsim.py` |
| The clock and register figures for any mode | `tools/acrtc_timing.py` |
| The emulators' model of the chip; the font image | `software/acrtcmodel.h`, `tools/v3font.py` (branch `video-v2`) |
| A driver: the start sequence, a console by method B | `firmware/monitor/monitor.asm` (branch `video-v3`) |
| The PS/2 firmware | `embedded/video-ps2` |
| Hitachi's user's manual (1985) and application note (1986) | `../private-resources/YACC1-D/docs/datasheets/` |

**Kept, part for part and net for net: 35 ICs.** The chip (1); the 74F163A
dot counter on the 25.175 MHz oscillator, the control logic, the delay line
and the two registers that make the control register from the chip's
attribute byte (16); the address latches, the two RAMs, the MAD buffers,
the font EPROM and its copy (8); the two graphics shift registers (2); text
method A's sockets (8). With them the DE-15 and its resistors. The control
register's bits stay: three colour-off bits, the two syncs' polarity, the
text layer's three.

**The rule that comes with it** (the YACC1 note's section 13): the margins
belong to these parts wired this way. A changed part, a gate moved into a
GAL or an equation rewritten is a new circuit to the timing check. So the
kept part stays in 74F and 74ACT, outside P32W's GALs, and P32W's netlist
is checked equal to `v30_netlist.py` for those parts (§9).

**The seam** between the kept part and the bus side is small: the chip's
RES, DACK, CS, RS, R/W, D0–D15, IRQ and DTACK, and the clear of the two
control registers.

**Not kept** (the YACC1's): the I/O ports PA–PF and their two decoders, the
latch on RS and R/W, the 8-bit bus buffer, the card status port, the DIN
41612 connector and the 250 mm outline. The PS/2 section's own parts stay;
their strobes and status are redesigned (§4.6).

## 3. The chip's host side, from the manual

From the user's manual's AC table and its figures T-2 and T-3 (the 8 MHz
grade), read again for this note. T is one 2CLK cycle, 158.9 ns.

| What | The manual | So |
|---|---|---|
| RS and R/W before CS falls; after CS rises | 50 ns; 0 | |
| CS before the 2CLK rising edge that takes it | 40 ns | the chip samples CS with its own clock |
| CS high between accesses | 60 ns | |
| A read's data | 80 ns after the **2CLK falling edge** that follows that rising edge | at worst 1.5 T + 120 = **358 ns after CS falls** |
| DTACK low | 70 ns after the next 2CLK rising edge | at worst 2 T + 110 = **428 ns after CS falls** |
| CS rises | not before DTACK is low | |
| A read's data after CS rises | held 10 ns, off within 60 ns | |
| A write's data | 40 ns before CS rises, 10 ns after | the chip takes it as CS rises |
| RES | low for ten 2CLK cycles | DACK's level as RES rises: high = the 16-bit bus, low = the 8-bit bus |

Three things follow.

1. **The chip is an asynchronous peripheral.** CS is taken by 2CLK, which
   has nothing to do with P32W's `CLK`. The chip says when it is done with
   DTACK (open drain), "between 1 and 1.5 2CLK cycles" after it takes CS.
2. **Without DTACK, CS is held at least three 2CLK cycles: 477 ns.** That
   is the application note's rule for a synchronous bus (its section 3.1,
   the HD6809). It covers the 428 ns above with 49 ns in hand.
3. **The chip may stretch an access** in two cases the manual names: an
   access straight after the one before, when 2CLK is much slower than the
   processor's clock; and a read straight after a write. Neither happens on
   P32W: 2CLK is faster than `CLK`, and the CPU's instruction fetch always
   stands between two accesses to the card.

**A correction to the YACC1 note.** Its sections 5 and 13 give a read's data
"80 ns after CS", and its simulation's stand-in chip answers so. The figure
times the data from 2CLK: up to 358 ns after CS. On the YACC1 that changed
no result at its 1 MHz clock (a 2 µs strobe); at the note's 4 MHz corner the
margin of 300 ns it lists would be about 20. P32W's cycle (§4.2) is designed
with 358 ns.

## 4. The bus side on P32W

### 4.1 Where the card is

A register page and nothing else: the top 256 bytes of device window n,
`0xFFFF_8F00 + n × 4 KiB` (D52); n is 5 by default, `0xFFFF_DF00`. The
card's 64 KiB at `0xFE00_0000 + n × 64 KiB` is **not used**: no `-ACK`, a
bus error.

| Offset | Size | Read | Write |
|---|---|---|---|
| `+00` | halfword | **the chip's status**, in bits 7–0: bit 0 WFE (write FIFO empty), 1 WFR (it has room), 2 RFR (read FIFO has a word), 3 RFF (it is full), 4 light pen, 5 CED (command end), 6 ARD (area detect), 7 CER (command error) | **the chip's address register** (bits 7–0) |
| `+04` | halfword | the register addressed, or the read FIFO (address register 0) | the register addressed, or the write FIFO |
| `+08` | byte | **card status**: bit 0 FULL (a PS/2 byte waits), 1 SRC (0 keyboard, 1 mouse), 2 BUSY (a command not yet taken), 3 TEXT (method A's parts are fitted), 4 the chip's IRQ | **PS/2 command**, YACC1-D's |
| `+0C` | byte | **PS/2 data**; reading it clears FULL | — |
| `+F0` / `+F1` | byte | card ID 3 / revision 2 | — |
| `+F4` | word | interrupt status: bit 0 PS/2, bit 1 the chip (bits 31–2 read 0) | — |

The chip's RS is **A2** (the bus has no A1 or A0). Anything else in the
page, or another size, gets no `-ACK`: a bus error. This is choice 1's
16-bit form; with the 8-bit bus `+00` and `+04` are bytes.

### 4.2 The cycle: four clocks, no handshake

Every access to the chip takes **four clocks, three of them waits**, made by
a count in a GAL on `CLK`. Nothing comes back from the chip's clock to the
bus's, as on revision 1's graphics access.

| | Clock 1 | Clock 2 | Clock 3 | Clock 4 |
|---|---|---|---|---|
| `-ACK` | low, from the decode | low | low | low |
| `-WAIT` | low | low | low | high |
| RS, R/W at the chip | move | **held** | held | held, and a clock more |
| CS | high | **low from mid-clock** | low | a read: low to the end. A write: **high from mid-clock** |
| The bus buffers | off | on from mid-clock | on | on to the end |

- **RS and R/W** are two registers in the GAL. They take A2 and the
  direction at the edge that ends clock 1 (when the decode is done, as for
  `-ACK`) and do not move again until a clock after the cycle. So they are
  steady half a clock before CS falls and a clock after it rises, whatever
  the master does at the cycle's end. This is the job of the YACC1 card's
  latch.
- **A write ends half a clock early**, as the I/O card's `-WR` does (D72):
  the chip takes the data as CS rises, and the master holds it half a clock
  more.
- **A read's data** is taken by the master at the edge that ends clock 4.

| | Wanted | At 3 MHz | At 4 MHz |
|---|---|---|---|
| CS low, a read (2.5 clocks) | 477 ns | 833 | 625 |
| CS low, a write (2 clocks) | 477 ns | 667 | **500** |
| RS, R/W before CS | 50 ns | 167 | 125 |
| A read's data at the chip before the cycle ends | — | 475 | 267 (for the buffer, the backplane and the MMU / data card's steering) |
| A write's data after CS rises | 10 ns | 167 | 125 |
| An access | | 1.33 µs | 1.0 µs |

The 500 ns is the tight one: 23 ns over the application note's rule and 72
over the chip's own figure (DTACK by 428 ns). Both of its edges come from
the same `CLK` edge through the same GAL, so its width is two clocks to a
few nanoseconds. Three clocks would not do at any clock (a write's CS would
be one clock). At 5 MHz, which P32W does not promise, the count would be
five: another fuse file, not another board.

**DTACK goes to a spare input of that GAL**, with a pull-up, and to a test
point. The equations do not use it. If the bench shows the chip stretching
an access, the wait can be made to follow it with a new fuse file.

### 4.3 Sixteen bits

The chip has a 16-bit host bus, which the YACC1 could not use. On P32W a
halfword is lanes 0 and 1, and P32W is little-endian: **the chip's D0–D7 are
the bus's D0–D7, D8–D15 the bus's D8–D15**, through two 74HCT245 (the chip's
outputs sink 2.2 mA). A command word or a register is then one access, a
`sth` or `lhu`, where the 8-bit bus takes two with the high byte first. In
this mode the chip allows no byte transfers, its address register counts in
twos, and WPTN's count is in words. DACK is pulled high for it; in 8-bit
mode DACK follows RES and D8–D15 stay open.

### 4.4 Reset and the pins not used

`-RES`, buffered, is the chip's RES, the clear of the two control registers
and, through the 7416, the ATmega's reset. DONE is pulled up, DREQ open,
LPSTB grounded, EXSYNC open (the chip is the master), as in the kept
netlist. No DMA (§6).

### 4.5 Interrupts

Two devices, as D52 and D57 want them:

| Device | Status bit | Asks when | Level jumpers (default) | Vector |
|---|---|---|---|---|
| 0, PS/2 | `+F4` bit 0 | FULL, with the firmware's interrupts on | 4 | n × 16 + 0 |
| 1, the chip | `+F4` bit 1 | its IRQ is low: whatever its command control register enables (write FIFO empty or ready, read FIFO, command end, command error, area detect) | 4 | n × 16 + 1 |

A 74LS145 for each pulls its level; in the acknowledge cycle the card
answers for the lowest of its devices asking on that level, as the I/O card
does. The chip makes no vector of its own; the card's GAL does.

**Why the chip's IRQ is worth its jumpers:** the chip is busy about 30 ms on
a scroll and its write FIFO holds eight words. A kernel that polls would
spin for those 30 ms with every process waiting. With "write FIFO empty" as
an interrupt the console is a transmitter like a UART's: fill the FIFO,
return, go on from the interrupt.

### 4.6 PS/2

D98's choice stands: the ATmega328P with YACC1-D's firmware, the keyboard as
ASCII and VT100 sequences, one queue for both ports. The V3 card's section
moves onto P32W's registers: the ATmega loads a byte into a latch and sets
FULL; a read of `+0C` puts it on D0–D7 and clears FULL; a write of `+08`
latches a command (at the cycle's end, D72) and sets BUSY. The ATmega never
sits on the bus. Its pins and its firmware do not change.

### 4.7 The bus side's parts (an estimate until the netlist)

| Block | Parts | ICs |
|---|---|---|
| Buffers | 3 × 74HCT541: A2–A15, BE0–3, `-MRD`, `-MWR`, `-IACK`, `CLK`, `-RES`, one load a line | 3 |
| Decode | 2 × 74HCT688 (A31–A16 all ones, as the I/O card); GALs SEL (the window, the page, read or write, `-IACK`) and DEC (which register, its size and lanes, `-ACK`) | 4 |
| The cycle | GAL WT: the count, `-WAIT`, CS, RS, R/W, the buffers' enables, the PS/2 strobes | 1 |
| Data | 2 × 74HCT245 (D0–D15); 2 × 74HCT541 that put zeros on D16–D31 for the word at `+F4` (revision 0 left them floating: backlog item 2) | 4 |
| Registers | GAL RD: card status, ID, revision, interrupt status and the vector onto D0–D7 | 1 |
| Interrupts | 2 × 74LS145; 74F138 (the level acknowledged); GAL INT (who asks, the vector's device) | 4 |
| `-ACK`, `-WAIT` | 74HC05, open drain | 1 |
| PS/2 | ATmega328P, 2 × 74HCT574, 74HCT74, 7416 | 5 |
| **New** | | **23** |
| Kept (§2) | | 35 |
| **The card** | | **about 58, 5 of them GALs**; 50 with method A's sockets empty |

**Jumpers**, at an edge: `WIN2`–`WIN0`; two sets of level jumpers; TEXT
(fitted with method A's parts); the RAMs' 128K / 512K jumper of the kept
netlist. **The front edge:** the DE-15, the two mini-DIN-6 (KEYBOARD,
MOUSE; +5 V through a polyfuse), a POWER LED. The ATmega's FTDI header at
an edge. 9U × 340 mm, six layers, through-hole (D21, D48, D77): the YACC1
card's trouble with a 218 mm outline does not arise.

## 5. What changes for the software

- **The emulators** (`p32io.h`): YACC1-D's `acrtcmodel.h` moves in, with
  the 16-bit bus if choice 1 says so: the registers, the two FIFOs, the
  status register, the commands a driver needs (it names the ones it lacks
  and sets the command error bit), the chip's busy time, a byte lost when
  the FIFO is full. `p32uemu -C` adds the three waits, so the gate-level
  runs still match it cycle for cycle. The 64 KiB of video RAM goes.
- **The kernel's `video.c`** is a new driver: the chip's register table and
  start sequence (as the V3 ROM's `vregs` and `vsetup`), then the console by
  method B: a character is one graphic copy (AGCPY, five words) from a
  picture of the font kept under the screen; a scroll is one CPY and one
  CLR; the ANSI sequences as now. The chip takes 160 to 320 µs a character
  (3,000 to 6,000 a second, a 38,400-baud terminal at best) and about 30 ms
  a scroll.
- **Attributes.** One bit a dot and one level: there is no bright dot.
  Inverse, underline and bold (for bright) are more pictures of the font,
  made by the chip at start (about 770 words each of the 128K). Blink goes;
  the kernel never used it. **The cursor** is drawn by the driver (a block
  in XOR): method B has no hardware cursor.
- **The font**: P32W's own (`gen_font.py`, ASCII with lower case), written
  as the EPROM's picture by a port of `tools/v3font.py`. The chip's bit 0 is
  the left-hand dot, where revision 0's font ROM has bit 7.
- **Tests and the debugger's Screen** read the frame buffer, 640 × 480
  dots. The screen's text for `video_test` comes from matching each 8 × 16
  cell against the font, since no character codes are stored anywhere.
- **Graphics for programs** (the backlog's overlay item): there is no
  memory to map into a process. `/dev/video` would take the chip's commands
  instead, and a C library and BASIC's statements send them.
- **Docs**: this page becomes `docs/video-card.md` when the software is
  done; the programmer's guide §6.6 and the architecture §4a follow.

## 6. What this card does not do

- **The CPU cannot read or write the picture directly.** Every dot goes
  through the chip (its RD, WT and MOD commands move single words).
- **No DMA.** The chip's DREQ, DACK and DONE could feed a bus master like
  the disk card's (D75); not designed here or in the YACC1 note.
- **One bit a dot.** Eight colours for the whole picture (the attribute
  byte), not for a dot. More bits a dot, 800 × 600 and 1024 × 768 are the
  YACC1 note's section 9: a wider memory, a new circuit to the timing check.
- **Method A has no attribute for a character**: a cell is a byte.
- **The emulators' model stops short of the chip**: no circles, ellipses,
  arcs, PAINT, PTN; no character screens (method A), cursors or interrupts.
  The interrupts are needed for §4.5 and are small; the rest as wanted.
- **Never on a bench.** The kept part is proven by simulation only. The
  YACC1 note's sections 10d and 12 list what it leaves for the bench: the
  chip's minimum delays behind the two smallest margins, the pattern RAM's
  walk, which cycle the cursor's skew counts from.

## 7. Choices for Ken

Each with my recommendation first.

1. **The chip's host bus: 16 bits or 8.**
   **16 bits** (§4.3): half the accesses for every command, plain `sth` and
   `lhu` in the driver, one more 74HCT245. The cost: the model and the ROM
   driver were written for 8 bits, so the model's FIFO and register access
   change as it moves to P32W (it has to be fitted to `p32io.h` either way;
   the ROM driver is YACC1 assembly and only its tables carry over).
   *Or 8 bits:* the model as it is, high byte then low byte.

2. **The cycle: a fixed count, or the chip's DTACK.**
   **A fixed four clocks** (§4.2), DTACK wired to the GAL but not used. No
   signal crosses between the two clocks, the emulator stays cycle-exact,
   and the manual allows it where the chip cannot stretch an access.
   *Or DTACK:* `-WAIT` held until DTACK has passed two flip-flops on `CLK`.
   It follows the chip at any clock, but an access is then four or five
   clocks as the phases fall, which `p32uemu -C` cannot predict, and it is
   the kind of handshake D98 set out to drop.

3. **The chip's interrupt: wired, or polled.**
   **Wired**, as device 1 with its own level jumpers and vector (§4.5), so
   the kernel does not spin through a scroll. Three jumpers, a 74LS145 and
   part of a GAL.
   *Or left off:* the driver polls the status register, as the YACC1 ROM
   does.

4. **The console's text.**
   **Method B first, the board with method A's eight sockets and tracks**,
   as you decided for the YACC1 card. B needs no text hardware, keeps the
   attributes (inverse, underline, bold) and is what the model runs today.
   A's sockets are part of the kept circuit and its timing check; its parts
   go in when a driver uses them (TEXT says they are there).
   *Or method A from the start:* far faster text (a character is a word
   written, a scroll a register write), a hardware cursor, text over
   graphics; but no underline or bold, drawing at a quarter speed while text
   shows, and the model has no character screens yet.
   *Or B only, no sockets:* eight fewer positions on a card that has the
   room.

5. **Rows: 25 or 30.**
   **25, as you chose in D50**, in the middle of the 480 lines: nothing in
   P32W Unix changes. 30 rows of 16 lines fill the screen and are one
   constant in the driver later, with the programs that assume 25.

6. **The two ROMs: 27C256, or P32W's flash.**
   **The 27C256s as timed** (the font; the character generator an
   AT27C256R-70 for method A).
   *Or the SST39SF010A*, the flash the rest of P32W uses, on the same
   programmer: a 32-pin part with other output times, so a changed circuit:
   its model goes into the simulation and the timing check runs again
   before the netlist is final.

Carried over without asking again: PS/2 on the ATmega328P with YACC1-D's
firmware (D98); VGA only, on a DE-15; window 5, level 4, card ID 3.

## 8. To check

- **Parts in DIP, in stock:** AS6C1008-55PCN, AT27C256R-70, 74F163A,
  74F299, 74F74, 74F21, the 74ACT types of the kept netlist's bill of
  materials. A substitute is a new circuit to the timing check.
- **The chip's supply current**: the manual's table says "T.B.D."; measure
  one before the power budget.
- **The chip on the bench before the board is ordered**, if a breadboard
  can do it: that it answers on a 16-bit bus as the manual says, and its
  DTACK against §3's 428 ns.
- **The DE-15's footprint** against the part in hand (the YACC1 note's
  caution; revision 0's DE-15 was put right in D90).

## 9. The order of work

1. **This note: Ken's choices** (§7).
2. **The software in the emulators:** the chip's model in `p32io.h`, the
   three waits in `p32uemu -C`, the debugger's Screen, the kernel's driver,
   the tests. P32W Unix on the new card in the emulator before any hardware.
3. **The netlist** (`generators/card_video.py`; revision 0's kept aside):
   the 35 kept ICs transcribed from `v30_netlist.py` and **checked equal to
   it, part for part, pin for pin, net for net**; the bus side new.
4. **The simulation:** `v30_netsim.py`'s memory-side scripts run on P32W's
   netlist at their eight corners, in `make test`. The bus side's script is
   written new: P32W's cycle against a model of the chip's host side from
   figures T-2 and T-3, at every phase of 2CLK against `CLK`, checking each
   line of §3's table. Then the card at gate level with the CPU, MMU / data
   and memory cards against `p32uemu -C`, and the bus side in the static
   timing pass (docs/timing.md).
5. **The schematic, the layout** (six layers, the layout rules), `pcb-check`,
   a design review. Revision 0's board comes off `pcb-check`'s list then.
6. **Ken orders it.**
