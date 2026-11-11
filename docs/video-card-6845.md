# The video card, revision 1: a 6845 (D98)

**Status:** DESIGN, FOR KEN TO CHECK (2026-11-10). Nothing is built from it
yet: no netlist, no emulator model, no driver. It replaces
[revision 0](video-card.md) (D67), which was laid out but never ordered or
built; until this card's software is done (§8, step 2) the emulators and the
kernel still model revision 0. D98; D50, D52, D53, D57, D72.

Modelled on YACC1-D's video card V2.0 (`~/Developer/YACC1-D/docs/cards/video-v2.md`):
a **6845** draws the picture from a **dual-port text RAM**, a **graphics
plane** shares one fast RAM with the display, and an **ATmega328P** speaks
PS/2. What P32W adds is its bus (32 bits, byte lanes, windows by jumpers,
vectored interrupts) and VGA's line rate.

**About 53 ICs, 6 of them GALs; about 42 built text-only.** An estimate
until the netlist; revision 0 has 88, 18 of them GALs.

## 1. What it holds

- **VGA, 640 × 480 at 60 Hz** (a 25.175 MHz dot clock, 31.47 kHz lines) on a
  DE-15, white in two levels (0.5 V, bright 0.7 V, on all three guns), as
  revision 0.
- **Text: 80 × 25 cells of 8 × 16 dots** from the font ROM
  (`generators/gen_font.py`, as revision 0), on 400 of the 480 lines. A cell
  is a halfword: the character, then its attribute: bit 0 inverse, 1
  underline, 2 bright, 3 blink, as revision 0. The cursor is the 6845's own.
- **Graphics: 640 × 400 one-bit dots** over the text, ORed or XORed, in
  **two pages**: the CPU draws in one while the other is shown.
- **Two PS/2 ports**, keyboard and mouse, through an ATmega328P running
  YACC1-D's firmware: the keyboard arrives as ASCII.
- **The console** (D53), `/dev/tty4`, as before.

## 2. As software sees it

### 2.1 Memory

At `0xFE00_0000 + n × 64 KiB` (n = the window jumpers; default 5, `0xFE05_0000`):

| Offset | What | Access |
|---|---|---|
| `0x0000`–`0x0FFF` | **text**: 2,048 cells, a cell a halfword (the character, then its attribute). The screen shows 2,000 of them, from the 6845's start address (R12, R13), wrapping at 2,048 | bytes, halfwords, words; never a wait |
| `0x8000`–`0xFFFF` | **graphics**: the page the CPU sees (control bit CPAGE), 16 planes of 2,048 bytes | bytes; two wait states |
| the rest | nothing: no `-ACK`, a bus error | |

With the start address at 0, as the kernel sets it, the text is where it is
on revision 0: row r, column c at `(r × 80 + c) × 2`.

**The graphics plane** is laid out as the 6845 reads it (YACC1-D V2.0's
way): plane *p*, at `0x8000 + p × 0x800`, holds scan line *p* of every cell,
a byte a cell, bit 7 the leftmost dot. Dot (x, y), x 0–639 and y 0–399, is
bit 7 − (x mod 8) of the byte at

```
0x8000 + (y mod 16) × 0x800 + ((start + (y div 16) × 80 + x div 8) mod 2048)
```

With the start address at 0 that is `(y & 15) << 11 | ((y >> 4) * 80 + (x >> 3))`.
The 6845 addresses both layers, so moving its start address scrolls the
graphics with the text.

### 2.2 Registers

Bytes, in the window's top page (`0xFFFF_8F00 + n × 4 KiB`; `0xFFFF_DF00` by default):

| Offset | Read | Write |
|---|---|---|
| `+00` | — | **the 6845's address register**: which register `+01` reaches (0–17) |
| `+01` | the 6845's register (R14–R17 read back; the rest are write-only) | the 6845's register |
| `+02` | **status**: bit 0 VSYNC, 1 DE (dots being drawn), 2 TEXT-ONLY (no graphics layer fitted) | **control**: bit 0 TEXT, 1 GRAPHICS, 2 XOR (else OR), 3 CPAGE (the page at `0x8000`), 4 DPAGE (the page shown) |
| `+03` | **PS/2 status**: bit 0 FULL (a byte waits), 1 SRC (0 keyboard, 1 mouse), 2 BUSY (a command not yet taken) | **PS/2 command**, YACC1-D's: `$01` interrupts on, `$02` off, `$03` acknowledge, `$04` mouse on, `$05` off, `$06` flush, `$07` keyboard reset, `$10`–`$17` the keyboard's LEDs, `$FF` start again |
| `+04` | **PS/2 data**: the next byte; reading it clears FULL. Read it only after FULL = 1 | — |
| `+F0` / `+F1` | card ID 3 / revision 1 | — |
| `+F4` | interrupt status, a word: bit 0, a PS/2 byte waits with interrupts on (bits 31–1 read 0) | — |

