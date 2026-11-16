# The backplane: theory of operation

**Status:** DESCRIBES THE NETLIST AND THE BOARD AS GENERATED AND CHECKED
(2026-10-29): the netlist `generators/card_backplane.py` and the board
`generators/p32backplane.py`, on the bus definition `generators/p32bus.py`.
The board's summary and parts list are in [the backplane](backplane.md);
the bus, pin by pin, in [the bus](bus.md). D49, D50, D52, D57, D64, D68,
D74, D76, D77, D78, D79. **Brought up to D99 (2026-11-11, Ken): ten slots,
1.5 inches apart, front and back, every part and the power at the back, 18
mounting holes, the slots labelled.** **And to D128 (2026-11-16): 1k on the
strobes (§5), column c's tracks on the bottom layer (§10).**

## 1. What the backplane does

The backplane is **passive**: sockets, copper and the parts no card can
hold (the pull-ups, the clock's termination, the power entry, a +5V LED,
the capacitors, the mounting holes, and its only chip, U1, the supply
supervisor that makes `PWROK`). It carries **the bus** (D50: physical
memory cycles, interrupts, arbitration, the clock, reset and power) to
ten positions, and **the local link** (J3) between the CPU card and the
MMU / data card. Every socket pin's net **is**
`p32bus.busnet(connector, pin, slot)`, from the file the cards take their
pins from, so the backplane cannot disagree with the bus or the cards:
**30 sockets, 124 parts, 194 bus nets** (`card_backplane.py --bom`).

## 2. The slots

Ten positions, **1.5 inches (38.1 mm) apart** (D99; eleven at 1 inch before,
D49, D64), slot 0 at the left: the **front**. The other end, past slot 9,
is the **back**. Each position has three female DIN 41612 C 3 × 32 sockets,
S*n*J1–S*n*J3, mating a card's J1–J3. **Every card goes in with its
components facing the front**: on the board a socket's column c is on the
front side of its column a, and on a card's right-angle connector row c is
the row farthest above the card (§10; to verify with real parts).

| Slot | Card | Its J3 | Silkscreen |
|---|---|---|---|
| 0 | the debug card (D64): listens to every signal, or drives, never both | the local link, a third drop | SLOT 0, DEBUG |
| 1 | the CPU card: drives `CLK`, `-CLK`, `-RES`; its CLOCK jumper, fitted, grounds `-FAST` | the local link | SLOT 1, CPU |
| 2 | the MMU / data card: the CPU's bus master, and the arbiter | the local link | SLOT 2, MMU/DATA |
| 3–9 | any: memory, I/O, disk, video | power only | SLOT *n*, GENERAL PURPOSE |

**J3, the local link** (62 lines: R as `LR0–31`, the microword's memory
fields, `FAULT`, `FC0–3`, `STALL`, `CCLK`, …; nets `L_LR0`, `L_STALL`, …)
joins slots 0, 1 and 2 only, so the microcode's memory controls never
become bus signals (D49). **Slots 3–9** take any card: a card is placed by
its jumpers, not its slot (D50, D52), and the slot sets only its
bus-request priority. The bus definition has a slot 10 as well, and the
arbiter a request pair for it; this backplane has no such position (D99).

## 3. Bused lines and per-slot lines

**Power on every socket:** pins 1–2 of every row VCC, 31–32 GND, row B's
odd pins GND (a guard between rows A and C), J2 row B's even pins VCC too
(D57): **32 VCC and 60 GND pins per card**.

**Bused to all ten slots**, the same pin in every slot: **J1, the cycle**
(70 lines: `D0–31`, `A2–31`, `BE0–3`, `-MRD`, `-MWR`, `-ACK`, `-WAIT`; J1
has no spare pins); **J2, control** (17 lines: `CLK`, `-CLK`, `-RES`,
`PWROK`, `-LOCK`, `-IRQ0–7` on A3–A15, and `-IACK`, `-FAST`, `XCLK`,
`-XCLKEN` on A18–A21); and **J2's 19 spare pins** (A22–A30, C21–C30, D68),
so a later signal needs no new backplane.

