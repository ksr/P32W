# The disk card

**Status:** NETLIST DONE (2026-10-26), verified at gate level with the CPU,
MMU / data, memory and I/O cards (§5); the board routed (D81, D82: 0
unconnected, 0 DRC violations; `backlog.md`, "The board layouts"). Any slot from 3 to 9 (the bus has a slot 10, the backplane none, D99),
9U × 340 mm (D49), through-hole (D21). D26, D50, D52, D57, D66. The
netlist is `generators/card_disk.py`.

**80 ICs, 15 of them GALs**; 205 parts (`python3 generators/card_disk.py --bom`).
41 of the ICs and 6 of the GALs are the **DMA engine** (D75, §4.1).

## 1. What it holds

**Four ATA ports**, programmed I/O, 16 bits wide (D26), in the card's 4 KiB
window (window 6, `0xFFFF_E000`, by default; D52):

| Port | Registers | Connector | Kernel |
|---|---|---|---|
| 0 | `FFFF_EF00`–`EF0F` | right-angle 40-pin header at the front edge: a **CF-to-IDE adapter** plugs on, its slot facing out (the boot card) | `/dev/disk`, minor 0 |
| 1 | `EF10`–`EF1F` | the same: the second CF card | `/dev/disk1` |
| 2 | `EF20`–`EF2F` | boxed 40-pin IDE header: channel A, master and slave | `/dev/hda`, `hdb` |
| 3 | `EF30`–`EF3F` | channel B | `/dev/hdc`, `hdd` |