Anything else in the page, or another size, gets no `-ACK`: a bus error.
The 6845's two registers take two wait states, the others none. **Reset**
clears control (both layers off: black) and restarts the ATmega; the 6845
holds no useful table, so the kernel programs it before it turns the text
on.

### 2.3 The 6845's table

The kernel writes R0–R15 at boot. R3's top four bits, the vertical sync's
width, are Hitachi's S type's (§5.1):

| Register | Value | What |
|---|---|---|
| R0 | 99 | 100 characters a line: 800 dots, 31.47 kHz |
| R1 | 80 | 80 of them shown |
| R2 | 84 | the horizontal sync from character 84: VGA's 16 dots of front porch, with the card's two-character pipeline (§5.2) counted |
| R3 | `0x2C` | vertical sync 2 lines; horizontal sync 12 characters (96 dots) |
| R4, R5 | 31, 13 | 32 rows of 16 lines, and 13 more: 525 lines, 59.94 Hz |
| R6 | 25 | 25 rows shown |
| R7 | 28 | the vertical sync at row 28 (line 448) |
| R8 | 0 | not interlaced |
| R9 | 15 | 16 scan lines a row |
| R10, R11 | `0x6E`, 15 | the cursor on scan lines 14–15, blinking every 32 frames, as revision 0 |
| R12, R13 | 0 | the start address |
| R14, R15 | | the cursor's cell, row × 80 + column from the start address |

Both syncs are negative, as VGA's 480-line mode wants. The text lands on the
picture's lines 42–441 (revision 0: 40–439): the 6845 places the vertical
sync by whole rows.

### 2.4 The keyboard and the mouse

YACC1-D's firmware (`~/Developer/YACC1-D/embedded/video-ps2`) turns scan
code set 2 into **ASCII**: Shift, Caps Lock (with its LED), Ctrl-letter
(`$01`–`$1A`), Enter (CR), Backspace (`$08`), Delete (`$7F`), and the
arrows, Home, End, Page Up and Page Down as VT100 sequences. The mouse's
3-byte packets are queued whole. It is one queue of 64 bytes, each byte with
its SRC. The rule from YACC1-D's re-review: **read the data only after the
status has shown FULL**.

### 2.5 The interrupt

FULL, with interrupts on (command `$01`), pulls the card's level (jumpers
L2–L0, default 4). In the acknowledge cycle the card answers with vector
**n × 16 + 0** (`0x50` by default): one vector for both ports, SRC telling
them apart.

## 3. Jumpers and the front edge

| Jumpers | Meaning | Default |
|---|---|---|
| `WIN2 WIN1 WIN0` | the window n: the RAM, the register page, the vector's bits 6:4 | 5 |
| `L2 L1 L0` | the interrupt level | 4 |

Fitted reads 0, open reads 1, as on revision 0. **The front edge:** the
DE-15 (VGA); the two mini-DIN-6, KEYBOARD and MOUSE, with +5 V through a
0.5 A polyfuse; a POWER LED. The ATmega's FTDI header (its firmware, its
debug console) at an edge, labelled.

## 4. Blocks

