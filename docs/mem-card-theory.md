# The memory card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-26),
`generators/card_mem.py`. The card's summary and jumpers are in
[the memory card](mem-card.md). D50, D52, D60, D63.

## 1. What the card does

A memory card is a **bus slave**: it never starts a cycle, it only answers
the ones addressed to it. It holds:
- **up to 16 MiB of static RAM**, in **eight banks of 2 MiB**. A bank is
  four AS6C4008 chips (512 K × 8, 55 ns), one per byte lane. Banks are
  fitted in order from bank 0, so a card can hold 2, 4, … 16 MiB (D60).
- optionally **the 1 MiB boot ROM**: four SST39SF020A flash chips
  (256 K × 8, 70 ns), one per lane. Only the card in window 0 has them
  fitted and enabled.

Up to four memory cards share the bus, each in its own **16 MiB window**
chosen by jumpers (D52), not by its slot. The physical address map:

| Physical address | What |
|---|---|
| `0x0000_0000`–`0x000F_FFFF` | the boot ROM (on window 0's card, with ROMEN open), hiding the RAM under it; a write is a bus error |
| `0x0000_0000`–`0x00FF_FFFF` | window 0's card |
| `0x0100_0000`–`0x01FF_FFFF` | window 1's card |
| `0x0200_0000`–`0x02FF_FFFF` | window 2's |
| `0x0300_0000`–`0x03FF_FFFF` | window 3's |

Within its window a card answers only in its fitted banks. An access to a
bank that is not fitted, or to a window with no card, gets no `-ACK`, and
the MMU / data card turns that into a **bus error**. That is how the kernel
finds the memory at boot ([boot](boot.md) §5.2).

## 2. The jumpers

Six jumpers at the card's edge. **Fitted reads 0, open reads 1**: each
jumper shorts its line to ground, and a 10k network pulls it up.

| Jumpers | Meaning |
|---|---|
| `WIN1 WIN0` | the window, 0–3 (both fitted: window 0) |
| `BANKS2 BANKS1 BANKS0` | the number of banks fitted **minus one**: all fitted = 1 bank (2 MiB), all open = 8 banks (16 MiB) |
| `ROMEN` | open: this card has the boot ROM; fitted on every other card |

## 3. The decoding: three GALs

**GAL MSEL**, *is this cycle for this card?*:
- `CARDRD` = A31:A26 all zero, A25:A24 = the window jumpers, and `-MRD`
  low;
- `CARDWR` the same with `-MWR`.

Eight address lines and two jumpers go in, and nothing else on the card
looks at A24–A31.

**GAL MBANK**, *where in the card, and does it answer?*:
- `ROMR`: the ROM is fitted (`ROMEN` open) and A23:A20 = 0, the first MiB;
- `FIT`: the bank A23:A21 is fitted, i.e. bank number ≤ `BANKS`;
- **`ACK`**:
  - a read of the ROM region;
  - a read or write of a fitted bank outside the ROM region;
  - **not** a write to the ROM, so that is a bus error, as `p32uemu`
    models it;
- `ROMCSN` (the ROM chips' chip select), `RAMENN` (enables the bank
  decoder), `RAMWR`, and the data transceivers' enable (`XOEN`, on exactly
  when the card answers) and direction (`DIRW`, for a write).

**A 74HCT138** decodes A23:A21 into the eight banks' chip selects
(`BANK0_N`–`BANK7_N`), enabled by `RAMENN`. Only the addressed bank's four
chips are selected.

**GAL MWE**, *the strobes*:
- each lane's **`-WE`**: a RAM write (`RAMWR`) with that lane's byte enable
  (`BE0`–`BE3`), **only while `CLK` is low**, the bus clock's second half.
  By then the address and data have been on the bus and settled for half a
  cycle, so a write can never land at a wrong address while they change;
- **`-OE`** for every chip: a read of this card (`CARDRD`).

A 74HC05 open-drain gate puts `ACK` on the backplane's `-ACK`.

## 4. The datapath

- **Address buffers:** three 74HCT541s take A2–A20 off the backplane (one
  load per line) onto the card's `MA0–MA18`. A RAM chip uses all 19 lines
  (512 K words of 4 bytes = 2 MiB per bank); a ROM chip uses 18 (256 K
  words = 1 MiB). Bytes are not addressed individually: each chip is one
  byte lane, and the byte enables pick lanes.
- **Data transceivers:** four 74HCT245s between the backplane's D0–D31
  and the card's `MD0–MD31`, one per lane. They are enabled only when the
  card answers, pointing onto the bus for a read and onto the card for a
  write.
- Each RAM chip's eight data pins are on its lane of `MD`. Its `-CE` is
  its bank's select, its `-OE` the read enable, its `-WE` its lane's write
  strobe. The ROM chips share the lanes; their `-CE` is `ROMCSN`, and
  their `-WE` is tied high (on the bus they can only be read; they are
  programmed off the card: writing them through the debug card was dropped,
  D101).

## 5. A read and a write, in time

One bus cycle is one `CLK` period, 333 ns at 3 MHz (CLK high in the first
half).

**A read** (a load, or a fetch):
1. From the start of the cycle the MMU / data card drives the address, the
   byte enables and `-MRD` (mmu-card-theory §6). Its translation takes
   most of the first ≈ 110 ns.
2. MSEL and MBANK decode (two GAL delays, ≈ 30 ns). `-ACK` goes low, the
   bank decoder selects four chips, `-OE` goes low, and the transceivers
   turn on toward the bus.
3. The SRAMs answer within **55 ns** (the ROM's flash within 70 ns), and
   the data crosses the 245s onto D0–D31 at ≈ 200 ns.
4. The MMU / data card steers it onto R, and the CPU card takes it at the
   commit that ends the cycle.

**A write** (a store):
1. The address, the byte enables, `-MWR` and the data come from the start
   of the cycle.
2. `-ACK` as for a read; the transceivers point onto the card.
3. When `CLK` falls (mid-cycle), MWE drives `-WE` low for each enabled
   lane. It rises again with `CLK` at the end of the cycle, and the SRAMs
   take the data on that rising edge. The bus holds the data until after
   the edge.

At 3 MHz the 55 ns RAM leaves well over 100 ns to spare, so the card
**never needs a wait state** and does not use `-WAIT`.

## 6. Fitting a card

- Fit RAM banks **in order from bank 0** and set `BANKS` to the number
  fitted minus one.
- Window 0's card must have **at least bank 0** (the monitor's stack and
  the kernel live in the first 2 MiB, D60), and its ROM chips with
  `ROMEN` open.
- Every other card: `ROMEN` fitted, its own window. Two cards jumpered to
  the same window would both answer, and fight on D0–31.
- The kernel finds what is fitted by itself: no software setting is needed.

## 7. Verified

GAL MSEL, MBANK and MWE are checked against their specs for every input.
The card runs at gate level with the CPU and MMU / data cards
(`generators/sim_sys.py --mem`): the boot program comes out of its four
ROM chips, its RAM is 32 simulated SRAMs, a write to the ROM is a bus
error, and the programs' state and cycles match the emulators exactly.
`make test` runs one such program.
