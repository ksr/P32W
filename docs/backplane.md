# The backplane

**Status:** NETLIST DONE (2026-10-26, D68, accepted by Ken 2026-10-27), checked against the bus
definition and against every card's netlist in every slot it may use;
laid out and routed by its generator (D79, §4: 0 DRC violations, 0
unconnected). The netlist is `generators/card_backplane.py`, the board
`generators/p32backplane.py`; the bus
itself, pin by pin, is [bus.md](bus.md) (`generators/p32bus.py`).

**33 sockets, 119 parts** (`python3 generators/card_backplane.py --bom`).

## 1. What it holds

**Eleven positions, 1 inch (25.4 mm, 5 HP) apart** (D49, D64): the debug
slot 0, then slots 1–10. Each has three female DIN 41612 C 3 × 32 sockets,
vertical, through-hole: **S*n*J1, S*n*J2, S*n*J3**, mating the card's J1–J3.

| Slot | Card | Its J3 |
|---|---|---|
| 0 | the debug card (D64) | the local link, a third drop |
| 1 | CPU | the local link |
| 2 | MMU / data, with the arbiter | the local link |
| 3–10 | any: memory, I/O, disk, video | power only |

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
  arbiter does: `-BREQ3`–`-BREQ10`, `-BGNT3`–`-BGNT10`, and slot 0's own
  pair, `-BREQ0` / `-BGNT0`, to slot 2. In slots 1 and 3–10, J2 C3–C20 are
  not connected.
- **J3, the local link** (LR0–31, RSRC, RDST, MEM, SIZE, SIGNED, SUPER,
  TLBW, CRSEL, FAULT, FC, `-WSTB`, STALL, LOCKREQ, PCINC, MARPC, MARINC,
  CCLK), joins slots 0, 1 and 2 only. In slots 3–10 a card's J3 carries
  power and ground; its signal pins are not connected, and its spare pins
  nowhere.
- **Power on every socket**: pins 1–2 of every row VCC, 31–32 GND, row B's
  odd pins GND, and J2 row B's even pins VCC as well (D57): 32 VCC and 60
  GND pins per card.

## 2. The parts no card can hold

| Ref | Part | Why |
|---|---|---|
| P1, P2 / P3, P4 | M4 bolt and ring lug, in a plated hole stitched to its plane | the power entry: +5V and GND at the slot 0 end / at the slot 10 end (D78) |
| U1 | TL7705ACP (DIP-8) | **PWROK**: the +5 V supervisor (D78) |
| D1, R1 | LED, 1k | **+5V**: the supply is on |
| R2 | 1k | `-ACK` pull-up |
| R3 | 680 | `-WAIT` pull-up |
| R4 | 1k | `-LOCK` pull-up |
| R5 | 10k | PWROK pull-up |
| R6 | 10k | `-FAST` pull-up: the CPU card's clock jumper, open = 3 MHz (D74) |
| RN1 | 2.2k × 8, bussed (SIP-9) | `-IRQ0`–`-IRQ7` pull-ups |
| RN2–RN13 | 10k × 8, bussed (SIP-9) | hold D0–31, A2–31, BE0–3, `-MRD`, `-MWR`, `-IACK`; each network on eight consecutive pins of one connector column, so it stands beside the socket rows on the board (D79; some resistors unused) |
| R7, C1 / R8, C2 | 100, 150 pF | AC termination of CLK / `-CLK` at slot 10 |
| C3–C46 | 100 nF beside each socket; 10 µF a slot | bypass |
| C47, C48 | 1000 µF 16 V | bulk, one at each power entry |
| C49, C50, C51 | 100 nF, 10 µF, 100 nF | U1's REF, its delay (CT), its bypass |
| H1–H8 | M3 hole, plated, on GND | the subrack: the four corners, four in the gaps between the sockets (D79) |

### 2.1 Power: a dedicated 5 V supply (D78)

The supply is a **dedicated +5 V switching supply**, 30 A at least and
**40 A recommended** (a known brand, e.g. Mean Well LRS-200-5): the
machine's worst case with four memory cards is 30.3 A, and typical about
17 A ([power-budget.md](power-budget.md)). It is switched on the mains
side (a switch or a switched, fused inlet on the case); the backplane has
no power switch or jumper.

