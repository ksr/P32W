# The I/O card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-30),
`generators/card_io.py` (every chip and GAL equation comes from there). The
card's summary, parts and jumpers are in [the I/O card](io-card.md). It
implements `emulator/p32io.h`'s model of the card at `p32uemu -C`'s timing,
which the gate-level simulation checks it against. D11, D23, D43, D45, D50,
D52, D57, D65, D71, D72, D74.

## 1. What the card does

The I/O card is a **bus slave** holding the devices below, the card ID,
revision and interrupt status every card has (D52), and for each device a
jumpered interrupt level and a vector (D57). Its 4 KiB window is set by
jumpers: window n at `0xFFFF_8000 + n × 0x1000`, the registers in its top
256 bytes. In window 7, the default:

| Address | Register | Size | Status bit | Level (default) |
|---|---|---|---|---|
| `FFFF_FF00`–`FF07` | UART 0, the console | byte | 1 | 2 |
| `FF08`–`FF0F` | UART 1 | byte | 2 | 3 |
| `FF10`, `FF14`, `FF18` | the timer: TCOUNT, TRELOAD, TCTRL | word | 0 | 0 |
| `FF20`, `FF21` | the input port, the output port | byte | — | — |
| `FF30`–`FF37`, `FF38`–`FF3F` | UARTs 2 and 3 | byte | 3, 4 | 2, 3 |
| `FF40`, `FF41` | the clock: register number, register | byte | 5 | 5 |
| `FF50`, `FF51` | USB 0: data; command and status | byte | 6 | 6 |
| `FF52`, `FF53` | the mailbox: NDATA; NSTAT / NCTRL | byte | 8 | 7 |
| `FF54`, `FF55` | USB 1, as USB 0 | byte | 7 | 6 |
| `FFF0`, `FFF1` | card ID (1), revision (0) | byte | — | — |
| `FFF4` | interrupt status | word | — | — |

Any other address in the window, or a register at the wrong size, gets no
`-ACK`: a bus error, as `p32io.h` behaves.

## 2. The jumpers

Thirty, along the bottom edge. **Fitted reads 0, open reads 1**: each
shorts its line to ground, and a 10k network (RN3–RN6) pulls it up.

| Jumpers | Nets | Meaning | Default |
|---|---|---|---|
| JP1–JP3 | `WIN0`–`WIN2` | the window, 0–7 | 7: all open |
| JP4–JP30, three a device | `Ld_0`–`Ld_2`, d = the status bit | the level of the timer, UARTs 0–3, the clock, USB 0, USB 1, the network | 0, 2, 3, 2, 3, 5, 6, 6, 7 |

A device's jumpers read as its level, `Ld_2` the high bit: the timer's
level 0 is all three fitted, level 2 `Ld_1` open and the other two fitted.
Devices on this card may share a level. **No other card may use one of
its levels:** both would answer the acknowledge cycle (bus.md, D57).

## 3. The bus interface and the decode

Two 74HCT541s buffer A2–A7 (`BA2`–`BA7`), BE0–BE3 (`BBE0`–`BBE3`),
`-MRD`, `-MWR`, `-IACK`, CLK (`BCLK`) and `-RES` (`BRESN`), one load per
line. Two 74HCT688s find A31:A16 all ones (`HI1N`, `HI2N`); A8–A15 go
straight to GAL IOSEL.

- **GAL IOSEL**, *is it this card's cycle?*: A31:A16 all ones, A15 = 1,
  A14:A12 = the window jumpers, A11:A8 = F; then `CRD` with `-MRD`, `CWR`
  with `-MWR`, `CACC` with either. `INTA` is `-IACK` low, at any address.
- **GAL IOBE**, *what size?*: `BYTE` (exactly one byte enable), `WORD` (all
  four), and `LN1:LN0`, a byte's lane. A byte register's offset is A7:A2 ×
  4 plus its lane; the lane is a UART's A1:A0 and a USB module's A0.
- **GAL IODEC1**, the byte devices' selects, for a byte access in range:
  `U0N`–`U3N` (eight bytes each), `USB0N`, `USB1N` (two each) and `RTCDN`
  (`FF41`), active low; `NETDS` (`FF52`), `NETSS` (`FF53`); `BHIT1`, any.
