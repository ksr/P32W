# The video card, revision 2: an HD63484 ACRTC (D103)

**Status:** DESIGN NOTE, ITS CHOICES DECIDED (2026-11-12, D104: §7); **THE
SOFTWARE IS DONE** (D105: §5): both emulators model this card, and P32W Unix
runs on it there. No netlist yet (§9, step 3). It replaces the
6845 description, [revision 1](video-card-6845.md) (D98), which never got
past its description. [Revision 0](video-card.md) (D67) was laid out and
never built; its netlist is kept aside until this card's replaces it.
D103, D104, D105; D50, D52, D53, D57, D72, D98.

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
page, or another size, gets no `-ACK`: a bus error; so does a write to
`+0C`. The status's bits 15–8 are not defined by the manual: the emulators
give ones there, so a driver that does not mask them fails. The card's own
registers (`+08`, `+0C`, the ID, the interrupt status) answer with no wait.

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

The chip has a 16-bit host bus, which the YACC1 could not use (decided:
§7, 1). On P32W a
halfword is lanes 0 and 1, and P32W is little-endian: **the chip's D0–D7 are
the bus's D0–D7, D8–D15 the bus's D8–D15**, through two 74HCT245 (the chip's
outputs sink 2.2 mA). A command word or a register is then one access, a
`sth` or `lhu`, where the 8-bit bus takes two with the high byte first. In
this mode the chip allows no byte transfers, its address register counts in
twos, and WPTN's count is in words. DACK is pulled high for it.

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

## 5. The software (done 2026-11-12, D105)

- **The chip's model**, `emulator/p32acrtc.h`: YACC1-D's `acrtcmodel.h` on
  the 16-bit bus. Registers as words, the address register in steps of 2,
  the two FIFOs of eight words, the status register, IRQ from the command
  control register's enables, the commands a driver needs (it names the
  ones it lacks and sets the command error bit), the chip's busy time, a
  word lost when the FIFO is full. `emulator/test/acrtc_test.c` checks it
  by itself, driven as the console drives it: 55 checks.
- **The card in both emulators** (`p32io.h`): the chip at `+00` and `+04`;
  the PS/2 section as the ATmega328P's firmware shows it (its key
  translation, its queue of 63 bytes, its nine commands); the two
  interrupts and their vectors; no memory on the bus. `p32uemu -C` adds the
  three waits. The chip's time is the machine's, and a `WAIT` ends when the
  chip finishes its work and interrupts. `-V FILE` writes the picture
  (`FILE.pbm`) and the console's text, read back from the dots: each cell
  matched against the font, since no character codes are kept anywhere.
- **The kernel's `video.c`**: the start sequence, then the console by
  method B. A character is one AGCPY (five words) from the font's picture
  under the screen; a run of characters needs one move. Seven more pictures
  (bold for bright, underline, inverse and their mixes) are made by the
  chip at start, about 0.2 s. Clearing is CLR; a scroll is one CPY and one
  CLR; the cursor is lines 14 and 15 of its cell in XOR. The command words
  wait in a ring and go to the chip eight at a time from its "write FIFO
  empty" interrupt; kernel messages wait for the chip, as they must work in
  a panic. The keyboard's bytes are ASCII: the kernel's scan-code table is
  gone. The mouse is turned off.
- **The chip takes** about 290 µs a character by the model (918 2CLK
  cycles, doubled while the display shares the memory: an estimate) and
  about 30 ms a scroll.
- **The font**: P32W's own (`generators/gen_font.py --acrtc`), written as
  the EPROM's picture in YACC1-D's layout, bit 0 the left-hand dot.
- **The debugger's Screen** draws the chip's picture, and its `screen`
  command prints the console's text.