**Per slot: the radial request lines.** A requester's own `-BREQ` / `-BGNT`
pins are J2 A16 / A17, and each slot's pair is two nets of its own:

| Nets | From | To |
|---|---|---|
| `-BREQn`, `-BGNTn` (n = 3–9) | slot *n*'s J2 A16, A17 | the same J2 C pin (C3–C18) in slot 2, the arbiter, and in slot 0, the debug card |
| `-BREQ10`, `-BGNT10` | no slot (D99) | their J2 C pins in slots 2 and 0 only |
| `-BREQ0`, `-BGNT0` | slot 0's J2 A16, A17 | slot 2's J2 C19, C20 |

**Not connected:** J2 A16–A17 in slots 1 and 2 (neither card asks for the
bus), J2 C3–C20 in slots 1 and 3–9, J2 C19–C20 in slot 0, J3's signal
pins in slots 3–9, and J3's spare pins there too. In slots 0, 1 and 2
J3's pins B28, B30 and C25–C30, spare when the backplane was drawn, are
joined, slot to slot, as the local link is (D99). Two of the eight are
now signals (D102): B28 is `-CPUIN` and B30 `-MMUIN` (nets `L_-CPUIN`,
`L_-MMUIN`), each tied to ground by the card it names and pulled up on the
debug card, which will not drive a line of a card that is in
(debug-card-theory §9). The backplane only joins them: it has no part on
either. C25–C30 (nets `L_SPARE_J3C25`, …) are six spare private lines.

## 4. Requests, grants and the lock

The backplane only carries the radial lines; the arbiter is **GAL ARB** on
the MMU / data card, clocked by `CLK` ([mmu-card-theory](mmu-card-theory.md)
§9). At a `CLK` edge with no grant out and `-LOCK` high, the requesting
slot of the highest priority, **slot 0 first, then the highest-numbered
slot**, gets its grant and is master **from the next clock**. It keeps the
bus as long as it holds its `-BREQ` low; `-RES` clears every grant. GAL
ARB drives the `-BGNT` lines totem-pole. The disk card's DMA engine (D75)
asks from GAL DKENG, the debug card through a 7406. **The request lines'
pull-ups are on the MMU / data card**, not here: a 10k on each of `-BREQ0`
and `-BREQ3–10` holds an empty slot's request high.