- **GAL IODEC2**: `SRTCI` (`FF40`), `SPIN`, `SPOUT` and `SIDR`
  (`FFF0`–`FFF1`) for a byte access, `BHIT2` any of them; `STCNT`,
  `STREL`, `STCTL`, `SISR` for a word access at `FF10`, `FF14`, `FF18`,
  `FFF4`, `WHIT` any of them.

Every select needs the right size, so a wrong size selects nothing.
**The byte bus `ID0`–`ID7`** carries every byte device. Four 74HCT245s
join it to D's lanes; GAL IOSTB enables only the access's lane
(`LE0N`–`LE3N`), pointing onto the card for a write (`LDIR` = `CWR`) and
onto the bus otherwise. The word registers sit on D0–D31 directly.

## 4. A byte access takes two clocks (D72)

With its strobe from mid-cycle, a byte read did not fit 3 MHz (D71), so
every byte register answers with one wait state, made by **GAL IOSTB** on
CLK: `W1`, a register, marks the access's second clock; `WAIT`, its first
(§13); `IORDN` (`-RD`) runs from mid clock 1 to the cycle's end, so the
data stays until the master takes it; `IOWRN` (`-WR`) from mid clock 1 to
mid clock 2, a whole clock with half a clock of hold.

| | Clock 1, CLK high | Clock 1, CLK low | Clock 2, CLK high | Clock 2, CLK low |
|---|---|---|---|---|
| `W1` | 0 | 0 | 1 | 1 |
| `-ACK` | low, from the decode | low | low | low |
| `-WAIT` | low, from the decode | low | high | high |
| `-RD` (a read) | high | low | low | low |
| `-WR` (a write) | high | low | low | high |
| NSTB's `REQCLK`, `POCLK`, `RTCAS` (a write) | low | low | low | high |
| NSTB's `NDWR`, `NDRD`, `CTLWR` | 0 | 0 | 1 | 1 |

At the edge ending clock 1 the master sees `-WAIT` and holds everything
for another clock (bus.md); the MMU / data card holds the CPU with `STALL`
(mmu-card-theory §6). The edge ending clock 2 ends the cycle: a read's
data is taken, AS falls, GAL MBOX acts, `W1` clears. `-RD` and `-WR` reach
every UART, the clock and both USB modules; only the selected one acts.

## 5. The UARTs and RS-232

Four TL16C550s, UART k selected by `-CS2` = `UkN` (CS0, CS1 high). D0–D7
are on the byte bus, A2:A0 = `BA2`, `LN1`, `LN0`; `-RD1` and `-WR1` are
the card's strobes (`-ADS`, RD2, WR2 tied low); MR is `-RES` inverted, by
GAL TIMCTL. One **1.8432 MHz oscillator** drives all four XINs, and each
one's `-BAUDOUT` is its receive clock: baud = 1.8432 MHz / (16 × divisor)
(programmers-guide §6.1). INTRPT, active high, is its request (`UINTk`).

A **MAX232A** each: SOUT → TXD and `-RTS` → RTS out, RXD → SIN and CTS
→ `-CTS` in, with four 100 nF pump capacitors. A **DB9 plug** each
(J4–J7) at the front edge, wired as a PC's COM port: 2 RXD, 3 TXD, 5 GND,
7 RTS, 8 CTS, the shell to ground. `-DSR` and `-DCD` are tied low, `-RI`
high; `-DTR`, `-OUT1`, `-OUT2` are not used. So the modem status's CTS is
the cable's; the emulators always assert it (io-card.md §5).

## 6. The interval timer

P32's design (D11): **the counter holds ~TCOUNT and counts up**, so
TCOUNT counts down, and its carry out (all ones) means TCOUNT = 0.
- **The counter:** eight 74F163s on CLK, in the datasheet's synchronous
  cascade: the first ENT high, each next ENT the previous RCO, every ENP
  `TICK`. The last RCO is **`ALLONES`**. 74F, because the eight-stage carry
  must settle within a clock of a load (D72). They load from **the load
  bus `LD0`–`LD31`** at an edge while `CNTLDN` is low.
- **The reload register:** four 74HCT374s hold ~TRELOAD, inputs and
  outputs both on the load bus, driving it except while TCOUNT or TRELOAD
  is written (`ROEN`).
