# The memory card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-26; and
D128, 2026-11-16: the `-ACK` gate a 7406), `generators/card_mem.py`. The card's summary and jumpers are in
[the memory card](mem-card.md). D50, D52, D60, D63. **D142 (2026-11-20,
branch `non-dip-memory`):** the boot ROM in 28-pin DIP sockets and its wait
state, as the netlist now has them; the board is routed for it.

## 1. What the card does

A memory card is a **bus slave**: it never starts a cycle, it only answers
the ones addressed to it. It holds:
- **up to 16 MiB of static RAM**, in **eight banks of 2 MiB**. A bank is
  four AS6C4008 chips (512 K × 8, 55 ns), one per byte lane. Banks are
  fitted in order from bank 0, so a card can hold 2, 4, … 16 MiB (D60).
- optionally **the boot ROM**: four 28-pin DIP sockets, one per lane,
  fitted with AT28C64B-15 EEPROMs (8 K × 8, 150 ns: 32 KiB in all) or
  another 28-pin ROM of 250 ns or faster (D142; §4). Only the card in
  window 0 has them fitted and enabled.

Up to four memory cards share the bus, each in its own **16 MiB window**
chosen by jumpers (D52), not by its slot. The physical address map:

| Physical address | What |
|---|---|
| `0x0000_0000`–`0x000F_FFFF` | the boot ROM (on window 0's card, with ROMEN open), hiding the RAM under it; a write is a bus error. Its 32 or 64 KiB repeat through the MiB |
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

**GAL MWE**, *the strobes, and the ROM's wait state*:
- each lane's **`-WE`**: a RAM write (`RAMWR`) with that lane's byte enable
  (`BE0`–`BE3`), **only while `CLK` is low**, the bus clock's second half.
  By then the address and data have been on the bus and settled for half a
  cycle, so a write can never land at a wrong address while they change;
- **`-OE`** for every chip: a read of this card (`CARDRD`);
- **`WAIT`** and its register **`W1`** (D142). `W1` is clocked by `CLK`'s
  rise: it is set at the end of a clock in which the ROM is selected
  (`ROMCSN` low) and `W1` is clear, and cleared at the end of the next.
  `WAIT` = the ROM selected and `W1` clear: high through the first clock
  of a ROM read, low through the second. The same two equations as the
  I/O card's GAL IOSTB (D72).

A second gate of the same 7406 puts `WAIT` on the backplane's `-WAIT`
(open collector; the backplane pulls the line up with 680 Ω). The MMU /
data card's GAL FAULT2 turns `-WAIT` low into `STALL`, and the CPU card's
clock makes no edge of its own while that stands: the cycle lasts another
clock. Any other master (the disk card's DMA engine, the debug card)
holds its cycle on `-WAIT` in the same way.

A 7406 open-collector gate (U48) puts `ACK` on the backplane's `-ACK`.
(D128: it was a 74HC05, whose inputs are not promised to read a GAL's high
level, 2.4 V at the least and about 3.7 V unloaded, as high: they want
3.5 V at 5 V. The 7406's read 2.0 V, and it sinks 40 mA: the line is down
49 ns after `ACK` rises at the worst case, by the current the part is
promised to sink. The same pins; nothing else on the card changed. Its
five unused inputs are grounded.)

## 4. The datapath

- **Address buffers:** three 74HCT541s take A2–A20 off the backplane (one
  load per line) onto the card's `MA0–MA18`. A RAM chip uses all 19 lines
  (512 K words of 4 bytes = 2 MiB per bank); a ROM socket has 14, `MA0`–
  `MA13` (16 K words = 64 KiB), of which an 8 K part uses 13. Bytes are
  not addressed individually: each chip is one byte lane, and the byte
  enables pick lanes.
- **Data transceivers:** four 74HCT245s between the backplane's D0–D31
  and the card's `MD0–MD31`, one per lane. They are enabled only when the
  card answers, pointing onto the bus for a read and onto the card for a
  write.
- Each RAM chip's eight data pins are on its lane of `MD`. Its `-CE` is
  its bank's select, its `-OE` the read enable, its `-WE` its lane's write
  strobe. The ROM chips share the lanes; their `-CE` is `ROMCSN`, their
  `-OE` the read enable. On the bus they can only be read; they are
  programmed off the card (writing them through the debug card was
  dropped, D101).
- **A ROM socket (D142)** is a 28-pin JEDEC ROM site wired for every part
  it may hold. Pins 2–10, 21, 23–25 are A0–A12 (`MA0`–`MA12`), 11–13 and
  15–19 the data, 20 `-CE`, 22 `-OE`, 14 ground, 28 +5 V in every one of
  these parts. The three pins that differ are wired once, for all:

  | Pin | On the card | AT28C64B | AT28C256 | 2764, 27128 | 27256 | 27512 |
  |---|---|---|---|---|---|---|
  | 1 | +5 V | not connected | A14: high | VPP: at VCC, a read | VPP: at VCC | A15: high |
  | 26 | `MA13` | not connected | A13 | n.c. (2764); A13 (27128) | A13 | A13 |
  | 27 | +5 V | `-WE`: never written | `-WE` | `-PGM`: high, a read | A14: high | A14: high |

  So an 8 K part is read whole (and its image repeats, pin 26 unused); a
  16 K part whole; a 32 K part in its top half and a 64 K part in its top
  quarter, where their upper address pins are held high. Each pinout was
  checked against its data sheet (Atmel 0270L, 0006M, 0014O, 0015P;
  Microchip DS11107, DS11003). `assembler/romsplit.py --device` puts the
  image where the card reads it.

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
3. The SRAMs answer within **55 ns**, and the data crosses the 245s onto
   D0–D31 at ≈ 200 ns.
4. The MMU / data card steers it onto R, and the CPU card takes it at the
   commit that ends the cycle.

**A read of the ROM** takes two clocks (D142):
1. As above; MBANK's `ROMCSN` goes low, and with it MWE's `WAIT` goes
   high: `-WAIT` is low on the backplane about 190 ns into the first clock
   at the worst case, and `STALL` is at the CPU card's clock 126 ns before
   that clock needs it.
2. The first clock ends with the cycle held; `W1` is set, and `-WAIT`
   lets go.
3. The ROM has had its address since ≈ 136 ns into the first clock: a
   part of 250 ns has its data out by ≈ 386 ns, on the backplane by
   ≈ 426 ns, and through the MMU / data card's byte steering to where it
   is taken by 573 ns at the worst case, of the 667 the two clocks give
   (500 at 4 MHz, where the typical figures leave 72 ns).
4. The cycle ends at the second clock's end, as any read does; `W1` clears
   for the next.

**A write** (a store):
1. The address, the byte enables, `-MWR` and the data come from the start
   of the cycle.
2. `-ACK` as for a read; the transceivers point onto the card.
3. When `CLK` falls (mid-cycle), MWE drives `-WE` low for each enabled
   lane. It rises again with `CLK` at the end of the cycle, and the SRAMs
   take the data on that rising edge. The bus holds the data until after
   the edge.

At 3 MHz the 55 ns RAM leaves well over 100 ns to spare, so **the RAM
never needs a wait state**. Only the ROM uses `-WAIT`: the monitor's own
instructions, one more clock each time one is fetched. Nothing that runs
in RAM, the kernel and every program, is slowed.

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
`make test` runs one such program. The wait state: with the I/O card in
(`--mem --io`) every clock is counted, and the count is `p32uemu -C`'s,
which gives a ROM read one wait state (D142); `generators/p32timing.py`
gives paths through a ROM chip two clocks, and nothing else on the card.
