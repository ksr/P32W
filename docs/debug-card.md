# The debug card (bus tester)

**Status:** NETLIST DONE AND VERIFIED at gate level (2026-10-26; §8):
`generators/card_debug.py`, **144 ICs, 6 of them GALs**, plus the Pico 2;
306 parts. §7's choices are Ken's. **D100 and D101 (2026-11-11), getting it
ready to order:** a two-digit status display (two TIL311s); the mode
switch turned to the edge; the capture SRAMs' write race, the power-on
state and `-BREQ0` in LISTEN put right; DRIVE reaching the grants, `-FAST`
and the interrupt lines; and this page brought into line with the netlist
(how the card works in detail: [its theory of operation](debug-card-theory.md)).
**D102 (2026-11-11):** it cannot drive against a CPU or MMU / data card
that is in its slot (`-CPUIN`, `-MMUIN`; §1, §4).
**D128 (2026-11-16, the review before building):** its three open-drain
chips are 7406s, not 74HC05s: two of their inputs come from GALs, whose
high level a 74HC05 is not promised to read (theory §9). The board was
reworked in place: every track and via as it was.
**THE BOARD IS ROUTED (2026-11-11, again for D102):** 6 layers, 620 nets,
nothing left open, 137.3 m of 0.2 mm track and 425 vias; the pre-order
check (`pcb-check`, with the system's tests) passes with no failures
(again after D128).
**D135 (2026-11-18): the firmware.** The Pico's firmware
(`hardware/debug-card/firmware`) and the Mac's `tools/dbgcard.py`, written
and run against the card's gate-level netlist in `make test`: every
function of the card's but playing the CPU card to an MMU / data card
alone. Writing it found four things for Ken to weigh, none of them a
fault the firmware cannot work round (theory §15, items 5–8).
Before an order, by hand: which end of S1 is LISTEN (a meter on the
part), that the TIL311s' digits stand upright with the front edge up,
the fab's gerber viewer. D50 (the
host link, DMA with the CPU halted, a fast clock), D64 (listen or drive,
never both; every signal; the dedicated slot 0).

## 1. What it is for

Bring-up and test, in two modes selected by a switch at its front edge:

- **LISTEN**: a logic analyzer on **every backplane signal, the CPU's
  local link (J3) included**. Every one of its drivers is held off, by
  wiring and not by software, so it can never disturb a running machine.
- **DRIVE**: it is the machine's master. It runs bus cycles and sets every
  line a card other than the CPU card reads:
  - **with the CPU and MMU / data cards present**, it borrows the bus by
    DMA (slot 0's own request line, the arbiter's highest priority): it
    loads programs, reads and writes memory and devices, with the CPU
    halted or running;
  - **with them pulled**, it also makes `CLK`, `-CLK` and `-RES`, so a
    memory, I/O, disk or video card can be tested alone. It plays the
    other masters' `-BREQ` lines to test the arbiter, and plays the CPU
    card's side of J3 (R, `RSRC`, `RDST`, `MEM`, `SIZE`, `CCLK`, `-WSTB` …)
    to test the MMU / data card alone, reading back `FAULT`, `FC`, `STALL`
    and R.

  **It cannot drive against a card that is in** (D102; Ken: "if debug is
  on drive mode is there protection that it wont drive bus if CPU is?").
  Beside a running CPU a cycle needs the arbiter's grant, as it always
  did. The "alone" setting is the host's word that the cards are out, and
  the card no longer takes it on trust: the CPU card and the MMU / data
  card each tie a line to ground (`-CPUIN`, `-MMUIN`), and while a line is
  low the card's drivers for that card's lines stay off, by wiring (§4).

A Pico 2 is the **host link**: USB to the Mac, a small command set
(read / write memory, run a bus cycle, arm a capture, read it back).

## 2. Slot 0 (D64)

An eleventh backplane position beside slot 1, wired to:
- **J1**: the whole bus cycle (70 lines);
- **J2**: the system lines (`CLK`, `-CLK`, `-RES`, `PWROK`, `-LOCK`,
  `-IRQ0–7`, `-IACK`), the eight radial `-BREQ3–10` / `-BGNT3–10` pairs as
  slot 2 sees them, and **its own pair**, `-BREQ0` / `-BGNT0`, to the
  arbiter;
- **J3**: the local link (62 lines), a third drop on lines that today run
  only between slots 1 and 2; and the two lines that say which of those
  cards are in, `-CPUIN` and `-MMUIN` (D102).

**169 signals in all** (167 sampled: `XCLK` and `-XCLKEN` are the card's
own). This needed two changes elsewhere, on cards still
netlist-only (none built or ordered):
- **the backplane** (its generator): the eleventh position, J3 and the
  fan-in carried to it;
- **the MMU / data card's arbiter**: a ninth requester, slot 0, at the
  highest priority. GAL ARB grows from 9 outputs to 10, its limit.

## 3. LISTEN: capture

- **What is sampled:** all 167 lines, into **22 × 74ACT574** input
  latches. The sample clock is one of:
  - **`CLK`'s rise**, one sample per bus cycle (and per microcycle: `CCLK`
    is the same edge), the state at the cycle's end. This is the normal
    mode, and its samples read like the emulators' traces.
  - **the card's own oscillator halved** (12 MHz: four samples per
    microcycle; D73: at 24 MHz the capture path did not meet its clock at
    the datasheets' worst case), a timing view for glitches and set-up
    problems.
