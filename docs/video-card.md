# The video card

**Superseded (D98, 2026-11-10):** this card, revision 0, is being replaced
by a 6845 card like YACC1-D's, [revision 1](video-card-6845.md). It was
never ordered or built. The emulators and the kernel model it until revision
1's software is done.

**Status:** NETLIST DONE (2026-10-26), verified at gate level with the CPU,
MMU / data, memory and I/O cards, its picture included (§6); layout to do.
Any slot from 3 to 10, 9U × 340 mm (D49), through-hole (D21). D50, D52,
D53, D57, D67. The netlist is `generators/card_video.py`, the font
`generators/gen_font.py`.

**88 ICs, 18 of them GALs**; 207 parts (`python3 generators/card_video.py --bom`).

## 1. What it holds

VGA **640 × 480 at 60 Hz** (a 25.175 MHz dot clock): **80 × 25 text** in
8 × 16 cells from a font ROM, on the screen's lines 40–439, and a
**640 × 480 one-bit overlay**, each layer switched on and off; **two PS/2
ports** (keyboard and mouse). The console (D53).

**Its RAM**, 64 KiB at `0xFE00_0000 + n × 64 KiB` (n = the window jumpers;
default 5, `0xFE05_0000`), bytes, halfwords and words, at any time:

| Offset | What |
|---|---|
| `0x0000`–`0x0F9F` | **text**: 25 rows × 80 cells; a cell a halfword: the character, then its attribute (bit 0 inverse, 1 underline, 2 bright, 3 blink) |
| `0x1000`–`0xA5FF` | **overlay**: 480 lines × 80 bytes, bit 7 the leftmost dot (D67: first planned at `0x8000`, where it does not fit) |
| the rest | RAM, not shown |

**Its registers**, bytes, in the window's top page (`0xFFFF_DF00` by default):

