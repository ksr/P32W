# The memory card

**Status:** NETLIST DONE AND VERIFIED with the CPU and MMU / data cards
(2026-10-26). **THE BOARD IS ROUTED** (D80; brought up to the rules before
any order, D90): 6 layers, nothing left open, and the pre-order check
(`pcb-check`) passes with no failures. **D128 (2026-11-16, the review
before building):** the gate that puts `-ACK` on the bus is a 7406, not a
74HC05 (§3); no copper changed. **D150 (2026-11-23):** every silkscreen
label turned to read upright with the card standing in the bus (silk only).
Not ordered. Any slot from 3 to 9 (the
bus has a slot 10, the backplane none, D99), 9U × 340 mm (D49),
through-hole (D21). D50, D52, D60, D63. The netlist is
`generators/card_mem.py`.

How it works, block by block: [the theory of operation](mem-card-theory.md).

**48 ICs, 3 GALs** (`python3 generators/card_mem.py --bom`).

## 1. What it holds

- **16 MiB of static RAM in eight banks of 2 MiB** (D60): each bank is four
  AS6C4008 (512K × 8, 55 ns), one per byte lane. Fit the banks in order from
  bank 0: a card holds 2, 4, … 16 MiB.
- **The 1 MiB boot ROM** (four SST39SF020A, 256K × 8, one per lane) on the
  card in window 0, over the first MiB (the RAM under it is hidden).

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
- 55 ns RAM, 70 ns ROM: no wait state at 3 MHz.

## 4. Verification

Its GALs against their specs, exhaustively; the netlist's electrical
checks; and **with the CPU and MMU / data cards at gate level**
(`generators/sim_sys.py --mem`): the ROM's program read through its four
lanes, the RAM through the 32 SRAMs, a write to the ROM a bus error — the
programs run with every instruction's state as `p32emu`'s and every cycle
count as `p32uemu`'s. `make test` runs one.

The emulators take each window's size (`-M 6,0,4`), and the kernel probes
in 2 MiB steps and uses every bank, holes included (D60; `make test` boots
on 6 + 0 + 4 MiB). The boot ROM's monitor keeps its stack in the first
bank.

**How much to fit.** The first card's first MiB is under the boot ROM, so
one bank gives 1 MiB of RAM and two give 3. The window system's test
session (`session26`) runs in two banks (`-M 4,0`) and stops at a `fork`
with one (`-M 2,0`), tried 2026-11-16: fit at least two banks.
