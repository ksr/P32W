# P32W backlog

Work Ken has asked for that is not yet scheduled, and smaller follow-ons.
The next software milestone is **M7, the native toolchain** (D24,
[os-design.md](os-design.md)). The hardware's next step is **the card
netlists**, the CPU card first. Items move into a design document (and a
decision, D-number) when they are taken up.

## Hardware still to design

### The board layouts (status 2026-10-30)

All on 6 layers (D77), from `sh generators/build_board.sh <card>` (Freerouting
2.5 at `~/freerouting`: FRJAR, FRJAVA, else the system's Java 25+). Each
run places the card fresh; a try that leaves connections open is improved
only by placing again (ROUTE_TRIES) or another direction cost
(`KT_AGAINST`, D80), not by routing the same board again: Freerouting
cannot finish an already-routed board (D77), and it stops by itself once
ten passes bring nothing.

- **Backplane:** done, placed and routed by `p32backplane.py` (D79): 0 DRC
  violations, 0 unconnected. Verify the sockets' 180° turn with a real
  card and socket before ordering.
- **I/O card:** routed (D80: a row per function), **0 unconnected**; its
  14 DRC "violations" are cosmetic (DB9 outlines past the front edge, silk
  clipped by pads, the P32W footprint library not registered). Routed at
  `KT_AGAINST=1`.
- **Memory card:** routed (D80: its grid spread across the card), **0
  unconnected, 0 violations**. Routed at `KT_AGAINST=1.5`.
- **MMU / data card:** routed (D80: a row per register), **0
  unconnected, 0 violations**. Routed at `KT_AGAINST=1`, 2 h tries
  (`FRTIME=7200`).
- **Disk card:** routed (D81), **2 unconnected**, 0 DRC violations: the IDE
  ports' activity lines, P2_DASP and P3_DASP, from J6 / J7 up the front
  edge to D3 / D4 in the LED bank; to route by hand (a 12 mm channel along
  the edge routed worse, 18). Matches its schematic.
- **Video card:** routed (D81), **1 unconnected**: KCNT, a 7 mm gap between
  U80 pin 7 and its own track; to close by hand. Its 1 DRC violation is
  cosmetic (silk). Matches its schematic.
- **Debug card:** routed (D81, D82), **1 unconnected**, 0 DRC violations:
  D23, from U29 pin 12 to U26 pin 9 (slice 2's drive register and sample
  latch, in one row); to close by hand. Its rows put the bus side left
  (sample latches, drive registers) and the capture side right; the first
  layout, two whole slices a row, stalled at 94. Matches its schematic.
- **CPU card:** routed (D81, D82) on 6 layers with the general packer (its
  195 chips fill the card at the tightest spacing): **18 unconnected** on
  four nets (LR21, NSTEP0, NSTEP1, ABORT_N), 0 DRC violations; matches its
  schematic. To finish by hand, or more tries; rows by function and 8
  layers both routed worse (D82).
- Route with `PARITY=` while a card has no generated schematic;
  `FRTIME=7200` gives a try 2 hours; one router at a time.

### Found by the theory documents: to settle before fabrication (2026-10-30)
Writing the theory of operation of the I/O, disk, video, debug cards and
the backplane (`docs/*-theory.md`), against the netlists and the gate-level
simulations, turned these up; each document's last section has the
detail. Ken's decisions; nothing is changed yet.

1. **The arbiter grants in the middle of a stretched cycle** (GAL ARB on
   the MMU / data card grants at any `CLK` edge with `-LOCK` high). A CPU
   cycle held by `-WAIT` is taken off the bus and run again, so a disk
   port's register sees two strobes: at gate level a PIO read during a DMA
   transfer got 7 extra strobes; a write would be done twice. Both
   emulators pass the same program. bus.md says a grant comes at a cycle
   boundary. (disk-card-theory §10, backplane-theory §11)
2. **The video card's `+F4` word read: bits 16-31 float** (no driver and,
   unlike XD0-15, no pull-downs). Two more 10k networks would fix it.
   (video-card-theory §14)
3. **The debug card's capture SRAMs: a write race** (`-WE` rises two
   74ACT00 delays after the sample clock edge that changes the latches;
   the CY7C199 wants its data 9 ns before). In simulation each sample
   lands one address early, and a DMA read can store the next clock's
   bus. Also: its GO / ARM / STEP flip-flops and drive registers are not
   cleared at power-on. (debug-card-theory §15)