- **Tests** (`make test`): the chip's model; `video_test` (12 cases, both
  emulators identical, and with the card's timing at 3 and 4 MHz); P32W
  Unix on the video console (`session23`, as recorded before the change);
  the debugger. Revision 0's two gate-level runs (`vidcard_test`,
  `ps2_test`) went with its model; its netlist's own checks stay until
  step 3, and `sim_sys.py --video` refuses until then.
- **Still to do in software**, when wanted: graphics for programs
  (`/dev/video` taking the chip's commands, a C library, BASIC's
  statements: the backlog); circles, arcs and PAINT in the model; text
  method A (character screens, the cursors) in the model and a driver.

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

## 6a. The picture on the bus as well (asked 2026-11-12; for Ken to decide)

Ken asked what it would take. It can be done, as a second port that
steals the chip's idle memory cycles, but it reopens the kept memory side.
The recommendation given: leave it off. Not decided; the card as designed
here has no such port.

- **Why it is not a decode.** The chip owns the frame buffer's address and
  data lines in every 318 ns cycle and has no hold pin there. The only free
  cycles are its dummy ones, a drawing slot with nothing to draw: every
  other cycle while it is idle, none while it draws.
- **What it needs**, about 12 more ICs, 2 of them GALs: three drivers for
  the bus's address onto the RAMs, with the two address latches switched
  off for that cycle; a 16-bit read latch and write drivers; two more lane
  buffers; control on the dot clock (the request across from the bus
  clock, a cycle with no address strobe by dot 3, the RAM run in dots 3–7,
  "done" back across to release `-WAIT`); the 64 KiB window's decode.
- **What it costs.** The latches' enables, the RAMs' second chip enable,
  the output enable and the write strobe each gain a term, and the address
  and data lines gain loads: the timing check runs again with a new kind of
  cycle. The two-clock handshake that D98 dropped comes back; an access is
  four to six clocks as the phases fall. While the chip draws there is no
  free cycle, so software touches the picture only at command end, or the
  card gives up with a bus error.
- **What it buys** (estimates): about twice the speed for words written in
  bulk and for characters, since P32W's CPU is the limit either way; and
  the picture mapped into a process, which nothing else gives.
- **Without it:** the chip's own word read, write and modify commands;
  text method A for fast text; later the chip's DMA transfers with a
  bus-master block like the disk card's.
- **If Ken wants it**, the time is before the netlist, as a socketed block
  like method A's, with the timing check run fitted and unfitted.

## 7. Decided (D104, 2026-11-12; Ken: "agree with all your recommendations")

| | Decided | The other way, not taken |
|---|---|---|
| 1. The chip's host bus | **16 bits** (§4.3): a command word or a register in one access, plain `sth` and `lhu`, one more 74HCT245. The model's FIFO and register access change as it moves to P32W | 8 bits, the model as it is, high byte then low byte |
| 2. The cycle | **A fixed four clocks** (§4.2). DTACK goes to a spare GAL input and a test point, and the equations do not use it. Nothing crosses between the two clocks, and the emulator stays cycle-exact | `-WAIT` held until DTACK has passed two flip-flops: four or five clocks as the phases fall |
| 3. The chip's interrupt | **Wired**, as device 1 with its own level jumpers and vector (§4.5), so the kernel does not spin through a scroll | polled, as the YACC1 ROM does |
| 4. The console's text | **Method B first; the board has method A's eight sockets and tracks.** A's parts go in when a driver uses them (TEXT says they are there) | method A from the start; or B alone with no sockets |
| 5. Rows | **25**, as D50, in the middle of the 480 lines (lines 40–439) | 30 |
| 6. The two ROMs | **27C256, as timed** (the font; the character generator an AT27C256R-70) | the SST39SF010A, with the timing check run again |

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

1. **This note: Ken's choices** (§7). Done, D104.
2. **The software in the emulators:** the chip's model, the three waits in
   `p32uemu -C`, the debugger's Screen, the kernel's driver, the tests.
   Done, D105 (§5): P32W Unix runs on the new card in both emulators.
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
