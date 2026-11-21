# The memory card

**Status:** NETLIST DONE AND VERIFIED with the CPU and MMU / data cards
(2026-10-26). **THE BOARD IS ROUTED** (D80; brought up to the rules before
any order, D90): 6 layers, nothing left open, and the pre-order check
(`pcb-check`) passes with no failures. **D128 (2026-11-16, the review
before building):** the gate that puts `-ACK` on the bus is a 7406, not a
74HC05 (§3); no copper changed. Not ordered. Any slot from 3 to 9 (the
bus has a slot 10, the backplane none, D99), 9U × 340 mm (D49),
through-hole (D21). D50, D52, D60, D63. The netlist is
`generators/card_mem.py`.

**D142 (2026-11-20, branch `non-dip-memory`, an exploration):** the boot
ROM is four 28-pin DIP sockets that take the EEPROMs and EPROMs still made
or in Ken's drawers (§1), and a ROM read has one wait state (§3). The
netlist is checked, timed (`docs/timing.md`) and simulated at gate level
with the CPU, MMU / data and I/O cards. **The board is routed for it**
(2026-11-20; Ken: "Lay out both"): the placement as before with the four
28-pin sockets in the ROM's row, 6 layers, nothing left open, 61.0 m of
0.2 mm track and 33 vias; KiCad's ERC clean, its DRC with nothing
unconnected and only the note every card has (the LED's footprint is the
project's own), the schematic's netlist the generator's, and the
pre-order check (`pcb-check --layers 6`) with no failures. Not ordered.

How it works, block by block: [the theory of operation](mem-card-theory.md).

**48 ICs, 3 GALs** (`python3 generators/card_mem.py --bom`).

## 1. What it holds

- **16 MiB of static RAM in eight banks of 2 MiB** (D60): each bank is four
  AS6C4008 (512K × 8, 55 ns), one per byte lane. Fit the banks in order from
  bank 0: a card holds 2, 4, … 16 MiB.
- **The boot ROM** on the card in window 0: four 28-pin, 0.6 inch DIP
  sockets (U44–U47, one per byte lane), over the first MiB (the RAM under
  it is hidden). Fitted: four **AT28C64B-15** (8K × 8 EEPROM, 150 ns,
  still made in DIP; Ken has six), 32 KiB of ROM, repeated through the
  MiB. The monitor is 2 KiB. The sockets are wired so that the other
  28-pin ROMs read in them too (D142), each in its own place:

  | Part | Size | The card reads | ROM in all |
  |---|---|---|---|
  | AT28C64B, 2764, 27C64 | 8K | the whole part | 32 KiB |
  | 27128, 27C128 | 16K | the whole part | 64 KiB |
  | AT28C256, 27256, 27C256 | 32K | its top 16K | 64 KiB |
  | 27512, 27C512 | 64K | its top 16K | 64 KiB |

  Any of them of **250 ns or faster** (§3). `assembler/romsplit.py
  --device NAME IMAGE.bin` makes the four files, each as large as its part
  with the image where the card reads it: program a chip with its file
  from address 0 (`minipro -p AT28C64B -w FILE`). The four must be the
  same kind. Pin 1 and pin 27 of each socket are at +5 V and pin 26 is the
  card's address line MA13: a part is only read on the card, never
  written.

## 2. Jumpers (at the card's edge, labelled)

Fitted = 0, open = 1.

| Jumpers | Meaning |
|---|---|
| `WIN1 WIN0` | the card's window: n × 16 MiB (D52); the first card is window 0, both fitted |
| `BANKS2 BANKS1 BANKS0` | the number of banks fitted, minus 1: all open = 8 banks (16 MiB); all fitted = 1 bank (2 MiB) |
| `ROMEN` | open: this card has the boot ROM (window 0's card); fitted on the others |

## 3. How it works

- **GAL MSEL** selects the card: A31:A26 zero, A25:A24 = the window.
- **GAL MBANK**: the ROM region (the first MiB, if the ROM is fitted) or a
  fitted bank (A23:A21 ≤ the jumpers); **`-ACK`** (open drain) only there —
  and not for a write to the ROM, so that is a bus error, as are addresses
  in unfitted banks. The data transceivers' enable and direction.
- **A 74HCT138** picks the bank's four chips; **GAL MWE** gives each byte
  lane its `-WE` (the enabled lanes only, in the bus clock's second half, the
  address and data settled since the first) and the read's `-OE`.
- **The ROM's wait state (D142).** GAL MWE has a register, `W1`, on the
  bus clock: `WAIT` is high in the first clock of a cycle that selects the
  ROM (`ROMCSN` low, `W1` clear) and a second gate of the 7406 pulls the
  backplane's `-WAIT` low with it, as the I/O card does for its byte
  devices (D72). The master holds the cycle one more clock; `W1`, set at
  the first clock's end, lets `-WAIT` go. So a ROM read has two clocks,
  667 ns at 3 MHz and 500 at 4, and the card is timed for a ROM of 250 ns
  (100 ns from its `-OE`): its data is where it is taken 101 ns early at
  3 MHz at the worst case, and 72 ns early at 4 MHz with typical parts. A
  ROM of 320 ns still passes every run, one of 350 does not.
  Only ROM reads wait: the monitor. The kernel and its programs run in
  RAM.
- **A 7406** (open collector, U48) puts `ACK` on the backplane's `-ACK`.
  It was a 74HC05 until D128: GAL MBANK drives it, and a GAL's high level
  is promised at 2.4 V and stands at about 3.7 V unloaded (the ATF22V10C's
  datasheet), where a 74HC05 wants 3.5 V at a 5 V supply. A 7406 reads
  2.0 V as high and has the 74HC05's pins. It is a line driver, 40 mA
  where `-ACK`'s 1k asks 5, so it has the line's 200 pF down in good time
  by its own datasheet's figures (60 ns in hand at the worst case). It
  takes 30 mA of supply, 51 at most.
- 74HCT541 address buffers and 74HCT245 data transceivers: one load per
  line on the backplane.
- 55 ns RAM: no wait state, at 3 MHz or 4. The ROM: one, above.

## 4. Verification

Its GALs against their specs, exhaustively; the netlist's electrical
checks; and **with the CPU and MMU / data cards at gate level**
(`generators/sim_sys.py --mem`): the ROM's program read through its four
lanes, the RAM through the 32 SRAMs, a write to the ROM a bus error — the
programs run with every instruction's state as `p32emu`'s and every cycle
count as `p32uemu`'s. `make test` runs one. With the I/O card in as well
(`--mem --io`) the ROM's wait states are counted too, and the count is
`p32uemu -C`'s, at 3 MHz and at 4 (D142).

The emulators take each window's size (`-M 6,0,4`), and the kernel probes
in 2 MiB steps and uses every bank, holes included (D60; `make test` boots
on 6 + 0 + 4 MiB). The boot ROM's monitor keeps its stack in the first
bank.

**How much to fit.** The first card's first MiB is under the boot ROM, so
one bank gives 1 MiB of RAM and two give 3. The window system's test
session (`session26`) runs in two banks (`-M 4,0`) and stops at a `fork`
with one (`-M 2,0`), tried 2026-11-16: fit at least two banks.
