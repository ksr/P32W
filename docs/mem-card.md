# The memory card

**Status:** NETLIST DONE AND VERIFIED with the CPU and MMU / data cards
(2026-10-26); layout to do. Any slot from 3 to 10, 9U × 340 mm (D49),
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
bank. To do: the layout.
