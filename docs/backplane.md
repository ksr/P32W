# The backplane

**Status:** NETLIST DONE (2026-10-26, D68, accepted by Ken 2026-10-27), checked against the bus
definition and against every card's netlist in every slot it may use;
layout to do. The netlist is `generators/card_backplane.py`; the bus
itself, pin by pin, is [bus.md](bus.md) (`generators/p32bus.py`).

**33 sockets, 101 parts** (`python3 generators/card_backplane.py --bom`).

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
  `-LOCK`, `-IRQ0–7`, `-IACK`). **Their 22 spare pins too** (J2 A19–A30,
  C21–C30): a later signal needs no new backplane (as P8X's rev D).
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
| P1 | ATX 24-pin header (Molex Mini-Fit Jr 5566-24A, vertical) | the power entry |
| JP1 | 1 × 2 header, **POWER** | PS_ON#: a jumper (on with the mains) or a latching switch |
| D1, R1 | LED, 1k | **+5V**: the supply is on |
| R2 | 1k | `-ACK` pull-up |
| R3 | 680 | `-WAIT` pull-up |
| R4 | 1k | `-LOCK` pull-up |
| R5 | 10k | PWROK pull-up |
| RN1 | 2.2k × 8, bussed (SIP-9) | `-IRQ0`–`-IRQ7` pull-ups |
| RN2–RN10 | 10k × 8, bussed (SIP-9) | hold D0–31, A2–31, BE0–3, `-MRD`, `-MWR`, `-IACK` (3 resistors spare) |
| R6, C1 / R7, C2 | 100, 150 pF | AC termination of CLK / `-CLK` at slot 10 |
| C3–C46 | 100 nF beside each socket; 10 µF a slot | bypass |
| C47, C48 | 1000 µF 16 V | bulk, at the power entry |

### 2.1 Power: ATX

The supply is an ordinary **ATX power supply** on its 24-pin plug, the
reason the bus has `PWROK`:

| ATX pins | Here |
|---|---|
| 4, 6, 21, 22, 23 (+5 V) | VCC |
| 3, 5, 7, 15, 17, 18, 19, 24 (COM) | GND |
| 8 (PWR_OK) | **PWROK**: the CPU card holds reset until the supply is good, and sees it fall first when power fails |
| 16 (PS_ON#) | JP1 to GND: fitted, the supply runs whenever the mains is on; or wire a switch to it |
| 1, 2, 12, 13 (+3.3 V), 9 (+5 V standby), 10, 11 (+12 V), 14 (−12 V), 20 | not connected: the cards make their own 3.3 V; ±12 V are not on the bus. The disk card's drives take their power from the supply's own drive cables |

Five +5 V pins carry about 30 A (Mini-Fit Jr, about 6 A a pin with
24 positions); each card has 32 VCC pins. **How much current the machine
draws, and so which supply** (modern ATX supplies give most of their power
at 12 V and often only 15–20 A at 5 V): [power-budget.md](power-budget.md).

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
- **The three-state lines**: D0–31 (driven only during a read or a write),
  A2–31, BE0–3, `-MRD`, `-MWR`, `-IACK` (driven by the master, so loose
  while mastership changes hands, or with the MMU / data card out).
  **10k** only holds them, so no CMOS input floats (the MMU / data card
  reads D through 74HCT253s on every cycle). The MMU / data card's own 10k
  on D0–7 (no vector reads `0xFF`) stay; with these, 5k.
- **PWROK**: 10k, for a supply whose PWR_OK is open-collector.
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

## 4. To do before layout

- **The board:** 9U tall (the cards' 366.7 mm connector edge), about
  320 mm wide (eleven slots at 25.4 mm, and the power entry beside slot
  0); the sockets' positions must match the cards' J1–J3 exactly.
  **Layers**: 6 or 8 (P32's backplane went to 8, a plane beside every
  signal layer); the straight-channel routing of P32's (every bus net one
  track along its row's channel) carries over.
- **Mounting holes** for the subrack, plated and on GND.
- **The silkscreen** (the board rules): each slot's number and what it is
  for (DEBUG, CPU, MMU / DATA), POWER at JP1, +5V at the LED, every
  reference beside its part, and the board's name, revision and
  `cottageworker.com`.
- **The supply:** docs/power-budget.md (D69, D72): a 5 V rating of at least 25 A.
