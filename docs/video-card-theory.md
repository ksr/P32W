# The video card: theory of operation

**Superseded (D98, 2026-11-10):** revision 0, being replaced by
[revision 1, a 6845 card](video-card-6845.md); never ordered or built.

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-30),
`generators/card_video.py` (every chip and GAL equation comes from there;
the font from `generators/gen_font.py`). The card's summary, its parts by
block and its verification are in [the video card](video-card.md); the
registers as programs see them in [the programmer's guide](programmers-guide.md)
§6.6; the bus in [bus.md](bus.md). D50, D52, D53, D57, D67, D71, D72.

## 1. What the card does

A **bus slave**, in any slot from 3 to 10. From its own 25.175 MHz dot
clock it draws VGA, 640 × 480 at 60 Hz, in two layers, each switched on
and off: **text**, 80 × 25 cells of 8 × 16 dots from a font ROM on the
screen's lines 40–439, each cell with an attribute (inverse, underline,
bright, blink), and a **cursor**; and **the overlay**, 640 × 480 one-bit
dots, ORed or XORed over the text. Both are in **64 KiB of RAM** at
`0xFE00_0000 + n × 64 KiB`, which the bus reads and writes at any time: the
text at `+0000`–`+0F9F` (a cell a halfword: the character, then its
attribute), the overlay at `+1000`–`+A5FF` (80 bytes a line, bit 7 the
leftmost dot). The registers are the top 256 bytes of device window n,
`0xFFFF_8F00 + n × 4 KiB` (D52); n is 5 by default. **Two PS/2 ports**,
keyboard and mouse, share one interrupt level, with a vector each. Fitted,
the card is the console, `/dev/tty4` (D53). The display runs on the dot
clock (`DCLK`), the rest on the bus clock (`CLK`); what crosses between
them is in §4, §9 and §10.

## 2. The jumpers

Six jumpers at the card's edge (JP1–JP6). **Fitted reads 0, open reads 1**:
each shorts its line to ground, and a 10k network (RN3) pulls it up.

| Jumpers | Meaning | Default |
|---|---|---|
| `WIN2 WIN1 WIN0` | the window n: the RAM's 64 KiB, the register page, bits 6:4 of the vector | 5: WIN1 fitted |
| `L2 L1 L0` | the interrupt level, both ports | 4: L1 and L0 fitted |

## 3. The bus interface: five GALs

Three 74HCT541s buffer A2–A15, BE0–3, `-MRD`, `-MWR`, `-IACK`, `CLK`,
`-CLK` and `-RES` (`BA2–15`, `BBE0–3`, `MRDN` … `BRESN`); a 74HCT688 finds
A25–A31 all ones (`TOPN`); GAL VHI reads A16–A24: one load per line. Four
74HCT245s join D0–31 to the card's data bus, `XD0–31`; RN1 and RN2 pull
`XD0–15` down, so a register's unused bits read 0.
- **VHI**: `MHIT`, A31:16 = `FE00` + n (the RAM); `RHI`, A31:16 = `FFFF`.
- **VSEL**: `MACC`, `MHIT` with `-MRD` or `-MWR` (a RAM access, any size);
  `RRD` / `RWR`, the register page (`RHI`, A15 = 1, A14:12 = n, A11:8 = F)
  with `-MRD` / `-MWR`; `RLOEN`, a RAM read: the read latches onto XD.