- **Three sets of four 74HCT540s** join D and the load bus, each path
  inverting once, so D carries the true value. GAL TIMDEC enables them:

| Access | Path | TIMDEC's signals |
|---|---|---|
| TCOUNT write | D → 540 → LD; the 163s load at the cycle's end | `D2LDN`; `ROEN` (the 374s off); `WRCNT`, to TIMCTL |
| TRELOAD write | D → 540 → LD; the 374s take it as `RCLK` rises with CLK, at the cycle's end (D72) | `D2LDN`, `ROEN`, `RCLK` |
| TCOUNT, TRELOAD read | the 163s → 540 → D; the 374s → LD → 540 → D | `RDCNTN`; `RDLDN` |
| TCTRL write, read | D0, D1 straight into TIMCTL; TIMCTL → the status bus → D | `WRCTL`; `RDCTL`, `STOEN` |

**GAL TIMCTL**, on CLK and cleared by `-RES`, is `p32uemu -C`'s
`io_hwedge()`:
- **`EN`** is TCTRL bit 0, as written. **The prescaler** `PS1:PS0` counts
  0–2 while enabled (0–3 with `-FAST` low, the 4 MHz clock: D74), else
  holds 0; `TICK`, its last count, lets the counters count: 1 MHz at
  either clock.
- At an edge with `EN` and `ALLONES`, `CNTLDN` **reloads** the counters
  from the load bus and **`PEND`** sets; a TCOUNT write in that cycle wins
  the load. TCOUNT reads 0 for the cycle before (programmers-guide §6.2).
- **`PEND`**, TCTRL bit 1 and the request, clears at a TCTRL write with
  bit 1 set; an expiry at the same edge wins. `SB0` = `EN` and `SB1` =
  `PEND` go onto the status bus for a TCTRL read.
- Reset clears `EN`, `PEND` and the prescaler, not TCOUNT or TRELOAD (the
  163s' `-CLR` is tied high; the 374s have no reset).

## 7. The status bus and the interrupt status

**`SB0`–`SB8`**, pulled down by 10k (an undriven bit reads 0), carries
TCTRL or `FFF4` to D0–D8 through four 74HCT541s (`STOEN`; D9–D31 read 0).
For `FFF4` (`ISROEN`) a 74HCT541 drives `SB0`–`SB7` and a 74HCT125 gate
`SB8`. Each bit is a device's request, the same one that drives its level
(§12), so reading `FFF4` changes nothing:

| Bit | 0 | 1–4 | 5 | 6, 7 | 8 |
|---|---|---|---|---|---|
| Device | the timer | UARTs 0–3 | the clock | USB 0, USB 1 | the network |
| From | `PEND` | INTRPT | `-IRQ`, inverted | INT#, inverted | `NETASK` (`ATTN` and `IEN`) |

## 8. The ports

- **`FF20`:** a 74HCT541 from J8 pins 1–8 (`PINRDN`), each pulled up by
  10k (RN2), so an open input reads 1. A write is answered and ignored.
- **`FF21`:** a 74HCT574 to J9 pins 1–8, its outputs always on, clocked
  by NSTB's `POCLK` in the middle of a write's second clock; a 74HCT541
  reads it back (`PORDN`). It has no reset.
- J8, J9: 10-pin IDC box headers, pins 1–8 bits 0–7, 9 +5 V, 10 GND.

## 9. The real-time clock

A DS12C887 (D23) in Intel bus mode (`MOT` = GND), `AD0`–`AD7` on the byte
bus. **Writing `FF40`** raises NSTB's `RTCAS` (the chip's AS) in the
second half of the second clock. It falls with CLK at the cycle's end, and
the chip latches the register number as it falls, the data still valid.
(Made in the first half, AS had fallen on a decode glitch and latched a
wrong number: D65.) **Reading `FF40`** gives `0xFF` from GAL IOVEC, as
`p32io.h`. **`FF41`** is `-CS` = `RTCDN` with the card's `-RD` and `-WR`.
`-RESET` is `-RES`, `-RCLR` is high, SQW is not used. `-IRQ`, open drain
with a 10k pull-up, is the request. The registers: programmers-guide §6.5.

## 10. The USB modules

