# The backplane

**Status:** NETLIST DONE (2026-10-26, D68, accepted by Ken 2026-10-27), checked against the bus
definition and against every card's netlist in every slot it may use;
laid out and routed by its generator (D79, §4: 0 DRC violations, 0
unconnected). **Changed 2026-11-11 (D99, Ken): ten slots, 1.5 inches apart,
every part and the power entry at the back, 14 mm taller than the cards at
the top and at the bottom with the edge mounting holes there, the slots
labelled.** **Changed 2026-11-16 (D128, after the review before building):
the strobes `-MRD`, `-MWR` and `-IACK` are pulled up through 1k, not held
through 10k (§2.2), and column c's tracks run on the bottom layer, so that
a plane lies between any two layers the bus runs along (§4).** Not ordered. The netlist is `generators/card_backplane.py`, the board
`generators/p32backplane.py`; the bus
itself, pin by pin, is [bus.md](bus.md) (`generators/p32bus.py`).

**30 sockets, 124 parts** (`python3 generators/card_backplane.py --bom`).

## 1. What it holds

**Ten positions, 1.5 inches (38.1 mm) apart** (D99; eleven at 1 inch
before, D49, D64): the debug slot 0, then slots 1–9. Each has three female
DIN 41612 C 3 × 32 sockets, vertical, through-hole: **S*n*J1, S*n*J2,
S*n*J3**, mating the card's J1–J3.

**Front and back (D99).** Slot 0's end is the **front**; the other end,
past slot 9, is the **back**, and holds every part that is not a socket or
its bypass, and both power entries. The one exception is the +5V LED with
its resistor, at the front edge where it can be seen.

**Taller than the cards (D99).** A card's connector edge is 366.7 mm; the
board is **394.7 mm**, 14 mm past the cards' ends at the top and at the
bottom, as a standard 9U backplane is. The frame's rails bolt to those two
strips, outside the cards, and the corner and edge mounting holes are in
them. **Every card goes in with its
components facing the front** (toward slot 0): the sockets are turned so
(§4).

| Slot | Card | Its J3 |
|---|---|---|
| 0 | the debug card (D64) | the local link, a third drop |
| 1 | CPU | the local link |
| 2 | MMU / data, with the arbiter | the local link |
| 3–9 | any: memory, I/O, disk, video | power only |

