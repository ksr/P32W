# P32W — system architecture

**Status:** PROPOSED (2026-10-24). The frame is decided (D49: 9U × 340 mm
cards, three bus connectors); the choices below are the recommendations Ken
accepted the same day (D50). Numbers marked *estimate* are to be confirmed
by the netlists.

The software — the ISA, the compilers, P32W Unix, Python, BASIC — runs on
the microarchitecture of [microarch.md](microarch.md) (the microcode and its
single internal bus); this document is the machine around it.

## 1. The shape of the machine

```
 slot:  1          2            3..9 (any order)
     ┌───────┐ ┌──────────┐ ┌────────┐ ┌──────┐ ┌──────┐ ┌───────┐ ┌───────┐
     │  CPU  │ │MMU / data│ │ MEMORY │ │ DISK │ │ I/O  │ │ VIDEO │ │ DEBUG │
     │       │ │ arbiter  │ │ ×1–4   │ │      │ │      │ │       │ │       │
     └─┬───┬─┘ └─┬─────┬──┘ └───┬────┘ └──┬───┘ └──┬───┘ └───┬───┘ └───┬───┘
       │   └─J3──┘     │        │         │        │         │         │
       │  local link   │        │         │        │         │         │
  ═════╧═══════════════╧════════╧═════════╧════════╧═════════╧═════════╧═══
       J1 + J2: the system bus — physical cycles, interrupts, arbitration
```

- **A system bus** ([bus.md](bus.md)): the backplane carries only physical
  memory cycles — address, data, byte enables, read/write strobes, `-ACK`
  and `-WAIT` — plus interrupts, bus arbitration, the clock and reset. It is
  the shape of VME, Multibus or a PDP-11's bus: the CPU's microcode fields
  never leave its card.
- **The CPU's internals stay on the CPU card.** The one private path is
  **J3 in slots 1 and 2**: the local link between the CPU card and the MMU /
  data card, carrying the CPU's result bus R and the memory-cycle fields
  every microcycle ([microarch.md](microarch.md)), so they never touch the
  system bus.
- **The MMU / data card is the CPU's master interface**: it translates,
  runs the CPU's cycles on the system bus, and holds the **arbiter** that
  lends the bus to DMA masters.

## 2. Decisions (D50, Ken accepted all recommendations, 2026-10-24)

| # | Question | Decided |
|---|---|---|
| 1 | MMU / data | **a card of its own**, beside the CPU (slot 2), linked by J3. Logic is **GALs + TTL** (no CPLDs) |
| 2 | Clock | **3 MHz guaranteed** at the datasheets' worst case (D72); **4 MHz** with the CPU card's clock jumper, at typical delays (D74) |
| 3 | Bus timing | **synchronous**, one clock, stretched by **`-WAIT`** |
| 4 | Arbitration, interrupts | **radial** bus request / grant pairs, one per slot, to the arbiter in slot 2; **8 shared priority levels**, each device's level set by a jumper (D52) |
| 5 | Card addresses | **jumpers** on each card, memory and devices alike — nothing depends on the slot (D52) |
| 6 | Slots | **10** (0–9), **1.5 inches apart**, the cards' components facing the front, slot 0's end (D99; 11 at 1 inch before) |
| 7 | Disk | **2 CF + 2 IDE** ports; **PIO first**, the card able to be a DMA master |
| 8 | Video | **VGA** (640 × 480, 31 kHz): **80 × 25 text + a 640 × 480 one-bit overlay**; **PS/2 keyboard and mouse** |
| 9 | USB, network | **USB for storage** (two CH376S); **one Pico 2 W + W5500** for Ethernet and Wi-Fi (D45) |
| 10 | ISA | an atomic swap (`SWAP`, D51) under the bus lock (D54) |

## 3. The cards