Two CH376S modules (D43) in 2 × 8 sockets (J10, J11), their USB-A sockets
at the front edge, wired to the module's header (usb-design.md §2):

| Pins | 1, 3, … 15 | 8 | 10 | 12 | 14, 16 | 2, 4; 6 |
|---|---|---|---|---|---|---|
| Module | D0–D7 | INT# | A0 | CS# | RD#, WR# | GND; +5 V |
| Card | the byte bus | `USBINT0N`/`1N`, 4.7k to +5 V | `LN0`: 0 data, 1 command / status | `USB0N`/`USB1N` | `-RD`, `-WR` | |

INT# low is the request. The pull-ups go to +5 V, which suits the modules
run at 5 V (D43); usb-design.md §2 warns against a 5 V pull-up on a module
left at 3.3 V. RSTI is not on the header: the card cannot reset a module,
so the driver sends `RESET_ALL` first.

## 11. The network coprocessor (D45)

A Raspberry Pi Pico 2 W (A1) and a WIZ850io (A2: a W5500 and its RJ45) on
the Pico's SPI0 (GP16 MISO, GP17 select, GP18 clock, GP19 MOSI; GP20 its
reset, GP21 its interrupt). VSYS comes from +5 V through a 1N5817 Schottky
diode; an LD1117V33 makes +3V3 for the WIZ850io, the 74LVC244 and the
CD74HC4050.

**Registers** (net-mailbox.md §1): `NDATA` (`FF52`); `NSTAT` (`FF53`
read, a 74HCT244): bit 0 `RXF`, 1 `TXE`, 2 `ATTN`, 3 `IEN`, 7 `READY`,
4–6 zero; `NCTRL` (`FF53` write): bit 3 `IEN`, bit 7 `RESET`. An NDATA
write clocks the 74HCT574 **request latch** with NSTB's `REQCLK`, mid
second clock; a 74LVC244 puts it on the Pico's `PD0`–`PD7` (GP0–GP7)
while the Pico holds `-PRD` (GP8) low. The Pico clocks the 74HCT574
**reply latch** from `PD0`–`PD7` with `PWR` (GP9); an NDATA read puts it
on the byte bus (`REPOEN`).

**GAL MBOX**, on CLK and cleared by `-RES`, keeps the flags. `-PRD` and
`PWR` are asynchronous: each passes two registers, and its **leading
edge** (the first set, the second not yet) acts, once.
- **`TXE`** resets high; an NDATA write (`NDWR`) clears it, `-PRD`'s edge
  sets it, the write winning a tie. **`RXF`**: `PWR`'s edge sets it, an
  NDATA read (`NDRD`) clears it, the set winning a tie.
- **`IEN`** takes NCTRL bit 3; **`RST`**, NCTRL bit 7, lasts one clock,
  sets `TXE` and clears `RXF`. **`NETASK`** = `ATTN` and `IEN`.
- **`NRUN`** = `RST` or `-RES`: a 74HC05 gate pulls RUN low, and 10k to
  the Pico's 3.3 V with 2.2 nF stretch `RESET` to about 15 µs.

`TXE` and `RXF` reach the Pico (GP10, GP11) through a CD74HC4050 at 3.3 V;
`READY` (GP12) and `ATTN` (GP13) come back. Until the firmware runs, 10k
resistors hold `-PRD` high, `PWR`, `READY`, `ATTN` low, and the W5500's
reset and select high: the card reads `READY` = 0.

## 12. Interrupts

**The levels.** A **74LS145** per device (a BCD-to-decimal decoder with
open-collector outputs): A–C its level jumpers, D its request, **active
low** (a 74HCT14 inverts `PEND`, the UARTs' INTRPT and `NETASK`; the
clock's and the modules' are low already). Outputs 0–7 go to
`-IRQ0`–`-IRQ7`, 8 and 9 nowhere. Asking, the code is the level and that
line is pulled; not asking, it is 8 + the level, and none is. Devices on
one level wire-OR. The CPU takes the lowest-numbered pending, enabled
level first (bus.md's Interrupts, cpu-card-theory §14).

**The acknowledge cycle** (D57): `-IACK` low and A4:A2 = n, the level the
CPU takes.
1. `INTA` enables a **74F138** (a 74F for speed, D72): A4:A2 →
   `ACK0N`–`ACK7N`.
2. **GALs IVA–IVE**, two devices each (IVA the timer and UART 0, IVB
   UARTs 1 and 2, IVC UART 3 and the clock, IVD the USB modules, IVE the
   network): `Md` = device d asks and its level's `ACK` line is low.
3. **GAL IOPRI**: `VEC3:0`, the lowest d matching (its status bit);
   `VHIT`, any; and `IDOE`, IOVEC's output enable (a 22V10's is one
   product term): `INTA` with `VHIT`, or a read of `FFF0`–`FFF1` or `FF40`.