- **Storage:** 22 SRAM chips side by side, one 8-bit slice each, with a
  counter for the address: a circular buffer that keeps running until
  the trigger, then stops after a chosen number of samples (pre- and
  post-trigger). A sample is written in the half clock after it is taken
  (D101), and a bit tells the host the buffer has gone round.
- **The trigger:** a pattern in **trigger RAM**. The address's two halves
  each address an SRAM whose stored bit says "this value matches". The
  host fills it, so any mask or range of addresses costs no extra logic:
  8 independent trigger patterns, on any bus cycle (a read, a write or an
  acknowledge alike). It can also simply start and stop by command.
- **Read-out:** after the capture, the Pico reads the SRAMs slice by slice
  over the card's local bus and sends them to the Mac. A Mac tool turns
  them into a trace (`p32emu -s`'s format for the bus view, or a VCD file
  for GTKWave).

## 4. DRIVE: playing the master

- **Output registers:** **19 × 74HCT574**, whose 3-state outputs reach
  every line it may drive. The register for a line is enabled only in
  DRIVE mode, and only for the lines that mode's sub-setting allows:
  - *bus master*: A, BE, strobes, D, `-BREQ0`;
  - *system* (alone): `CLK`, `-CLK`, `-RES`, `-BREQ3–10`, and since D101
    `-BGNT3–10` and `-FAST`;
  - *CPU side of J3* (alone).
  The mode switch's LISTEN position ties every enable off.
- **Never against a card that is in** (D102). Two lines on J3 tell the
  card who is there: the CPU card ties **`-CPUIN`** (J3 B28) to its ground,
  the MMU / data card **`-MMUIN`** (J3 B30); the debug card pulls both up
  (10k), so a line is high only with that card out. GAL DBDRV, which makes
  every enable, reads them:

  | Cards in | What "alone" may drive |
  |---|---|
  | neither | everything: `CLK`, `-CLK`, `-RES`, the requests, the grants, `-FAST`, D, the bus cycle, LR, J3; a GO cycle needs no grant |
  | the MMU / data card | `CLK`, `-CLK`, `-RES`, `-BREQ3–10`, `-FAST`, LR and J3 (the CPU's side, to test that card). **Not** D, the bus cycle's lines or the grants: they are that card's (its address drivers and strobes are on the bus whenever no grant is out, and its arbiter makes the grants). A GO cycle drives nothing |
  | the CPU card (with or without the other) | nothing. ALONE counts for nothing: no `CLK`, no `-RES`, no group |

  A DMA cycle beside a running CPU (ALONE clear) is not touched by this:
  the arbiter's grant is its protection. The open-drain lines (`-ACK`,
  `-WAIT`, `-LOCK`, `-IRQ0–7`, `-BREQ0`) need none. Both lines are also
  sampled (slice 21), so the host can read which cards are in and say why
  a command did nothing.
- **The interrupt lines** (D101): a register of eight bits pulls any of
  `-IRQ0–7` low by open drain, in DRIVE, beside a running CPU or alone. In
  LISTEN the register is held cleared.