| Offset | Read | Write |
|---|---|---|
| `+00` | control | bit 0 TEXT, 1 OVERLAY, 2 XOR (else OR), 3 CURSOR |
| `+01` | status: bit 0 VBLANK, 1 DE (a drawn dot's column and line) | taken, ignored |
| `+02` / `+03` | cursor row / column | the same |
| `+08` | keyboard data (reading takes it; 0 when nothing waits) | a command byte to the keyboard |
| `+09` | keyboard status: bit 0 FULL, 1 BUSY (a command being sent) | taken, ignored |
| `+0C` / `+0D` | the mouse's, as `+08` / `+09` | |
| `+10` | interrupt enables: bit 0 keyboard, 1 mouse | the same |
| `+F0` / `+F1` | card ID 3 / revision 0 | taken, ignored |
| `+F4` | interrupt status (a word: bit 0 keyboard FULL, 1 mouse FULL) | taken, ignored |

Anything else in the window, or another size, gets no `-ACK`: a bus
error. Reset clears control, the cursor and the enables: a black screen.

**The picture.** A text dot is the font's, unless the cell blinks and the
blink is off (32 frames, ~1.9 Hz); the underline lights scan line 15;
inverse inverts the cell; the cursor (CURSOR on, its row and column)
inverts scan lines 14–15 while the blink is on. Bright text is 0.7 V, the
rest 0.5 V, on all three guns (white). The overlay ORs over the text, or
with XOR inverts it.

## 2. Jumpers (at the card's edge, labelled)

Fitted = 0, open = 1:

| Jumpers | Meaning | Default |
|---|---|---|
| `WIN2 WIN1 WIN0` | the window: registers at `0xFFFF_8F00 + n × 4 KiB`, RAM at `0xFE00_0000 + n × 64 KiB` | 5: WIN1 fitted |
| `L2 L1 L0` | the interrupt level (both ports) | 4: L0, L1 fitted |

## 3. Blocks

| Block | Parts | ICs |
|---|---|---|
| **Bus interface** | 3 × 74HCT541 (A2–A15, BE, strobes, CLK, `-CLK`, `-RES`: one load a line); 74HCT688 (A31–A25); GALs VHI (A24–A16: the RAM's window, the device windows), VSEL (the access: RAM or register page), VBE (lanes, `-ACK`, the transceivers), VDEC (register reads), VWR (register writes); 4 × 74HCT245 (D ↔ the card's XD0–31); 74HC05 (`-ACK`, `-WAIT`, the PS/2 lines: open drain); pull-downs on XD0–15 | 15 |
| **Registers** | GAL VCTL (control, enables, the interrupt); GALs VRD0 / VRD1 (the lane 0 and lane 1 registers read); 74HCT32; 74HCT244 (the vector's window); 2 × 74HCT273 + 2 × 74HCT541 (the cursor, read back) | 8 |
| **Timing** | 25.175 MHz oscillator; 74ACT04 (DCLK, DCLKN); 6 × 74F163 (DC: the dot; HC: the character, 0–99; VC: the line, 0–524); 4 × 74F161 (the overlay's address); GALs HORZ, VERT (the decodes, sync), VTIM (the slots on DCLK's rise, the handshake), VSLOT (on its fall: whose address the RAM sees), VBUS (on `-CLK`: the handshake's bus side); 74HCT393 (frames: the blink) | 17 |
| **RAM** | 2 × CY7C199 (32K × 8, 15 ns: the even bytes, the odd bytes); 6 × 74ACT541 (the text's, the overlay's and the bus's addresses); GAL ROW5 + 2 × 74F283 (row × 80 + column); 4 × 74HCT541 (write buffers), 4 × 74HCT574 (read latches) | 19 |
| **Display** | 74HCT574 × 2 (CL: the character; AL: the attribute, the cursor's match, the column's place); SST39SF010A (the font); 3 × 74ACT574 (FL: its dots; OL0, OL1: the overlay bytes); 2 × 74F151 (a character's dot, bit 7 first); 2 × 74HCT688 (the cursor); GALs CELL (a character's attributes), VOUT (the dots, the syncs); 74ACT541 (the VGA drivers) | 13 |
| **PS/2** | per port: GAL PS2K / PS2M, 74HCT163 (bits), 74HCT595 (receive), 74HCT597 (send); 74HCT14, 74HCT175 (the lines in), 74HC280 + 74HCT74 (the command's parity), 74HCT00 | 13 |
| **Interrupts** | 74LS145 (the level, open collector), 74HCT85 (the level acknowledged) | 2 |
| **Front edge** | the DE-15 (VGA) socket; two mini-DIN-6 (KEYBOARD, MOUSE), +5 V through a 0.5 A polyfuse | — |

## 4. Theory of operation

### 4.1 The raster

A **character time** is 8 dots (318 ns). DC counts the dots, HC the
characters of a line (0–99: 80 drawn, then 2 front porch, 12 sync, 6 back
porch: 800 dots, 31.47 kHz), VC the lines (0–524: 60 Hz). VC 0 is the
screen's line 40, the first text line, so that **the text row is VC8:4 and
the scan line VC3:0**; the overlay's line 0 is VC 485. After `-RES`
(synchronized: DRESN) the counters start at 0. HC and VC advance at dot 5 of
a character time (ENH), the overlay's address at dot 1 (OENA), so neither
changes while a slot uses it.

### 4.2 The RAM's four slots

The RAM is **16 bits wide**: the even bytes in one CY7C199, the odd in the
other, so a text cell (character and attribute) is one halfword. Each
character time has four 2-dot slots. GAL VSLOT, on the dot clock's falling
edges, puts a slot's address on the RAM (enabling one pair of 74ACT541s);
GAL VTIM, on the rising edges, takes its data 1.5 dots later and lets the
address go half a dot after that:

```
dot          0    1    2    3    4    5    6    7    0
address        [text      )[bus, low  )[bus, high )[overlay   )
data taken           TLK ^      RLKA ^     RLKB ^      BCK ^
-WE (a write)                [   ]      [   ]        (per byte lane)
```

- **Text** (dots 0.5–2.5): the cell at **row × 80 + column**
  ((VC8:4 × 5 + HC6:4) × 16 + HC3:0: GAL ROW5 and two 74F283s). CL takes
  the character, AL the attribute, the cursor's match (two 74HCT688s:
  the row against VC8:4, the column against HC) and whether this column is
  text / drawn. The character and the scan line address the font ROM; at
  the next character time's start (BCK) FL takes the dots.
- **The bus** (dots 2.5–6.5): the word's low halfword, then its high one.
- **The overlay** (dots 6.5–8.5): the halfword holding the column's byte
  (the overlay's address counter, from `0x1000` at the frame's start, one
  byte a drawn column); OL0 and OL1 take both bytes at BCK, and the
  address's bit 0 (in GAL CELL) enables the right one.

So **a character's font and overlay bytes are both ready as its dots
begin**. Two 74F151s pick each dot (DC selects, bit 7 first). GAL CELL has
the character's attributes as K (the glyph shows) and M (light or invert
regardless): the text dot is **(TD & K) ^ M**, the overlay's **OD & E**.
GAL VOUT ORs or XORs them and registers every dot with the syncs (HSR from
GAL HORZ, VSR from GAL VERT), so the dots and the syncs leave together. A
74ACT541 drives the resistors: a dot is 0.5 V into 75 Ω (680 Ω), a bright
one 0.7 V (with 1.5 kΩ more), the syncs TTL through 47 Ω.

### 4.3 The bus: the handshake

The card **answers its RAM at once** (`-ACK`) and holds **`-WAIT`** until
the dot clock's side has done the access, whatever the bus clock:

1. REQ = the access and not DONEB (GAL VBUS). It is `-WAIT` itself, and
   crosses into the dot clock through two flip-flops (R1, R2 in GAL VTIM).
2. At dot 2 of a character time, with R2 and not DONE, GO starts the
   access: the bus's two slots in this character time. A write: the RAM's
   `-OE` off and the write buffers (XD → RD) on through both slots, `-WE`
   for a dot in each, per byte lane (BE0–3). A read: RLKA and RLKB clock
   the read latches (RD → XD), which drive the card's data bus for the
   whole cycle.
3. DONE (at dot 6) crosses back through one flip-flop clocked by `-CLK`
   (DONEB, mid-cycle): REQ, so `-WAIT`, ends; the cycle ends at the next
   CLK. With REQ gone, DONE clears.

At 3 MHz an access waits **one or two clocks** (from 8 to 16 dots after
REQ). Back-to-back accesses work: DONEB is clear again by the middle of the
next cycle's first clock, before `-WAIT` is looked at. **The registers**
are on the bus's own clock and answer without waiting.

**Register writes are taken at the cycle's end** (D72): the cursor
registers' and the PS/2 send registers' clocks fall in the bus clock's
second half and rise with CLK as the cycle ends, so the address decode,
the transceivers and the data have the whole cycle (at mid-cycle they did
not, at the datasheets' worst case). The PS/2 GALs still see the command
strobe (KCMDC / MCMDC) as a level in the second half.

### 4.4 PS/2

Each port is a **GAL state machine on the bus clock** (PS2K, PS2M), its
clock and data lines read through a 74HCT14 and a 74HCT175 and pulled low
through the 74HC05 (4.7 kΩ pull-ups).

- **Receiving.** A fall of the device's clock is CK2 & !CK1. A 74HCT163
  counts them: the data bits (falls 2–9) shift into a **74HCT595**; the
  11th sets **FULL**, which loads the 595's output register and **holds
  the clock low**, so the device waits with its next byte. Reading the
  data register (KRD) clears FULL. The 595 drives lane 0 only while FULL:
  an empty port reads 0 (the pull-downs).
- **Sending.** A write to the data register loads the byte into a
  **74HCT597** (and its odd parity, from a 74HC280, into a 74HCT74) and
  sets **BUSY**. The GAL holds the clock low for five line ticks (TICK:
  HSYNC through GAL VBUS: 127–159 µs, ≥ 100 µs), then pulls data low (the
  start bit) and lets the clock go. The device clocks the frame: at each
  fall the GAL presents the next bit (the 597 shifts at falls 2–9, the
  parity behind the eight data bits), lets data go for the stop bit, and
  the 11th fall (the device's acknowledge) ends BUSY. The answer (FA)
  arrives as an ordinary byte.
- **Interrupts** (D52, D57): FULL and its enable, either port, pull the
  card's level through the 74LS145. In the acknowledge cycle (the level
  matching, 74HCT85) the vector is **window × 16 + 0** (keyboard) or
  **+ 1** (mouse, if the keyboard is not asking): `0x50` / `0x51` by default.

## 5. The font

`generators/gen_font.py` draws printable ASCII on an 8 × 8 grid (capitals
on rows 0–6, descenders on row 7), each row shown on two scan lines: the
look of an 8 × 8 font on a VGA screen. `python3 generators/gen_font.py`
writes `video/font8x16.bin`, the whole SST39SF010A (128 KiB: the font at
the start, FF after it); address = character × 16 + scan line.

## 6. Verification

- Its 18 GALs against their specs, exhaustively; the netlist's electrical
  checks.
- **At gate level with the CPU, MMU / data, memory and I/O cards**
  (`generators/sim_sys.py --mem --io --video`), the card whole: its own
  25.175 MHz clock against the CPU card's 24 MHz (generators/sim_video.py);
  only the PS/2 devices are models, driving the sockets' lines.
  - **`vidcard_test`**: the RAM in every width and lane (each access
    through the handshake), the registers, the status register three times
    as the raster moves, five refused accesses, then a screen. Every
    instruction's state and every cycle as **`p32uemu -C`**, which now
    models the card's timing exactly: the two clocks' schedule, the
    handshake's flip-flops and the raster (`p32io.h`, `vid_hw_*`). All 614
    RAM accesses wait as many clocks in both.
  - **The picture** (`--frame PNG`): after HALT the dot clock runs on for a
    frame; the VGA outputs, cut by the syncs, must be dot for dot the
    picture the card's RAM and registers describe (`sim_video.expected`),
    with HSYNC 96 dots every 800 and VSYNC 2 lines every 525, and nothing
    lit outside 640 × 480.
  - **`ps2_test`**: the keyboard's bytes as they arrive, its vectored
    interrupt (`0x80005004`), the mouse's reset (FF: FA AA 00), a command
    with its data byte (ED 02: FA FA), the parity checked by the device.
    Self-checking on both emulators and the card; not cycle for cycle (on
    the card a byte takes a frame on the wire).

## 7. To check before layout

- The mini-DIN-6 footprint (Same Sky MD-60SM: as on YACC1's video card) in
  `hardware/lib/P32W.pretty`.
- The 25.175 MHz oscillator in a DIP-14 can (25.000 MHz works with most
  monitors, if that is what is in stock).
- Timing against the datasheets: the slots assume the 74ACT541's enable
  times and the CY7C199-15's access (≤ 45 ns of the 60); the dot-rate GALs
  are the -7 grade (ATF22V10C-7PX).
- **Programming** (TL866II+ or T48): the GALs' fuse files are
  `hardware/gal/video/*.jed` (`minipro -p ATF22V10C -w FILE`), the font
  `video/font8x16.bin` (`minipro -p SST39SF010A -w ...`):
  hardware/gal/README.md.