All four ports are the same circuit. The CompactFlash cards sit in
off-the-shelf CF-to-IDE adapters (Ken's choice, D66), because every CF
socket is a surface-mount part.

**Each port's 16 bytes:**

| Offset | Read | Write |
|---|---|---|
| +0 | **data**: a halfword (16 bits, D26) or a byte | data |
| +1 | error | features |
| +2 … +5 | sector count, LBA 7:0, 15:8, 23:16 | the same |
| +6 | drive / head (bit 4: master or slave; bits 3:0 LBA 27:24) | the same |
| +7 | status (reading it acknowledges INTRQ) | command |
| +8 … +15 | the control block (CS1): +14 alternate status | +14 device control |

`FFFF_EFF0` card ID (2), `EFF1` revision (**1**: the DMA engine, D75),
`EFF4` **interrupt status** (a word: bit *p* = port *p*'s INTRQ, bit 4 =
the DMA engine). The data register answers halfwords and bytes, the other
port registers bytes, `EFF4` and the DMA registers words; anything else
gets no `-ACK`, which is a bus error.

**The DMA engine's registers** (D75; words):

| Address | Read | Write |
|---|---|---|
| `FFFF_EF40` DMAADR | the physical address of the next word | the first word's (bits 1:0 ignored) |
| `EF44` DMACNT | the words left (16 bits) | the words to move (0–65535: 256 KiB) |
| `EF48` DMACTL | bit 0 BUSY, 1 DIR, 3:2 the port, 4 IE, 5 MODE, 6 DONE, 7 ERR | bit 0 RUN (1 starts; 0 stops after the word in hand), 1 DIR (0: the drive to memory; 1: memory to the drive), 3:2 the port, 4 IE, 5 MODE (multiword DMA: reserved, write 0); a write clears DONE and ERR | **A port with nothing on it reads 00** in
every register (pull-downs on its data lines); the kernel's probe sees
"not ready" and leaves it alone.

## 2. Jumpers (at the card's edge, labelled)

Fitted = 0, open = 1:

| Jumpers | Meaning | Default |
|---|---|---|
| `WIN2 WIN1 WIN0` | the card's window | 6: WIN0 fitted, the others open |
| `L2 L1 L0` | the interrupt level of all four ports | 1: L0 open, the others fitted |

## 3. Blocks

| Block | Parts | ICs |
|---|---|---|
| **Bus interface** | 2 × 74HCT541 (A2–A7, BE, strobes, CLK, `-RES`: one load per line); 2 × 74HCT688 (A31–A16); GALs DKSEL (the window), DKBE (byte, halfword, word, lane), DKDEC (a port register, the data register's halfword, ID, status word), DKSEQ (the wait states and strobes), DKSTB (`-ACK`, the lanes); 74HC05 (`-ACK`, `-WAIT`, open drain) | 10 |
| **Card bus CD0–15** | 5 × 74HCT245 (lane 0 ↔ CD0–7, lane 1 ↔ CD8–15 for the data register, lanes 1–3 ↔ CD0–7 for a byte); 2 × 74HCT574 (the read latch); 3 × 74HCT541 (zeros on lanes 1–3 for the status word) | 10 |
| **Ports** | per port: 2 × 74HCT245 (CD ↔ DD0–15), a 74HCT541 (DA2–0, `-CS0`, `-CS1`, `-DIOR`, `-DIOW`, `-RESET`); GALs DKPORT0 / DKPORT1 (each port's selects, strobes and transceiver enable); 74HCT153 (the addressed port's IORDY); pull-downs on DD0–15, IORDY's pull-up, INTRQ's and DMARQ's pull-downs | 15 |
| **Interrupts** | 74LS145 (the card's level onto its `-IRQ` line, open collector); 74HCT85 (the acknowledged level against the jumpers); GALs DKIRQ (any source asking: the four ports, the DMA engine) and DKVEC (ID, revision, the status byte, the vector onto CD0–7: the lowest asking source) | 4 |
| **DMA engine** (D75) | GALs DKDMA (its register selects), DKDCTL (DMACTL), DKENG (the sequence, `-BREQ`), DKEOUT (enables, latch clocks, the CPU's wait), DKPSEL (the port access's address: the CPU's or the engine's), DKMWD (multiword DMA: wired, reserved); 8 × 74F163 (the address, 30 bits) and 4 × 74F163 (the count); 5 × 74HCT541 (the address, byte enables, `-MRD` / `-MWR` onto the bus as master); 4 × 74HCT541, 2 × 74HCT540 and a 74HCT541 (DMAADR, DMACNT, DMACTL read back); 2 × 74HCT540 (the count loaded inverted); 8 × 74HCT574 (the word buffers, 32 bits each way); 74HCT541 (the ports' `-DMACK`) | 41 |
| **Front edge** | the two CF headers; an activity LED per port (its drive's `-DASP`), labelled DISK 0–3 | — |

## 4. How it works

- **Two wait states on every port access** (D66). ATA's slowest PIO mode
  (0) wants the address 70 ns before the strobe, a strobe of 165 ns, and
  write data held 30 ns after it. **GAL DKSEQ**, clocked by CLK, steps
  through three clocks:

  ```
  clock          1                2                  3
  address, -CS   ═══════════════════════════════════════════
  -WAIT          ‾‾\_______________________________/‾‾‾‾‾‾‾‾‾   (released in clock 3: the cycle ends with it)
  -DIOR / -DIOW  ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_________________/‾‾‾‾‾‾‾‾‾   (all of clock 2: 333 ns)
  read data      ................<drive>==latched==|=latch onto the bus=
  ```

  The read latch takes the drive's data as `-DIOR` ends, so the drive can
  let go, and the latch drives the bus in clock 3. A write's data is
  held through clock 3. A drive that pulls IORDY low stretches clock 2.
  An access to the ID or the status word needs no wait states.
- **The card bus:** a halfword to the data register uses lanes 0–1, all of
  CD0–15. A byte register sits on its own lane and is steered onto CD0–7,
  so a register's address is its port's base plus its offset, as in
  `p32io.h`. Only the addressed port's transceivers and strobes are
  enabled. Its DA2–0 are the offset's low three bits, and its `-CS0` /
  `-CS1` come from offset bit 3.
- **Interrupts** (D52, D57): each port's INTRQ goes to GAL DKIRQ. While
  any port asks, the 74LS145 pulls the jumpered `-IRQ` line. In the
  acknowledge cycle, if the level being acknowledged (A4:A2, compared by the
  74HCT85) is the card's, it answers with **window × 16 + the lowest asking
  port**: `0x60`–`0x63` by default. Reading a port's status register
  acknowledges its drive's INTRQ.

### 4.1 The DMA engine (D75)

Built in now so no later board is needed; P32W Unix starts without it
(its driver moves the data itself, PIO) and can take it up with no
hardware change.

- **Per sector:** the driver issues the ATA command as today; when the
  drive has a sector ready (DRQ), it writes DMAADR, DMACNT (128 words) and
  DMACTL, and the engine moves the sector while the CPU runs on. The engine
  does the PIO itself: every drive and CF card works.
- **A word** (drive to memory): two reads of the port's data register,
  exactly as the CPU's (GAL DKSEQ's three clocks), into a 32-bit buffer
  (4 × 74HCT574; the first halfword is the low one), then **one bus cycle
  as master**: `-BREQ` to the MMU / data card's arbiter, and when granted,
  one clock driving A2–A31 from the address counter, all four byte enables,
  `-MWR` and the buffer onto D. Memory to the drive is the reverse (a read
  into the other buffer, then two data-register writes). About 11 clocks a
  word; the bus is the engine's for **one clock in eleven**, so the CPU,
  whose cycles use the bus about two in six, is rarely held.
- **Counters:** the address (8 × 74F163, A31:2) and the count (4 × 74F163,
  loaded with its complement through 74HCT540s; all ones is done) advance
  when a word is done. A bus cycle no card answers (no `-ACK`) stops the
  engine with **ERR**, that word not counted: DMAADR is the failing address.
  The count running out sets **DONE**. With IE either is the card's fifth
  interrupt source: status bit 4, **vector window × 16 + 4** (`0x64` by
  default), acknowledged by writing DMACTL.
- **Sharing the card's byte bus:** the engine's port accesses use the same
  DKSEQ, port transceivers and strobes as the CPU's; GAL DKPSEL gives them
  the engine's port and its data register (DA2–0 = 0, `-CS0`). The CPU may
  use the card while the engine runs: an access that needs the byte bus CD
  (a port register, the ID, the status word, an acknowledge) during an
  engine access waits on `-WAIT` (GAL DKEOUT's EWAIT) and runs after it;
  the engine starts its next access only when no such access is asked
  (CDREQ). The DMA registers are on D directly and never wait.
- **Multiword DMA wired, reserved:** each port's DMARQ goes to GAL DKMWD
  and its `-DMACK` comes from it through a 74HCT541; MODE and the selected
  port's DMARQ reach the sequencer, and DMAM the port GALs (to negate the
  chip selects). As programmed now `-DMACK` is never asserted and MODE does
  nothing: the drives' own DMA is a matter of reprogramming the socketed
  GALs DKENG, DKMWD and DKPORT0–1, not of a new board.

## 5. Verification

- Its 9 GALs against their specs, exhaustively; the netlist's electrical
  checks.
- **At gate level with the CPU, MMU / data, memory and I/O cards**
  (`generators/sim_sys.py --mem --io --disk`): only the drives' insides are
  `p32io.h`'s model, reached through each port's 40 header pins. The card's
  wait states are the first test of the MMU / data card's `STALL` path.
  `p32uemu -C` adds the same two wait states to every port access, so the
  reference stays exact:
  - `card_test` (the card's ID and status word);
  - `io_test` (the boot CF port: reads, a written sector, the disk image
    identical);
  - the new **`disk_test`**: an empty port reads 00; IDE A's master and
    slave each read; CF 1 written and read back; the status word; a port's
    interrupt through its vector `0x62`.

  Each runs with every instruction's state and every cycle as `p32uemu -C`.
  `make test` runs `disk_test`.
- **The DMA engine** (D75): **`diskdma_test`** at gate level, the engine a
  bus master through the MMU / data card's gate-level arbiter into the
  gate-level memory card: the registers; a sector read in two transfers
  while the CPU keeps reading the card's ID, status word, an empty port and
  DMACTL (each held while the engine has the byte bus); the sector written
  to CF 1 from memory and read back by DMA; the done interrupt through
  vector `0x64`; a transfer to an address no card answers, stopped with
  ERR. Self-checking (the CPU polls, so its cycle count is its own), and
  the same program passes on `p32emu` and `p32uemu -C`, which model the
  engine; the sector it writes is the same on all three.

## 6. To check before layout

- The CF-to-IDE adapter in hand: that it plugs onto a right-angle header
  with its slot facing out of the front edge, and its height (D48: parts
  up to about 20 mm).
- The 40-pin ATA pinout (standard) and the 74LS145's pin numbers.
- Timing against the datasheets: the drive's data valid before `-DIOR`
  rises into the 74HCT574s.