| Block | Parts | ICs |
|---|---|---|
| **Bus interface** | 3 × 74HCT541 (the address and strobes, one load a line); 74HCT688 (A31–A25); GALs WIN (A24–A16 and the jumpers: the RAM's window or the register page), DEC (which access: text, graphics, 6845 or register; its size and lanes; `-ACK`), WT (the wait count, the 6845's E, the transceivers, the write strobes); 4 × 74HCT245 (D0–31 to the card's XD0–31) and 3 more (a byte on lanes 1–3 onto XD0–7, the card's byte bus); 74HC05 (`-ACK`, `-WAIT`: open drain); pull-downs on XD8–31 | 15 |
| **Registers** | GAL RD (status, PS/2 status, ID, revision, interrupt status and the vector onto XD0–7); 74HCT574 (control) | 2 |
| **The 6845** | HD68B45SP (§5.1) | 1 |
| **Timing** | 25.175 MHz oscillator (DIP-14 can); 74ACT04 (the dot clock); 74F163 (the dots, 8 a character; the 6845's clock) | 2 |
| **Text** | 4 × IDT7134 (4K × 8 dual-port, 1K of each used; as YACC1-D's card): the even and odd cells' characters and attributes; 2 × 74ACT574 (the character, the attribute); SST39SF010A (the font); 74ACT574 (its dots); 74F151 (a dot, bit 7 first); 2 × 74F175 (the attribute, DE and CURSOR, two characters on); 74HCT393 (frames: the blink); 74HC20 (scan line 15: the underline) | 13 |
| **Graphics** | AS6C1008-55 (128K × 8: two pages of 32K); 4 × 74ACT157 (its address: the CPU's or the 6845's); 74HCT574 (the CPU's read), 74HCT541 (the CPU's write); 2 × 74ACT574 (the display's byte, two stages); 74F151 (a dot); GAL SLOT (the two halves, the write, the read latch, TEXT-ONLY) | 11 |
| **Dots** | GAL DOT (text and graphics, the attributes, the cursor, DE; registered with the syncs); 74ACT541 (the DE-15's resistors) | 2 |
| **PS/2** | ATmega328P (16 MHz crystal); 2 × 74HCT574 (the byte for the CPU, the command from it); 74HCT74 (FULL, BUSY); 7416 (its reset from `-RES`) | 5 |
| **Interrupts** | 74LS145 (the level, open collector); 74HCT85 (the level being acknowledged) | 2 |
| **Total** | | **53** |

**Built text-only**, the graphics block (11, GAL SLOT among them) is left
off: a resistor holds its dot low, another pulls TEXT-ONLY high (GAL SLOT
drives it low when fitted), and graphics accesses get no `-ACK`. YACC1-D
recommends its own card's text-only build as the first.

What revision 0 needed and this card does not: the four-slot RAM schedule
and its handshake across the two clocks; the character and line counters
and the sync GALs; the overlay's address counter; row × 80 + column in a GAL
and two adders; the cursor's comparators; PS/2 in two GAL state machines
with their shift registers.

## 5. How it works

### 5.1 The raster, and which 6845

The 74F163 counts the dots, 8 a character time (318 ns). Its third bit is
the 6845's clock (3.147 MHz): high for dots 4–7, falling as a character time
begins. On that fall the 6845 moves on, and everything about the raster
comes from it: MA (the cell), RA (the scan line in the row), DE, CURSOR,
HSYNC and VSYNC. Nothing else on the card counts.

**The part.** 3.147 MHz is more than Motorola rates its MC6845 for (3.0
MHz; 2.5 in the early sheet). Hitachi's S type is rated 3.7 MHz, and it can
make the vertical sync 2 lines long, as VGA's is (the MC6845's is always
16): **HD6845SP, HD68A45SP or HD68B45SP**. The B grade is YACC1-D V2.0's
part, and the one wanted here for the bus side too (§5.4). UMC's UM6845E
may do (to check). Look at the marking: an HD46505 or HD46505R, sometimes
sold as an HD6845S, is the 3 MHz part.

### 5.2 Text

**Dual-port RAM, as YACC1-D's card.** Four IDT7134s hold the text: the even
cells' characters and attributes, and the odd cells'. The 7134 has no
arbitration logic, so nothing ever holds a write back. On the CPU's side
they are 32 bits wide, a word two cells (lane 0 the even cell's character,
1 its attribute, 2 and 3 the odd cell's), so bytes, halfwords and words all
work, at once. On the display's side MA10–1 address them and MA0 picks the
pair that drives the character and the attribute. Each side has its own
port: **the CPU never waits for the display, and the display never sees the
CPU.** A cell written while it is read shows old or new for that frame.

**The pipeline.** In character time k the 6845 puts out cell k's MA, and
the RAMs find its character and attribute. As k+1 begins two 74ACT574s take
them, and a 74F175 DE and CURSOR. In k+1 the font ROM finds the dots
(character × 16 + RA). As k+2 begins a 74ACT574 takes them, and the
attribute, DE and CURSOR move a stage on. In k+2 a 74F151 picks each dot,
bit 7 first. The picture runs two characters behind the 6845's count, which
R2 allows for.

**Attributes and the cursor** (GAL DOT, on the dot clock): a text dot is
the font's; blink hides it while the blink is off (a 74HCT393 counts
frames: 32, about 1.9 Hz); underline lights scan line 15; inverse inverts
the cell; the 6845's CURSOR, on its scan lines and blinking as R10 says,
inverts it too; bright raises the level.

### 5.3 Graphics

**One RAM, two halves of each character time**, as YACC1-D V2.0: the
74ACT157s give the RAM the CPU's address for dots 0–3 and the 6845's for
dots 4–7. The display's address is **RA3–0 : MA10–0** in page DPAGE; the
CPU's is its offset in the window, in page CPAGE. The display's byte is
taken at the end of its half, held a second character time beside the
text's dots (the second 74ACT574), and its dots picked by a 74F151 in step
with the text's.

**The CPU's access waits a fixed two clocks**, so one whole CPU half always
falls inside the cycle: at 3 MHz the cycle is 1 µs, three character times.
GAL SLOT takes the request at a half's start, on the dot clock. A write's
`-WE` falls inside that half, **once** (in the first whole half), with the
data steady; a read's byte goes into the 74HCT574 at that half's end and
stays on the bus until the cycle ends. Nothing signals back to the bus
clock: the count is enough, as YACC1-D V2.0's long strobes are. Drawing is
the same with the layer shown or not (YACC1-D's direct mode is not needed).

**The two layers** (GAL DOT): the text dot AND TEXT, the graphics dot AND
GRAPHICS, ORed, or XORed with XOR set, and gated by DE.

### 5.4 The bus

- **Text RAM**: `-ACK` in the first clock and no wait. The dual-port RAM
  answers well inside a clock at 3 MHz or 5.
- **Registers**: `-ACK` and no wait; writes are taken at the cycle's end
  (D72).
- **The 6845**: two waits. E is high from the middle of the first clock to
  the middle of the third for a write, or to the cycle's end for a read:
  667 ns at 3 MHz, 400 ns at 5, against the B grade's 220.
- **Graphics**: two waits (§5.3).
- **Lanes**: the text RAM uses the four lanes straight. Everything else is
  a byte on the card's byte bus, XD0–7, reached from any lane through its
  own 74HCT245, so the registers sit at consecutive offsets as before.

### 5.5 PS/2

YACC1-D V2.0's circuit and firmware: the ATmega loads a byte into a
74HCT574 and sets FULL (a 74HCT74); the CPU's read of `+04` clears it. A
write of `+03` latches a command and sets BUSY; the ATmega takes it and
clears BUSY. The ATmega never sits on the bus, and the CPU never waits for
it. FULL, ANDed with the ATmega's interrupt request, is the card's
interrupt. The bus's `-RES` restarts the ATmega (through the 7416); its
FTDI header loads the firmware (Optiboot, as an Uno).

### 5.6 Interrupts

As revision 0: a 74LS145 pulls the jumpered level. In the acknowledge cycle
a 74HCT85 sees that the level asked for is the card's, GAL DEC pulls `-ACK`
and GAL RD puts the vector on D7–0.

## 6. What changes for the software

- **The kernel's `video.c`**: the 6845's table at boot; the cursor through
  R14 and R15; scrolling by the start address (R12, R13), or by copying as
  now (words still work); the keyboard's bytes arrive as ASCII, so its
  scan-code table goes; one interrupt vector; the mouse's bytes kept for
  later.
- **The emulators** (`p32io.h`): the new card: the 6845's registers, the
  text RAM, two graphics pages, control and status, the ATmega's queue with
  the firmware's translation (so the debugger's keys still go as scan
  codes), the waits in `p32uemu -C`.
- **The debugger's Screen** draws from the 6845's registers (the start
  address, the cursor) and shows the graphics layer.
- **Tests**: `video_test`, `vidcard_test`, `ps2_test` rewritten.
- **Docs**: this page becomes `docs/video-card.md`, revision 0's moves
  aside; the programmer's guide §6.6 and the architecture §4a follow.

## 7. To check

- **The 6845's clock rating** in Hitachi's own datasheet (3.7 MHz by
  secondary sources), and its 2-line vertical sync, before buying parts.
- **Two text RAMs or four:** four, as here, let words through. Two (a
  16-bit text RAM, halfwords and bytes only) would save two 48-pin chips
  and their power, since dual-port RAMs draw several hundred milliwatts
  each.
- **The parts in stock:** HD68B45SP, IDT7134, AS6C1008-55,
  ATmega328P.
- **The timing at the datasheets' worst case** (D72): the 6845's MA settling
  (160 ns on Motorola's sheet) and the RAMs within a character; the
  graphics read at 5 MHz (about 560 ns of the 600).
- **YACC1-D's firmware has not run on hardware yet:** its card is not built.

## 8. The plan

1. This description: Ken checks it.
2. The emulators, the debugger's Screen and the kernel's driver on the new
   card, with tests: P32W Unix running on it in the emulator.
3. The netlist (`generators/card_video.py`, revision 0's kept aside), its
   gate-level check with a 6845 model, the layout, pcb-check, a design
   review. Revision 0's board comes off pcb-check's list then.
4. Ken orders it.