- **One bus cycle at a time:** the Pico loads a cycle (address, byte
  enables, read / write, data) into the registers and says "go". A GAL
  sequencer clocked by `CLK` requests the bus (`-BREQ0`), waits for the
  grant, runs **exactly one bus cycle** in step with `CLK`, honours
  `-WAIT`, latches D and `-ACK` at the cycle's end, and lets the bus go.
  The Pico reads the result. At USB speed that is a few hundred kilobytes a
  second: a kernel loads in well under a second.
- **Alone on the bus** (the CPU and MMU / data cards pulled): the card's
  oscillator, divided, makes `CLK` and `-CLK`; `-RES` is a command. The
  cycle sequencer then needs no grant. With a CPU card in, none of this
  happens (D102, above).
- **Clocking the machine (D76), in either mode:** CTRL1 bit 7 (XCLKEN)
  pulls the bus line `-XCLKEN` low, and the CPU card then takes the card's
  `XCLK` as its oscillator: GAL DBCLK's 12 MHz while CLKRUN (a 1.5 MHz
  machine), or **one edge per STEP command** -- one of the CPU's eight
  phases -- with nothing moving between. With LISTEN capturing at CLK, the
  host can step a program a phase or a bus cycle at a time and read every
  cycle back. `XCLK` and `-XCLKEN` are this card's own two lines, driven in
  LISTEN too (nothing else drives them); every other driver is off in
  LISTEN as before.
- **Testing the MMU / data card alone:** the Pico sets the J3 lines for one
  microcycle, pulses `CCLK` and `-WSTB` as the CPU card would, and reads
  back R, `FAULT`, `FC` and `STALL`. A microcycle at a time, so any TLB,
  fault or steering case can be checked.

## 5. The host link and the rest

- **A Raspberry Pi Pico 2** at the front edge, its USB port facing out
  (D48): 3.3 V, buffered to the card's 5 V by a 74LVC245; an 8-bit local bus
  with seven address lines and strobes to the card's registers.
- **A status display** (D100): two TIL311 hexadecimal digits at the front
  edge, just below the LEDs, showing a byte the Pico's firmware writes
  (local bus address `4D`). The debug card is the front card of the machine
  (slot 0, its components facing the front), so they are always in view.
  The digits stand upright with the card's front edge up, the high digit
  on the left.
- **Not built, and dropped (Ken, D101):** the card's own register window
  on the bus (an ID register and a mailbox for P32W software to talk to
  the Mac), and writing the boot ROM in place with a PROG jumper. The card
  is a tester only; it answers no bus address. The boot ROM's flash chips
  are programmed in a chip programmer (the memory card ties their write
  line high).

## 6. Size

**144 ICs** (22 capture latches, 22 capture SRAMs, 19 drive registers,
the trigger RAMs, counters, six GALs, buffers, two hex displays) and the
Pico, on a 9U card: about 3.2 A. It is the biggest card after the CPU card, and the one that makes
every other card testable.

## 7. Decided (Ken, 2026-10-26)

1. **Capture depth: 32 K samples** in 22 × CY7C199 (15 ns), with a timing
   mode beside the normal one: one sample per bus cycle by default (about
   11 ms of machine time), or four per microcycle for timing work (12 MHz:
   D73 halved the 24 MHz first decided).
2. **Both changes on the not-yet-built cards:** the backplane's eleventh
   position (slot 0: J1, J2 with the request / grant fan-in, J3), and the
   MMU / data card's arbiter taking slot 0 as a ninth requester at the
   highest priority.

## 8. As built

**The slices.** The 167 lines are grouped into **22 slices of eight**, each
with a 74ACT574 sample latch, a CY7C199 capture SRAM and a 74HCT541 onto
the local bus. The 19 slices of driven lines also get a 74HCT574 drive
register:

