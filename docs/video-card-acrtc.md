# The video card, revision 2: an HD63484 ACRTC (D103)

**Status:** DESIGNED, SIMULATED AND LAID OUT; NOT ORDERED (2026-11-12). Its
choices are decided (D104: §7), **the software is done** (D105: §5: both
emulators model this card, and P32W Unix runs on it there), **the netlist is
written** (D106: §4.7, `generators/card_video.py`: 58 ICs, 5 of them GALs;
its kept part checked equal to YACC1-D's netlist), **it is simulated and
timed** (D107: §4.8: with every part's data-sheet delays, at every phase of
the chip's clock; and in a machine at gate level, cycle for cycle as the
emulator) and **it has its schematic and its board** (D108: §4.9: six
layers, routed, the pre-order check without a failure, and the timing
checks run again with the board's own tracks as delays). What is left is
Ken's: §8's checks by hand, and the order (§9). It replaces the 6845
description, [revision 1](video-card-6845.md) (D98), which never got past
its description, and [revision 0](video-card.md) (D67), which was laid out
and never built: revision 0's netlist and GALs are in the repository's
history only (last in commit `78473d7`), and its schematic and board (last
in commit `5d447f7`) have made way for this card's in
`hardware/video-card/kicad`.
D103, D104, D105, D106, D107, D108, D109, D110; D50, D52, D53, D57, D72, D98.

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
| ICs | 88, 18 GALs | about 53, 6 GALs | **58, 5 GALs** (§4.7); 50 with method A's sockets empty |

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

**Kept, part for part and net for net: 35 ICs** (checked: §4.7). The chip (1); the 74F163A
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
give ones there, so a driver that does not mask them fails (on the card
they read zero if the chip leaves them alone: its data lines have
pull-downs). The card's own
registers answer with no wait, but one: **a PS/2 command written to `+08`
takes two clocks** (one wait, D106: §4.6).
A byte register answers on its own lane only: `+08`, `+0C` and `+F0` on
D0–D7, `+F1` on D8–D15.

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
the same clock edge through the same flip-flop, so its width is two clocks,
give or take that part's rising delay against its falling one: 11.5 ns at
the data sheet's two extremes, so 488 ns at the least (§4.8). Three clocks would not do at any clock (a write's CS would
be one clock). At 5 MHz, which P32W does not promise, the count would be
five: another fuse file, not another board.

**DTACK goes to a spare input of that GAL**, with a pull-up, and to a test
point. The equations do not use it. If the bench shows the chip stretching
an access, the wait can be made to follow it with a new fuse file.

**How it is built** (D106). GAL VWT, on `CLK`, counts the clocks and holds
RS and R/W. **CS is not a GAL's decode: it is a flip-flop** (half a
74ACT74) clocked by the bus's `-CLK`, which rises in the middle of a clock,
and cleared by RUN, a register of VWT that is high in clocks 2 to 4. It
takes "not the middle of a write's clock 4" from VWT. So CS falls in the
middle of clock 2; a write's rises in the middle of clock 4, out of the
same flip-flop from the same clock edge; a read's rises when RUN clears the
flip-flop as the cycle ends. Its twin, the other half, is set in the middle
of clock 2 and cleared with it: the two data buffers' window. **Nothing
that reaches CS can make a runt pulse**, which matters because the chip
samples CS with its own clock: an address settling at the start of some
other cycle moves the decode, and the decode moves nothing here until a
`CLK` edge has taken it.

### 4.3 Sixteen bits

The chip has a 16-bit host bus, which the YACC1 could not use (decided:
§7, 1). On P32W a
halfword is lanes 0 and 1, and P32W is little-endian: **the chip's D0–D7 are
the bus's D0–D7, D8–D15 the bus's D8–D15**, through two 74HCT245 (the chip's
outputs sink 2.2 mA). A command word or a register is then one access, a
`sth` or `lhu`, where the 8-bit bus takes two with the high byte first. In
this mode the chip allows no byte transfers, its address register counts in
twos, and WPTN's count is in words. DACK is pulled high for it.

**The chip's 16 data lines are also the card's own data bus** (HD0–HD15).
GAL VRD puts the card status, the ID, the interrupt status and the vector
on HD7–0, the PS/2 data latch its byte, and the command latch takes its
byte from there; one output of GAL VSEL is the revision's bit. The two
74HCT245s are then the only load the card puts on D0–D15, and they open for
the chip only inside its cycle. Two 74HCT541s put zeros on D16–D31 when the
interrupt status is read, a word.