4. **GAL IOVEC** drives the byte bus: **the window × 16 + `VEC`** in an
   acknowledge cycle, else `0xFF` (`FF40`), 1 (`FFF0`) or 0 (`FFF1`); and
   **GAL IOSTB** turns lane 0's 245 toward the bus and raises `ACK`.

With none of its devices asking on that level the card does not answer;
with no card answering, the MMU / data card's pull-ups make the vector
`0xFF` (mmu-card-theory §7), and the kernel reads the cards' interrupt
status instead (D57). The vector lands in CAUSE bits 15:8.

## 13. -ACK and -WAIT

IOSTB's `ACK` (a register selected, or `INTA` with `VHIT`) and `WAIT`
(§4) drive the open-drain lines through 74HC05 gates, `-WAIT` through a
spare one (D72):

| Cycle | `-ACK` | `-WAIT` | Clocks |
|---|---|---|---|
| a byte register, a byte access | yes | the first clock | 2 |
| TCOUNT, TRELOAD, TCTRL or `FFF4`, a word access | yes | — | 1 |
| acknowledge, one of its devices asking on that level | yes | — | 1 |
| acknowledge, none of its devices asking | no | — | — |
| anything else in the window, or a wrong size | no: a bus error | — | — |

A write to `FFF0`, `FFF1`, `FFF4` or the input port is answered and
ignored, as `p32io.h`.

## 14. Two cycles, end to end

**`lbu r10, 0xFFFFFF05(r0)`**, UART 0's LSR:
1. **Clock 1:** the MMU / data card drives the address, BE1 and `-MRD`
   (mmu-card-theory §6). IOSEL gives `CRD`, IOBE `BYTE` in lane 1, IODEC1
   `U0N`: A2:A0 = 1, 0, 1, register 5. IOSTB pulls `-ACK` and `-WAIT` and
   turns lane 1's 245 toward the bus; mid-clock `-RD` falls, and LSR
   crosses the byte bus to D15:8.
2. **Clock 1's end:** `-WAIT` is low, so the cycle repeats unchanged and
   the CPU holds its commit; `W1` sets. **Clock 2's end** ends the cycle:
   the MMU / data card has steered lane 1 onto R, zero-extended, and the
   CPU commits it to r10. Two clocks: 667 ns at 3 MHz.

**The timer's interrupt**, at the default jumpers:
1. Enabled, a tick takes TCOUNT to 0 (`ALLONES`). At the next edge TIMCTL
   reloads and sets `PEND`; the timer's 74LS145 pulls `-IRQ0`, and `FFF4`
   bit 0 reads 1.
2. The CPU card samples `-IRQ0`; at an instruction's end, with `IE` set
   and level 0 enabled, it enters INTR (cpu-card-theory §14).
3. The acknowledge cycle, A4:A2 = 0: `ACK0N`, IVA's `M0`, IOPRI's `VEC` =
   0 and `VHIT`; IOVEC drives 7 × 16 + 0 = `0x70`, IOSTB answers `-ACK`.
   CAUSE becomes `0x8000_7000`.
4. The handler writes 3 to TCTRL: at that cycle's end `PEND` clears, `EN`
   stays, and the 74LS145 lets `-IRQ0` go.

## 15. Timing