- **The entry: two pairs of M4 bolts**, +5V and GND, one pair at each end
  of the board (P1 / P2 by slot 0, P3 / P4 by slot 10). Each is a plated
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

### 2.2 The pull-ups: a rule, checked against the cards

**Every bus line on J1 and J2 that no card drives with a totem-pole output
gets a resistor to VCC here, and no other line does.** The generator reads
every card's netlist to find each line's drivers, and fails if a line
breaks the rule either way:

- **The open-drain lines** (74HC05, 74LS145): they rise only through the
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
  A2–31, BE0–3, `-MRD`, `-MWR`, `-IACK` (driven by the master, so loose
  while mastership changes hands, or with the MMU / data card out).
  **10k** only holds them, so no CMOS input floats (the MMU / data card
  reads D through 74HCT253s on every cycle). The MMU / data card's own 10k
  on D0–7 (no vector reads `0xFF`) stay; with these, 5k.
- **PWROK**: 10k, for the supervisor's open-collector output (D78).
- **Not here:** the radial `-BREQ` lines (the MMU / data card pulls them
  up, so an empty slot asks for nothing, D62), and J3's lines (between the
  CPU and the MMU / data card, which drive them).

### 2.3 Termination

**CLK and `-CLK`** clock every card. The CPU card drives them from slot 1,
near one end; at the other end, **slot 10**, each line has **100 Ω in
series with 150 pF to ground** (AC termination, as P32's terminator card,
D56 there): it damps the reflection from the open end while an edge
passes, and draws no current in the steady state, so it is no load on a
74HCT or GAL driver. Values to settle on the bench (scope CLK at slots 0,
1 and 10, with every card in). The other lines change while the clock is
steady, and are taken at its edges.

### 2.4 Bypass

A 100 nF (0.1" leads, D47) beside each of the 33 sockets, a 10 µF a slot,
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
  cards in 3–10: each connector pin the card uses must be wired there
  (a device card using a J3 signal, for instance, would fail);
- the pull-up rule above, against every card's drivers; termination only
  on lines a card drives totem-pole.

The gate-level system simulation (`generators/sim_sys.py`) joins the cards
by these same net names, with the open-drain lines' pull-ups modelled.

## 4. The board (D79)

`generators/p32backplane.py` places **and routes** it; no autorouter.
`sh generators/build_board.sh backplane` adds the gerbers, renders, the
placement drawing and the DRC.

- **376 × 366.7 mm, 6 layers** (D77): F.Cu, In1 GND, In2, In3, In4 VCC,
  B.Cu. Slot 0 at the left, 25.4 mm apart.
- **The sockets** are turned 180°, so that pin 1 is at the bottom as on
  the cards: each socket's a1 height comes from the card generator's own
  placement of J1–J3, so the two cannot disagree. **VERIFY with a real
  card and socket before ordering** (which way the card's component side
  faces).
- **Routing:** a bused net is the same pin in every slot, at one height.
  Each pin drops a 1.27 mm stub into the channel between its row and the
  next, and one straight track runs along the channel: column a on F.Cu,
  b on In2, c on In3. The request lines (`-BREQn` / `-BGNTn`): the C pins
  in slots 0 and 2 are one channel, carried on along the same row to slot
  n; there a via and a riser on B.Cu, between two pin columns, climb to
  the slot's A16 or A17. Every track 0.2 mm, at least 0.395 mm from the
  next pad.
- **The pull-ups** stand in a field past slot 10, every network pin and
  resistor end on its own net's row, so each net's channel simply runs on
  to it; the termination (R7 / C1, R8 / C2) beside them. The **TL7705A**
  is at the left end on PWROK's row (its channel runs left from slot 0).
- **Power:** M4 bolts (+5V, GND) at each end in the gap between the J1
  and J2 sockets; the bypass and bulk capacitors in the gaps; the planes
  reach every VCC and GND pin.
- **Silkscreen:** each slot's number and what it is for (DEBUG, CPU, MMU /
  DATA), +5V and GND at each bolt, +5V at the LED, every reference beside
  its part, and the board's name, revision and `cottageworker.com`.
- **The supply:** a dedicated +5 V supply, 40 A recommended (§2.1, D78;
  docs/power-budget.md).
