# The I/O card

**Status:** NETLIST DONE AND VERIFIED with the CPU, MMU / data and memory
cards (2026-10-26); layout to do. Any slot from 3 to 10, 9U × 340 mm (D49),
through-hole (D21). D23, D43, D45, D50, D52, D57, D65. The netlist is
`generators/card_io.py`.

**86 ICs, 16 of them GALs**, plus a Raspberry Pi Pico 2 W and a WIZ850io
module; 255 parts (`python3 generators/card_io.py --bom`).

## 1. What it holds

The devices `emulator/p32io.h` models for the I/O card, in its 4 KiB
window (window 7, `0xFFFF_F000`, by default; D52), registers in the top
256 bytes:

| Address | Device | Level (default) |
|---|---|---|
| `FF00`–`FF07`, `FF08`–`FF0F`, `FF30`–`FF37`, `FF38`–`FF3F` | **four UARTs**, TL16C550 (1.8432 MHz clock), each on a **DB9 plug at the front edge** through a MAX232A (TXD, RXD, RTS, CTS at RS-232 levels, wired as a PC's COM port) | 2, 3, 2, 3 |
| `FF10` TCOUNT, `FF14` TRELOAD, `FF18` TCTRL | **the timer**: 32 bits, 1 MHz, reload, pending (words only) | 0 |
| `FF20`, `FF21` | **the input and output ports**, 8 bits each, on 10-pin IDC headers | — |
| `FF40`, `FF41` | **the real-time clock**, DS12C887 (D23): register number, register | 5 |
| `FF50`/`51`, `FF54`/`55` | **two USB ports**: CH376S modules in 2 × 8 sockets (D43) | 6, 6 |
| `FF52`/`53` | **the network coprocessor's mailbox** (D45): a Pico 2 W and a WIZ850io (W5500, RJ45) | 7 |
| `FFF0`, `FFF1`, `FFF4` | card ID (1), revision (0), interrupt status (a word: bit per device) | — |

Byte registers answer byte accesses only, the timer and `FFF4` words only.
Anything else in the window gets no `-ACK`, which is a bus error, exactly
as `p32io.h` behaves.

## 2. Jumpers (at the card's edge, labelled)

Fitted = 0, open = 1 (10k pull-ups). **30 jumpers:**

| Jumpers | Meaning | Default |
|---|---|---|
| `WIN2 WIN1 WIN0` | the card's window, 0–7 | 7: all open |
| `L0_2 L0_1 L0_0` | the timer's interrupt level | 0 |
| `L1_*` … `L4_*` | UARTs 0–3 | 2, 3, 2, 3 |
| `L5_*` | the clock | 5 |
| `L6_*`, `L7_*` | USB 0, USB 1 | 6, 6 |
| `L8_*` | the network | 7 |

Each device's level is its own three jumpers (D52). Devices may share a
level, and the acknowledge cycle still tells them apart.

## 3. Blocks

| Block | Parts | ICs |
|---|---|---|
| **Bus interface** | 2 × 74HCT541 (A2–A7, BE0–3, `-MRD`, `-MWR`, `-IACK`, CLK, `-RES`: one load per line); 2 × 74HCT688 (A31–A16 all ones); GALs IOSEL (the window), IOBE (size, lane), IODEC1 (the byte devices' selects), IODEC2 (the other registers), IOSTB (strobes, byte lanes, `-ACK`); 4 × 74HCT245 (D lanes ↔ the byte bus ID0–7) | 13 |
| **UARTs** | 4 × TL16C550, the 1.8432 MHz oscillator; 4 × MAX232A; 4 × DB9 (right angle, front edge) | 9 |
| **Timer** | 8 × 74HCT163 (~TCOUNT, counting up); 4 × 74HCT374 (~TRELOAD, on the load bus LD); 12 × 74HCT540 (D → LD, LD → D, count → D, each inverting); GALs TIMDEC (the paths), TIMCTL (enable, pending, prescaler) | 26 |
| **Status word** | 74HCT541, 74HCT125 (the devices' requests → the status bus SB), 4 × 74HCT541 (SB → D: TCTRL, `FFF4`); pull-downs | 6 |
| **Ports** | 74HCT541 in, 74HCT574 out, 74HCT541 read-back | 3 |
| **Clock** | DS12C887 | 1 |
| **USB** | two CH376S modules in sockets | — |
| **Network** | 74HCT574 request latch, 74LVC244 (to the Pico at 3.3 V), 74HCT574 reply latch, 74HCT244 (NSTAT), CD74HC4050 (TXE, RXF to the Pico), GALs NSTB (strobes) and MBOX (TXE, RXF, IEN, RESET); LD1117V33; the Pico and the WIZ850io | 8 |
| **Interrupts** | 9 × 74LS145 (one per device: its level jumpers decoded onto that `-IRQ` line, open collector); 2 × 74HCT14 (request polarities); 74HCT138 (the level acknowledged); GALs IVA–IVE (each device: asking on that level?), IOPRI (the lowest), IOVEC (ID, revision, the vector onto the byte bus); 74HC05 (`-ACK`, the Pico's RUN) | 19 |

## 4. How it works

- **Decoding:** IOSEL matches A31:A8 against the window. IOBE turns the byte
  enables into *byte* (exactly one), *word* (all four) and the lane, LN1:LN0.
  A byte register's address is its word's address plus the lane, and the
  lane is its chip's A1:A0. IODEC1 and IODEC2 select one device or register,
  and only for an access of the right size.
- **Strobes in the cycle's second half:** the byte devices' `-RD` / `-WR`,
  the latches' clocks and the clock chip's AS are all gated by CLK low.
  By then the address has been stable for half a cycle, so a strobe can
  never fire on a wrong address while the decode settles. (Found by the
  simulation: AS in the first half fell on a passing glitch and latched a
  wrong register number.)
- **The byte devices take two clocks (D72):** the UARTs, the clock, the USB
  modules, the mailbox, the ports and the ID answer with one wait state.
  GAL IOSTB's W1 (a register on CLK) marks the second clock and `-WAIT`
  (a 74HC05 gate) holds the first. `-RD` runs from the first clock's
  middle to the cycle's end, so the data stays until the master takes it;
  `-WR` from the first clock's middle to the second's, a whole clock with
  half a clock of hold. GAL NSTB's latch clocks and AS act in the second
  clock; the mailbox's flags see one clock of the access. Without it a read
  could not get back to the CPU in time (docs/timing.md, D71).
- **The timer's reload latch takes D at the cycle's end** (D72), and its
  counters are 74F163s: their carry chain to ALLONES (eight stages) must
  settle within a clock of a load, which 74HCT's 40 ns a stage does not.
- **The byte bus:** the four lane transceivers put the selected byte lane on
  ID0–7, where every byte device sits. Word registers (the timer, the status
  word) sit on D0–31 directly.
- **The timer:** P32's design (D11). The counter holds the complement of
  TCOUNT and counts up on CLK every third clock (1 MHz); every fourth when
  the bus line `-FAST` is low, the CPU card's clock jumper at 4 MHz (D74),
  so the tick stays 1 MHz and the kernel needs no change. Its carry out of
  the top (`ALLONES`) means TCOUNT = 0. At that edge, while enabled, it
  reloads from the reload register and sets pending, exactly as `p32uemu
  -C`'s `io_hwedge()`.
- **Interrupts:** each device's request goes to its 74LS145, whose
  open-collector outputs are wired to `-IRQ0`–`-IRQ7`; the jumpers pick
  the output. In the acknowledge cycle (D57) the 74HCT138 decodes A4:A2, the
  IV GALs find which devices are asking on that level, and IOPRI picks the
  lowest. IOVEC puts `window × 16 + its status bit` on D7:0, and `-ACK`
  answers.
- **The mailbox:** P32's design (net-mailbox.md). P32W writes the request
  latch. The Pico reads it through the 3.3 V buffer (`-PRD`) and writes the
  reply latch (`PWR`). GAL MBOX, clocked by CLK, keeps TXE and RXF, acting
  on each Pico strobe's leading edge once. RESET is a one-clock pulse on the
  Pico's RUN pin.

## 5. Verification

- **Its 16 GALs** against their specs, exhaustively; **the netlist's**
  electrical checks.
- **At gate level with the CPU, MMU / data and memory cards**
  (`generators/sim_sys.py --mem --io`): the card's own decoding, strobes,
  lanes, timer, status word, interrupt levels and acknowledge cycle, and its
  mailbox latches and flags, all wired as built. Only the insides of the
  TL16C550s, the DS12C887, the CH376S modules and the Pico's firmware are
  `p32io.h`'s models, reached through the card's pins. The timer counts as
  the card does, so the reference is **`p32uemu -C`**:
  - `hello`, `card_test` (IDs, UARTs 2 and 3, shared levels), `io_test`
    (UARTs, ports, the disk) and the new **`iocard_test`** (the timer at
    hardware timing, its vectored interrupt from WAIT, the status word, the
    clock's RAM and registers, both USB controllers, five accesses that
    must be bus errors): every instruction's state and every cycle as
    `p32uemu -C`;
  - **`mbox_test`** (`-N`): a HELLO request and its reply through the
    latches, IEN. The result is compared; the flags follow the
    coprocessor's own pace.
- `make test` runs `iocard_test` and `mbox_test` at gate level.

**Known differences from the emulators:** a UART's modem status reads
CTS from its cable (the emulators: always asserted; no software uses it).
With no coprocessor running
(no `-N`), the emulators' NSTAT reads 0. The card's reads TXE (the request
latch empty) and IEN as written. Only READY (0 in both) matters to
the kernel.

## 6. To check before layout

- The CH376S module's header pinout and its 5 V setting (D43); the
  WIZ850io's pinout and the RJ45's height; the Pico 2 W's antenna corner
  free of copper.
- The 74LS145's pin numbers (as the 7445 / 74145 family).
- Timing against the datasheets: the TL16C550's and DS12C887's set-up and
  hold around the strobes' rising edge at CLK's rise.