4. **GAL IOSTB (I/O card): a possible glitch on `-RD` / `-WR`** between
   the two clocks of a byte access (a term `BCLK & !W1` in both). Nothing
   checks hazards. (io-card-theory §17)
5. **The disk card's internal bus CD0-15 has no pull resistors**, and
   IORDY reaches the sequencer GAL's registers unsynchronised. (disk-card-theory §10)
6. **The emulators against the hardware:** a TRELOAD write at the reload
   edge (the gate level reloads the new value, p32io.h the old one); the
   clock's interrupt (status bit 5, vector +5) is not modelled; a DMA bus
   error in DIR 0; BUSY in the PS/2 status. (io-, disk-, video-card-theory)
7. **The backplane:** with no card in slot 2 nothing drives the grants or
   holds the requests (their drivers and pull-ups are on the MMU / data
   card); `XCLK` is not terminated. bus.md's signal table says "7 highest"
   for `-IRQn`, against everything else (0 first). (backplane-theory §11)
8. **The I/O card's DB9s stand 2.84 mm further out than KiCad's footprint
   intends.** card_io.py puts pin 1 7.70 mm from the edge ("the shell
   through the panel"); the footprint's 7.7 mm "pin-PCB-offset" is the
   inner row (pins 6-9): its mounting holes, 9.12 mm from the edge, put
   pin 1's row at 10.54 mm, the flange at the board's edge. If the flange
   should sit at the edge, set 10.54 and route the I/O card again. (The
   video card's DE-15 had the same mix-up worse, its outer row off the
   board; fixed, D81.)
9. **The debug card's documents have drifted from its netlist** (164
   lines, 22 slices, 138 ICs, 12 MHz timing mode, the trigger on the
   address only, no window-4 registers or flash programming); DRIVE cannot
   drive `-BGNT3-10`, `-FAST` or `-IRQ0-7`. The Pico firmware and the host
   tool are not written. (debug-card-theory §15)

### The card netlists
The cards are specified ([architecture.md](architecture.md), D50) and the
bus is generated (docs/bus.md, `generators/p32bus.py`); no card's netlist is
designed yet. The order:

1. **The CPU card** (slot 1): sequencer and control store, instruction
   register and decode, register file, PC, the ALU module, the clock, the
   front panel.
2. **The MMU / data card** (slot 2): MAR, the split I/D TLB, the fault
   checks, MDR and byte steering, BADADDR, the bus master interface, the
   arbiter, `-LOCK`.
3. **The memory card**: 16 MiB of SRAM, the boot ROM, the window and ROM
   jumpers.
4. **The backplane**: the netlist is done (D68, docs/backplane.md): slot 0
   and ten slots, the radial `-BREQ` / `-BGNT` lines to slots 2 and 0, J3
   in slots 0–2 only, the power entry (D78), the pull-ups. Its layout to do.
5. **The device cards:** disk, I/O, video, then debug.

Each card follows the board rules (D19, D21, D47, D48): every part
through-hole, bypass caps on 0.1" footprints, switches, LEDs and cable
connectors at the front edge.

### Gate-level simulation of the cards
Done for the CPU card (D61: `generators/sim_cpu.py`, in `make test`).
Next the MMU / data card the same way, then the two together over the
local link (J3), and a 5 MHz CPU card (§ "5 MHz without a wait state").

### A jumper for the clock source: oscillator or an external clock
The CPU card's 15 MHz oscillator drives the clock GAL's clock input (D17).
Add a jumper choosing between that oscillator and an **external clock
brought in on a pin** (a header near the card edge, D48), labelled on the
silkscreen with each position (INT OSC / EXT CLK). Uses: a slower or faster
crystal for bring-up, a signal generator, or a clock from the control
panel (below). To decide with the CPU card: the input's buffering (a
74HCT14 Schmitt input, with a pull so an open pin cannot float), whether
the external pin is on the front edge or a header, and how it relates to
the debug card's fast clock (that one drives the bus `CLK`, after the GAL;
this one feeds the GAL).

### A probe header on the CPU card for the debug card
The debug card's logic analyzer reaches only what is on the backplane. A
**probe header on the CPU card's front edge** (D48) lets it see signals
inside the card: the microword, the sequencer's state, the internal data
bus. To decide with the netlist: which signals, the header's size, and how
the cable runs to the debug card.