**`-LOCK`** (J2 A7) is open-drain, from a 74HC05 on the MMU / data card (a
`SWAP`'s read and write, D51, D54) and a 7406 on the debug card; while it is
low the arbiter grants nothing. **The masters** (the MMU / data card for
the CPU, the disk card's DMA engine, the debug card) drive the address,
byte enables, `-MRD` / `-MWR` and a write's data through three-state
drivers; only the MMU / data card and the debug card drive `-IACK`.

## 5. The pull-ups

**The rule** (D68): every J1 / J2 bus line that **no card drives with a
totem-pole output** gets a resistor to VCC here, and no other line does.
The generator reads every card's drivers and fails if a line breaks the
rule either way.

| Line | Pin | Ref | Value | Driven by |
|---|---|---|---|---|
| `-ACK` | J1 B28 | R2 | 1k | open drain or collector: a 74HC05 on the I/O, disk and video cards, a 7406 on the memory and debug cards (D128) |
| `-WAIT` | J1 B30 | R3 | 680 Ω | 74HC05: the I/O, disk, video cards; 7406: the debug card |
| `-LOCK` | J2 A7 | R4 | 1k | 74HC05: the MMU / data card; 7406: the debug card |
| `PWROK` | J2 A6 | R5 | 10k | U1, open collector (§8.2) |
| `-FAST` | J2 A19 | R6 | 10k | the CPU card's CLOCK jumper: fitted, ground (4 MHz); open, or no CPU card, high (3 MHz) |
| `-IRQ0–7` | J2 A8–A15 | RN1 | 2.2k × 8 | 74LS145, open collector: the I/O (nine), disk, video cards |
| `D0–31`, `A2–31`, `BE0–3` | J1 | RN2–RN12 | 10k × 8 | three-state: the masters; on a read, the addressed card's `D` |
| `-MRD`, `-MWR` | J1 B24, B26 | R9, R10 | 1k | three-state: the masters (a GAL on the MMU / data card, a 74HCT541 on the disk card, a 74HCT574 on the debug card) |
| `-IACK` | J2 A18 | R11 | 1k | three-state: the MMU / data, debug cards |

- **The open-drain lines** rise only through their resistor, so it is sized
  for speed: about 200 pF of line (ten sockets, the cards' inputs, 45 cm
  of track) and 0.5 RC to reach HCT's 2.0 V (backplane.md §2.2). **`-WAIT`**
  is let go at mid-cycle (on `-CLK`) and must be high by the next `CLK`
  edge: 680 Ω, about 70 ns, 7.4 mA into a 74HC05 at about 0.45 V. **`-ACK`**
  and **`-LOCK`** have a whole clock: 1k. **`-IRQ0–7`** are levels: 2.2k.
- **`PWROK`** (U1's open-collector output, D78) and **`-FAST`** (a static
  level, D74): 10k.
- **The three-state lines** are loose whenever no one drives them: `D`
  outside a read or a write; the address, byte enables and strobes while
  mastership changes hands, or with the MMU / data card out. 10k only holds
  the data, address and byte enables, so no CMOS input floats (the MMU /
  data card reads `D` through 74HCT253s on every cycle).
- **The strobes have 1k** (R9–R11; D128). A held strobe is high, no cycle;
  but a master lets its strobe go **while it is still low**. The debug
  card's `-MRD`, `-MWR` and `-IACK` are bits of a drive register whose
  output enable is on for the one clock of its cycle (debug-card-theory
  §10); the MMU / data card's come from GAL STROBE, whose outputs go off
  when a grant is given (mmu-card-theory §9), about as soon as the
  microword that ends the cycle can raise them. Left low on 10k, a line of
  about 200 pF needs 1 µs to reach 2.0 V, three clocks, and every GAL
  input on it works against the resistor: an ATF22V10C's pin keeper holds
  the pin's last level and takes about 40 µA to turn (its datasheet, §8).
  Meanwhile the memory card, which writes whenever `-MWR` is low in a
  clock's second half (mem-card-theory §3), writes again, with the address,
  data and byte enables no longer driven: the same word a second time as
  long as those lines stay where they were, another word once one of them
  has drifted across a threshold, which on a lightly loaded bus (less
  capacitance) comes sooner. With 1k the strobe is at 2.0 V in 0.51 RC,
  about 0.1 µs, while a 10k line has moved a few tenths of a volt: the
  strobe always ends first. A driver sinks 5 mA. A device master drives no
  `-IACK` (D75), so R11 holds it off during a disk DMA. (The gate-level
  simulations take a pull-up as instant, and so never saw the strobe
  linger: this is from the netlists and the datasheet, not from a test.)
- **No resistor** on the lines a card drives totem-pole: `CLK`, `-CLK`,
  `-RES` (the CPU card's GALs CLKGEN and PANEL), `XCLK`, `-XCLKEN` (the
  debug card's GAL DBCLK); none on the request lines (§4) or on J3.

**The networks** are bussed SIP-9s, each on eight consecutive pins of one
connector column (`pull_networks()`, D79), so that on the board every pin
stands on its own net's row: RN2–RN5, RN6–RN8, RN9–RN12 on J1 columns A,
B, C. A row with no such net leaves its resistor unused: 22 of their 88,
14 of them in RN6–RN8 (row B's odd pins are GND, and the strobes' rows,
B24 and B26, have their own resistors now). They stand, with R2–R6, R9–R11
and the termination, in a field past slot 9, at the back, where each net's
track ends. (Until D128 a thirteenth network held `-IACK` alone.)

## 6. The clock

**`CLK` and `-CLK`** (J2 A3, A4) come from the CPU card in slot 1, from GAL
CLKGEN; `CLK` is high in phases 0–3 of the eight
([cpu-card-theory](cpu-card-theory.md) §3). One period is one bus cycle:
333 ns at 3 MHz, or 4 MHz with the CLOCK jumper (D74). Every card works
from `CLK`. Each line is one straight track past all ten slots.

**The termination** is at the far end from the CPU card, **past slot 9**: on
each line **100 Ω in series with 150 pF to ground** (R7 / C1 on `CLK`,
R8 / C2 on `-CLK`). It damps the reflection from the open end while an edge
passes and draws no current in the steady state, so it is no load on the
GAL driving the line; the values are to settle on the bench (backplane.md
§2.3). The other lines are not terminated: they change while the clock is
steady and are taken at its edges.

**The clock's other lines**, on J2: with **`-FAST`** (A19) low the I/O
card's timer divides by 4, so it ticks at 1 MHz at either clock (D74).
**`XCLK`** and **`-XCLKEN`** (A20, A21; D76) come from the debug card's GAL
DBCLK; the CPU card's GAL CKSEL takes `XCLK` as its oscillator while
`-XCLKEN` is low (12 MHz, or one edge per host STEP). With no debug card,
the CPU card's own 10k resistors hold `XCLK` low and `-XCLKEN` high. With
no CPU card, the debug card makes `CLK` and `-CLK` itself (GAL DBCLK) and
can drive `-RES`, through three-state drivers (D64).

## 7. Interrupts and the acknowledge cycle

**`-IRQ0–7`** (J2 A8–A15) are eight bused open-drain levels, held high by
RN1. A device's level is set by jumpers on its card (D52) and decoded onto
its line by a 74LS145, open collector: one per device on the I/O card, one
on the disk card, one on the video card. The CPU card takes the levels
through a 74ACT574 at `-CLK`'s rise (`card_cpu.py`); the debug card
listens. No level depends on a slot. A level belongs to **one card**; one
card may drive several (D57).

**The acknowledge cycle** (D57) is a bus cycle like a read, with **`-IACK`**
(J2 A18) low in place of `-MRD` and `A4:A2` = the level; the MMU / data
card runs it for the CPU. The card driving that level pulls `-ACK` and puts
its vector, window × 16 + the asking device, on `D7:D0`. **No answer is not
a bus error:** R2 keeps `-ACK` high, and the vector reads `0xFF` through
RN2 and the MMU / data card's own 10k on `D0–7` (5k together). The kernel
then reads the cards' interrupt status registers.

## 8. Power

### 8.1 The supply, the bolts and the capacitors (D78)

**A dedicated +5 V switching supply**, 30 A at least, **40 A recommended**
(e.g. Mean Well LRS-200-5): the machine with four memory cards is 16.9 A
typical and 30.3 A at its datasheet worst case, the backplane itself 5 mA,
79 mA at most ([power budget](power-budget.md)). It is switched on the
mains side; the backplane has no power switch or jumper. **+12 V is not on
the backplane:** the IDE drives take it from the supply's 12 V rail or a
second supply, and their +5 V from the supply directly.

**The entry is two pairs of M4 bolts, both at the back** (D99): P1 (+5V)
and P2 (GND) in the gap between the J1 and J2 rows, P3 (+5V) and P4 (GND)
in the gap between the J2 and J3 rows, over the field past slot 9, where no
channel runs: a plated 4.3 mm hole ringed with vias into its plane, a
ring lug on 10 AWG wire under the bolt's head. Both pairs come from the
same supply, so each carries about half the current. **The planes**, In1
GND and In4 VCC, reach every socket's VCC and GND pins; a card's 32 VCC and
60 GND contacts are rated 1–2 A each, against the largest card's 7.8 A at
most. **H1–H18**, M3 plated holes for the subrack, are on GND (D99). The
board is 14 mm taller than the cards at the top and at the bottom (394.7 mm
against 366.7), and fourteen of the holes are in those two strips, outside
the cards' ends: the corners, one in line with every second gap between
slots (1–2, 3–4, 5–6, 7–8: 76.2 mm apart) and one over the field. Four more
are mid-board, in the gaps between the socket rows.

**The capacitors:** a 100 nF beside each of the 30 sockets and a 10 µF a
slot (C3–C42, in the gaps between the J1, J2 and J3 sockets), and a 1000 µF
16 V beside each pair of bolts (C43 by P1 / P2, C44 by P3 / P4);
every card has its own as well. **D1**, an LED through R1 (1k), labelled
+5V, shows the supply is on: the two stand at the front edge, where they
can be seen (D99), the only parts there.

### 8.2 PWROK: the supervisor (D78)

**U1, a TL7705A** (TL7705ACP, DIP-8), stands at the back with the other
parts (D99), on `PWROK`'s row, so the line runs through every slot and on
to R5 and to it:

| Pin | Net | Use |
|---|---|---|
| 8 VCC, 7 SENSE | VCC | it senses +5 V itself: threshold 4.55 V |
| 2 `-RESIN` | VCC | tied high |
| 1 REF | C45, 100 nF to GND | the datasheet's reference capacitor |
| 3 CT | C46, 10 µF to GND | the delay, 1.3 × 10⁴ × CT: about 130 ms |
| 5 `-RESET` | `PWROK` | open collector, pulled up by R5 (10k) |
| 6 `RESET` | — | unused |

`PWROK` is low until +5 V is above 4.55 V, then for about 130 ms more, and
low again whenever +5 V dips below the threshold. The CPU card's GAL PANEL
registers `RESET = RESBTN or not PWROK` and drives `-RES` onto the bus
([boot](boot.md) §1), so every card is held in reset until power is good.
The debug card listens to `PWROK`. C51 (100 nF) decouples U1.

## 9. An empty slot

Nothing need be fitted in any slot. An empty socket stays on every bused
net, and the bus sees **no `-ACK`** at the addresses its card would answer:
R2 holds `-ACK` high and the MMU / data card makes the cycle a **bus
error**, so a missing card never hangs the bus (the kernel finds the cards
this way, D52). It sees **no wait** (R3), **no interrupt** (RN1; an
acknowledge cycle on a level no card drives reads `0xFF`), and **no
request**: the MMU / data card's 10k holds the slot's `-BREQn` high, so GAL
ARB never grants it. In slots 3–9, J3 is power only. Three are special:
- **Slot 0 empty** (no debug card): the MMU / data card's 10k holds
  `-BREQ0` high; the CPU card's resistors hold `XCLK` low and `-XCLKEN`
  high, so the CPU card keeps its own oscillator.
- **Slot 1 empty** (the debug card alone, D64): R6 holds `-FAST` high, the
  3 MHz setting; `CLK`, `-CLK` and `-RES` have no driver but the debug card.
- **Slot 2 empty:** no arbiter, and no pull-ups on the request lines; the
  backplane's 10k still holds `D`, the address, byte enables and strobes.

## 10. Verified

`python3 generators/card_backplane.py`, in `make test`, checks the
electrical rules (`p32net.check`: every pin connected or declared unused,
no pin on two nets, no net with one end, at most one totem-pole driver);
every pin of every socket in every slot against `busnet()`, every bus net
and spare on two sockets or more; **every card in every slot it may use**
(debug 0, CPU 1, MMU / data 2, memory, I/O, disk, video 3–9), each pin the
card uses wired there; the pull-up rule against every card's drivers, both
ways; and termination only on lines a card drives totem-pole. It prints
`backplane OK: 10 slots (0-9), 30 sockets, 194 bus nets, 124 parts; every
card fits its slots`. `p32bus.py --check`, also in `make test` (which keeps
docs/bus.md current), checks each signal's one pin, the local link's
`RSRC`, `RDST` and `MEM` widths against the microword, and that each radial
line reaches its own slot, slot 2 and, from slots 3–10, slot 0.

**The board** (D77, D79): `p32backplane.py` places **and routes** it, with
no autorouter: **477 × 394.7 mm, 6 layers** (F.Cu, In1 GND, In2, In3, In4
VCC, B.Cu; D99). **The bus runs on three of them with a plane between any
two** (D128): column a's tracks on F.Cu, column b's on In2, column c's on
B.Cu; In3 has only the request lines' risers and a few short links. (Column
c was on In3, so that in every channel a row's b track on In2 lay straight
over its c track on In3, for the whole 36 cm of the bus: 14 pairs, `-MWR`
over A21 and `-ACK` over A23 among them, on the two layers a 6-layer board
presses closest together, about 0.1 mm against the 0.55 mm from each to
its plane. Measured on the board file after the change: no track on In2
runs along one on In3 anywhere.) The sockets are turned 180° so that pin 1 is at the bottom as
on the cards and column c is on the front side of column a (so a card's
components face the front, by KiCad's footprints of the two connectors),
each bused net one straight track. It asserts each socket's pins
where the cards put theirs, each network on eight rows of one column, and
no two nets' tracks overlapping on a layer; KiCad's DRC found **0
violations, 0 unconnected**. **Not yet checked:** the termination's values
(bench), and the sockets' orientation against a real card (backplane.md §4).

## 11. Open questions

1. **Interrupt priority.** Settled 2026-11-11: bus.md's signal table said
   `-IRQn` "(IRQ0 = timer; 7 highest)"; it now says what its Interrupts
   text, isa-system.md, cpu-card-theory §14 and the emulators do: the
   lowest-numbered level first.
2. **The clock rates.** bus.md's cycle text said "5 MHz planned, 3 MHz as
   the safe setting"; since 2026-11-11 it says 3 MHz, or 4 MHz with the
   jumper (D72, D74). Still open: backplane.md §2.2 budgets `-WAIT` at 5
   and 3 MHz only, and its 680 Ω rise (about 70 ns) is over its own 5 MHz
   budget (about 60 ns).
3. **When the arbiter grants.** bus.md: "when the CPU's next microcycle
   does not use the bus"; GAL ARB has no such input and grants at any `CLK`
   edge with no grant out and `-LOCK` high (mmu-card-theory §9). What
   becomes of a CPU cycle stretched by `-WAIT` when a grant falls on one of
   its clock edges is not said. (Still open, and the MMU / data card's to
   settle: backlog, "found by the theory documents", item 1. D128's 1k on
   the strobes makes the hand-over itself safe for the memory, but a
   device's cycle cut short by a grant is still run twice. Since D99 the
   arbiter has no slot 10 to serve, which would free a pin of GAL ARB for
   `-WAIT`.)
4. **`XCLK` is not terminated**, though it is a clock (12 MHz, or single
   edges, D76) driven from slot 0 into the CPU card's oscillator select;
   backplane.md §2.3's reason for terminating only `CLK` and `-CLK` does
   not cover it.
5. **Slot 2 empty:** `-BGNT0` and `-BGNT3–10` are driven, and `-BREQ0` and
   `-BREQ3–10` held, only by the MMU / data card; with the debug card alone
   nothing drives the grants or holds the requests. Intended?
6. **Stale comments and names.** (The mounting holes: settled by D99, which
   puts them at the corners, along both edges and between the sockets, in
   the netlist, the board and backplane.md alike.) p32bus.py's comments give
   J3 as "slots 1-2" and the "arb" lines as "slot 2 only"; `busnet()` wires
   both to slot 0 too. D78 and backplane.md §2.1 call U1's pins 5 and 2
   `RESET` and `RESIN`, the netlist `-RESET` and `-RESIN` (pin 6, `RESET`,
   is unused). cpu-card-theory §14 (a 74HCT574 on `-IRQ0–7`) and
   mmu-card-theory §6 (74HCT541s on the address) predate D72's 74ACT574
   and 74ACT541s.