Seven general slots: four memory cards, I/O, disk and video fill them. The
bus definition still has an eleventh position, slot 10, and the arbiter a
request pair for it; this backplane has no such slot (the MMU / data card
pulls an empty slot's `-BREQ` up, so it is never granted).

**The nets are the bus definition's.** Every pin of every socket is on
`p32bus.busnet(connector, pin, slot)`, so the backplane cannot disagree
with docs/bus.md or with the cards:

- **J1 and J2 are bused to every slot**: the cycle (D0–31, A2–31, BE0–3,
  `-MRD`, `-MWR`, `-ACK`, `-WAIT`) and control (CLK, `-CLK`, `-RES`, PWROK,
  `-LOCK`, `-IRQ0–7`, `-IACK`). **Their 19 spare pins too** (J2 A22–A30,
  C21–C30; A19–A21 became `-FAST`, XCLK and `-XCLKEN`, D74, D76): a later
  signal needs no new backplane (as P8X's rev D).
- **The radial request lines.** Each requester's own `-BREQ` / `-BGNT`
  pins (J2 A16 / A17) run to their own pins in slot 2 (the arbiter: J2
  C3–C20) and in slot 0, so the debug card sees every request as the
  arbiter does: `-BREQ3`–`-BREQ9`, `-BGNT3`–`-BGNT9`, and slot 0's own
  pair, `-BREQ0` / `-BGNT0`, to slot 2 (slot 10's pair joins slots 0 and 2
  only). In slots 1 and 3–9, J2 C3–C20 are not connected.
- **J3, the local link** (LR0–31, RSRC, RDST, MEM, SIZE, SIGNED, SUPER,
  TLBW, CRSEL, FAULT, FC, `-WSTB`, STALL, LOCKREQ, PCINC, MARPC, MARINC,
  CCLK), joins slots 0, 1 and 2 only. In slots 3–9 a card's J3 carries
  power and ground; its signal pins are not connected. **J3's eight spare
  pins (B28, B30, C25–C30) join slots 0, 1 and 2 as well** (D99): spare
  private lines between the debug, CPU and MMU / data cards, should one
  be needed after the backplane is made. **Two were needed before it was**
  (D102): B28 is now **`-CPUIN`** and B30 **`-MMUIN`**, which the CPU card
  and the MMU / data card each tie to ground so that the debug card can
  tell they are in. No copper changed, only the two nets' names. C25–C30,
  six pins, are still spare.
- **Power on every socket**: pins 1–2 of every row VCC, 31–32 GND, row B's
  odd pins GND, and J2 row B's even pins VCC as well (D57): 32 VCC and 60
  GND pins per card.

## 2. The parts no card can hold

| Ref | Part | Why |
|---|---|---|
| P1, P2 / P3, P4 | M4 bolt and ring lug, in a plated hole stitched to its plane | the power entry: +5V and GND, two pairs, both at the back (D78, D99) |
| U1 | TL7705ACP (DIP-8) | **PWROK**: the +5 V supervisor (D78) |
| D1, R1 | LED, 1k | **+5V**: the supply is on |
| R2 | 1k | `-ACK` pull-up |
| R3 | 680 | `-WAIT` pull-up |
| R4 | 1k | `-LOCK` pull-up |
| R5 | 10k | PWROK pull-up |
| R6 | 10k | `-FAST` pull-up: the CPU card's clock jumper, open = 3 MHz (D74) |
| R9, R10, R11 | 1k | `-MRD`, `-MWR`, `-IACK` pull-ups: a master may let a strobe go while it is still low (D128) |
| RN1 | 2.2k × 8, bussed (SIP-9) | `-IRQ0`–`-IRQ7` pull-ups |
| RN2–RN12 | 10k × 8, bussed (SIP-9) | hold D0–31, A2–31, BE0–3; each network on eight consecutive pins of one connector column, so it stands beside the socket rows on the board (D79; some resistors unused) |
| R7, C1 / R8, C2 | 100, 150 pF | AC termination of CLK / `-CLK` past slot 9 |
| C3–C42 | 100 nF beside each socket; 10 µF a slot | bypass |
| C43, C44 | 1000 µF 16 V | bulk, one at each power entry |
| C45, C46, C47 | 100 nF, 10 µF, 100 nF | U1's REF, its delay (CT), its bypass |
| H1–H18 | M3 hole, plated, on GND | the subrack (D99): the four corners (H1–H4); in the 14 mm strip above the cards, in line with the gaps between slots 1–2, 3–4, 5–6 and 7–8 and over the parts at the back (H5–H9); the same in the strip below the cards (H10–H14); four mid-board, in the gaps between the sockets (H15–H18) |

### 2.1 Power: a dedicated 5 V supply (D78)

The supply is a **dedicated +5 V switching supply**, 30 A at least and
**40 A recommended** (a known brand, e.g. Mean Well LRS-200-5): the
machine's worst case with four memory cards is 30.3 A, and typical about
17 A ([power-budget.md](power-budget.md)). It is switched on the mains
side (a switch or a switched, fused inlet on the case); the backplane has
no power switch or jumper.

- **The entry: two pairs of M4 bolts**, +5V and GND, both at the back
  (D99: P1 / P2 between the J1 and J2 rows, P3 / P4 between the J2 and J3
  rows, past slot 9). Each is a plated
  4.3 mm hole ringed with vias into its plane, an M4 bolt and nut through
  it and a ring lug on **10 AWG** wire under the head. Both pairs come from
  the same supply's terminals, so each carries about half the current and
  no one point runs hot; one pair alone works for a light machine. A ring
  lug cannot pull out as a clamped bare wire can.
- **PWROK** comes from **U1, a TL7705A supply supervisor** (TI): its
  open-collector `RESET` output holds PWROK low until +5 V is above 4.55 V
  (SENSE on VCC), then for t = 1.3 × 10⁴ × CT (10 µF: about 130 ms), and
  pulls it low again whenever +5 V dips below the threshold. The CPU card
  holds reset until PWROK is high and sees it fall first when power fails,
  as it would have with an ATX supply's PWR_OK. `RESIN` is tied high; REF
  has the datasheet's 100 nF.
- **+12 V is not on the backplane.** The disk card's IDE hard drives take
  +12 V (and their +5 V) from the supply's own outputs if it has a 12 V
  rail, or from a second, 12 V supply; CompactFlash needs 5 V only.
- **Order it** 1.6 mm thick (D133) with **1 oz copper on the inner
  layers** (D134; JLCPCB's default is 0.5 oz): the planes, In1 GND and
  In4 +5 V, carry the whole machine's current from the bolts.

### 2.2 The pull-ups: a rule, checked against the cards

**Every bus line on J1 and J2 that no card drives with a totem-pole output
gets a resistor to VCC here, and no other line does.** The generator reads
every card's netlist to find each line's drivers, and fails if a line
breaks the rule either way:

- **The open-drain lines** (74HC05, 7406, 74LS145): they rise only through the
  resistor, so it is sized for speed. A card lets `-WAIT` go at mid-cycle
  (on `-CLK`); it must be high at the next CLK edge, half a clock later,
  less the card's GAL and gate delays and the master's setup: about 60 ns
  at 5 MHz, 130 ns at 3 MHz. The line is about 200 pF (eleven sockets,
  their cards' inputs, 30 cm of track); reaching HCT's 2.0 V takes
  0.5 RC. **`-WAIT` 680 Ω**: 70 ns, with 7.4 mA into a 74HC05 at about
  0.45 V (HCT reads up to 0.8 V as low). **`-ACK`** and **`-LOCK`** have a
  whole clock: **1k**. **`-IRQ0–7`** are levels: **2.2k**, as P32's.
- **`-FAST`** (D74): a static level, the CPU card's clock jumper, which
  grounds it when fitted (the 4 MHz clock). **10k** holds it high when the
  jumper is open, or when no CPU card is fitted (the debug card alone).
- **The three-state lines**: D0–31 (driven only during a read or a write),
  A2–31, BE0–3 (driven by the master, so loose while mastership changes
  hands, or with the MMU / data card out). **10k** only holds them, so no
  CMOS input floats (the MMU / data card reads D through 74HCT253s on every
  cycle). The MMU / data card's own 10k on D0–7 (no vector reads `0xFF`)
  stay; with these, 5k.
- **The strobes `-MRD`, `-MWR`, `-IACK`** are three-state too, and were on
  10k with the rest until D128. But a master lets its strobe go **while it
  is still low**: the debug card's strobes are register bits behind an
  output enable that is on for the cycle's one clock, and the MMU / data
  card's drivers go off as a grant is given. Through 10k and 200 pF the
  line would take a clock or more to come up, and each GAL on it has a pin
  keeper (a weak latch holding the pin's last level, about 40 µA each)
  pulling the other way. A memory card writes whenever `-MWR` is low in a
  clock's second half, so it would write again in the clock after, with no
  one driving the address, the data or the byte enables. **1k** (R9–R11)
  has the strobe at 2.0 V in about 0.1 µs, long before those lines, still
  on 10k, have moved; a driver sinks 5 mA, which the GALs (16 mA) and the
  74HCT574s and 74HCT541s (6 mA) do. The gate-level simulation could not
  show this: it takes a pull-up as instant.
- **PWROK**: 10k, for the supervisor's open-collector output (D78).
- **Not here:** the radial `-BREQ` lines (the MMU / data card pulls them
  up, so an empty slot asks for nothing, D62), and J3's lines (between the
  CPU and the MMU / data card, which drive them).

### 2.3 Termination

**CLK and `-CLK`** clock every card. The CPU card drives them from slot 1,
near one end; at the other end, **past slot 9**, each line has **100 Ω in
series with 150 pF to ground** (AC termination, as P32's terminator card,
D56 there): it damps the reflection from the open end while an edge
passes, and draws no current in the steady state, so it is no load on a
74HCT or GAL driver. Values to settle on the bench (scope CLK at slots 0,
1 and 10, with every card in). The other lines change while the clock is
steady, and are taken at its edges.

### 2.4 Bypass

A 100 nF (0.1" leads, D47) beside each of the 30 sockets, a 10 µF a slot,
and two 1000 µF at the power entry. Each card has its own bypass and bulk
capacitors as well.

## 3. Verification

`python3 generators/card_backplane.py` (in `make test`):

- the netlist's electrical checks (p32net: every pin connected or
  declared unused, no net with one end);
- every pin of every socket against `busnet()`; every bus net reaches at
  least two sockets;
- **every card in every slot it may use**: the CPU card in slot 1, the
  MMU / data card in 2, the debug card in 0, the memory, I/O, disk and video
  cards in 3–9: each connector pin the card uses must be wired there
  (a device card using a J3 signal, for instance, would fail);
- the pull-up rule above, against every card's drivers; termination only
  on lines a card drives totem-pole.

The gate-level system simulation (`generators/sim_sys.py`) joins the cards
by these same net names, with the open-drain lines' pull-ups modelled.

## 4. The board (D79)

`generators/p32backplane.py` places **and routes** it; no autorouter.
`sh generators/build_board.sh backplane` adds the gerbers, renders, the
placement drawing and the DRC.

- **477 × 394.7 mm, 6 layers** (D77, D90, D99): F.Cu, In1 GND, In2, In3,
  In4 VCC, B.Cu. Slot 0 at the left (the front), 38.1 mm apart; 34 mm of
  board in front of slot 0, the room its card's components have; 14 mm
  above and below the cards' ends (a card's top edge is 14 mm below the
  board's).
- **The sockets** are turned 180°, so that pin 1 is at the bottom as on
  the cards: each socket's a1 height comes from the card generator's own
  placement of J1–J3, so the two cannot disagree. Turned so, column c is
  on the front side of column a. On a card's right-angle connector row a
  is the row nearest its board and row c the farthest above it, so **a
  card's components face the front** (D99). That reading is from KiCad's
  footprints of the two connectors: **VERIFY with a real connector and
  socket before ordering.**
- **Routing:** a bused net is the same pin in every slot, at one height.
  Each pin drops a 1.27 mm stub into the channel between its row and the
  next, and one straight track runs along the channel: column a on F.Cu,
  b on In2, **c on B.Cu** (D128). Until then c was on In3, each of its
  tracks straight under the same row's b track on In2: 14 pairs of bus
  lines one above the other for 358 to 373 mm (`-MWR` over A21, `-MRD`
  over A19, `-ACK` over A23, `-WAIT` over A25 among them), on the two
  layers that a 6-layer board presses closest together (about 0.1 mm of
  prepreg; each is 0.55 mm from its own plane). Now In1 GND lies between
  F.Cu and In2, and In4 VCC between In2 and B.Cu; In3 carries only the
  risers and a few short links, which cross In2's tracks at right angles.
  The request lines (`-BREQn` / `-BGNTn`): the C pins in slots 0 and 2 are
  one channel, carried on along the same row to slot n; there a via and a
  riser **on In3**, between two pin columns, climb to the slot's A16 or
  A17. Every track 0.2 mm, at least 0.395 mm from the
  next pad.
- **Everything else is at the back** (D99), in a field past slot 9: the
  pull-ups, every network pin and resistor end on its own net's row, so
  each net's channel simply runs on to it; the termination (R7 / C1, R8 /
  C2) and the **TL7705A**, on PWROK's row, among them.
- **Power:** the two pairs of M4 bolts (+5V above GND) at the back, in
  the gaps between the socket rows where no channel runs, a 1000 µF
  beside each pair; the **+5V LED (D1) and its resistor (R1) at the front
  edge**, 6 mm in, at the J1 end; the bypass capacitors in the gaps beside
  each slot; the planes reach every VCC and
  GND pin.
- **Mounting holes** (D99): 18, M3, plated and on GND. Fourteen are in
  the two 14 mm strips past the cards' ends, 7 mm from the board's edge:
  the corners, one in line with every second gap between slots (1–2, 3–4,
  5–6 and 7–8: 76.2 mm apart) and one over the field. Four are mid-board,
  in the same four gaps, between the socket rows.
- **Silkscreen:** along the front edge, reading from the front: FRONT,
  **the board's name, revision and `cottageworker.com`, 7 mm tall**, and
  "CARDS GO IN WITH THEIR COMPONENTS FACING THE FRONT (SLOT 0)"; BACK
  along the back edge; **each slot's number
  and what it is for, 3 mm and 2.5 mm tall**, between its J1 and J2
  sockets: SLOT 0 DEBUG, SLOT 1 CPU, SLOT 2 MMU/DATA, SLOT 3–9 GENERAL
  PURPOSE (D99); **"a1" beside pin a1 of every socket**, outside the
  socket's body (below); +5V and GND at each bolt, +5V at the LED, every
  reference beside its part.
- **Fitting the sockets.** The board's wiring fixes how a *card* must
  stand: its components toward the front and its pin 1 at the bottom, so
  that its pin a1 meets the hole wired as a1 (on the memory card J1's a1
  is 89.37 mm below the card's top edge, and so is the board's a1 hole;
  J2 222.72, J3 356.07). The socket itself is 96 straight-through
  contacts, the same electrically either way round, and its markings are
  only markings (Ken); but its keyed shell decides which way a card can
  be pushed in. So each socket is soldered the way round that takes a card
  standing as above: by the connectors' own markings that is with the
  socket's a1 at the board's printed "a1". The other way round, a card
  would only go in turned about, with its a1 in the hole wired as c32:
  every signal on the wrong pin and +5 V and ground swapped (pins 1–2 are
  +5 V, 31–32 ground). To try with a real card connector before
  soldering. A socket's two fixing holes are 0.3 mm off its pins' centre
  line, so turned about they sit 0.6 mm from the board's.
- **Renders:** top, bottom and 3D (`backplane-render-*.png`), as for every
  board since 2026-11-11.
- **The supply:** a dedicated +5 V supply, 40 A recommended (§2.1, D78;
  docs/power-budget.md).