### 5 MHz without a wait state
The memory microcycle (MAR → TLB → physical address → memory → R, about
200 ns) does not fit a 200 ns clock with the control store's share. At
5 MHz the MMU / data card asks for **one wait state on every access**
(`-WAIT` on itself, no microcode change); 3 MHz needs none. Measured with
`p32uemu -W N` on the three-bus datapath (P32W Unix booted, root logged in,
`halt`; 7,049,876 instructions with the address adder and the D59
instructions; 47% of all microcycles are bus cycles):

| Clock, waits | Cycles | Cycles / instruction | Speed |
|---|---|---|---|
| 3 MHz, 0 | 22,427,463 | 3.18 | 0.94 MIPS |
| 5 MHz, 1 on every bus cycle | 32,868,985 | 4.66 | **1.07 MIPS** |
| 5 MHz, 0 (translation earlier) | 22,427,463 | 3.18 | 1.57 MIPS (+47%) |

**Moving the address translation earlier**, into the microcycle that loads
MAR, removes the wait state and is worth another 47%. It changes the MMU /
data card and the microcode; to design after the wait-state version works.

### The ALU's division at 5 MHz
The ALU core's worst latency is 18 of its 108 MHz clocks (167 ns,
`fpga/alu/tb_p32alu.v`), set by division; every other function is a few
clocks. At 3 MHz the result is on R in time; at 5 MHz (200 ns, less the
operand and result transfers) a division needs its result cycle held a
clock (`busy` into the sequencer, as `STALL`), or the divider made faster.

### The VGA timing parts (done, D67)
Counters (74F163) and fast GALs on the 25.175 MHz dot clock; the card's RAM
16 bits wide, shared in four slots of each character time (text, the bus's
two halfwords, the overlay). docs/video-card.md.

### The power budget
Done (D69, docs/power-budget.md, generated by `generators/p32power.py`):
about 14 A typical, 26 A at most for the whole machine on +5 V, plus the
drives. Left: measure the Tang Nano 20K and the Picos, and replace the
estimated rows with datasheet figures as they are checked.

### An external control panel
The machine's controls are on the CPU card's front edge (D50): the RUN /
instruction-step switch, the RESET and STEP buttons, and the RUN and HALT
LEDs. A separate control panel, cabled to the card, would have room for
more: an address and data display, a clock-speed selector, and the
front-panel ports' switches and lights (the I/O card's ports at `FF20` /
`FF21`). To decide then: the cable (a header on the CPU card in place of
the parts), what the panel shows, and whether the debug card's probes and
fast clock join it.

### For the MMU / data card: a bigger or 2-way D-TLB
A hardware question for Ken, not decided. The D-TLB (D12, D13: 64 entries,
direct-mapped) costs about 8% of the time of a compile (see "Memory-system
statistics" below). Doubling it to 128 entries (one more index bit on the
D-TLB's SRAMs) cuts its misses about elevenfold; making it 2-way (a second
bank, two comparators, an LRU bit per set, and TLBW choosing the way) about
eightfold, with the same 64 entries. The I-TLB can stay as it is. The
emulators, the microcode and the kernel's refill would change with it (a
decision of its own).

### A DRAM memory card (later; Ken: "later, for now stick to Static")
For density: 30-pin or 72-pin SIMMs (4 or 16 MiB a module, DIP-free but
through-hole sockets) or DIP DRAMs, with a refresh controller and the
RAS / CAS timing on the card, answering in the same 16 MiB windows (D52) and
stretching its cycles with `-WAIT` as needed. To weigh against D60's static
cards (simple, no refresh, 16 MiB a card) when memory runs short.

### Faster disk transfers: DMA from the disk card
The CPU moves every 16-bit halfword through the CF data register: about
140 KB/s, and the CPU does nothing else meanwhile. That bounds swapping
(below) and all file I/O.

- **Software first (with swapping):** the sector loop in hand-written
  assembly (about 2×, ~280 KB/s; docs/bench.md), interrupt-driven waits
  (the CPU runs another process while the card is busy), READ/WRITE
  MULTIPLE. No hardware.
- **Then DMA: the engine is on the disk card (D75, 2026-10-28)**, verified
  at gate level and modelled in `p32emu` / `p32uemu`; per sector, the
  driver starts it once the drive has the data (docs/disk-card.md §4.1).
  To do: the driver (`kernel/`), using it for whole sectors and the
  interrupt instead of the copy loop; measure swap and file-I/O times.
  Later, if wanted: the drives' own multiword DMA, by reprogramming the
  card's GALs.