| Slices | Lines | Driven as group |
|---|---|---|
| 0–3 | D0–D31 | GD (alone, both cards out), or a DMA write |
| 4–8 | A2–A31, BE0–3, `-MRD`, `-MWR`, `-IACK` | GBUS (alone, both cards out), or a DMA cycle |
| 9–10 | `-RES`, `-BREQ3–10`, CLK, `-CLK`, `-FAST` | GSYS (alone, no CPU card in; CLK from GAL DBCLK) |
| 11 | `-BGNT3–10` | GSYS too, but only with the MMU / data card out as well (D102) |
| 12–15 | LR0–LR31 | GLR (alone) |
| 16–18 | the CPU's J3 lines: RSRC, RDST, MEM, SIZE, SIGNED, SUPER, TLBW, CRSEL, `-WSTB`, LOCKREQ, PCINC, MARPC, MARINC, CCLK | GJ3 (alone) |
| 19–21 | `PWROK`, `-LOCK`, `-IRQ0–7`, its own `-BREQ` / `-BGNT`, `-ACK`, `-WAIT`, FAULT, FC0–3, STALL, `-CPUIN`, `-MMUIN` | no drive register; `-ACK`, `-WAIT`, `-LOCK`, `-BREQ` and `-IRQ0–7` driven by open drain |

**The GALs:** DBADR (the local bus's decoding), DBDRV (every driver's
enable: none without DRIVE, and none against a card that is in, D102), DBSEQ (one DMA cycle on the bus clock),
DBCAP (the capture: run, post-trigger, done; which of the latches or the
SRAMs holds the slices' bus), DBTRIG (the trigger RAMs' patterns and the
bus cycle), DBCLK (CLK and `-CLK` when alone, from the 24 MHz oscillator; and T12, that oscillator halved: the timing mode's 12 MHz, D73).

**The control registers** are 74HCT273s, cleared while the Pico holds
`CRESN` low; a pull-down holds it low until the Pico's firmware runs.
`CRESN` also clears the GO, ARM and STEP flip-flops, the DMA and capture
state machines and the interrupt register (D101). So at power-on nothing
is enabled and nothing can start, whichever way the mode switch stands.

**The local bus** (the Pico's GP0–18): data through a 74LVC245, seven
address lines, `-XWR`, `-XRD`, the 245's direction, `CRESN`. Its register
map is in `card_debug.py`'s opening comment.

**Verified at gate level** (`generators/sim_debug.py`, in `make test`), the
Pico's firmware being a script of local-bus operations:
- **listen**: in slot 0 beside the CPU, MMU / data and memory cards, the
  card captures `hello` (one sample a CLK). After HALT its SRAMs are read
  back through the local bus, and every bus cycle captured is the cycle
  the bus carried (768 samples, 355 cycles); `-CPUIN` and `-MMUIN` are low
  in every sample (both cards are in);
- **dma**: DRIVE, with the CPU running a loop that polls a word. The card
  asks on `-BREQ0`, the arbiter grants slot 0 between the CPU's cycles,
  and the card writes the word; the program sees it and halts with it.
  Then the card reads the word back by DMA, and the snapshot of that
  cycle in the capture SRAMs holds its address, strobe, `-ACK` and data;
- **alone**: only the debug and memory cards. The card makes CLK itself
  (STEP), writes a word into RAM, and reads it back through its latches.
  Also: a byte written to the status display shows on both digits; the
  reset clears GO, ARM and STEP; the interrupt register pulls lines low in
  DRIVE and none in LISTEN; the system group drives a grant and `-FAST`.
  And the cards that are in (D102): with every group and ALONE set,
  `-MMUIN` pulled low turns off the D, bus cycle and grant enables and
  leaves the rest, and a GO then drives nothing; `-CPUIN` low turns off
  every enable and `CLK`; released, they come back;
- **xclk** (D76): the debug and CPU cards. With XCLKEN off the CPU card
  keeps its own oscillator; on, each host STEP advances the CPU's phase
  counter by exactly one (ten steps) and nothing moves between; with
  CLKRUN, one phase every two of the card's oscillator periods; and the CPU
  card's own CLOCK switch at STEP gives one phase per CLOCK STEP press.

**And the firmware itself** (D135, `generators/sim_dbgfw.py`, in `make
test`): its own `core.c`, compiled for the Mac, against the same netlist,
the Mac's tool with it: memory written and read by GO alone and by DMA
beside the running CPU (words, bytes, halfwords, blocks; every address and
data line of a memory bank; a read held by `-WAIT`), captures with trigger
patterns and ranges, the 12 MHz mode, the buffer's wrap, the CPU card
stepped on XCLK, and the local bus's strobe limits.

**Not yet exercised:** playing the CPU's side of J3 against the MMU / data
card alone.