| What | How long |
|---|---|
| CLK | 333 ns at 3 MHz; 250 ns with the clock jumper (4 MHz, D74) |
| a word register; an acknowledge cycle | one clock |
| a byte register (D72) | two clocks: `-RD` 1½ clocks to the cycle's end; `-WR` one clock, then ½ clock of hold |
| the timer's tick | every third clock (every fourth at 4 MHz): 1 µs |
| `RESET`; the Pico | one clock on `NRUN`, about 15 µs on RUN; it answers within a microsecond (net-mailbox.md §1) |
| the CH376S (usb-design.md §1) | pulses of at least 30 ns (write) and 40 ns (read); at least 1.5 µs after a command byte and 0.6 µs between data bytes, which P32W meets by itself |
| the whole machine | 3 MHz at the datasheets' worst case, the byte devices' second clock allowed (docs/timing.md, D72); the TL16C550's and DS12C887's set-up and hold around their strobes still to check (io-card.md §6) |

## 16. Verified

The 16 GALs are checked against their specs for every input, and their
JEDEC files (`hardware/gal/io`) through the fuse map; the netlist passes
its electrical checks (86 ICs, 436 nets, 255 parts). At gate level with
the CPU, MMU / data and memory cards (`generators/sim_sys.py --mem --io`),
only the insides of the TL16C550s, the DS12C887, the CH376S modules and
the Pico's firmware are `p32io.h`'s models, behind the card's pins; the
clock's `-IRQ` is not modelled. Against `p32uemu -C`:
- **`iocard_test`**: the timer's registers, counting, expiry, reload and
  pending, `FFF4` bit 0, the vectored interrupt from `WAIT` (`0x70`, level
  0), the clock's RAM and registers, both USB controllers, five bus
  errors, a byte write to the ID ignored. Every instruction's state and
  every cycle match: 302 instructions, 1,178 cycles; with `-FAST` low
  (`p32uemu -C -F`), 1,238.
- **`mbox_test`** (`-N`): `READY`, a HELLO request and its reply through
  the latches on `TXE` and `RXF`, and `IEN`. Only the result is compared:
  the flags follow the coprocessor.

`make test` runs both, and has the card at gate level under the disk and
video cards' tests too; `hello`, `card_test` and `io_test` ran on it only
when it was built (D65).

## 17. Open questions

1. **Stale part names:** the netlist fits 74F163s and a 74F138 (D72), but
   io-card.md §3 says 74HCT163 and 74HCT138 (its §4 says 74F163), and so
   do card_io.py's comments (its docstring, `gal_iv()`, `ACKL`).
2. **"Every strobe in the second half"** (io-card.md §4, D65, card_io.py's
   docstring) predates D72: `-RD` is low through all of clock 2, `-WR` in
   its first half. NSTB's docstring likens AS falling at CLK's rise to "a
   UART takes a write", and io-card.md §6 checks the strobes' rise at
   CLK's rise, but `-WR` rises mid clock 2.
3. **The clock's interrupt:** the card gives it status bit 5 and a vector
   (window × 16 + 5). programmers-guide §6's `FFF4` list skips bit 5;
   `p32io.h`'s `card_reg()` has no bit 5 (card_io.py says it follows it),
   its `io_iack()` answers level 5 with `0xFF`, and `sim_sys.py` does not
   model the `-IRQ`. Which is intended?
4. **A TRELOAD write at the reload's edge:** the 374s are off the load bus
   during the write, so an expiry at its end reloads the value written;
   `io_hwedge()` says the old one. A scratch program writing TRELOAD while
   the timer ran parted the gate-level and `p32uemu -C` traces that way
   (`iocard_test` writes TRELOAD only when stopped). Which should change?
5. **The strobes at clock 2's start:** IOSTB's `IORDN` and `IOWRN` both
   hold the term `BCLK & !W1`. BCLK rises and `W1` follows its register at
   that edge, so `-RD` or `-WR` might rise for an instant mid-access. The
   timing analysis times edges and the simulator has zero delay.
6. **Small ones:** NSTAT bits 4 and 5 read 0 (grounded with bit 6), but
   net-mailbox.md §1 names only bit 6. bus.md's signal table (p32bus.py)
   says of the IRQ lines "7 highest", where its Interrupts section, D11,
   programmers-guide §7 and cpu-card-theory §14 take the lowest number
   first. io-card.md §3's interrupt block lists 20 ICs but counts 19 (the
   rows sum to 85 of 86), and leaves out the 74HC05's `-WAIT`.
7. **The output port at power-on:** its 74HCT574 has no reset; the
   emulators start it at 0. What should a device on J9 see first?