### Analysis: component speeds for a faster clock
**Done as an analysis: docs/speed.md** (2026-10-27): the clock jumper, the
SRAM control store, 5 MHz at worst case, DMA, the overlapped fetch.
The clock jumper is built into the design (D74, 2026-10-28): 4 MHz at
typical delays.

**The tool exists now** (D71, docs/timing.md, `generators/p32timing.py`):
it gives each path's delay and the fastest clock the machine allows, worst
case and typical; trying faster parts is a change to `p32delays.py` and a
rerun. First the 3 MHz failures it found (D71). (Analysis only, nothing to change yet.) The clock is 3 MHz, with 5 MHz the
design goal (D50; microarch.md §8). Find the critical paths card by card
and what each faster part would buy: 74HCT → 74AC / 74ACT / 74F / 74ABT
(through-hole availability, edge rates and ground bounce, drive into the
backplane), the microcode flash (55 ns, D18; or copied to fast SRAM at
reset), the register-file SRAM (15 → 10 / 12 ns), main memory SRAM and ROM
(55 ns → 10–25 ns SRAM, ROM shadowed in RAM), the TLB SRAM and tag
comparator, GAL speed grades (-25 / -15 / -10 / -7), the ALU FPGA's 5 V ↔
3.3 V buffers and latches, and the backplane itself (propagation, loading,
settling, and whether its long lines need termination). Structural
options: the earlier translation (above), wait states only for slow
devices, or a pipelined fetch. Result: a table of path → parts → delay →
achievable clock, and the cheapest steps to 6, 8 and 10 MHz.

### Idea (parked): a PDP-11/23 clone on this hardware
Could P32W's cards, or variants of them, run PDP-11/23 microcode and so the
11/23's operating systems (RT-11, RSX-11M, RSTS/E, Unix V6 / V7) and their
software? First analysis: [pdp11-analysis.md](pdp11-analysis.md): feasible
as a second microcode set with CPU card, MMU / data card and ALU FPGA
variants; the hard parts are the 64-byte MMU relocation and DMA disk
controllers. Emulator first (in `p32uemu`, checked against SIMH).

## Devices: what is left

### Networking
The network is a Pico 2 W with a W5500 behind a mailbox (D45,
docs/net-mailbox.md), doing TLS too. **Done:** the emulator model, kernel
sockets, `ifconfig`, `ping`, `telnet`, `fetch`, `telnetd`, `ftp`, `ntpdate`
(D45, D46). **Left:**

- **The coprocessor's firmware:** the mailbox's other side, then the W5500
  and TLS, then the bench.
- **httpd:** static files, and CGI (shell, awk or BASIC scripts writing
  HTML) for status pages (uptime, processes, `df`, the TLB statistics);
  then directory listings, downloads and uploads. **No HTTPS on P32W:** for
  access from outside, a proxy or VPN on another machine, never P32W
  exposed directly.
- **`select` / `poll`**, to wait on several descriptors at once.
- **SLIP** over a UART (RFC 1055), as a second interface under the same
  stack, if a machine without the network ever needs one.

### Wi-Fi: joining a wireless network
The Pico 2 W's CYW43439 radio gives Wi-Fi beside the W5500's Ethernet. The
mailbox speaks sockets, not Ethernet, so Wi-Fi changes the coprocessor's
firmware, not P32W's kernel. To do:

- **The firmware's socket layer** over the Pico SDK's lwIP for the radio,
  the coprocessor using whichever link is up.
- **The protocol gains a few commands:** scan for networks, join one
  (network name and password), the link's state and signal. P32W gains
  `ifconfig wifi NAME` (or a `wifi` command), the password kept in a file
  only root reads (`/etc/wifi.conf`), and a join at boot from `/etc/rc`.
- **The emulator's model** pretends: a list of networks, any password
  accepted, the Mac's own network behind it.
- **Security:** WPA2/WPA3 is done on the radio; the password must not be
  readable by other users on P32W.

### USB drives
**Done** (D43, D44, docs/usb-design.md): the CH376S model in the emulators,
the driver (`/dev/usb`, `/dev/rusb`) and the FAT tools (`dosdir`, `doscp`,
`dosrm`, `dosmkdir`, `dosformat`). **Left:** checking a real module's
pinout and USB socket position against the I/O card's front edge, then
trying real sticks on the bench; later, FAT mounted directly, which needs
a VFS (a filesystem switch).