| Slot | Card | Contents | ICs (*estimate*) |
|---|---|---|---|
| 1 | **CPU** | the sequencer and control store (microcode flash, pipeline register), the instruction register and decode, immediates, the condition mux, STATUS / interrupts, the 32 × 32 register file, the PC counter, the ALU (the Tang Nano 20K FPGA module, D21), the clock (24 and 32 MHz oscillators, the CLOCK jumper: 3 or 4 MHz, D74). Front edge: RUN / HALT LEDs, RESET, STEP, the RUN / STEP switch, the probe header | ~125 |
| 2 | **MMU / data** | MAR, the split I/D TLB, the fault checks, the MDR and its byte steering, BADADDR; the system-bus master interface (drivers, `-ACK` timeout = BUSERROR, `-WAIT` → `STALL`), the **arbiter** and `-LOCK` | ~100 |
| 3+ | **Memory** (1–4) | **16 MiB** of SRAM (32 × AS6C4008, 512K × 8, 55 ns, DIP-32: eight 2 MiB banks of four byte lanes) and the boot ROM (4 × SST39SF0x0 flash). Jumpers: the card's **16 MiB window** and **ROM enable** (only one card answers for the ROM). 4 cards = 64 MiB | ~50 |
| any | **Disk** | 2 **CompactFlash** sockets (front edge, removable) and 2 **IDE** 40-pin drive ports (2 channels, master + slave each), 16-bit PIO; a DMA engine later (`-BREQ`) | ~30 |
| any | **I/O** | 4 serial ports (16C550, DIP-40; DB9 at the front), the timer (IRQ0), the RTC (DS12C887, D23), 2 USB-A (2 × CH376S, D43), Ethernet + Wi-Fi (Pico 2 W + W5500, D45; RJ45 at the front), an 8-bit input and output port | ~50 |
| any | **Video** | VGA: 80 × 25 text (8 × 16 cells in a font ROM; 400 of the 480 lines, centred — Ken chose 25 rows over 30), a 640 × 480 × 1 overlay (37.5 KiB), each layer on / off (as YACC1's overlay); the frame buffer on the card, read and written over the bus; **PS/2 keyboard and mouse** (mini-DIN at the front). D67: 88 ICs, 18 GALs | 88 |
| 0 | **Debug** (D64, docs/debug-card.md) | in its own slot, which every signal reaches (J3 included): **listen** (a logic analyzer: 32 K samples of all 165 lines, at the bus clock or 12 MHz) or **drive** (the bus master, by DMA at the highest priority; with the CPU and MMU / data cards pulled, the clock, reset and the CPU's side of J3, to test every other card alone), never both; a two-digit status display (D100); the host link (a Pico 2) | 144 |

Every card follows D48 and the global layout rules: switches, LEDs and cable
connectors at the front edge, pointing outwards; jumpers and headers at an
edge; references readable with the parts in; every control labelled.

## 4. Physical address map (D52)

Every card is placed by **jumpers**, not by its slot (Ken, D52).

| Range | What | Notes |
|---|---|---|
| `0x0000_0000`–`0x000F_FFFF` | boot ROM (1 MiB) | the memory card with its ROM jumper; hides the RAM under it |
| `0x0000_0000`–`0x03FF_FFFF` | RAM, up to 64 MiB | memory cards in 16 MiB windows (window n = n × 16 MiB), 2 jumpers |
| `0x0400_0000`–`0xFDFF_FFFF` | nothing | BUSERROR |
| `0xFE00_0000`–`0xFE07_FFFF` | video memory | 64 KiB per video card, at `0xFE00_0000 + n × 64 KiB` |
| `0xFFFF_8000`–`0xFFFF_FFFF` | device registers | 8 windows of 4 KiB, window n at `0xFFFF_8000 + n × 4 KiB`, 3 jumpers per card |

- The device windows are in the top 32 KiB so that every register is one
  instruction away, as a sign-extended 16-bit offset from `r0`.
- **A card's registers are the top 256 bytes of its window** (`0xF00`–
  `0xFFF`). At `0xFF0` every card has a read-only **card ID** (1 I/O,
  2 disk, 3 video; 4 was kept for the debug card, which has no registers
  on the bus: D101), at `0xFF1` its revision, at `0xFF4` its
  **interrupt status** (a bit per device). The kernel finds the cards by
  reading each window's ID; an empty window is a BUSERROR.
- **Default jumpers:**

  | Window | Base | Card | Registers |
  |---|---|---|---|
  | 7 | `0xFFFF_F000` | I/O | UART 0 `FF00`, UART 1 `FF08`, timer `FF10`–`FF18`, ports `FF20`/`FF21`, UART 2 `FF30`, UART 3 `FF38`, RTC `FF40`/`FF41`, USB 0 `FF50`/`FF51`, network `FF52`/`FF53`, USB 1 `FF54`/`FF55` |
  | 6 | `0xFFFF_E000` | disk | port p at `EF00 + 16 p`: 0 CF 0, 1 CF 1, 2 IDE A, 3 IDE B |
  | 5 | `0xFFFF_D000` | video | registers and PS/2 at `DF00`; its memory at `0xFE05_0000` |
  | 0–4 | | free | a second I/O card, ... (window 4 was kept for the debug card, which answers no bus address: D101) |

- **Interrupt levels** are jumpers too (devices may share one):
  0 timer, 1 disk, 2 UART 0 and 2, 3 UART 1 and 3, 4 PS/2, 5 RTC, 6 USB,
  7 network.

User mode traps at `0x8000_0000` and above (isa-system.md C2), so devices
are supervisor-only.

## 4a. The video card as software sees it (D67; docs/video-card.md)

**Being replaced (D98, 2026-11-10):** by a 6845 card like YACC1-D's, with
the same text cells, window and vector, a graphics layer of 640 × 400 and
PS/2 through an ATmega328P: [docs/video-card-6845.md](video-card-6845.md).
What follows is revision 0, which the emulators and the kernel model until
then.

Modelled on YACC1's V2.0 video card: a text layer and a one-bit graphics
overlay, each switched on and off, and two PS/2 ports. VGA 640 × 480 at
60 Hz (25.175 MHz dot clock, 31.5 kHz lines).

**Memory**, at `0xFE00_0000 + n × 64 KiB` (n = the card's window jumpers;
default 5, so `0xFE05_0000`), read and written by the CPU at any time (the
card time-shares its RAM with the display):

| Offset | Size | What |
|---|---|---|
| `0x0000`–`0x0F9F` | 4,000 bytes | **text**: 25 rows × 80 cells, a cell 2 bytes: the character (low byte) and its attribute (bit 0 inverse, 1 underline, 2 bright, 3 blink) |
| `0x1000`–`0xA5FF` | 38,400 bytes | **overlay**: 480 lines × 80 bytes, bit 7 the leftmost dot of each byte |

The text is 8 × 16 cells from the card's font ROM, 400 lines centred on the
480 (40 blank lines above and below); the overlay covers all 480 lines.
Words, halfwords and bytes all work. (The card's RAM is 16 bits wide and
time-shared with the display; an access waits one or two clocks at 3 MHz,
D67. The overlay was first put at `0x8000`, where 38,400 bytes do not fit
in the 64 KiB: it is at `0x1000`, D67.)

**Registers** (window 5: `0xFFFF_DF00`):

| Address | Read | Write |
|---|---|---|
| `DF00` | control | control: bit 0 TEXT on, 1 OVERLAY on, 2 XOR (the overlay inverts the text under it; 0: ORs with it), 3 CURSOR on |
| `DF01` | status: bit 0 VBLANK (in the vertical blanking interval), 1 DE (dots being drawn) | — |
| `DF02` / `DF03` | cursor row (0–24) / column (0–79) | the same |
| `DF08` | keyboard data: the next scan code (set 2); reading takes it | a command byte to the keyboard |
| `DF09` | keyboard status: bit 0 FULL (a byte waits), 1 BUSY (a command is being sent) | — |
| `DF0C` | mouse data (as `DF08`) | a command byte to the mouse |
| `DF0D` | mouse status (as `DF09`) | — |
| `DF10` | interrupt enables | bit 0 keyboard, 1 mouse (FULL raises the card's level, 4 by default) |
| `DFF0` / `DFF1` / `DFF4` | card ID 3, revision, interrupt status (bit 0 keyboard, 1 mouse) | — |

Reset: control 0 (everything off, the screen black), the cursor at 0, 0,
interrupts off. The registers answer byte accesses only (else BUSERROR);
the interrupt status, a word. The cursor is scan lines 14-15 of its cell,
inverted, blinking (as the blink attribute) every 16 frames.

## 5. The CPU on the bus

- **The microcode does not depend on the bus's speed.** A memory microcycle
  runs one system-bus cycle; `STALL` from the MMU / data card freezes the
  sequencer while the cycle waits (`-WAIT`) or the bus is lent to a DMA
  master. No instruction sequence changes length on a slow card.
- **Atomic swap** (D51, D54). `SWAP rd, imm(rs1)` exchanges a register with
  a memory word; its read and write are one bus tenure under `-LOCK`, for
  locks the kernel shares with DMA masters and, later, other processors.
- **Page swapping** needs no further hardware: faults are restartable (D6),
  the software-managed TLB tells the kernel a page is used (a TLB miss) and
  dirty (a write to a page mapped read-only). The per-process space (8 MiB,
  D34) can grow with the memory.
- **5 MHz.** The memory microcycle (MAR → TLB → physical address → memory →
  MDR, about 200 ns) does not fit a 200 ns clock with the control store's
  share. Either the MMU / data card asks for one wait state on every access
  (`-WAIT` on itself — no microcode change), or translation moves to the
  cycle that loads MAR. 3 MHz needs neither.

  **Measured** (`p32uemu -W N`, 2026-10-25: P32W Unix booted, root logged
  in, `halt`; 7,490,575 instructions, on the three-bus datapath of
  [microarch.md](microarch.md), D56). 40% of all microcycles are bus cycles
  (fetches, loads, stores), so one wait state on each adds 40% to the cycle
  count:

  | Clock, waits | Cycles | Cycles / instruction | Speed |
  |---|---|---|---|
  | 3 MHz, 0 | 22,427,463 | 3.18 | 0.94 MIPS |
  | 5 MHz, 1 on every bus cycle | 32,868,985 | 4.66 | **1.07 MIPS** (+14%) |
  | 5 MHz, 0 (translation moved earlier) | 22,427,463 | 3.18 | 1.57 MIPS (+67%) |

  (With the address adder and the D59 instructions: 7,049,876 instructions;
  47% of all microcycles are bus cycles. Before them the run was 7,490,575
  instructions, 26,738,361 cycles at 3 MHz: the same work is now 1.19×
  faster at every clock.)

  The wait state is the safe first step (no microcode change); moving the
  translation is worth another 40% later.

## 6. The order of work

1. **This document and [the bus](bus.md)** (`generators/p32bus.py`). Done.
2. **The emulators and P32W Unix on this map** (D52): jumpered device
   windows and card IDs, 16 MiB memory cards, four UARTs, two USB ports,
   the disk card's six drives, the video console (D53), `SWAP`. Done.
3. **The microcode and the card-level emulator** (`p32uemu.c`): the bus
   lock (D54), wait states and a DMA master; the three-bus datapath (D56),
   3.57 cycles an instruction; the address adder and indexed /
   post-increment loads and stores and `LI32` (D59), 3.18. Done.
4. **Card netlists**, the CPU card first, then MMU / data, memory; then the
   backplane (radial lines, D68: docs/backplane.md), then the device cards.
   Netlists done; schematics and layouts next.
