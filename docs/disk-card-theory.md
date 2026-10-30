# The disk card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-30),
`generators/card_disk.py` (every chip and GAL equation comes from there).
The card's summary, its parts by block and its jumpers are in
[the disk card](disk-card.md); its registers as programs see them in
[the programmer's guide](programmers-guide.md) §6.4; the bus cycle,
mastership and interrupts in [the bus](bus.md). D26, D50, D52, D57, D66,
D75.

## 1. What the card does

The disk card is a **bus slave** for its registers and, while its DMA
engine runs, a **bus master** (D75). It holds four **ATA ports**,
programmed I/O with the data register 16 bits wide (D26, §2); the **card
bus CD0–15** between the bus's byte lanes and the ports (§4); the **port
sequencer**, which gives every port access two wait states (§5); the
**interrupt logic**, one vectored level for the card (§7); and the **DMA
engine** (§8). Its registers are the top 256 bytes of its 4 KiB window
(D52): `0xFFFF_EF00`–`EFFF` in the default window 6.

| Offset | Register | Answers |
|---|---|---|
| `+00`–`+3F` | port *p* at `16 p` (§2) | the data register (`+0`): a halfword or a byte; the others: a byte |
| `+40`, `+44`, `+48` | DMAADR, DMACNT, DMACTL (§8.1) | a word |
| `+F0`, `+F1` | card ID 2, revision 1 (the DMA engine) | a byte |
| `+F4` | interrupt status (§7) | a word |

Any other address or size in the window gets no `-ACK`: a bus error. A
write to the ID, the revision or the status word is acknowledged and does
nothing.

## 2. The ports

| Port | Offsets | Connector |
|---|---|---|
| 0 | `+00`–`0F` | J4, a right-angle 40-pin header at the front edge: a CF-to-IDE adapter plugs on, its slot facing out (the boot card) |
| 1 | `+10`–`1F` | J5, the same: the second CF card |
| 2 | `+20`–`2F` | J6, a boxed 40-pin IDE header: channel A, master and slave |
| 3 | `+30`–`3F` | J7, the same: channel B |

The four ports are one circuit, four times:
- **two 74HCT245s** between CD0–15 and the cable's DD0–15, enabled only in
  that port's access, toward the drive for a write;
- **a 74HCT541** driving DA2–0, `-CS0`, `-CS1`, `-DIOR`, `-DIOW` and
  `-RESET` (the bus's `-RES`);
- **10k pull-downs on DD0–15**: an empty port reads 00 everywhere, so its
  status says "not ready". IORDY 1k up; INTRQ 10k (no drive, no interrupt)
  and DMARQ 5.6k down; CSEL grounded; `-IOCS16`, `-PDIAG` not connected;
- **an activity LED** at the front edge, labelled DISK *p*, lit through
  470 Ω while the drive pulls `-DASP`.

Offset bit 3 picks `-CS0` (the task file, +0–7) or `-CS1` (the control
block, +8–15); bits 2–0 are DA2–0 (the registers: disk-card.md §1).

## 3. The jumpers

Six jumpers at the card's edge, labelled. **Fitted reads 0, open reads 1**:
each grounds its line, and a 10k network pulls it up.

| Jumpers | Meaning | Default (D52) |
|---|---|---|
| `WIN2 WIN1 WIN0` | the window, 0–7: the registers at `0xFFFF_8F00` + window × `0x1000`; the vector's high digit (§7) | 6: WIN0 fitted, the others open |
| `L2 L1 L0` | the interrupt level of all five sources | 1: L0 open, the others fitted |

## 4. Decode and the byte lanes

**Off the bus**, one load per line: two 74HCT541s buffer A2–A7, BE0–3,
`-MRD`, `-MWR`, `-IACK`, CLK (it clocks the registered GALs and the
counters) and `-RES` (it clears those GALs and is each port's `-RESET`);
two 74HCT688s see A16–31 all ones; A8–A15 go to GAL DKSEL directly.

**GAL DKSEL**, *is this cycle for the card?* The registers: A31:A16 all
ones, A15 = 1, A14:A12 = the window jumpers, A11:A8 = F; `CRD` with
`-MRD`, `CWR` with `-MWR`, `CACC` with either. `INTA`: `-IACK` low,
whatever the address (§7).

**GAL DKBE**, *how wide?* `BYTE` (exactly one byte enable) with its lane
`LN1:LN0`; `HALF` (BE0 and BE1, no others); `WORD` (all four).

**GAL DKDEC**, *which register?* With `CACC`: `PSEL`, a port register
(offset bits 7:6 = 0, and a byte at any of the 64 offsets, or a halfword
at a port's +0: `WIDE`, the data register); `SIDR`, a byte at +F0 or +F1;
`SISR`, a word at +F4. GAL DKDMA decodes the DMA registers (§8.1).

**GAL DKSTB**, *does the card answer, on which lanes?* **`ACK`** for
`PSEL`, `SIDR`, `SISR`, the DMA registers (`DMAK`), or an acknowledge cycle
the card answers (`INTA` and `VHIT`, §7); a 74HC05 open-drain gate puts it
on `-ACK`, two more put the sequencer's `WAIT` and the engine's `EWAIT` on
`-WAIT`. DKSTB enables **five 74HCT245s** between the lanes and CD, and
sets their direction (`LDIR`: toward the card for a write):

| Access | Lane ↔ CD |
|---|---|
| the data register, a halfword | lane 0 ↔ CD0–7 and lane 1 ↔ CD8–15 |
| a byte register, the ID, the revision | its lane ↔ CD0–7 |
| the status word, the vector | lane 0 ↔ CD0–7 |

None is enabled while the DMA engine has CD (`EACC`, §8.4). A byte
register sits on its own lane at its own address, as in `p32io.h`. Three
74HCT541s drive zeros onto D8–15 (`ZO1N`: a read of the status word or
DMACTL) and D16–31 (`ZO2N`: those and DMACNT).

## 5. The port access: DKSEQ, DKPSEL, DKPORT0/1

ATA's slowest PIO mode (0) wants the address 70 ns before the strobe, a
strobe of 165 ns, and write data held 30 ns after it (disk-card.md §4).
The card gives every port access **three clocks: two wait states** (D66).

| S | Clock | What happens |
|---|---|---|
| 0 | 1: address | DA2–0 and `-CS0` or `-CS1` come out; `-WAIT` (a CPU access) |
| 1 | 2: strobe | `-DIOR` or `-DIOW` low for the whole clock; `-WAIT`; repeated while IORDY is low |
| 2 | 3: recovery | the strobe off; a write's data still held; a CPU read's data from the read latch, `-WAIT` released, so its cycle ends with this clock |

**GAL DKSEQ**, clocked by CLK and cleared by `-RES`, holds S: 0 → 1 → 2 →
0 while a port access is asked (`PACC`), 0 whenever none is. It makes
`WAIT` (a CPU port access in S = 0 or 1, or any while the engine's runs),
`-DIOR` / `-DIOW` and `STRB` in S = 1, `RLOEN` for a CPU read in S = 2.

**GAL DKPSEL**, *whose access?* `PACC` is the CPU's (`PSEL`) or the
engine's (`EACC`). The CPU's: port BA5:4 (`PS1:PS0`), block BA3 (`PA3`),
DA2 = BA2 (`PA2`), DA1:0 the lane (`PL1:PL0`), direction `CWR` (`PWR`).
The engine's: DMACTL's port and direction, the rest 0 (`-CS0`, DA2–0 = 0:
the data register).

**GALs DKPORT0** (ports 0, 1) and **DKPORT1** (ports 2, 3) drive the
addressed port only: `-CS0` or `-CS1` for the whole access, `-DIOR` /
`-DIOW` from DKSEQ, and its transceivers' enable: a write all three clocks
(the data held through the recovery), a read only in S = 1.

**IORDY:** a 74HCT153 picks the addressed port's IORDY (`IORDYS`); DKSEQ
looks at it as the strobe clock ends, and while it is low the strobe lasts
another clock. **The read latch:** two 74HCT574s across CD0–15, clocked by
DKSEQ's `-DIOR`, take the drive's data as the strobe ends, so the drive
can let go, and drive it back onto CD in S = 2 (`RLOEN`). The drive's own
`-DIOR` passes DKPORT and the 541 first, so it rises after that clock.

## 6. A register access, end to end

`lhu r2, 0xFFFFEF20(r0)`: port 2's data register (IDE channel A).

1. **Clock 1** (S = 0). The MMU / data card drives the address, BE0–1 and
   `-MRD` (mmu-card-theory §6). DKSEL gives `CRD`, DKBE `HALF`, DKDEC
   `PSEL` and `WIDE`. DKSTB pulls `-ACK` and turns lanes 0–1 toward the
   bus. DKPSEL and DKPORT1 put DA2–0 = 0 and `-CS0` on port 2's cable.
   DKSEQ pulls `-WAIT`; the MMU / data card raises `STALL`, and the CPU's
   commit waits.
2. **Clock 2** (S = 1). `-DIOR` low on port 2; its transceivers carry
   DD0–15 onto CD0–15. With IORDY low at the clock's end, it repeats.
3. **The edge.** S = 2: DKSEQ's `-DIOR` rises and the read latch takes
   CD0–15; the port's transceivers turn off.
4. **Clock 3** (S = 2). The latch drives CD0–15, lanes 0–1 carry it to
   D0–15 (DD0–7 on D0–7: the first byte in bits 7:0). `-WAIT` is released.
5. **The edge that ends clock 3** ends the cycle: the MMU / data card
   steers the halfword onto R, and the CPU commits. S returns to 0.

**A write** takes the same three clocks, the lane transceivers toward the
card (`LDIR`) and the port's toward the drive in all three; the master
holds D through the waits (bus.md), so the data stays a clock after
`-DIOW` rises. **A byte register**: `lbu r2, 0xFFFFEF27(r0)` (port 2's
status) has BE3 alone: lane 3 onto CD0–7, DA2–0 = 7 (A2 and the lane),
`-CS0`. **The ID, the revision and the status word** take one clock:
DKVEC drives CD0–7 (§7), unless the engine has CD (§8.4). At 3 MHz the
strobe lasts 333 ns; a port access costs the CPU two clocks more than a
memory access, and `p32uemu -C` adds the same two (D66).

## 7. Interrupts

Five sources: the ports' INTRQ0–3 and the DMA engine's `DMAIRQ` (§8.1). A
drive's INTRQ goes away when its status register (+7) is read
(programmer's guide §6.4), the engine's when DMACTL is written. One level
for the card, set by L2–L0.

**GAL DKIRQ:** `ANYN`, low while any source asks; `VHIT`, an acknowledge
cycle (`INTA`) for the card's level (`EQ`) with a source asking; `IDOE`,
DKVEC onto CD0–7 for `VHIT` or a read of the ID, revision or status word,
never while the engine has CD.

**The request: a 74LS145**, a BCD-to-decimal decoder with open-collector
outputs: A–C are L0–L2, D is `ANYN`. While a source asks the code is the
level, 0–7, and that output pulls its `-IRQ` line; with none asking it is
8–15, which pulls none (outputs 8 and 9 are not connected).

**The acknowledge cycle** (D57). The MMU / data card pulls `-IACK` with
the level on A4:A2:
1. DKSEL raises `INTA`; a **74HCT85** compares BA4:BA2 with L2:L0: `EQ`.
2. DKIRQ raises `VHIT` if a source asks; DKSTB pulls `-ACK` and turns lane
   0 toward the bus.
3. **GAL DKVEC** drives the vector onto CD0–7: **window × 16 + the lowest
   asking source** (ports 0–3, the engine 4): `0x60`–`0x64` in window 6.
4. The MMU / data card puts D7:0 into CAUSE bits 15:8 (mmu-card-theory
   §7): port 2 at level 1 gives CAUSE = `0x8000_6201`.

If no source asks by then, the card does not answer and the vector is
`0xFF` (D57). **The status word** (+F4): DKVEC drives bit *p* = INTRQ*p*
and bit 4 = `DMAIRQ` onto CD0–7, the 541s zeros above.

## 8. The DMA engine (D75)

Built in so that no new board is needed; P32W Unix does not use it yet.
Per sector, a driver issues the ATA command and, at DRQ, writes DMAADR,
DMACNT and DMACTL; the engine moves the sector while the CPU runs on,
doing the drive's PIO itself through the CPU's port circuits (§4.1 of
disk-card.md).

### 8.1 Its registers

| Register | Read | Write |
|---|---|---|
| `+40` DMAADR | the physical address of the next word | the first word's (bits 1:0 ignored) |
| `+44` DMACNT | the words left (16 bits) | the words to move, 0–65535 |
| `+48` DMACTL | bit 0 BUSY, 1 DIR, 3:2 the port, 4 IE, 5 MODE, 6 DONE, 7 ERR | bit 0 RUN, 1 DIR (0: the drive to memory), 3:2 the port, 4 IE, 5 MODE (reserved, 0); clears DONE and ERR |

**GAL DKDMA** decodes a word access to each (the load and read strobes,
`DMAK` for `-ACK`). They sit on D, not CD, and never wait; a write takes
effect at the edge that ends its cycle.
- **The address:** eight 74F163s, A31:2, counting words, loaded from
  D31:2; four 74HCT541s read them back, bits 1:0 zero.
- **The count:** four 74F163s hold its complement, loaded from D15:0
  through two 74HCT540s and counted up once a word; their carry out of all
  16 bits, `ALLONES`, means none left. Two 74HCT540s read them back
  inverted. (74F, so the carry chains settle within a clock.)
- **GAL DKDCTL**, clocked by CLK, is DMACTL: a write loads RUN, DIR, the
  port, IE and MODE from D5:0 and clears DONE and ERR. `BUSY` is RUN with
  words left. RUN clears itself when the count runs out (DONE sets, a
  clock after the last word) or a bus cycle has no `-ACK` (`BERR`: ERR
  sets). `DMAIRQ` is IE and (DONE or ERR). A 74HCT541 reads DMACTL back.
  `-RES` clears it; the counters keep their contents.

### 8.2 The word buffers

**In** (DIR 0): four 74HCT574s. The first halfword goes from CD0–15 into
the low pair at `CKLO`, the second into the high pair at `CKHI`, each as
the engine's `-DIOR` rises; they drive D0–31 in the engine's write cycle
(`EWRN`). **Out** (DIR 1): four 74HCT574s take D0–31 as CLK rises at the
end of the engine's read cycle (`CKRD`); the low pair drives CD0–15 in the
first port write (`OLON`), the high pair in the second (`OHIN`). Either
way the first halfword is bits 15:0 of the word in memory, as the CPU's
own `lhu` / `sh` copy loop leaves it.

### 8.3 The sequence: GAL DKENG

Clocked by CLK, cleared by `-RES`. A word is three steps, `PH`:

| PH | DIR 0: the drive to memory | DIR 1: memory to the drive |
|---|---|---|
| 0 | read the data register: the low halfword | the bus read |
| 1 | read it again: the high halfword | write the data register: the low halfword |
| 2 | the bus write | write it again: the high halfword |

- **A port step** (`EACC`) starts only with DKSEQ idle (S = 0) and no CPU
  access asking for CD (`CDREQ`, §8.4). It is exactly a CPU access's three
  clocks through DKSEQ, DKPSEL, DKPORT and the port's transceivers; it
  ends with S = 2, and PH advances.
- **The bus step:** `REQ` pulls the card's `-BREQ`. With `-BGNT` low at an
  edge, `CYC` is the bus cycle: one clock, longer while `-WAIT` is low. At
  its end PH advances (to 1 after a read with `-ACK`, otherwise to 0).
- A word **starts** only while BUSY; one started is finished, so writing
  RUN = 0 stops the engine after the word in hand.
- **`INC`** advances both counters: in DIR 0 at the end of a bus write
  with `-ACK`, in DIR 1 at the end of the second port write.
- **`BERR`**: the bus cycle ended with no `-ACK`. The word is not counted:
  DMAADR holds the failing address, DMACNT the words left. In DIR 0 its
  two halfwords have already left the drive.

**GAL DKEOUT** makes `MSTN` (the engine's bus drivers, on in `CYC`),
`ERDN` / `EWRN` (`-MRD` / `-MWR`; `EWRN` also opens the in buffer onto D),
the buffers' clocks and enables (§8.2), `CDREQ` and `EWAIT` (§8.4).

### 8.4 Sharing CD with the CPU

The CPU may use the card while the engine runs. `CDREQ` is a CPU access
that needs CD: a port register (`PSEL`), the ID, the revision, the status
word, or an acknowledge cycle (`INTA`, of any level). While a port step
runs, such an access waits: `EWAIT` pulls `-WAIT` (so does DKSEQ's `WAIT`
for a port register), and DKSTB's lane enables and DKIRQ's `IDOE` stay
off. When the step ends the access runs (a port register with its own
three clocks); the engine starts its next step only with no `CDREQ`. The
DMA registers are on D and never wait. A CPU port access that the
engine's grant takes off the bus after its strobe is run again in full
when the grant drops (§10).

### 8.5 As a bus master

- **The request:** `-BREQ` is the slot's radial request. The MMU / data
  card's arbiter grants at a CLK edge when no grant is out and `-LOCK` is
  high: slot 0 first, then the highest slot (mmu-card-theory §9).
- **The cycle:** in `CYC` five 74HCT541s drive A2–A31 from the address
  counter, BE0–3 all high (a word), and `-MRD` (DIR 1) or `-MWR` (DIR 0),
  the other high; `-IACK` is left alone. For a write the in buffer drives
  D0–31. The addressed card answers as it would the CPU, and `-ACK` is
  looked at as the cycle ends. The address is physical: no MMU (bus.md).
- **The release:** `-BREQ` goes high after `CYC`; the arbiter drops
  `-BGNT` at the next edge.

So `-BGNT` is low **three clocks a word**: the bus idle, the cycle, and
one after `-BREQ` is released; a CPU bus cycle in any of them waits
(`STALL`). The engine's bus cycles need 261.5 ns of the 333 ns clock at
worst case, 144 ns typical (D75, timing.md).

### 8.6 A word, end to end

DIR 0, nothing else using the card or the bus (DKENG's equations; the
gate-level simulation of `diskdma_test` shows the same):

| Clock | What |
|---|---|
| 1–3 | `EACC`, PH 0: DA2–0 = 0, `-CS0` on DMACTL's port; `-DIOR` in clock 2; `CKLO` takes the low halfword as it rises |
| 4 | idle; PH = 1 |
| 5–7 | the second read: the high halfword at `CKHI` |
| 8 | idle; PH = 2 |
| 9 | `REQ`: `-BREQ` low |
| 10 | `-BGNT` low; the bus idle |
| 11 | `CYC`: the address, BE0–3, `-MWR`, the in buffer on D; the memory card pulls `-ACK`; `INC` at the end |
| 12 | `-BREQ` high, `-BGNT` still low; PH = 0 |

**DIR 1** is the reverse: `REQ` in clock 1, the grant in 2, the bus read in
3 (`CKRD` at its end), the release in 4, the port writes in 5–7 (`OLON`)
and 9–11 (`OHIN`; `INC` at the end), idle in 8 and 12. Either way a word
takes **12 clocks**: a 128-word sector 1536 clocks, about 0.5 ms at
3 MHz. A CPU access to CD between two steps delays the next.

### 8.7 Multiword DMA: wired, reserved

Each port's DMARQ goes to GAL DKMWD, and its `-DMACK` comes from DKMWD
through a 74HCT541. DKMWD gives the selected port's DMARQ (`DRQS`) to
DKENG, which also has MODE; DKPSEL gives `DMAM` (MODE in an engine access)
to DKPORT0/1, to negate the chip selects. As programmed, `-DMACK` is never
asserted and no equation uses `DRQS`, MODE or `DMAM`: the drives' own DMA
is a reprogramming of the socketed GALs DKENG, DKMWD and DKPORT0–1.

## 9. Verified

`python3 generators/card_disk.py` checks every output of the 15 GALs
against its spec, exhaustively, and the netlist (every pin connected or
declared unused, every input driven, at most one totem-pole driver a net,
the connector pins as bus.md). `make test` runs it, then two programs at
gate level with the CPU, MMU / data, memory and I/O cards
(`generators/sim_sys.py --mem --io --disk`; only the drives' insides are
`p32io.h`'s model). **`disk_test`** (the ID and revision, an empty port
reading 00, IDE A's master and slave, CF 1 written by PIO and read back,
the status word, port 2's interrupt through vector `0x62`) matches
`p32uemu -C` in every instruction's state, the cycle count and the written
image. **`diskdma_test`** (the DMA registers; port 2's sector read by DMA
in two transfers while the CPU reads the ID, the status word, the empty
port 3 and DMACTL; a sector written to CF 1 by DMA and read back; the
interrupt through `0x64`; a transfer stopped with ERR) checks itself, also
passes on `p32emu` and `p32uemu -C`, and writes the same sector on all
three. Not covered: the CPU using a live port while the engine runs (§10),
the drive's data timing into the read latch (disk-card.md §6).

## 10. Open questions

- **A CPU port access can be cut in two.** The arbiter grants at any CLK
  edge with no grant out (mmu-card-theory §9), even inside a cycle held by
  `-WAIT`; bus.md says "at a cycle boundary". A CPU port access that
  waited behind the engine's last port step of a word strobes in the clock
  where `REQ` first shows; the grant at its end takes the cycle off the
  bus, and the MMU / data card runs it again: two strobes. `diskdma_test`
  at gate level does this to three reads of the empty port 3, unnoticed.
  A scratch program reading port 1's data register by PIO, with varying
  delays, while the engine read port 2's sector gave port 1 seven extra
  strobes and a wrong copy at gate level; `p32emu` and `p32uemu -C`, which
  move a word at once, pass it. A write would be strobed twice. How to
  prevent it is open.
- **Clocks a word.** D75 and disk-card.md §4.1 say about 11, the bus the
  engine's for one; `p32uemu` moves a word every 11 cycles (`p32io.h`).
  The equations and the gate-level runs give 12, `-BGNT` low for 3.
- **Every acknowledge waits for the engine:** `CDREQ` and `EWAIT` take
  `INTA` without the level, so another card's acknowledge also waits while
  a port step runs; disk-card.md §4.1 says "an acknowledge". Intended?
- **A bus error in DIR 0** comes after the word's halfwords left the
  drive; `p32io.h`'s `dma_word()` checks memory first and reads nothing
  (`diskdma_test`'s bus error is in DIR 1, where they agree).
- **Before layout:** CD0–15 has no pull resistors (it floats between
  accesses; in a byte write CD8–15 is undriven, copied onto DD8–15), and
  IORDY reaches DKSEQ's registers unsynchronised.
- **Stale text.** `card_disk.py`'s docstring: revision 0 (`CARD_REV` is 1),
  no status bit 4, no DMA registers. `p32io.h`'s header and the
  programmer's guide's §6 table: the status word without bit 4.
  disk-card.md: 9 GALs (§5; 15), "all four ports" on the level (§2; five
  sources), zeros "for the status word" (§3; DMACTL and DMACNT too), the
  DMACTL row running into the next paragraph (§1). The programmer's guide
  §6.4: port 0's CF card "in a socket" (an adapter on a header, D66).