### The graphics overlay for programs
The video card has a 640 × 480 one-bit overlay at offset `0x1000` of its
memory (architecture.md). The console uses only the text layer. To let
programs draw: **map the video memory into a process** (a device such as
`/dev/video` with a call that maps it, root or a `video` group only), a
small C graphics library (points, lines, circles, text), and BASIC's
graphics statements (SCREEN, LINE, CIRCLE, PSET) on top of it.

### A windowing system on the video console
After the overlay: overlapping windows, a terminal window running a shell
(several sessions on one screen), mouse and keyboard focus, menus, and an
API for programs. P8X has a resident window manager and its design
(`docs/p8x-wm-design.md` in the P8X repository: chrome, painter's repaint,
launching programs, per-window state). On P32W, with protected processes,
it becomes a **server process** (`/etc/wm`) that clients talk to through
pipes or a device.

### getty discarding typed-ahead input (optional)
`getty` could discard input typed ahead on a terminal before it shows the
login prompt (a flush of the terminal's input queue), so that stray keys do
not reach `login`. Optional.

## Operating system and tools

### Swapping: whole processes to disk
When page frames run out, `fork`, `exec` or `sbrk` fails (or a growing
stack kills its process). V6/V7-style swapping writes an idle process's
pages (data, bss, heap, stack; text can be re-read from its program file
or stay while others share it) to a swap area on disk, frees its frames,
and reads it back when it can run. More processes than fit in RAM; no
process bigger than RAM.

- **Software only:** the MMU, the page tables and restartable faults are
  there. In the kernel: a swap area (a reserved region or partition), a
  map of each swapped-out process's swap blocks, and a swapper that picks
  a victim (the longest-sleeping) when frames run out and brings processes
  back.
- **How long a swap takes** (each way, at 3 MHz; only pages a process
  touched are written):

  | swapped | C loop (~140 KB/s) | assembly loop (~280 KB/s) |
  |---|---|---|
  | idle `sh`, ~150 KB | ~1 s | ~0.5 s |
  | a 1 MB program | ~7 s | ~3.5 s |
  | `cc` on `lvm.c`, ~3 MB | ~20 s | ~10 s |
  | a full 8 MiB process (D34) | ~55 s | ~28 s |

  With 16 MiB of RAM or more, everyday use fits without swapping; it is
  the safety net for heavy moments. The swapper should keep a process in
  for a good while once it is in, so two big jobs do not thrash.
- **Faster disk transfers first** (they speed up every file too): see
  above.
- `ps` / `top` should show swapped-out processes; `p32emu -m`'s statistics
  should count swaps.

### Demand paging (later, if needed)
Single pages go to disk and come back on a fault, so a process can be
bigger than RAM. Needs a replacement policy (the clock algorithm) and
"used" and "dirty" information, which the software TLB does not keep: the
kernel emulates them by mapping pages read-only or not at all and noting
the faults. A process much bigger than RAM thrashes. Only after swapping,
and only if a real need appears.

### A debugger: source level
`db` (machine level: breakpoints, stepping, finish, backtrace, registers,
memory, disassembly, symbols from the a.out) runs on `ptrace` (unix.md).
To do: **source level** (`p32cc -g` emitting a line table and the locals'
frame offsets / register variables, kept by the linker in the a.out;
`print var`, `break file:line`), **`core` dumps** on fatal signals for
post-mortem use, and a **`gdb`-style stub in `p32emu`** so the same
debugger runs on the host against the emulator. Best done with M7, whose
object format must leave room for the debug sections.

### Measure hand-written assembly against C
Inner loops are measured (docs/bench.md): C is 1.2–2.3× slower than
assembly, and `memcpy` and `memset` are in assembly. Still to do: the
trap / system-call path, whole commands (`wc`, `grep`, `sort`), a
boot-plus-login figure, and the `n--` test. Report the ratio per piece and
per whole program, and whether the gap is in the compiler (worth improving
it) or inherent (worth a few assembly routines in libc and the kernel).
The benchmarks live in `software/bench/`, so they can be rerun as the
compiler improves.

### MicroPython
MicroPython is `/usr/bin/python` (D35, D36, D39). Left:
- **`pow` correctly rounded** (about 93%: y·log x needs more than 62 bits,
  a two-part log).
- **`tgamma`, `lgamma`, `erf`, `erfc` in 62 bits:** musl's code rounds at
  every step and is a few ulp off (`tgamma(5.5)` 1.8 ulp). Rewritten on
  softfp's 62-bit core, as `sin` and `atan` are (D38), they would be
  correctly rounded about 99.5% of the time. Rarely used: later.
- **`select` and `asyncio`** need `poll()` in the kernel.
- **Startup takes 3.1 s** (D37); the rest is the CF loop itself (DMA).

### Lua
Lua 5.4 is `/usr/bin/lua` and `luac` (`software/lua/README.P32W`). Left:
- **Deep recursion is slow:** 4,000 levels (each making a small table and
  a string) take 36 s of P32W time. Find out where it goes (stack
  reallocation? the incremental GC?).
- **Startup takes 4.1 s.** Measure where the time goes.

### BASIC
`/bin/basic` (D27; basic(1)). Left, from basic(1)'s BUGS:
- the graphics statements (SCREEN, LINE, CIRCLE, PSET, COLOR), with the
  overlay (above);
- INKEY$ (always ""), the MID$ statement, PRINT USING's `^^^^` and `**$`,
  CHAIN / COMMON / AUTO / EDIT, random-access files (FIELD, GET, PUT), and
  a DEF FN of more than one line.

### vi
`software/cmd/vi.c` (unix.md). Later: line wrapping, named registers,
marks, `:g`, `:set`.

### The SQL database
`/bin/sql`, `sqlui` and `libsql` (sql(1), sqlui(1), sqldb(3)). Left:
- a decimal or floating-point type (BASIC uses doubles, D27; SQL has none,
  and AVG is truncated to an integer);
- joins of more than two tables. (Not attempted: subqueries, UNION, views,
  ALTER, defaults, foreign keys.)

### Commands
- `dc`, and `grep -E` (grep has only -vnci).
- `ps -l` (a long listing: priority, nice, wait channel).

### Known bugs (small)
- **sh:** no `until`, `trap`, `$@`, PS1 or job control; words split at
  spaces only (no IFS); `"a b"$X` is split at the quoted space; past the
  word limit the rest stays one word, silently; `sh -c`'s `$0` is the
  command text; `done > file` cannot redirect a loop (`{ ...; } > file`
  can); `wait` takes no pid; `exec > file` does nothing. (sh(1)'s BUGS.)
- **Kernel:** `#!` is in libc's execve, not the kernel (no setuid
  scripts); `wait` on a stopped traced child marks the stop reported
  before the copyout, so an EFAULT loses it.
- **du** counts blocks from `st_size` (there is no `st_blocks`), so a file
  with holes is overcounted.
- **init**'s back-off is a `sleep(60)` in its main loop, so for that
  minute it does not reap or respawn the other terminals.
- **The kermit test is intermittent:** about 1 run in 3, under load,
  `kermit -s` (P32W sending to the host) ends with "the line closed". It is
  probably timing in `tools/pykermit.py` or the test, not Kermit on P32W.

### Follow-ons
- **The native cc's memory:** all the tokens exist before parsing starts;
  running the preprocessor and the parser together (a token at a time), or
  smaller tree nodes, would cut it further, if ever needed.
- **Code generation:** constant folding of addresses, compare-and-branch
  on immediates, leaf functions without a frame.
- **Disk writes** (10 MiB takes 143 s of kernel time): the buffer cache's
  lookup is a linear search (a hash), and the CF transfer loop in
  assembly.
- **Interrupt-driven disk transfers** (the disk card's interrupt, level 1)
  instead of polling.
- **LBA48** in the CF driver (and the emulator's CF model) for cards past
  128 GiB.
- **Partitions** on a disk (several filesystems on one big card or drive).
- **Speed on the hardware:** measure a boot with login; hand-written
  transfer loops for the disk driver and `memcpy`.

### Memory-system statistics: what they show
`p32emu -m` and the kernel's counts (`time -l`, `top`'s `Paging:` line)
show that the D-TLB thrashes on big programs. Compiling `ls.c`, a sort and
the shell tests: 24.6 M data accesses, 203,373 D-TLB misses (0.82%, 97% of
them conflicts), about **8% of all instructions spent refilling**. The same
accesses miss 25,826 times in a 64-entry 2-way TLB, 18,648 in a 128-entry
direct-mapped one, 7,504 in 128 2-way; a hashed index (VPN ^ VPN >> 6) is
worse (235,220). The I-TLB is fine (0.01%). See the D-TLB item above.
