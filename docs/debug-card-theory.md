# The debug card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT (2026-10-30),
`generators/card_debug.py` (every chip, net and GAL equation comes from
there). The card's purpose and decisions are in [the debug
card](debug-card.md); the bus cycle and slot 0 in [the bus](bus.md). What
the gate-level simulation (`generators/sim_debug.py`) verifies is §14;
what is unclear, §15. D48, D50, D64, D73, D76. **Since D100 (2026-11-11) the
netlist also has a status display, two TIL311s written at `4D`, and the mode
switch is turned so its lever points out past the front edge (§4, §6); the
board is not yet laid out again for them.**

## 1. What the card does

The debug card is the machine's bus tester. It sits in **slot 0**, which
every backplane signal reaches, in one of two modes chosen by a switch at
its front edge, never both at once (D64):

- **LISTEN**: a logic analyzer. It samples 164 lines (the bus, the system
  lines and J3, the CPU card's local link) into 32 K samples, with a
  trigger. None of its bus drivers can turn on.
- **DRIVE**: a bus master. With the CPU and MMU / data cards in, it
  borrows the bus by DMA, one cycle at a time. **Alone** (the CPU card
  pulled) it makes `CLK` and `-CLK` and may drive the bus cycle, `-RES`,
  `-BREQ3–10` and the CPU card's side of J3.

In either mode it can **clock the CPU card** over its own two bus lines,
`XCLK` and `-XCLKEN` (D76). A **Raspberry Pi Pico 2** is the host link: USB
to the Mac, an 8-bit local bus to the registers. **Its firmware, USB
command set and Mac-side tools are not written yet**: this chapter gives
the register interface, and §12 walks a host through it.

## 2. Slot 0

Slot 0 is an eleventh backplane position beside slot 1 (D64). It carries
167 signals (`p32bus.busnet()`): J1's 70 (the bus cycle); J2's 35 (`CLK`,
`-CLK`, `-RES`, `PWROK`, `-LOCK`, `-IRQ0–7`, `-IACK`, `-FAST`, `XCLK`,
`-XCLKEN`, every requester's `-BREQ3–10` / `-BGNT3–10` as the arbiter in
slot 2 sees them, and the card's own pair); J3's 62 (LR0–31, the CPU
card's 24 control lines, `FAULT`, `FC0–3`, `STALL`). The card samples 164:
all but `-FAST`, and `XCLK` and `-XCLKEN`, which are its own outputs.

**The card's own pair**, its `-BREQ` / `-BGNT` pins (J2 A16, A17), is in
slot 0 the backplane's `-BREQ0` / `-BGNT0`: the first priority of GAL ARB,
the arbiter on the MMU / data card (mmu-card-theory §9). This chapter uses
the backplane's names.

## 3. The host link

The Pico (A1) sits at the front edge, its USB port facing out (D48). It
runs from the backplane's 5 V through a 1N5817 Schottky into its VSYS;
its 3.3 V output supplies the 74LVC245 and two pull-ups.

| Pico | Net | What |
|---|---|---|
| GP0–7 | PD0–7 | data, through a **74LVC245** to the card's 5 V `XD0–7` |
| GP8–14 | `XA0–6` | the register address |
| GP15, GP16 | `-XWR`, `-XRD` | the write and read strobes |
| GP17 | `XDIR` | high: the Pico drives `XD`; low: `XD` drives the Pico |
| GP18 | `CRESN` | low: the control registers held clear |

The 245 is always enabled: `XDIR` alone decides who drives `XD`. The
address and strobes go straight to the decoders. **Until the firmware
runs**, 10k pull-ups hold `-XWR` and `-XRD` high and a 10k pull-down holds
`CRESN` low, so the control registers stay clear whichever way the mode
switch stands (but see §15, item 2). **A write:** address and data out,
`XDIR` high, `-XWR` low, then high; registers take `XD` at the strobe's
rising edge, and the GO, ARM and STEP flip-flops are preset while it is
low. **A read:** `XDIR` low, `-XRD` low; a 74HCT541 drives `XD`. sim_debug's
`Pico` holds each strobe low for two periods of the card's 24 MHz
oscillator.

## 4. The local bus decode

**GAL DBADR** decodes `XA6–XA3` and the strobes into seven decoder
enables: `SWLON` / `SWHIN` (a write with `XA6`, `XA5` low; `XA4` picks the
74HCT154 for slices 0–15 or 16–21), `SRLON` / `SRHIN` (the same for a
read), `CW0N` / `CW1N` (a write with `XA6` high; `XA3` picks the 74HCT138
for `40–47` or `48–4D`) and `CRN` (a read with `XA6` high: the 74HCT138
for `40–42`). The 154s decode `XA3–XA0`, the 138s `XA2–XA0`. The map
(every local-bus address and value here is hex):

| `XA` | A write | A read |
|---|---|---|
| `00–11` | slice 0–17's drive register (§7) | slice 0–17 (§7) |
| `12–15` | nothing (no drive register) | slice 18–21 |
| `40` | **CTRL0** | **status** |
| `41` | **CTRL1** | the capture address CA, bits 7–0 |
| `42` | the post-trigger count, bits 7–0 | CA bits 14–8 (bit 7 reads 0) |
| `43` | the post-trigger count, bits 15–8 | — |
| `44` | **GO**: one DMA cycle | — |
| `45` | **ARM**: a capture | — |
| `46` | **STEP**: one `CLK` period alone, or one `XCLK` pulse | — |
| `47` | **CSTEP**: CA + 1 | — |
| `48`, `49` | the trigger setup address TS, bits 7–0, 14–8 | as `40`, `41` |
| `4A`, `4B` | trigger RAM A, B at TS | `4A` as `42` |
| `4C` | **CAPCLR**: CA = 0 | — |
| `4D` | **the status display** (D100): two hex digits, bits 7–4 and 3–0 | — |

GO, ARM, STEP, CSTEP and CAPCLR act by their strobe; their data is not
used.

**The status display** (D100; Ken: for status information, always visible,
since the debug card is the front card of the machine). Two **TIL311**
hexadecimal displays, U139 (the high digit, `XD7–4`) and U140 (the low one,
`XD3–0`), stand at the front edge just below the LEDs, on 0.45 in centres.
A TIL311 holds its own 4-bit latch, decoder and LED drivers: its latch
follows its data inputs while its strobe is low and holds them when it is
high. The strobe of both is `DISPN`, the `48–4F` decoder's output for `4D`,
so a write to `4D` shows its byte and the digits keep it; no register chip
is needed. Their blanking inputs are tied low and their decimal points are
not connected. They are never cleared: from power-on until the Pico's
firmware first writes `4D` they show what their latches woke up with. What
the byte means is the firmware's to define. Each draws about 105 mA
(180 mA at most), 0.2 A of the card's 3.2 A. The data must be steady 50 ns
before the write ends and 40 ns after it (TI SODS001D): the firmware's
concern, as for the other registers. Above `3F` the writes ignore `XA5–XA4` and the reads `XA5–XA3`, so
the registers repeat; nothing answers at `16–3F`.

## 5. The control and status registers

Six 74HCT273s hold CTRL0, CTRL1, the post-trigger count (POSTL, POSTH) and
TS (TSAL, TSAH). `CRESN` low clears them. They cannot be read back, so
the host keeps copies.

| Bit | CTRL0 (`40`) | CTRL1 (`41`) |
|---|---|---|
| 0 | GD: alone, D0–31's drive registers on | ODLOCK: `-LOCK` low |
| 1 | GBUS: alone, the bus cycle's | CLKRUN: DBCLK runs (`CLK` alone; `XCLK` with XCLKEN) |
| 2 | GSYS: alone, `-RES` and `-BREQ3–10` | SCLKSEL: the sample clock is 12 MHz, not `CLK` |
| 3 | GLR: alone, LR0–31 | SETUP: the trigger RAMs being written; no trigger |
| 4 | GJ3: alone, the CPU card's J3 control lines | TNOW: trigger now |
| 5 | ALONE: the card drives `CLK`, `-CLK`; the groups may drive; GO needs no grant | LIVE: the slices read the latches, not the SRAMs |
| 6 | ODACK: `-ACK` low | DMAWR: a DMA cycle drives D (a write) |
| 7 | ODWAIT: `-WAIT` low | XCLKEN: `-XCLKEN` low, the CPU card on `XCLK` (D76) |

ODACK, ODWAIT and ODLOCK act only in DRIVE. **Three flip-flops** are set
by the host and cleared by the card, never by `CRESN`: **GOQ** (74HCT74;
GAL DBSEQ clears it at SNAP), **ARMQ** (74HCT74; GAL DBCAP, once the
capture runs) and **STEPQ** (74ACT74, D73; GAL DBCLK, when the step is
done).

| Status (`40`) bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Signal | DRIVE | `-BGNT0` | GOQ | ACT | DONE | ARMQ | TRIG | SNAP |

DRIVE: the switch at DRIVE. `-BGNT0` low: slot 0 granted. GOQ: a DMA cycle
not yet done. ACT: a capture running. DONE: it has finished. ARMQ: armed,
not yet running. TRIG: the trigger condition now. SNAP: the clock after a
DMA cycle.

## 6. LISTEN and DRIVE

The **mode switch** S1 (an SPDT switch at the front edge; the footprint is
E-Switch's EG1224 right-angle slide switch, though the parts list calls it
a toggle: §15) is turned so that its lever points out past the card's edge
(D100): about 4 mm of it stands beyond the edge. It is labelled MODE, with
LISTEN at one end of the lever's travel and DRIVE at the other. It ties the
net `DRIVE` to 5 V (DRIVE) or to ground. `DRIVE`
gates every driver: **GAL DBDRV** makes every drive register's output
enable and `CLK`'s, none without `DRIVE` (§9); a **74HCT08** passes ODACK,
ODWAIT and ODLOCK on only with `DRIVE`; **GAL DBSEQ** asks for the bus only
with it (§10); and **GAL DBCAP** captures only *without* it (§8). So LISTEN
captures and drives nothing; DRIVE drives, and keeps only its DMA cycles'
snapshots. `XCLK` and `-XCLKEN`, the card's own lines, are driven in both
modes (D76).

A 74HC05 lights the mode LEDs through 470 Ω each: **DRIVE** (red) from
`DRIVE`, **LISTEN** (green) from DBDRV's `LISTEN` (`DRIVE` inverted). The
green POWER LED is every card's (D76).

## 7. The slices

The 164 lines are cut into **22 slices of eight** (spare inputs
grounded). Each slice has a **74ACT574 sample latch** clocked by the
sample clock `SCLK`, a **CY7C199 capture SRAM** (32 K × 8, 15 ns) on the
same 8-bit bus, addressed by CA14–0, and a **74HCT541** from that bus onto
`XD` for the host's reads. Slices 0–17 also have a **74HCT574 drive
register**, written from `XD` at the slice's address and driving its lines
while its group is enabled.

| Slice | Its lines, bit 0 first | Group |
|---|---|---|
| 0–3 | D0–7, D8–15, D16–23, D24–31 | D |
| 4–6 | A2–9, A10–17, A18–25 | BUS |
| 7 | A26–31, BE0, BE1 | BUS |
| 8 | BE2, BE3, `-MRD`, `-MWR`, `-IACK` | BUS |
| 9 | `-RES`, `-BREQ3–9` | SYS |
| 10 | `-BREQ10`, `CLK`, `-CLK` | SYS (`-BREQ10` only) |
| 11–14 | LR0–7, LR8–15, LR16–23, LR24–31 | LR |
| 15 | RSRC0–3, RDST0–2, MEM0 | J3 |
| 16 | MEM1, MEM2, SIZE0, SIZE1, SIGNED, SUPER, TLBW, CRSEL0 | J3 |
| 17 | CRSEL1, CRSEL2, `-WSTB`, LOCKREQ, PCINC, MARPC, MARINC, CCLK | J3 |
| 18 | `PWROK`, `-LOCK`, `-IRQ0–5` | — |
| 19 | `-IRQ6`, `-IRQ7`, `-BREQ0`, `-BGNT0`, `-BGNT3–6` | — |
| 20 | `-BGNT7–10`, `-ACK`, `-WAIT`, `FAULT`, `FC0` | — |
| 21 | `FC1–3`, `STALL` | — |

A line's bit is its place in the row: in slice 8 `-IACK` is bit 4, in
slice 20 `-ACK` is bit 4. Slices 18–21 are listened to only; `-ACK`,
`-WAIT`, `-LOCK` and `-BREQ0` have open-drain drivers of their own (§9),
and `CLK`, `-CLK` come from GAL DBCLK. **A slice's bus** is driven by its
latch while LIVE is set, a capture runs, or during SNAP; otherwise by its
SRAM, showing the sample at CA (DBCAP's `LATOEN`, `SROEN`).

## 8. LISTEN: the capture

**The sample clock.** A 74ACT157 makes `SCLK` from `CLK` (SCLKSEL = 0) or
**T12**, GAL DBCLK's register toggling at every edge of the card's 24 MHz
oscillator: 12 MHz (D73). At `CLK`'s rise a sample is the state at a bus
cycle's end (`CCLK` rises at the same edge); 32 K are about 11 ms at 3 MHz
(debug-card.md §7). At 12 MHz there are four per microcycle, taken
asynchronously like a logic analyzer's; 32 K are about 2.7 ms (D73). A
74ACT00 makes the SRAMs' `-WE`, low in `SCLK`'s low half while DBCAP's
`SWEN` is high: a sample is written in the half period after its edge.

**The capture address.** Four 74F161s count CA; bits 14–0 address the
SRAMs and bit 15 is unused, so the buffer is a circle of 32 768 samples.
They count `SCLK` while a capture runs (`ACT`), and otherwise CSTEP, at the
end of its strobe (a 74ACT00, a 74HCT32, a 74ACT04 choose); CSTEP does
nothing during a capture. CAPCLR clears CA at once (the 74F161's clear is
asynchronous); nothing else sets it.

**GAL DBCAP**, clocked by `SCLK`, runs the capture:
1. ARM sets ARMQ. While it is set and nothing runs, `PLDN` has the
   post-trigger counter load.
2. At the next sample edge, in LISTEN: **RUN** (ACT). `ARMCLRN` clears
   ARMQ; DONE clears. The latches take the lines and, in the low half,
   the SRAMs write them at CA (`SWEN`).
3. At every later sample edge the same, CA first stepping by one.
4. At the sample edge that finds `TRIG` high: **POST**. `PCNT` lets the
   post-trigger counter count at each edge.
5. At a sample edge in POST that finds its carry `PDONE` high: **DONE**,
   until the next ARM. CA points one past the last address written.

RUN and POST need LISTEN: moving the switch to DRIVE abandons a capture
without DONE. In DRIVE, `SWEN` is SNAP's (§10).

**The post-trigger count.** Four more 74F161s on `SCLK`, loaded from
POSTH:POSTL; `PDONE`, their carry, is high at `FFFF`. Written as
65 536 − n, the count brings DONE n sample edges after the edge that saw
the trigger: `FFFF` is n = 1, `FF00` is 256. A count beyond the buffer
overwrites the trigger's own sample.

**The trigger.** Two more CY7C199s are the **trigger RAMs**: A is
addressed by A16–A2, B by A31–A17, and bit i of each belongs to **pattern
i**, 0–7. Pattern i matches when A's bit i at the cycle's A16–A2 and B's
bit i at its A31–A17 are both 1 (two 74HCT08s make `M0–M7`). A pattern is
thus any set of low halves with any set of high halves: one word, a mask
(a don't-care bit set at both values), a range within one half.
**GAL DBTRIG** makes `TRIG`: never during SETUP; otherwise TNOW, or a bus
cycle (`-MRD`, `-MWR` or `-IACK` low) whose address any pattern matches.
Reads and writes are alike to it.

**Setting the trigger up** (SETUP = 1): eight 74HCT157s switch both RAMs'
address inputs from the bus to TS14–0; the RAMs' outputs turn off (`-OE`
is SETUP); two 74HCT541s, enabled by DBTRIG's `SETUPN`, put `XD` on their
data pins; a write to `4A` or `4B` strobes RAM A's or B's `-WE`. TS is a
register, not a counter. Nothing clears the RAMs: at power-on they hold
anything (the simulated ones read 0).

**Reading back.** After DONE, `41` and `42` give the final CA. For each
sample the host reads the slices it wants (LIVE off), then CSTEPs; once
the buffer has wrapped, starting at the final CA gives the samples oldest
first. Read-back and trigger setup happen only while nothing is captured
(timing.md §3); during a capture a slice read shows the latest sample.

## 9. DRIVE: the drivers

**GAL DBDRV** makes the output enables, each needing `DRIVE`:

| Group | Slices | On in DRIVE when |
|---|---|---|
| D | 0–3 | ALONE and GD; or a DMA cycle with DMAWR |
| BUS | 4–8 | ALONE and GBUS; or a DMA cycle |
| SYS, LR, J3 | 9–10, 11–14, 15–17 | ALONE and GSYS, GLR, GJ3 |
| `CLK`, `-CLK` | (GAL DBCLK) | ALONE |

"A DMA cycle" is GAL DBSEQ's `BUSEN` (§10). The drive registers are not
cleared at power-on: the host loads them before enabling their group.
**ALONE** is for when the CPU card is out, since the card then drives
`CLK` and `-CLK`. The MMU / data card may stay in: the J3 and LR groups
then play the CPU card's side of the local link to it, and the SYS group
the other masters' `-BREQ` lines to its arbiter (debug-card.md §1).

**The open-drain lines** come from a 74HC05: `-ACK`, `-WAIT` and `-LOCK`
from ODACK, ODWAIT and ODLOCK (through the 74HCT08: only in DRIVE), and
`-BREQ0` from GAL DBSEQ.

## 10. The DMA cycle: GAL DBSEQ

**GAL DBSEQ**, clocked by `CLK`, runs one bus cycle per GO, from GOQ,
`-BGNT0`, `-WAIT`, ALONE and DRIVE:
- **REQ** (DRIVE, not alone): set at a `CLK` edge when GO is pending and
  nothing is under way; held until the grant;
- **CYC**, the cycle: set at the edge that finds REQ and the grant (alone:
  at the first edge after GO, with no request); held while `-WAIT` is low;
- **SNAP**, the clock after: set at the edge that ends CYC.

Its outputs: `BUSEN` = CYC; `BREQOD` = REQ or CYC (`-BREQ0` low); `GOCLRN`
= not SNAP (GO cleared). It does not look at `-ACK`.

**A DMA cycle, edge by edge** (the CPU, MMU / data and memory cards in, no
wait; sim_debug's dma case runs it):

| `CLK` edge | What happens |
|---|---|
| (any time) | the host writes GO: GOQ (status bit 2) |
| 1 | REQ: `-BREQ0` low |
| 2 | the arbiter, the bus free, grants: `-BGNT0` low (bit 1); the MMU / data card's drivers are off while the grant is out (mmu-card-theory §9) |
| 3 | CYC: the card drives A2–31, BE0–3 and the strobe from its registers, and D for a write |
| 4 | the cycle ends (a slave's `-WAIT` would hold it): a write is taken; the latches sample the lines; SNAP: GO clears, `-BREQ0` released, drivers off |
| 5 | SNAP ends; the arbiter drops `-BGNT0` |

GO clears three clocks after edge 1: three to four clocks from the host's
GO, about 1–1.3 µs at 3 MHz. Another master or a held `-LOCK` delays the
grant; the CPU is held (`STALL`) only if it wants the bus meanwhile
(bus.md).

**SNAP** keeps the result: in its low half the SRAMs write the latches at
CA, which does not move in DRIVE. During SNAP the slices show the
latches; after it, the SRAMs. The word read is in slices 0–3, `-ACK` in
slice 20 bit 4 (0: a card answered). The snapshot is the cycle's end only
with the sample clock at `CLK`. No test reads one back (§15, item 1).

## 11. The clock: GAL DBCLK

**`CLK` alone.** GAL DBCLK is clocked by the card's 24 MHz oscillator.
Its phase counter, P2–P0, counts the oscillator's edges
while CLKRUN or STEPQ is set. With `CLKOE` (DBDRV: DRIVE and ALONE) it
drives `CLK` high in phases 0–3 and `-CLK` in 4–7: the oscillator divided
by eight, 3 MHz, as the CPU card makes it (cpu-card.md §1).

**STEP** presets STEPQ; the counter runs until phase 3, where `STEPCLRN`
clears STEPQ. From phase 3 a STEP is one whole period: `CLK` falls at
phase 4, rises at phase 0, and stands high at phase 3. The first STEP
after power-on only takes the counter from 0 to 3, with no rising edge
(sim_debug's alone case steps once first). At phase 3 `STEPCLRN` goes low
as soon as STEPQ is set, so the host's strobe, which holds the preset,
must span an oscillator edge (the simulation's spans two).

**`XCLK`** (D76). `-XCLKEN` is XCLKEN inverted; low, it makes the CPU
card's GAL CKSEL take `XCLK` as its oscillator, ahead of that card's CLOCK
switch and oscillators (cpu-card.md §1). `XCLK` is DBCLK's register XCK:
with XCLKEN and CLKRUN it toggles at every oscillator edge (12 MHz, a
1.5 MHz machine); with XCLKEN and a STEP it is high for one oscillator
period, one edge for the CPU card, one of its eight phases (XDN marks the
pulse given while the host's strobe still holds STEPQ); without XCLKEN it
stays low. `CRESN` low clears XCLKEN, so with no firmware running the CPU
card keeps its own oscillator. **Change XCLKEN with RESET held** (D76):
CKSEL is a plain multiplexer, and a change mid-phase can cut a phase short.

**T12** toggles at every oscillator edge, whatever else is set: the
12 MHz sample clock.

## 12. Using it

There is no firmware yet: each step below is one local-bus write
(`XA` ← value) or read. Sequences marked *tested* are what sim_debug.py's
Pico script does in `make test`; the rest follow from the netlist.

**Start-up.** `-XWR`, `-XRD` high, `XDIR` low, GP0–7 inputs; then raise
`CRESN` (GP18). Every control register starts at 0.

**(a) A capture with a trigger.** The switch at LISTEN. Trigger on any bus
cycle at the word `0x0018_0000` (pattern 0) and keep 256 samples from it:
1. `41` ← `08` (SETUP).
2. For t = `0000` to `7FFF`: `48` ← t's bits 7–0, `49` ← its bits 14–8
   (when they change), `4A` ← RAM A's byte, `4B` ← RAM B's. Here A's byte
   is `01` at t = `0000` (the word's A16–A2), B's `01` at t = `000C` (its
   A31–A17), and both `00` elsewhere.
3. `41` ← `00` (SETUP off; the sample clock `CLK`, or `04` for 12 MHz).
4. `42` ← `00`, `43` ← `FF` (65 536 − 256). `4C` (CA = 0). `45` (ARM).
5. Read `40` until bit 4 (DONE). Without a trigger: `41` ← `10` (TNOW),
   wait for DONE, `41` ← `00`.
6. Read `41`, `42` (the final CA); `4C`; then for each address read the
   slices wanted among `00–15` and write `47` (CSTEP).

By §8 the trigger's sample lies 256 addresses before the final CA (one
more in the simulation: §15, item 1). *Tested* without the trigger RAMs:
sim_debug's listen case does steps 3–6 with the count `FFFF`, ending by
TNOW after HALT and reading samples 0 to CA − 1.

**(b) A write and a read as bus master.** The switch at DRIVE, CTRL0 =
`00`, the CPU and MMU / data cards in. The word `5A5A1234` to
`0x0018_0000` by DMA (*tested*: sim_debug's dma case):
1. `41` ← `40` (DMAWR; the sample clock `CLK`, LIVE off).
2. `00–03` ← `34 12 5A 5A` (D); `04–07` ← `00 00 06 C0` (A2–A31, BE0,
   BE1); `08` ← `17` (BE2, BE3; `-MRD` 1, `-MWR` 0, `-IACK` 1).
3. `44` (GO); read `40` until bit 2 (GOQ) is 0.

Reading it back by DMA (not tested; §15, item 1): `41` ← `00`; `04–07` as
before, `08` ← `1B` (`-MRD` 0, `-MWR` 1); `44`; read `40` until bit 2 is
0; read `00–03` (the word) and `14` (bit 4: `-ACK`).

Alone, with only a memory card besides, `C0FFEE42` to `0x0010_0040` and
back (*tested*: sim_debug's alone case):
1. `40` ← `20` (ALONE: the card drives `CLK`); `46` (STEP) once.
2. `00–03` ← `42 EE FF C0`; `04–08` ← `10 00 04 C0 17`; `09` ← `FF`,
   `0A` ← `01` (`-RES`, `-BREQ3–10` high). `40` ← `27` (GD, GBUS, GSYS,
   ALONE) puts the write on the bus; `46` gives it one `CLK` period (the
   memory card writes while `CLK` is low, mem-card-theory §5); `40` ← `20`.
3. `41` ← `20` (LIVE); `08` ← `1B`; `40` ← `26` (GBUS, GSYS, ALONE); `46`.
   The latches took D at `CLK`'s rise and the clock stands still: read
   `00–03`.

**(c) Stepping the machine's clock.** The CPU card in, the switch either
way (*tested*: sim_debug's xclk case, in LISTEN):
1. Holding the CPU card's RESET, `41` ← `80` (XCLKEN): its clock stops.
   Release RESET.
2. `46` (STEP): one `XCLK` pulse, one phase of eight, nothing moving
   between. Eight STEPs are one microcycle, one bus clock.
3. `41` ← `82` (XCLKEN, CLKRUN) runs the machine at 1.5 MHz; `80` stops it.
4. Back to the CPU card's oscillator: hold RESET, `41` ← `00`.

To watch it (not tested): arm a capture as in (a), keeping XCLKEN in every
CTRL1 write: one sample per eight STEPs, and slice reads show the latest.
Or `41` ← `A4` (XCLKEN, LIVE, SCLKSEL): the latches follow the lines at
12 MHz, so between steps the slices show them as they stand.

## 13. Timing

The timing pass (D71, D73; timing.md §1) times the card alone in its
12 MHz mode: every path meets the oscillator's 41.7 ns, at 41.0 ns worst
case (26.5 typical). It takes the sample latches as asynchronous, leaves
the read-back and the trigger setup untimed (only while stopped) and
checks no hold times (timing.md §3, §5); the card is also timed in the
whole machine at 3 MHz. The local bus is not timed: nothing yet specifies
the host's strobe widths or read access time. `-BREQ0`, released at SNAP,
rises through its 10k pull-up in about 255 ns (timing.md §4), before the
arbiter's next look.

## 14. Verified

`card_debug.py` checks that every pin is connected or declared unused, and
each GAL's equations against its spec for every input (`make test`,
run_tests.sh 6c6); `p32jed.py` checks the six JEDEC files in
`hardware/gal/debug/` through their fuse maps (6c8). At gate level,
`sim_debug.py` in `make test`, the Pico a script of local-bus operations:
- **listen**: `hello` beside the CPU, MMU / data and memory cards, one
  sample a `CLK`; read back after HALT, the samples hold every bus cycle
  from the capture's start to HALT as the bus carried it (768 samples, 355
  cycles);
- **dma**: one DMA write while the CPU polls the word (`dma_test.asm`); the
  program halts with `5A5A1234`;
- **alone**: the debug and memory cards, ALONE and STEP: a word written and
  read back through the latches;
- **xclk** (D76): the debug and CPU cards: XCLKEN off, the CPU card on its
  own oscillator; on, ten STEPs advance its phase by exactly ten; CLKRUN, a
  phase per two of the card's oscillator periods; the CPU card's CLOCK
  STEP button, a phase a press.

**Not exercised:** the trigger RAMs' patterns (the simulated RAMs read 0,
so none matches), a post-trigger count other than 1, the 12 MHz mode, a
DMA read or any snapshot's read-back, GO or CLKRUN alone, `-WAIT` in a DMA
cycle, ODACK / ODWAIT / ODLOCK, and the J3 and LR groups.

## 15. Open questions

1. **The SRAMs' write end.** `-WE` rises two 74ACT00 delays after the
   sample clock's rising edge, the edge at which the 74ACT574 latches
   change; the CY7C199 wants its data steady 9 ns before (tSD,
   `p32delays.py`); the timing pass checks no hold times. In zero-delay
   simulation the new sample wins. Scratch runs of sim_debug's machine (not
   in the repo): a triggered capture held at each address the sample after
   the one §8 describes (the trigger's at CA − n − 1), and a DMA read of the
   ROM word at 0 left the next clock's idle bus in the SRAMs (D =
   `000000FF`, `-ACK` high). `make test` cannot notice: listen accepts any
   starting offset, and dma only writes.
2. **Power-on.** `CRESN` does not clear GOQ, ARMQ, STEPQ, the drive
   registers or the trigger RAMs. debug-card.md §8 says nothing is enabled
   at power-on whichever way the switch stands, but a GOQ that powers up set
   (status bit 2) runs one bus cycle from the drive registers' contents
   once the switch is at DRIVE and `CLK` runs. Acceptable?
3. **`-BREQ0` in LISTEN** comes from DBSEQ's registers, not DBDRV: after a
   switch to LISTEN it can stay low until the next `CLK` edge, indefinitely
   with `XCLK` stopped.
4. **Lines not driven:** `-BGNT3–10` (a device's DMA engine reads its
   grant, D75), `-FAST` (the I/O card reads it; not sampled either) and
   `-IRQ0–7`, though D64 says DRIVE reaches every signal a card other than
   the CPU card sees, and debug-card.md §4 lists `-IRQ0–7` as driven.
5. **RESET for XCLKEN** (D76): with the CPU card in, the card cannot drive
   `-RES` (GSYS needs ALONE, which drives `CLK`), so someone holds the CPU
   card's RESET button. Intended?
6. **Wrap:** CA's bit 15 is not readable, so nothing tells whether the
   buffer wrapped; the listen case assumes not.
7. **Comments:** card_debug.py's docstring samples the timing view at
   "24 MHz" (T12: 12 MHz, D73); DBCLK's has STEP "held low at phase 4"
   (it stops at phase 3, `CLK` high, in simulation too); sim_debug.py's
   has the dma case "then reads it back by DMA" (`dma_plan` only writes).
8. **debug-card.md** (built in brackets): §2–3, also D64 and
   architecture.md, 162 lines, 21 × 74HCT574 latches, 21 SRAMs (164, 22 ×
   74ACT574, 22 + 2 CY7C199); §4, 21 output registers (18); §6, about 110
   ICs (138); §7, 24 MHz, eight samples a microcycle (12 MHz, four: D73);
   §3, control lines addressing the trigger RAMs and qualifiers (read,
   write, acknowledge, no `-ACK`, `FAULT`, an interrupt) (the address
   only; any strobe); §5, "74LVC245s" (one); 290 parts (292, D76's POWER
   LED). §5's bus window 4 (an ID register, a mailbox; architecture.md
   too) and PROG-jumper flash programming (programmers-guide.md too;
   `card_mem.py` ties the flash's `-WE` high) are in no netlist.
9. **Front edge:** S1's footprint is a slide switch (E-Switch EG1224,
   right angle) while its value says "SPDT toggle, right angle", on this
   card and on the CPU card's S4: which part does Ken mean to fit? And
   which end of S1 is LISTEN is "to check on the part"
   (card_debug.py); the BOM lists the mode LEDs D2, D3 as "LED 3mm", their
   colours only in the function text.