- **VBE**: `BYTE`, `WORD`, `LN1:0` (a byte's lane); **`ACK`** = `MACC` or
  `RHIT` or `VHIT` (§11), onto the open-drain `-ACK` through a 74HC05;
  `LE0N`–`LE3N`, a lane's 245 on when the card answers and the master asks
  for that lane; `LDIR`, toward the card for a write.
- **VDEC**, from the byte's offset (`BA7:2`, `LN1:0`): `RHIT`, a byte at
  `+00`–`+03`, `+08`, `+09`, `+0C`, `+0D`, `+10`, `+F0`, `+F1` or a word at
  `+F4` (anything else in the window, or another size, gets no `-ACK`: a
  bus error); `KRD` / `MRDP`, a byte read of `+08` / `+0C`; `ROWRDN` /
  `COLRDN`, the cursor's read-back; `R0EN0`, a read for GAL VRD0.
- **VWR**: `WCTL`, `WIE` (`+00`, `+10`), levels that GAL VCTL takes at the
  `CLK` edge ending the cycle; `KCMDC`, `MCMDC` (`+08`, `+0C`), levels in
  the second half (`CLK` low) for the PS/2 GALs; `WROWC`, `WCOLC`,
  `KCMDK`, `MCMDK`, clocks low in the second half that rise as the cycle
  ends, so the decode, the transceivers and the data have the whole cycle
  (D72). A register write takes effect as its cycle ends, and the
  registers answer with **no wait state**; only the RAM waits (§9).

## 4. The registers

| Offset | Read | Write | Parts |
|---|---|---|---|
| `+00` | control | bit 0 TEXT, 1 OVERLAY, 2 XOR, 3 CURSOR | GAL VCTL (`CTL0–3`); read through GAL VRD0 |
| `+01` | status: bit 0 VBLANK, 1 DE | ignored | GAL VRD1 |
| `+02` / `+03` | cursor row / column | the same | a 74HCT273 each, cleared by `-RES`; a 74HCT541 reads it back |
| `+08` / `+0C` | keyboard / mouse data | a command byte | the port's 74HCT595 / 74HCT597 (§10) |
| `+09` / `+0D` | its status: bit 0 FULL, 1 BUSY | ignored | GAL VRD1 |
| `+10` | interrupt enables: bit 0 keyboard, 1 mouse | the same | GAL VCTL (`KIE`, `MIE`); read through VRD0 |
| `+F0` / `+F1` | card ID 3 / revision 0 | ignored | VRD0 / the pull-downs |
| `+F4` | interrupt status, a word: bit 0 keyboard FULL, 1 mouse FULL | ignored | VRD0 |

A byte register is on its offset's lane (`+02` on lane 2, `XD16–23`).
- **VCTL**, on `CLK`, cleared by `-RES`: `CTL0–3` take XD0–3 with `WCTL`,
  `KIE` and `MIE` XD0–1 with `WIE`; it also makes the interrupt (§11).
- **VRD0** drives XD0–3 while `R0EN` (`R0EN0` or `VHIT`, a 74HCT32): by
  BA7, BA4 and BA2 the control register, the enables, the ID 3, or `KF`
  and `MF` (the ports' FULL); in the acknowledge cycle the device (§11).
- **VRD1** drives XD8–9 for a byte read at `+01`, `+05`, `+09` or `+0D`
  (`+05` gets no `-ACK`): VBLANK (not `VIS`) and DE (`OOUT`: the column
  being fetched is drawn, on a drawn line), straight from the dot clock's
  side as they are when the read ends; or the port's FULL and BUSY.

Reset clears control, the enables and the cursor: a black screen.

## 5. The dot clock and the counters

A 25.175 MHz oscillator drives a 74ACT04 (`DCLKN`, then `DCLK`). A dot is
39.7 ns, a **character time** 8 dots (318 ns). "At dot n" is the rising
edge that starts dot n (dot 8 is the next character time's dot 0); "n.5"
is the falling edge in its middle.

| Counter | Parts | Counts | Cleared |
|---|---|---|---|
| **DC**, the dot, 0–7 | 74F163 | every dot | in reset (`DRESN`) |
| **HC**, the character, 0–99 | 2 × 74F163 | at dot 5 (`ENH`) | after 99; in reset (`HCLRN`) |
| **VC**, the line, 0–524 | 3 × 74F163 | as HC wraps (`VENA`) | after 524; in reset (`VCLRN`) |
| **OA**, the overlay's byte address | 4 × 74F161 | at dot 1 (`OENA`) | loads `0x1000` (`OALDN`) |

**GAL HORZ**, on `DCLK`, makes `DRESN` (`-RES` on the dot clock);
`ENH` (DC = 4); `HCLRN` and `VENA` (DC = 4, HC = 99); `OENA` (DC = 0, HC
1–80, a drawn line); `TOUT` / `OOUT` (HC < 80 on a text / drawn line); and
`HSR`, the horizontal sync, registered at dot 8 (§6). **GAL VERT**,
combinational: `VCLRN`; `TL` (VC < 400); `VIS` (VC < 440 or ≥ 485); `VSR`
(VC 450–451); `UL` (VC3:0 = 15, the underline's line); `CL` (VC3:1 = 7,
the cursor's lines 14–15); `OALDN`, low on undrawn lines and in reset. So
HC and VC change at dot 5 and OA at dot 1, never while a slot uses them.
**The blink:** a 74HCT393 counts the falls of `VSR`, one a frame; its bit 4
is `BLINK`, 16 frames low and 16 high (about 1.9 Hz), never cleared.

## 6. A frame and a line

**A line** is 100 character times, 800 dots (31.47 kHz). Column h is
fetched in the character time that begins with HC = h, and its dots go out
in the next (§8). Columns 0–79 are drawn (640 dots), 80–81 are the front
porch, 82–93 HSYNC (96 dots), 94–99 the back porch: as column h's fetch
ends HC is already h + 1, and HORZ registers `HSR` = (HC is 83–94), so the
sync goes out with columns 82–93.

**A frame** is 525 lines (60 Hz). VC 0 is the screen's line 40, so that on
a text line **the row is VC8:4 and the scan line VC3:0**:

| VC | Screen lines | Drawn |
|---|---|---|
| 0–399 | 40–439 | text rows 0–24, and the overlay |
| 400–439 | 440–479 | the overlay |
| 440–484 | 480–524 | nothing (VBLANK); VSYNC on VC 450–451, lines 490–491 |
| 485–524 | 0–39 | the overlay, whose line 0 is VC 485 |

On undrawn lines OA loads `0x1000`; from VC 485 it counts a byte a drawn
column, reaching `0xA5FF` in the 480th line. After `-RES` the raster starts
from DC, HC and VC 0, the first text line, with OA at `0x1000`.

## 7. The RAM, its four slots, the text address

The RAM is **16 bits wide**: two CY7C199 (32 K × 8, 15 ns), the even bytes
(`RD0–7`, the characters) and the odd (`RD8–15`, the attributes); `RA0–14`
addresses a halfword, and `-CE` is grounded. Three pairs of 74ACT541s offer
an address: the text's (HC3:0, TA4–10, zeros; `TXENN`), the overlay's
(OA15:1; `OVENN`) and the bus's (`HALF`, BA2–15; `CPENN`). A character time
has **four slots of two dots**: **GAL VSLOT**, on falling edges, puts a
slot's address on; **GAL VTIM**, on rising edges, takes the data a dot and
a half later.

```
dot           0     1     2     3     4     5     6     7     0
address          [text      )[bus, low  )[bus, high )[overlay   )
data taken            TLK ^      RLKA ^      RLKB ^       BCK ^
-WE (a write)                   [     ]     [     ]
```

The bus slots serve lanes 0–1 (`HALF` = 0), then lanes 2–3 (`HALF` = 1).
VSLOT makes `TXENN`, `CPENN`, `HALF` and `OVENN` from DC alone. For a write
in progress (`GO` and `-MWR`) it also turns the RAM's `-OE` off (`ROEN`,
2.5–6.5) and the write buffers on (`WBAN`, lanes 0–1, 2.5–4.5; `WBBN`,
lanes 2–3, 4.5–6.5); its active-low outputs reset high, off at power-up.
VTIM's `WEEN` (the even chip) is low in dot 3 with BE0 and in dot 5 with
BE2, `WEON` (the odd) with BE1 and BE3: half a dot inside the address and
the data. Four 74HCT541 write buffers carry lanes 0–1 or 2–3 onto
`RD0–15`; four 74HCT574 read latches take `RD0–15` back to lanes 0–1
(`RLKA`) and 2–3 (`RLKB`), driving XD while `RLOEN`. So the bus's byte at
offset x is in halfword x / 2, in the even or odd chip by bit 0: where the
text and the overlay read it.

**The text address: row × 5.** The cell is halfword row × 80 + column, the
row in VC8:4 and the column in HC6:0. **GAL ROW5** makes row × 5
(`R50–R56`, rows 0–24), and two **74F283**s add HC6:4 to it (the low one's
carry, `TAC`, into the high one):

    TA10:4 = row × 5 + HC6:4;   RA = TA10:4 × 16 + HC3:0 = row × 80 + HC

Row 24, column 79 is halfword 1999: bytes `0x0F9E`–`0x0F9F`.

## 8. A character cell, and the VGA output

Column h of a text line, through the character time that begins with
HC = h, and the next:

| Dot | What happens |
|---|---|
| 0.5 | VSLOT puts the cell's address on the RAM |
| 2 (`TLK`) | **CL** takes the character; **AL** the attribute's bits 0–3, the cursor's match (`CEQN`), `TOUT` and `OOUT` (74HCT574s). The font ROM sees the character and the scan line |
| 6.5 | OA, at column h's byte since dot 1, on the RAM |
| 8 (`BCK`) | **FL** takes the font's byte, **OL0** / **OL1** the overlay halfword (74ACT574s), **GAL CELL** the attributes, HORZ the sync |
| the next 8 | two **74F151**s pick bit 7 − DC of FL (`TD`) and of the enabled overlay latch (`OD`); **GAL VOUT** registers each dot at the next edge |

So a column's dots leave a character time after its fetch, one dot behind
the 151s, with the syncs registered beside them. Two 74HCT688s find **the
cursor's cell**: the row register against VC8:4 and, enabled by that, the
column register against HC; outside the text area `TOUT` hides it. **The
font** is an SST39SF010A flash (128 K × 8, 55 ns): A3–A0 the scan line,
A11–A4 the character, A12–A16 grounded. Its image, `video/font8x16.bin`,
is the whole chip: printable ASCII (20–7E) on an 8 × 8 grid, each row on
two scan lines, the other codes blank, then FF.

**GAL CELL**, on `BCK`, makes `K`, the glyph shows: TEXT on, a text column
(`TOUTL`), not blink with `BLINK` high, not underline on scan line 15;
`M`, lit or inverted whatever the glyph: TEXT on, a text column, and
(underline on scan line 15) XOR inverse XOR the cursor (CURSOR on, `CEQL`
low, `CL`, `BLINK` low); `BRTT`: TEXT on, a text column, bright; `E`:
OVERLAY on, a drawn column (`OOUTL`); `OSEL` / `OSELN`: OA0, OL0 or OL1
onto the 151. The text dot is **(TD & K) ^ M**: dark with TEXT off or
outside the text; else the font's, inverted for inverse; the background
(dark, lit if inverse) in a blinking cell while `BLINK` is high; lit (dark
if inverted) on an underline's scan line 15; inverted on the cursor's
lines 14–15 while `BLINK` is low. **GAL VOUT** ORs it with the overlay's
dot, `OD & E`, or XORs while XOR (`CTL2`) is set: `VID`. `VBR` is `VID` in
a bright cell; `HSO` and `VSO` are the syncs, active low.

**The VGA output.** A 74ACT541 drives `VID` and `VBR` three times each, and
the syncs. Each gun's pin (DE-15 pins 1, 2, 3) has 680 Ω from a `VID`
output and 1.5 kΩ from a `VBR` output: into the monitor's 75 Ω a dot is
0.5 V and a bright one 0.7 V, on all three guns (white). HSYNC and VSYNC
reach pins 13 and 14 through 47 Ω; pins 5–8, 10 and the shell are ground.
The socket, J4 (VGA), is at the front edge.

## 9. A bus access to the video RAM

The card **answers at once** (`-ACK`) and **holds `-WAIT`** until the dot
clock's side has done the access, so it is right at any bus clock (D67).
`REQ` (GAL VBUS) is `MACC` and not `DONEB`, and through the 74HC05 it is
`-WAIT`. GAL VTIM, on `DCLK`, takes `REQ` through two flip-flops (`R1`,
`R2`); sets `GO` at dot 2 with `R2` and not `DONE`, until dot 8; and sets
`DONE` at dot 6 while `GO`, holding it while `R2`. VBUS takes `DONE` on
`-CLK`, mid-clock: `DONEB`.

**A word write:**
1. The MMU / data card drives A, BE0–3, `-MWR` and D from the clock's
   start: `MACC`, `-ACK`, the transceivers toward the card; `REQ` rises
   (`DONEB` is low) and `-WAIT` falls. `R1`, `R2` follow at two dot edges.
2. At the next dot 2, `GO`. From 2.5 the RAM's outputs are off, lanes 0–1's
   buffers on and `HALF` 0: `-WE` for dot 3. From 4.5 `HALF` is 1 and lanes
   2–3's buffers are on: `-WE` for dot 5. From 6.5 the RAM is the overlay's.
3. At dot 6, `DONE`; at the next mid-clock, `DONEB`: `REQ` falls and
   `-WAIT` is let go. The MMU / data card finds `-WAIT` high as that clock
   ends, and the cycle ends. With `MACC` gone, `R1`, `R2` and then `DONE`
   clear; `DONEB` at the next mid-clock.

**A read** is the same with the RAM's outputs on: `RLKA` and `RLKB` take
both halfwords, and the read latches drive XD all through the cycle
(`RLOEN`); the transceivers pass the lanes asked for. A byte or halfword
write strobes only its own lanes.

**How long.** By VTIM's equations `DONE` comes 6 to 14 dots after `REQ`
rises: two or three dots until `R2` can start `GO`, up to seven more for
dot 2, four to dot 6. At 3 MHz the first mid-clock (167 ns) is always too
soon and the third (833 ns) always late enough: **one or two wait states**.
**Back to back**, `DONEB` is still high as the next access begins, so it
starts without `-WAIT`; `DONE` has cleared by its mid-clock (three dot
edges after `REQ` fell), and `REQ` pulls `-WAIT` low before that clock ends
(video-card.md §4.3).

## 10. PS/2

Each port's lines come in through a 74HCT14 (Schmitt) and a 74HCT175 on
`CLK` (its inverted outputs, `KCK1`, `KDAT1`, `MCK1`, `MDAT1`, in the
lines' own sense), and go low through the 74HC05 (`CKDRV`, `DTDRV`)
against 4.7 kΩ pull-ups; the sockets (J5 KEYBOARD, J6 MOUSE) take +5 V
through a 0.5 A polyfuse. Per port: **GAL PS2K** or **PS2M** on `CLK`,
cleared by `-RES`; a 74HCT163 (`BC`, which `-RES` loads with 0); a
74HCT595 to receive, a 74HCT597 to send. Shared: a 74HC280 (parity), a
74HCT74 (a parity bit per port), a 74HCT00 (a data register onto lane 0
only while FULL). GAL VBUS takes `HSR` twice on `-CLK`: `TICK` (`HS1 &
!HS2`) is one bus clock in each line.

The GAL sees a fall of the device's clock as `CK2 & !CK1` (`CK2` is `CK1` a
clock later). `CNT` counts falls, or line ticks in the inhibit. `CLRN`
clears the count on a command write, the 11th fall, the start of sending,
and while FULL and not BUSY. `SH` pulses at falls 2–9 (`BC` 1–8), shifting
the 595 and the 597. `CKDRV` is FULL, or BUSY before `TXA` (the inhibit).

**Receiving.** Idle, the card drives neither line. The keyboard clocks out
a start bit, eight data bits (bit 0 first), odd parity and a stop bit.
Falls 2–9 shift `KDAT1` into the 595; the start, parity and stop bits are
counted, not checked. The 11th fall sets FULL: the 595's output register
takes the byte (`RCLK`), the clock is held low (the keyboard waits with its
next byte), the count is held at 0, and with `KIE` the card asks for its
interrupt. A read of `+08` (`KRD`, with FULL) turns the 595 onto XD0–7, bit
0 on XD0; as the cycle ends FULL clears and the clock is let go. Without
FULL the read gives the pull-downs' 0.

**Sending** (`ED`, say):
1. A byte write to `+08`: as the cycle ends `KCMDK` rises, the 597's input
   register takes XD0–7 (bit 0 into H), and the 74HCT74 the 74HC280's ΣEVEN
   (odd parity). The GAL sets BUSY and clears the count.
2. The inhibit holds the clock low while the counter counts line ticks. At
   the fifth (`BC` = 4) `TXA` rises, the data goes low (the start bit), the
   count clears and the clock is let go: after 4 to 5 lines, 127–159 µs
   (≥ 100 µs, D67).
3. The keyboard clocks the frame. With `TXA` high the 597 stops loading
   (`-SRLOAD`) and shifts at falls 2–9, the parity entering behind. After
   each fall `DTDRV` drives the next bit: `BC` 0 the start bit, 1–9 a 0
   from QH' (the data, then the parity), 10 nothing (the stop bit).
4. The 11th fall, the keyboard's acknowledge, ends `TXA` and BUSY. Its
   answer (`FA`) arrives as a received byte.

A command written while a byte waits unread is held: after the inhibit the
clock stays low (`CKDRV` = FULL) until the byte is read.

## 11. Interrupts

**GAL VCTL** asks with `KF & KIE` or `MF & MIE`. Its inverse, `ANYN`, is
the **74LS145**'s D input, with L2–L0 on C, B, A: with a request the code
is the level, and that output pulls its `-IRQ` line low (open collector);
without one the code is 8 or more and O0–O7 are off. `VDEV` is 0 if the
keyboard asks, else 1. A **74HCT85** compares BA4:2 with L2:0 (`EQ`), and
`VHIT` is `-IACK` with `EQ` and a request.

**The acknowledge cycle** (D57): the MMU / data card drives `-IACK`, A4:A2
= the level and BE0 alone. `VHIT` makes VBE raise `ACK` and turn lane 0's
transceiver toward the bus, and makes `R0EN` (through the 74HCT32): VRD0
drives XD0 = `VDEV` and XD1–3 = 0, and a 74HCT244, enabled by `VHITN`,
drives XD4–6 = the window and XD7 = 0. The vector is **window × 16 + 0**
(the keyboard) or **+ 1** (the mouse, when the keyboard is not asking):
`0x50` / `0x51` by default. The CPU puts it in `CAUSE` bits 15:8: the
keyboard at level 4 is `0x8000_5004`. For another level, or with no
request, the card does not answer (vector `0xFF`, D57). Reading the data
register clears the request. `+F4` shows FULL whether enabled or not.

## 12. Timing

| What | Time |
|---|---|
| a slot's address to its data taken | 1.5 dots, about 60 ns, for the 74ACT541's enable, the CY7C199-15 and the latch (≤ 45 ns used, video-card.md §7) |
| the font, `TLK` to `BCK` | 6 dots, about 238 ns, for the 55 ns flash |
| the dot clock's paths (timing.md, D71) | 37.5 ns of the 39.7 at worst case; 23.2 ns typical |
| `-WAIT`'s release (timing.md §4) | about 69 ns through the backplane's 680 Ω, of the 167 ns it has at 3 MHz |

The dot clock's GALs (HORZ, VERT, VTIM, VSLOT, VOUT) are ATF22V10C-7PX, the
others -10. The bus side is timed with the whole machine, which meets 3 MHz
at worst case (D72); paths between the two clocks are not timed, nor is
metastability (timing.md §3, §5).

## 13. Verified

`python3 generators/card_video.py` (in `make test`) checks every GAL's
equations exhaustively against its spec, and the netlist's electrical
rules; the fuse files are checked through the 22V10's fuse map (D67). At
gate level with the CPU, MMU / data, memory and I/O cards (`sim_sys.py
--mem --io --video`), the card's 25.175 MHz against the CPU card's 24 MHz
(`sim_video.DotClock`), **vidcard_test** (the RAM in every width and lane,
sixteen words back to back, the registers, the status three times, five
refused accesses, a screen) must match `p32uemu -C`, which models the
handshake and the raster, in every instruction's state and the cycle count.
With `P32W_FRAME=1` only (about 12 minutes more), the frame drawn after
HALT must be dot for dot what the RAM and registers describe
(`sim_video.expected`): HSYNC 96 dots every 800, VSYNC 2 lines every 525.
**ps2_test**, on `p32uemu -C` and at gate level with PS/2 device models on
the sockets' lines (`sim_video.PS2Device`), checks the keyboard's bytes,
its vector (`0x8000_5004`), the mouse's reset (`FF`: `FA AA 00`) and a
command (`ED 02`: `FA FA`) whose parity the device checks; not cycle for
cycle.

## 14. Open questions

1. **`+F4`'s upper half floats.** A word read of `+F4` turns on all four
   lanes, but nothing drives `XD16–31` and, unlike `XD0–15`, they have no
   pull-downs: D16–31 are undefined (floating in `p32sim`, the card alone).
   vidcard_test and ps2_test read the word and expect 0.
2. **The wait, in dots.** video-card.md §4.3: "from 8 to 16 dots after
   REQ". By VTIM's equations `DONE` comes 6 to 14 dots after `REQ`; 8 to 16
   is the end of the access's character time.
3. **Block counts.** video-card.md §3 has 15, 8 and 17 ICs for the bus
   interface, the registers and the timing (87, not its 88); its part lists
   and the netlist give 14 (U1–U14), 9 (U15–U23) and 18 (U24–U41).
4. **D52** calls the card's RAM "text, font and overlay"; as built (D67)
   the font is in a ROM.
5. **Test runs.** video-card.md §6 has ps2_test "on both emulators and the
   card"; `make test` runs it on `p32uemu -C` and at gate level only, and
   the picture check only with `P32W_FRAME` set, which §6 does not say.
6. **BUSY** (`+09` / `+0D` bit 1) is on the card but not in the emulators:
   `p32io.h`'s `vid_read` returns FULL alone.
7. **Received frames.** The PS/2 GALs read no data line, so a frame's
   start, parity and stop bits are not checked; no document says whether
   that is intended.
8. **BE0 in the acknowledge cycle.** bus.md says BE0–BE3 do not matter
   there, but VBE turns lane 0 on only with BE0: the vector reaches D7:0
   because the MMU / data card drives BE0 alone (mmu-card-theory §6).