### 4.4 Reset and the pins not used

`-RES`, buffered, is the chip's RES, the clear of the two control registers
and, through two open-drain gates of the card's 74HC05 (the YACC1 card's
7416 is not needed), the ATmega's reset. DONE is pulled up, DREQ open,
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
does: a 74F138 gives the level acknowledged, GAL VINT matches it against
each device's jumpers, GAL VRD puts the vector on D7–0. The chip makes no
vector of its own. Its IRQ is open drain: a pull-up, then straight to its
74LS145.

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

**The two strobes are registers of GAL VWT, never a decode** (D106). A
strobe made from the decode in the cycle itself could come from an address
that was only settling, and would lose a byte or latch a false command.

- **FULL's clear** is a pulse a clock long, in the clock after a read of
  `+0C`. The firmware already waits 5 µs after it sees FULL clear before it
  loads the next byte, and checks that the load took; the clear here lasts
  a third of a microsecond.
- **A command written to `+08` takes two clocks**, one of them a wait. The
  command latch takes the byte, and BUSY is set, as the second clock
  begins: the master has held the byte for a clock by then and holds it a
  clock more. (Taking it after a one-clock cycle would have left the
  latch's hold time to the gates' shortest delays.) `p32uemu -C` adds that
  wait.

**The firmware's time (D109).** The ATmega runs a loop, and the card's two
flip-flops show how long it takes. Both emulators and the gate-level
machine's stand-in model it (`emulator/p32io.h`, "the firmware's time"):

| what software sees | how long | why |
|---|---|---|
| after a reset, nothing | **about 2.1 s** | the ATmega's start-up and its bootloader's second (Optiboot), then the sketch's `start_all()`: a reset and self-test of the keyboard and of the mouse, half a second each |
| a command written before the sketch runs | **dropped** | BUSY's flip-flop has no reset, so `setup()` reads the command latch and throws the byte away |
| FULL clear after a byte is read | **about 50 µs** | the loop comes round, waits 5 µs, sets SRC and the byte and pulses LDDATA |
| BUSY after a command is written | **about 30 µs**, or until the command in hand is done | the loop comes round and reads the latch; it does one thing at a time |
| a command to a device | **3 ms** each: the mouse off one, the keyboard's LEDs two; a reset about half a second | the PS/2 frame both ways at the device's own clock, the device's self-test |

Estimates, all of them: the sketch's own delays and the Arduino core's (a
pin read or written 4 to 5 µs), rounded up, for a part that has run on no
hardware yet. **Software must depend on FULL and BUSY, never on the
figures.** What follows from the firmware, and holds whatever the figures:

- **Read `+0C` only when FULL is set**, one byte at a time. The next byte
  is not there yet when the first has been read: an interrupt handler gets
  one byte, and FULL asks again for the next.
- **Write `+08` only when BUSY is clear.** A command written over one the
  firmware has not taken replaces it: the first is lost.
- **After a reset, the first command may be dropped**: send one that does
  not matter first (02, interrupts off, as a reset leaves them). When BUSY
  has cleared after it the sketch is past that point.
- **Never wait for the ATmega with the machine stopped**: BUSY can stay set
  for two seconds after a reset, and for ever with the socket empty. The
  kernel writes its three commands from its clock tick, one each time BUSY
  is clear.
- **03 (acknowledge) silences more than it says.** No interrupt comes until
  a device sends something new; bytes the firmware already holds (the rest
  of an arrow key's `ESC [ A`) then arrive in the latch with FULL and no
  interrupt. The kernel does not use it: reading the byte releases the line
  anyway.
- **06 (flush) empties the firmware's queue, not the card's latch**: a byte
  already there stays, with FULL, until it is read.

### 4.7 The netlist (D106)

`generators/card_video.py`: **58 ICs, 5 of them GALs**; 351 nets, 173 parts.
Text method A's eight sockets are among the 58: 50 ICs on the first build.

| Block | Parts | ICs |
|---|---|---|
| Buffers | 2 × 74HCT541: A2–A7, BE0–3, `-MRD`, `-MWR`, `-IACK`, `CLK`, `-CLK`, `-RES`, one load a line (A8–A15 go to one GAL only, A16–A31 to the comparators) | 2 |
| Decode | 2 × 74HCT688 (A31–A16 all ones, as the I/O card); GAL VSEL (the page, read or write; the revision's bit); GAL VDEC (which register and its size, `-ACK`, who is on HD, the lanes) | 4 |
| The cycle | GAL VWT (the count, RUN, `-WAIT`, RS, R/W, the PS/2 strobes); a 74ACT74 on `-CLK` (CS; the buffers' window) | 2 |
| Data | 2 × 74HCT245 (D0–D15 to HD0–HD15); 2 × 74HCT541, zeros on D16–D31 for `+F4`; GAL VRD (status, ID, interrupt status, the vector) | 5 |
| Interrupts | 2 × 74LS145; a 74F138 (the level acknowledged); GAL VINT (who asks, the vector's device) | 4 |
| `-ACK`, `-WAIT`, the ATmega's reset | 74HC05, open drain | 1 |
| PS/2 | ATmega328P, 2 × 74HCT574 (data, command), a 74HCT74 (FULL, BUSY) | 4 |
| **New** | | **22** |
| Kept (§2) | the 35, and the oscillator | 36 |
| **The card** | | **58** |

**The kept part is checked equal to YACC1-D's netlist.** A copy of
`v30_netlist.py` is in `hardware/video-card/yacc1-v3/` (so the check runs
wherever P32W is checked out). The generator transcribes the kept part line
for line, by that file's own pin numbers and under its net names (its
A0–A16 are VA0–VA16 here: A2–A31 are the bus's). `kept_check()`, part of
`make test`, then proves:

- every part of YACC1-D's netlist is kept, or named as not kept (its bus
  side, its PS/2 section, its power parts);
- **50 parts, 765 pins, 178 nets**: a kept part is the same part, and each
  of its pins is on the same net. Two kept pins share a net here if and
  only if they do there; a pin left open there is open here;
- **11 pins differ, each listed with its reason**: DACK (pulled high, not
  tied to RES: the 16-bit bus), D8–D15 (connected), DTACK (a pull-up, a test
  point, GAL VWT's spare input), the oscillator's pin 1 (tied high, as on
  P32W's other cards);
- **nothing of the bus side is on a kept net** except at the seam: RS, R/W,
  CS, D0–D15, RES, IRQ, DACK, DTACK. So the bus side adds no load to
  anything the timing check counted.

Six planted faults (a pin on a neighbour's net, a pin opened, a load added
to the memories' output enable, ...) were each caught.

**The bus side is run at gate level, as wired** (`generators/sim_video.py`,
part of `make test`): the card's netlist alone in the project's simulator,
its backplane pins driven half a clock at a time, with stand-ins at the
chip's and the ATmega's pins. It checks §4.2's table (in which half of
which clock CS, RS, R/W, the buffers and `-WAIT` move, for a read and a
write), every register of §4.1 with its size, 12 bus errors, the PS/2
handshake, both interrupts on their levels and the vector, the jumpers.
Twenty-two planted wiring faults were each caught. It is at zero delay: the
chip's timing against its own clock is §4.8's.

**The five GALs** are fitted and each equation is checked against its
specification over every combination of its inputs; their fuse files are
in `hardware/gal/video`. The fuse maps are not yet compared with GALasm's
(§8).

**Jumpers**, at an edge: `WIN2`–`WIN0`; the PS/2 section's level and the
chip's (three each); TEXT (fitted with method A's parts); the RAMs'
128K / 512K jumper of the kept netlist. **The front edge:** the DE-15, the
two mini-DIN-6 (KEYBOARD, MOUSE; +5 V through a polyfuse), the POWER LED.
The ATmega's FTDI header and the DTACK test point at an edge. 9U × 340 mm,
six layers, through-hole (D21, D48, D77): the YACC1 card's trouble with a
218 mm outline does not arise.

**The EPROMs:** the font (a 27C256) is `video/font-acrtc.bin`, which
`python3 generators/gen_font.py --acrtc` writes (ROM images are built, not
kept in git); the emulators hold the same bytes (`emulator/p32vfont.h`).
Method A's character generator has no image yet: it comes with method A's
driver.

### 4.8 The simulation and the timing (D107)

Four checks, all in `make test`.

**1. The memory side and the dots: YACC1-D's own check, on this netlist.**
`generators/sim_video_timing.py` loads YACC1-D's `v30_netsim.py` (a copy is
beside the netlist's, in `hardware/video-card/yacc1-v3/`, both byte for
byte) and runs its two scripts, with its models of the parts and of the
chip's memory side and its checks, unchanged, on the kept part of P32W's
netlist: 20 runs at its eight corners, all pass. Three faults planted in
the kept part's wiring each failed all 20.

**2. The bus side with delays, against the chip's host timing.** The same
engine, with the bus side's parts added at their data-sheet delays, and at
the chip's pins a model of §3: CS taken by the first rising 2CLK edge it
has been low 40 ns before, a read's data 80 ns after the next falling
edge, DTACK 70 ns after the rising edge after that. A write and a read of
the chip are run **at every 4 ns of the chip clock's phase** against the
bus clock, at 3 MHz and at 4, at six sets of corners (the chip and the
parts slow or fast, and rising edges against falling ones): 480 runs; then
the card's own registers and the PS/2 section, 36 runs. All pass. The
least to spare, all at 4 MHz:

| What the chip or the machine asks | Asked | To spare |
|---|---|---|
| CS does not rise before DTACK | | **62 ns** (CS low 488 ns, DTACK 427 ns after its fall) |
| CS low three 2CLK cycles (the application note's rule) | 477 ns | 12 ns (a write, the flip-flop's rising edge fast and its falling one slow) |
| RS and R/W steady before CS falls | 50 ns | 70 ns |
| A write's data steady before CS rises | 40 ns | 392 ns |
| CS high between accesses | 60 ns | over a microsecond |
| A read's word on the backplane's D lines before the cycle ends | 140 ns (below) | **56 ns** (196 ns) |
| The command latch's data before its clock | 25 ns | 73 ns |

Between the chip's two accesses go another master's cycles, one whose
address is the chip's for 40 ns on its way to another window's; likewise
past `+0C` and `+08` with a byte waiting. Nothing answers, CS does not
move, FULL stays, no command is latched. The engine also checks every
flip-flop's and register's set-up and hold on the way, any pulse under
5 ns on a clock, a clear or CS, and two outputs on a net for more than
3 ns: none. Planted faults that bring a decode back onto a strobe (FULL's
clear, the command latch's clock) fail it.

**3. The static timing pass** (docs/timing.md) has the video card in the
machine's runs for its bus side: nothing fails at 3 MHz worst case, and
the machine's fastest clock is what it was. `-WAIT` reaches the CPU card
104 ns before it is needed, the cycle's GAL has its inputs 202 ns early,
the CS flip-flop its data 147 ns early. The pass now also says how early
the machine needs a slave's read data on the D lines: **140 ns** before a
cycle's last edge at worst case, 71 typical. The chip's word is checked
against that figure in 2.

**4. The card in a machine at gate level** (`generators/sim_sys.py
--video`): the CPU, MMU / data, memory, I/O and video cards as wired, with
`p32io.h`'s models at the pins of the chip and of the ATmega328P. A short
program, `emulator/test/vidcard_test.asm`, runs there and on `p32uemu -C`
with the same trace, instruction for instruction, and the same cycles,
at 3 MHz and at 4: the chip's cycle, its registers and FIFO, the PS/2
latches, **the interrupts taken, each with its vector from the
acknowledge cycle**, bus errors. The whole of `video_test` was run the same
way once, by hand (ten minutes a run): identical, 70,585 cycles at 3 MHz
and 92,055 at 4 (before D109 gave the PS/2 firmware its time; not run
again since).

Since D109 the ATmega's stand-in there follows the firmware's model clock
by clock (§4.6): the model says in which clock the firmware loads a byte or
reads the command latch, the stand-in moves LDDATA, -RDCMD and MD0-7 for
it, and **after every clock the card's own FULL and BUSY flip-flops must
equal the model's, or the run stops**. `vidcard_test` has the gap after a
read, a command written over another at the card's latch, and a second
PS/2 interrupt that comes when the firmware loads the next byte by itself:
1,555 cycles at 3 MHz and 1,731 at 4, identical; and identical again with
one to four instructions put in before the reads and the interrupts (by
hand, once).

**What the machine-level run found.** The emulator saw the chip's interrupt
an instruction too soon. The CPU card samples the `-IRQ` lines in the
middle of a bus clock; the chip takes a register write at that same moment
of its cycle's last clock, as CS rises. So a request that a write causes
(the kernel turning "write FIFO empty" on) is first seen in the next
microcycle, and taken after the next instruction. `p32uemu -C` now has it
so; nothing else changed for it.

**What these do not cover:**

- **The chip's real delays where the manual gives none** (its minimums; how
  long after a write its IRQ moves). The bench decides.
- **The PS/2 firmware's real figures.** Its time is modelled since D109
  (§4.6), from the sketch and the Arduino core's own delays: estimates. The
  bench has the last word, and software may depend on none of them.
- **The dot side in the machine-level run:** the chip's memory side stands
  still there (no dot clock); check 1 is what runs it.

### 4.9 The schematic, the board and the review (D108)

**The schematic** is drawn from the netlist (`generators/p32kicad_sch.py`):
eleven sheets, `hardware/video-card/kicad/video-card.kicad_sch` and its
PDF. KiCad's rule check finds nothing, and the netlist KiCad reads back
from the drawing is the generator's: 351 nets, the same pins on each.

**The board** (`video-card.kicad_pcb`; placed by `p32kicad_pcb.py` from the
rows in `card_video.py`, routed by Freerouting): 340 × 366.7 mm, six layers
(signals on the two outer and two inner ones, a ground plane and a supply
plane), every part through-hole. 730 connections, all routed, in under
five minutes; 59.5 m of track. Down the card:

| where | what |
|---|---|
| the top two rows | the bus side: comparators, buffers, the five GALs' first four, the CS flip-flop |
| the middle, towards the front | **the kept part**, six rows close together (below) |
| under it | the five resistors of the video output, beside the DE-15 |
| lower | the PS/2 section: the ATmega328P, its crystal, the three latches |
| the bottom row | the interrupt logic; along the bottom edge the ten jumpers (window, two levels, TEXT), each named |
| the front edge | the power LED and the DTACK test point at the top; the RAM-size jumper (128K / 512K at its ends); the DE-15 ("VGA"); the two mini-DINs ("KEYBOARD", "MOUSE"); the FTDI header for the ATmega (GND and DTR at its ends) |

Every part has its reference and its type or value on the silkscreen,
outside its outline. **Five chips are turned end for end**, pin 1 at the
right where every other chip's is at the left: the ACRTC, the two address
latches (74ACT573) and two of text method A's latches (74ACT574). Each says
`PIN 1 >` on its line of the silkscreen. Why they are turned is the next
part.

**The tracks as delays.** The kept part's margins are small: YACC1-D's own
check (§4.8, 1) leaves 0.2 ns at the least, and a track costs about 0.07 ns
a centimetre. So the layout was reviewed with the tracks in the timing:

- `generators/board_tracks.py` measures, on the routed board, the track from
  every pin that can drive a net to every other pin of it, and how much of
  it runs on an inner layer (`video-card-tracks.json`, with the board file's
  SHA-256: `make test` fails if the file is not the committed board's;
  `build_board.sh` writes it after routing).
- `sim_video_timing.py` then runs its checks again with each pin seeing
  what another pin drives that much later: 5.9 ps a mm on an outer layer,
  7.0 between the planes. A driver whose net is over the 50 pF the data
  sheets' delays are given at (the track at 0.12 pF a mm, 5 pF a pin) is
  slower by 0.03 ns a pF. **These figures are estimates**; the point of the
  check is that no margin hangs on them. Two outputs on a net at once are
  judged as before, by when each is on. With every track of length nothing
  the runs give §4.8's figures exactly.

**What it found.** The first routing, of a placement made row by row from
the netlist's groups, **failed one run in twenty**: with text method A (the
eight sockets the first build leaves empty), chip slow, the left character's
latch got its data 0.7 ns too late. The first build's own runs passed; over
all the runs the dot flip-flop was down from 7.2 ns to 0.7. The router was
not at fault (its
tracks were within a tenth of the shortest trees that join the pads): the
parts were in the wrong places, and the chip faced the wrong way, its
memory side away from the memories.

Taking each family of nets' tracks away in turn (`sim_video_timing.py
--cost`) shows what the thin margins hang on. It is not the address path
(the chip's MAD lines, the latches, the RAMs' address lines), whose tracks
cost none of them anything: it has time in hand.

| margin | what its tracks cost |
|---|---|
| **MAD's hand-over** (the buffers off before the chip is on; 0.2 ns) | the way from the oscillator through the buffers' flip-flop and gate to the two 74ACT245 (`CK`, `BUFQN`, `BUFEN#`), against the way to the chip (`CK` to the counter, `2CLK`): the first must not be the longer |
| **text method A's latches' set-up** (2.9 ns) | `2CLK`; the RAMs' chip enable (`CHR` to the gate that inverts it, `CHRN`, `RAMCE#`); the RAMs' data lines to the latches; less the dot clock's way to the latches' own clock |
| **one memory off before the next is on** (2.0 ns) | the output enable `MOE#` to the three memories, which must reach them alike; `AS#` |
| **the dot flip-flop's set-up** (7.2 ns) | the dot's way through its gates |

So the placement is made for those nets (the comment in `card_video.py`
lists it): the chip turned, its memory side down; the counter under its
`2CLK` pin; the oscillator between the counter and the buffers' flip-flop;
that flip-flop's gate straight below it, between the RAMs and the buffers;
a RAM, and under it its text latch; the gate of `MOE#` between the font and
the RAMs. A placement can be tried without routing it:
`board_tracks.py --estimate` takes each net as the shortest tree of its
pads, and came within 0.3 ns of the routed board on the three thinnest
margins (within 1 ns on the others).

**The board as routed** (all 20 memory-side runs pass, and the bus side's
at every second phase of the chip's clock, 276 runs):

| the least to spare, ns | no tracks | this board's tracks |
|---|---|---|
| two outputs on MAD at a hand-over | 0.2 | 0.5 |
| one memory off before the next is on | 2.0 | 1.9 |
| text method A's latches: data before their clock | 2.9 | 1.3 |
| the dot flip-flops' D (74F74) | 7.2 | 4.9 |
| the counter's -LOAD | 7.7 | 6.3 |
| RAM write data | 14.4 | 12.0 |
| the chip's read data | 19.2 | 18.2 |
| bus side: CS low over three 2CLK cycles | 12 | 12 |
| bus side: a read's word on D before the cycle ends (140 needed) | 196 | 190 |

The first figure went up, not down: the chip's clock now takes a little
longer to arrive than the order to the buffers does. It is still only half
a nanosecond, and with the dot clock's way to three parts deciding it, **a
board routed again must be measured again**; `make test` does that.

**The pre-order check** (`pcb-check --layers 6`, D100's master check): no
failure. Two warnings: the files are not yet committed (they are after this
commit), and two footprints are not in its table, so not checked by it: the
27C256's (an ordinary 28-pin, 0.6-inch DIP) and **the HD63484P8's**, drawn
as a 64-pin DIP with 0.9 inches (22.86 mm) between its rows, as Hitachi's
DP-64 outline is: to be held against a chip before ordering (§8).

**What the review does not cover:** reflections and crosstalk (a track is a
delay here, nothing else); the chip's outputs' own dependence on their load
(its sheet's test load is not 50 pF); the PS/2 section's lines (slow); and
everything §4.8 lists.

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
- **Tests** (`make test`): the chip's model; `video_test` (14 cases since D109, both
  emulators identical, and with the card's timing at 3 and 4 MHz); P32W
  Unix on the video console (`session23`, as recorded before the change);
  the debugger. Revision 0's two gate-level runs (`vidcard_test`,
  `ps2_test`) went with its model, and its netlist with step 3 (D106);
  `sim_sys.py --video` runs this card since D107 (§4.8).
- **Programs draw through `/dev/video`** (D110; `video(4)`,
  docs/kernel.md): the chip's command words written, what a command read
  back read. One program at a time; the console's text is kept meanwhile
  and drawn again when the program closes the device or ends, the chip's
  drawing state and the font made afresh. `/usr/examples/draw.c` is a
  first picture; `sh /usr/tests/run video` checks it on the machine.
- **A C library over it, `libgfx`** (D111; `gfx(3)`, `cc ... -lgfx`):
  dots, lines, boxes, circles, ellipses, arcs and text, in white, black
  or XOR. Of the chip it uses DOT, ALINE, ARCT, AFRCT and AGCPY, each with
  area mode 010, so the chip does the clipping; circles are the
  library's own dots, drawn as runs, not the chip's CRCL.
  `/usr/examples/clock.c`; `emulator/test/gfx_test.c` checks it dot by
  dot against the model.
- **Still to do in software**, when wanted: BASIC's statements over the
  library, the mouse for programs (the backlog); circles, arcs and PAINT
  in the model, and then in the library; text method A (character
  screens, the cursors) in the model and a driver.

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

## 6a. The picture on the bus as well (asked 2026-11-12; decided: left off, D106)

Ken asked what it would take. It can be done, as a second port that
steals the chip's idle memory cycles, but it reopens the kept memory side.
The recommendation given: leave it off. **Ken: "leave it off"** (D106): the
card has no such port, and the netlist was written without it. What it
would have taken stays here for the record.

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
- **Had it been wanted**, the time was before the netlist, as a socketed
  block like method A's, with the timing check run fitted and unfitted.

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

Decided after (D106, 2026-11-12): **the picture is not on the bus** (§6a;
Ken: "leave it off").

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
- **The HD63484P8 against its footprint** (§4.9): 64 pins, 0.1 inch apart,
  the two rows 0.9 inches (22.86 mm) apart. `pcb-check` has no entry for it.
  And the mini-DINs' against the parts in hand.
- **Five chips are turned end for end** (§4.9: U15, U28, U29, U46, U47),
  marked `PIN 1 >`: to remember when the sockets go in.
- **A board routed again is measured again** (§4.9): the tracks' file is of
  one board file; `build_board.sh` rewrites it and `make test` runs it. If
  a run then fails, `sim_video_timing.py --cost` says which nets to look at.
- **The five GALs' fuse files against GALasm.** The other cards' were each
  assembled by GALasm too and found identical (D67); GALasm is not on this
  Mac. They are checked through the fuse map against their specifications,
  as all are.
- **The supply currents marked "estimate"** in docs/power-budget.md for this
  card's new parts (the RAMs, the EPROMs, the 74F299, 74F74 and 74F21, the
  ATmega), against their data sheets.
- **The data buffers going off as a read ends** (RUN, the flip-flop's
  clear, GAL VDEC, the 74HCT245): about as late as the I/O card's buffers,
  up to some 115 ns into the next cycle at worst case. The timing checks
  look at set-up, not at this; the other device cards are the same.
- **The PS/2 firmware's time on the real card** (§4.6): how long FULL is
  clear after a read, BUSY after a command, the two seconds from a reset.
  The tests wait for the flags, so different figures do not break them.

## 9. The order of work

1. **This note: Ken's choices** (§7). Done, D104.
2. **The software in the emulators:** the chip's model, the three waits in
   `p32uemu -C`, the debugger's Screen, the kernel's driver, the tests.
   Done, D105 (§5): P32W Unix runs on the new card in both emulators.
3. **The netlist** (`generators/card_video.py`): the 35 kept ICs transcribed
   from `v30_netlist.py` and **checked equal to it, part for part, pin for
   pin, net for net**; the bus side new, and run at gate level as wired.
   Done, D106 (§4.7). Revision 0's netlist, its 18 GALs and its place in the
   timing pass went with it.
4. **The simulation and the timing:** `v30_netsim.py`'s memory-side scripts
   run on P32W's netlist at their eight corners, in `make test`. The bus
   side against a model of the chip's host side from figures T-2 and T-3,
   at every phase of 2CLK against `CLK`, checking each line of §3's table.
   Then the card at gate level with the CPU, MMU / data and memory cards
   against `p32uemu -C` (`sim_sys.py --video`), and the bus side in the
   static timing pass (docs/timing.md). Done, D107 (§4.8).
5. **The schematic, the layout** (six layers, the layout rules), `pcb-check`,
   a design review. Done, D108 (§4.9): the review ran the timing checks
   with the board's tracks as delays and the kept part was placed again for
   what they showed. Revision 0's schematic and board in
   `hardware/video-card/kicad` are replaced.
6. **Ken orders it**, after §8's checks by hand and the pre-order check on
   the committed files (`~/Developer/pcb-check/pcb-check
   hardware/video-card/kicad/video-card.kicad_pcb
   hardware/video-card/kicad/video-card-gerbers.zip --layers 6 --tests`,
   then `--archive --fab F --order NUMBER`).
