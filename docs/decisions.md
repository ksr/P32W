# P32W design decisions

This is the log of P32W's design decisions, in the order they were made.
Each entry gives its date and, where Ken made the decision, his words; it
states the decision, what it settles and what it leaves open. New
decisions are added at the end as dated entries.

## D1 — 32-bit data, 32-bit address, virtual memory (2026-10-11)

**Decision:** 32-bit data, 32-bit (virtual) addresses, and virtual memory
supported in hardware.

**What it settles:**
- **Address width:** full 32-bit addressing, not 24-bit.
- **Datapath width:** the data word and data bus are 32 bits. A 16-bit
  internal datapath with two-cycle ops is possible in principle but would
  have to sit behind a 32-bit bus; with the ALU in an FPGA (D21) removing
  the ALU's chip-count argument, the datapath is a full 32 bits.
- **The bus (D50):** 32 data and 32 address bits, not multiplexed.

**What virtual memory requires (settled in stages, D9–D13):**
- **An MMU** translating virtual to physical addresses. Classic homebrew/TTL
  options: an SRAM page map indexed by the upper virtual-address bits (plus a
  process/context ID), a small TLB with page-table walks done by microcode, or
  a software-managed TLB filled by the OS on a miss.
- **Page size** (e.g. 4 KiB, giving a 20-bit virtual page number).
- **Physical address width** — may be smaller than 32 bits; decides whether the
  bus address lines carry virtual or physical addresses (normally physical,
  after the MMU).
- **Page faults with restartable instructions:** the microcode must be able to
  abort an instruction mid-way, trap to the OS, and re-execute it cleanly —
  this shapes the microcode sequencer and which registers are updated when.
- **Privilege modes** (at least user/supervisor) and protection bits per page
  (read/write/execute, user/supervisor), so user programs cannot change the
  page tables.
- **Exceptions and interrupts** as first-class microcode paths (P8X has IRQ
  wired but simple).
- **ISA interplay:** RISC-V RV32 defines a virtual-memory scheme (Sv32: 4 KiB
  pages, two-level page tables, 34-bit physical) and supervisor mode, so
  choosing RV32 would also choose much of the MMU design; a custom ISA leaves
  it free (D2).

## D2 — Custom minimal ISA (2026-10-11)

**Decision:** P32W gets its own minimal instruction set, not RV32I or another
existing 32-bit ISA.

**What it settles:**
- **The ISA is custom.** It keeps the P8X lineage: the opcode table lives in
  the microcode generator and the assembler imports it (one source of truth),
  and documentation is generated from the same tables.
- **The MMU design (D1) is free:** no Sv32 constraints; page size, page-table
  format and TLB/page-map style are P32W's own choices.

**What "minimal" still has to cover (because of D1):**
- **Privileged operations:** enter/leave supervisor mode, return from
  exception, read/write MMU state (page map or TLB entries, context ID), and
  control/status registers.
- **Traps:** a system-call instruction, plus fault entry for page faults,
  protection faults, illegal opcodes and interrupts.
- **Restartability:** every instruction that touches memory must be safe to
  abort and re-execute after a page fault — simplest when only loads and
  stores touch memory (load/store architecture).

**Costs accepted:** no existing compiler or OS. The toolchain is built
in-house as on P8X (assembler first; a C compiler later — P8X's p8cc is the
starting point, or a retargetable compiler such as LCC or vbcc).

**Left open here (settled by D3–D5):** register count and width of register
specifiers; load/store vs. accumulator/memory-operand style; instruction
encoding (fixed 32-bit words vs. variable length); addressing modes; how much
is microcoded.

## D3 — Load/store, 16 registers, fixed 32-bit instructions (2026-10-11)

**Decision:**
- **Load/store architecture:** only load and store instructions access memory;
  ALU instructions operate on registers (and immediates).
- **16 general-purpose 32-bit registers** (4-bit register specifiers).
- **Fixed-length 32-bit instructions.**

**Why:** with virtual memory (D1) only loads, stores and instruction fetch can
page-fault, so restarting a faulted instruction is simple. Fixed 32-bit,
4-byte-aligned instructions never straddle a page boundary, so an instruction
fetch faults at most once, and decode is a fixed field split.

**What it implies:**
- **Encoding budget:** a three-register form (rd, rs1, rs2) uses 12 bits,
  leaving 20 for opcode and function bits; a two-register-plus-immediate form
  leaves room for an opcode and a 12–16-bit immediate. A full 32-bit constant
  takes two instructions (e.g. load-upper-immediate + OR/ADD).
- **Microcode addressing:** the microcode ROM is addressed by an opcode field
  (6–8 bits), not by the whole instruction register as on P8X (where ROM
  address = IR | step<<8 | cond<<12). Register fields go straight to the
  register-file address inputs.
- **Register file:** 16 × 32 bits = 512 bits. In discrete latches that is 64
  octal latch chips plus read-port multiplexing — strongly favouring a
  fast-SRAM register file. A three-register ALU op needs two reads and one
  write; options are dual SRAM copies (two read ports, writes to both), or
  two read cycles per operation.

**Left open here (settled by D4–D6):** register 0 hardwired to zero or not;
dedicated stack pointer / link register conventions; byte addressing with
byte/halfword loads and stores vs. word-only addressing; exact instruction
formats and opcode width; condition codes (a flags register, as on P8X) vs.
compare-and-branch.

## D4 — r0 = 0, byte addressing, little-endian, trap on misaligned (2026-10-11)

**Decision:**
- **Register r0 is hardwired to zero:** reads return 0, writes are discarded.
  r1–r15 are general purpose.
- **Byte addressing:** every byte has its own address; loads and stores come in
  byte, halfword and word sizes (signed and unsigned byte/halfword loads).
- **Little-endian:** the least significant byte of a word is at the lowest
  address.
- **Misaligned access traps:** a halfword access to an odd address or a word
  access to an address not divisible by 4 raises an alignment exception; the
  hardware never splits an access.

**Why:**
- r0 = 0 turns whole opcodes into ordinary instructions: move
  (`ADD rd,rs,r0`), load small constant (`ADDI rd,r0,imm`), clear, NOP,
  compare with zero, discard a result, absolute addressing (`LOAD rd,addr(r0)`).
  Keeps the ISA minimal (D2).
- Byte addressing is what C, byte-wide peripherals (UARTs, the CF/IDE task
  file, PS/2) and byte-oriented file/disk formats expect; pointers stay
  plain numbers.
- Trapping on misalignment means no access ever spans two words or two pages,
  so every memory access faults at most once — consistent with simple restart
  (D1, D3).

**Hardware consequences:**
- **r0:** register-file writes to register 0 must never land; the reset
  microcode writes 0 into location 0, and reads then need no extra logic
  (D16: such writes go to a write sink).
- **Bus:** the bus carries A2–A31 and four byte-enable lines BE0–BE3 in place
  of A0–A1 (D50).
- **Memory:** organised as four 8-bit byte lanes, each with its own write
  enable.
- **Load path:** byte/halfword loads need a lane-steering mux to bring the
  selected byte/halfword to bit 0, plus sign or zero extension.
- **Alignment check:** compares the low address bits with the access size
  before the bus cycle starts and raises the exception instead.

## D5 — Compare-and-branch, no architectural flags (2026-10-11)

**Decision:** conditional branches compare two registers themselves; there is
no programmer-visible flags register.
- Branches: `BEQ`, `BNE`, `BLT`, `BGE` (signed), `BLTU`, `BGEU` (unsigned),
  each of the form `Bcc rs1, rs2, offset`. Comparisons with zero use r0 (D4);
  `BGT`/`BLE` are assembler aliases with the operands swapped.
- Set-on-compare: `SLT rd, rs1, rs2` and `SLTU` (rd = 1 if less than, signed /
  unsigned, else 0) — used for boolean results and for the carry in
  multi-word arithmetic.
- The ALU still produces C/Z/N/V **internally**, routed to the CPU card's
  condition mux for the microcode to test (as P8X's FC/FZ/FN/FV bus lines do),
  but they are not stored as architectural state.

**Why:** on a microcoded, non-pipelined machine every instruction pays a full
fetch + decode, and most conditional branches follow a compare. Folding the
compare into the branch removes roughly 5–10% of instructions from typical
compiled code. The hardware is the same either way (ALU flag outputs + the
condition mux); only programmer visibility differs. No flags means nothing
extra to save on traps and context switches, and keeps the ISA minimal (D2).

**Costs accepted:**
- Multi-word arithmetic is longer: a 64-bit add is 4 instructions
  (ADD, SLTU for the carry, ADD, ADD) instead of 2 with ADC.
- Signed-overflow detection takes several instructions (no V flag).
- Branch offset is smaller: opcode + two 4-bit register fields leave ~18 bits;
  scaled by 4 (instructions are word-aligned, D3) that is ±512 KiB.
  Longer jumps use an unconditional jump/jump-register.
- P8X's carry-flag lesson (active-high carry) applies only inside the ALU
  and microcode: internal C is active-high, and SLTU/BLTU are defined on it.

**Left open here (settled by D8):** exact branch encoding and offset width;
unconditional jump and call forms and their reach.

## D6 — Calls push the return address on the stack (2026-10-11)

**Decision:** the call instruction pushes the return address onto the stack and
jumps; the return instruction pops it into the PC — as P8X's `JSR`/`RTS`. There
is no link register.

**What it implies:**
- **A hardware stack pointer.** CALL/RET (and trap entry/return) use one
  specific register, so the stack pointer is architectural, not just a
  software convention. It is one of r1–r15 (r15, below), so ordinary
  instructions can address stack frames (`LOAD rd, off(sp)`).
- **CALL is a store and RET is a load,** so both can page-fault (D1). The
  microcode must not change SP or PC until the memory access has succeeded:
  compute the new SP in a temporary, do the access, then commit SP and PC.
  Same rule for any other instruction that updates a register and touches
  memory.
- **Alignment:** SP must stay word-aligned; a misaligned SP traps on the
  push/pop like any other access (D4).
- **Cost accepted vs. a link register:** every call costs a memory write and
  every return a memory read, including leaf functions that call nothing
  (with a link register those never touch memory). In exchange: simpler
  call sequences, return addresses always where a debugger or stack walker
  expects them, and the P8X programming model.

**Settled with D6 (2026-10-11):**
- **SP is r15.**
- **Full-descending stack:** a push decrements SP by 4, then writes; SP
  points at the last item pushed, so `0(sp)` is the top of stack. (P8X is
  empty-descending — write, then decrement.)
- **Banked supervisor SP:** in supervisor mode r15 is a separate register
  (supervisor SP); in user mode it is the user SP. Traps and interrupts push
  onto the supervisor stack, so an invalid or unmapped user SP cannot cause a
  second fault on trap entry. Supervisor code needs a privileged way to read
  and write the user SP (e.g. for context switches).

**Left open here (settled by D8):** call reach — direct call offset width
(PC-relative), and a call-via-register form for function pointers.

## D7 — Instruction-level reference emulator first (2026-10-11)

**Decision:** the first P32W emulator works at the instruction level: it
decodes each 32-bit instruction and performs its effect directly, without a
microcode model.

**Why:** a microcode-interpreting emulator (the P8X rule: the emulator runs
the real ROM images) needs the whole microarchitecture first — microword,
control lines, bus timing, register file. An instruction-level emulator can
run programs and test the ISA and assembler now, while ISA mistakes are
cheap to fix.

**Role:** it is the **golden reference model** for the ISA. The
microcode-level emulator, the FPGA ALU (D21) and eventually the hardware are
checked against it, as P8X's FPGA build is co-simulated against p8xemu. This
is a deliberate departure from P8X's "the emulator knows no instructions"
rule, which applies to the microcode-level emulator.

**Single source of truth kept:** the opcode table lives in one Python module;
the assembler imports it and a generator emits a C header for the emulator
(as P8X's gen_p8xopc.py does for its tables).

**v1 scope:** flat physical memory (no MMU), supervisor mode only, no
interrupts; exceptions (illegal opcode, misaligned access, unmapped address)
stop the emulator with a message and a register dump. Instruction formats,
opcode map and memory map: see [isa-v0.md](isa-v0.md).

## D8 — ISA v0 accepted (2026-10-11)

**Decision:** [isa-v0.md](isa-v0.md) is accepted as the ISA for the reference
emulator v1, with all four review points as proposed: 8-bit opcode (branches
±128 KiB), offsets relative to PC + 4, the emulator's v1 memory map (ROM at 0,
RAM from 1 MiB, I/O page in the top 32 KiB; the machine's map is D52), and a
`NOR` instruction (0x1A) so that `NOT rd,rs` = `NOR rd,rs,r0`. Multiply and
divide (0x30–0x37) are included; the FPGA ALU (D21) implements them in
hardware.

## D9 — Goal: a protected, MMU-based multitasking OS, built in stages (2026-10-11)

**Decision:** P32W targets a protected multitasking operating system:
separate processes, each in its own virtual address space (D1), unable to
touch each other's memory, the kernel, or the hardware directly.

**Staged plan** — each stage is specified, added to the reference emulator and
tested (D7) before the next:

| Stage | Machine support | What runs |
|---|---|---|
| A | Trap/exception mechanism, status register, user/supervisor mode, SYSCALL, return-from-exception, privileged instructions | a small kernel running user-mode programs that request services by system call |
| B | Timer interrupt (and the interrupt-enable machinery) | preemptive multitasking among tasks in one address space |
| C | MMU: page tables/translation, per-page protection, page faults | protected processes, each in its own address space |

**Why staged:** each stage is testable on its own, and the trap mechanism of
stage A is designed from the start for the page faults and interrupts of
stages B and C, so nothing has to be redesigned later.

Stage A proposal: [isa-system.md](isa-system.md).

## D10 — Stage A (traps and privilege) accepted (2026-10-11)

**Decision:** [isa-system.md](isa-system.md) stage A is accepted with all five
review points as proposed:
1. Trap entry writes no memory (EPC, CAUSE, BADADDR and the mode bits are
   saved in control registers; the handler saves general registers), so trap
   entry cannot fault.
2. One handler entry point at `VBR`, dispatching on `CAUSE`.
3. `EPC` holds the address of the trapping instruction for every exception,
   SYSCALL and BREAK included (the handler adds 4 to continue after them).
4. User-mode access to the I/O page traps (PRIVILEGE) already in stage A.
5. In the emulator, `VBR` = 0 means "no handler": an exception stops the
   emulator with a register dump, as in v1.

## D11 — Stage B (interrupts and timer) accepted (2026-10-11)

**Decision:** [isa-system.md](isa-system.md) stage B is accepted with all five
review points as proposed:
1. Interrupts are taken only between instructions; `EPC` = the next
   (unexecuted) instruction; `CAUSE` = `0x8000_0000 | line`.
2. Eight level-triggered IRQ lines, fixed priority (lowest number first),
   enabled per line by `IMASK` (CR 7) and globally by `STATUS.IE`; `IPEND`
   (CR 6) shows the asserted lines.
3. The timer is a memory-mapped device on IRQ0 (`TCOUNT`/`TRELOAD`/`TCTRL` at
   `0xFFFF_FF10`–`FF18`); in hardware it is on the I/O card (D50, D52).
4. The emulator's timer ticks once per instruction, so interrupt timing is
   exactly repeatable; hardware ticks from a divided clock.
5. A privileged `WAIT` instruction (0x75) for idle loops.

## D12 — Stage C (virtual memory) accepted (2026-10-11)

**Decision:** [isa-system.md](isa-system.md) stage C is accepted with all six
review points as proposed:
1. **Software-managed TLB:** a miss raises an exception and the kernel
   refills the entry from its own page tables (format chosen by software);
   no hardware page-table walker.
2. **Translation in user mode only;** supervisor mode uses physical
   addresses, so the refill handler can never miss.
3. **4 KiB pages; 64-entry direct-mapped TLB** tagged with an **8-bit ASID**
   (context switches need no flush). `ASID`, `TLBHI`, `TLBLO` (CR 8–10),
   `TLBW` (0x76); exceptions 8–10 TLB miss on fetch/load/store, 11 PROTECT.
4. **User space is the lower 2 GiB;** user access at or above `0x8000_0000`
   traps (PRIVILEGE), which includes the stage A I/O-page rule.
5. **Permission bits V/W/X only;** no hardware accessed/dirty bits.
6. **The system bus carries physical addresses** (32-bit physical; RAM up to
   64 MiB, D52). Settles the D1 question.

With stages A–C the machine has everything D9 asked for: protected,
preemptively multitasked processes in separate address spaces.

## D13 — Split instruction/data TLB (amends D12) (2026-10-11)

**Decision:** the stage C TLB is split into a **64-entry I-TLB** (instruction
fetches) and a **64-entry D-TLB** (loads and stores), each direct-mapped and
ASID-tagged. A miss or protection fault sets `TLBHI.I` (bit 8) = 1 for a fetch,
0 for a data access; `TLBW` writes the half that `TLBHI.I` selects.

**Why:** found by the emulator, running the stage A trap test. One
instruction can need two translations — its own fetch and its data access.
In a single direct-mapped TLB, when the two pages share a slot (VPN mod 64),
each refill evicts the other entry and the instruction never completes: a
livelock that any program could hit by chance. With the split, every
instruction needs at most one entry per half, so it always completes.

**Cost:** almost none — in hardware both halves are one 128-entry SRAM whose
extra address line is the fetch/data signal. A 2-way set-associative TLB
would also have fixed it, at the cost of a second SRAM and comparator,
replacement logic, and kernel control of the way.

## D14 — Microarchitecture accepted (2026-10-11)

**Decision:** [microarch.md](microarch.md) is accepted with its review
points as proposed:
1. **One shared 32-bit internal data bus,** one driver per microcycle via
   encoded `DOE`/`DLD` fields decoded where each source and destination sits
   (P8X style). Between the CPU card and the MMU / data card it runs on the
   local link (D50); the MMU / data card puts the physical address (A2–A31,
   BE0–BE3) on the system bus.
2. A 32-word fast-SRAM register file: r0–r15 (15 = USP), EPC, CAUSE,
   BADADDR, VBR, eight microcode scratch registers T0–T7, and the SSP at 31
   (the r15 bank is one gate); r0 by write redirection (D16).
3. PC is a dedicated counter (+4 without the ALU; it holds bits 31:2 only).
4. Microcode in 5 V parallel flash behind a pipeline register (55 ns parts,
   D18); microaddress = {SEQ, opcode, step, cond}.
5. Precise exceptions: every instruction's microcode commits programmer-
   visible state only after its last possibly-faulting access (checked by the
   generator), and a fault aborts the cycle into the trap-entry sequence.
6. A 4 MHz first estimate of the microcycle (≈ 0.6 MIPS), refined on the
   bench: D17 sets 3 MHz, and D50 designs the bus for 5 MHz.

The cards that carry it are in D50.

Next: the microcode generator and a microcode-level emulator checked against
the instruction-level reference emulator (D7).

## D15 — Two stage A details pinned down for the hardware (2026-10-11)

Writing the microcode showed two places where isa-system.md left the
behaviour open or tied to the reference emulator's internals:
1. **`BADADDR` is 0** for exceptions without an address (illegal and
   privileged instructions, SYSCALL, BREAK, reserved control register) and
   for interrupts.
2. **`VBR` and `EPC` read back exactly as written;** because the PC holds
   only bits 31:2 (D14), trap entry and `RTE` ignore their low 2 bits.
Both emulators implement these, so their traces can be compared exactly.

## D16 — The register-file write strobe; the r0 write sink (2026-10-11)

**Decision:**
1. **`-WSTB`,** a write strobe on the local link (D50), low late in the
   datapath phase and rising with `CLK`. Every asynchronous SRAM write on the
   CPU and MMU / data cards (the register file, the TLB) is gated by it, so no
   write starts before its address has settled.
2. **r0 by redirection:** a register write whose address comes from an IR
   field equal to 0 goes to physical register 28 (the write sink), chosen with
   the register address; physical 0 is written with 0 only by the reset
   microcode. The register file itself has no r0 logic.
3. **Activity LEDs** (register-file access, PC load) are gated with the real
   enables.

## D17 — The CPU card: clock, GALs, abort, BADADDR, front panel (2026-10-11)

**Decision:** for the CPU card's control logic:
1. **Clock:** a 15 MHz master oscillator divided by 5: a **3 MHz microcycle**
   (333 ns), with the control-store lookup in the first 2 ticks (133 ns, `CLK`
   high) and the datapath in the last 3 (200 ns, `CLK` low); `-WSTB` low during
   the last tick. Every memory access stays one cycle; ≈ 0.5 MIPS. Why: with
   the datapath in half of each microcycle, a 4 MHz clock (D14) leaves 125 ns,
   and the memory path through the TLB needs ~200 ns. 3 MHz is the safe
   setting; the bus is designed for 5 MHz (D50).
2. **Technology:** 74HCT plus **ATF22V10 GALs** (5 V, DIP-24) for the glue
   logic. GAL equations are generated from Python and verified there (single
   source), like everything else.
3. **Abort gating:** while `FAULT` is asserted the CPU card forces `DLD` to
   NONE, `PCINC` to 0 and the MODE action to none, so no destination latches
   in a faulting cycle.
4. **BADADDR:** the trap-entry microcode tests a condition **MMUF** (the
   condition mux's 8th input): BADADDR comes from the MMU / data card for MMU
   faults and is 0 for traps the CPU card raises itself (D15).
5. **Front panel:** RUN/HALT, STEP (one microcycle; one instruction by running
   to the next fetch), RESET, activity LEDs.
6. **Decode ROMs** (opcode table, control-register table) in the same flash
   as the control store, generated from `p32isa.py`.

## D18 — Decode traps by a suppressed step; 55 ns flash (2026-10-11)

1. **Illegal/privileged-instruction traps take one suppressed step.** The
   decision (IR → decode table → trap) cannot be made before the first
   microword of the instruction (step 2) is looked up within the 133 ns lookup
   phase. So step 2 runs with all its effects suppressed by the abort gating
   (D17: no DLD, PCINC, MEM or MODE action), then trap entry follows.
   Invisible to programs; those two traps take one extra cycle. The
   microcode-level emulator models it.
2. **55 ns flash** (SST39SF010A-55) for the control store: the condition path
   from the ALU's compare outputs to the flash address needs ~137 ns with
   70 ns parts, over the 133 ns lookup phase.

## D19 — Boards: through-hole only; 4 layers; 0.13 mm clearance (2026-10-12)

- **Every component is through-hole** (Ken: important). Footprints are DIP,
  through-hole sockets, axial/radial passives, THT connectors and modules.
  `p32kicad_pcb.py` refuses a board with any SMD footprint.
- **4 copper layers by default** (F.Cu signal, In1 GND, In2 VCC, B.Cu
  signal); **6 where a card needs it** (Ken: "if need be move to 6 layer";
  6 layers cost ~1.5–2× at JLCPCB). Layer count is per card (`LAYERS` in
  `p32kicad_pcb.py`). The 6-layer stackup: F.Cu signal, In1 GND, In2 signal,
  In3 signal, In4 VCC, B.Cu signal.
- **Placement:** parts in affinity order (parts sharing nets adjacent), rows
  snaking, then pairwise swaps of identical footprints while the
  half-perimeter wire length falls.
- **Freerouting 2.5** (on a private Java 25 runtime): it logs every pass,
  stops when passes stop improving and saves what it routed.
- **Clearance 0.13 mm** (5 mil, a standard fab tier), tracks 0.2 mm — the
  P8X backplane's fix. The DIN 41612 pads are 1.55 mm on a 2.54 mm pitch, a
  0.99 mm gap; a card using most connector pins must escape two tracks per
  gap, which needs 1.0 mm at 0.2 / 0.2 and 0.79 mm at 0.2 / 0.13.
- **Tracks 0.2 mm everywhere:** Freerouting 2.5's automatic neck-down is off
  (it thins tracks to 0.10–0.12 mm to pass between pins).
- `build_board.sh` gates routing on a placement DRC (colliding parts short
  nets, and Freerouting then searches for hours) and stops Freerouting after
  `FRTIME` seconds (default 2400).

## D20 — Interrupt decision timing: TRAPENTER and the interrupt shadow (2026-10-13, ACCEPTED by Ken 2026-10-13)

**Found by simulating the sequencer cycle by cycle** running `irq_test`: the
sequencer decides "take an interrupt" in an instruction's last microcycle
from `IE` and `IPEND & IMASK` as they are during that cycle; the cycle's own
writes (STATUS, IMASK) land at its closing edge. Two rules follow:
1. **No interrupt at the end of a TRAPENTER cycle.** At the end of trap or
   interrupt entry (`MODE = TRAPENTER`, which clears IE at that edge) IE is
   still 1, so with an interrupt pending the machine would enter *again* and
   overwrite `EPC` with the handler's address (and an exception taken with
   interrupts enabled would lose its `EPC` the same way). The sequencer takes
   `MODE0–2` and never starts an interrupt at the end of a TRAPENTER cycle.
2. **The interrupt shadow (ISA rule, isa-system.md B2):** after `MTCR` or
   `RTE` the check uses `IE`/`IMASK` from before the instruction, so one more
   instruction runs before a pending interrupt is taken. Both emulators do
   exactly this. (The alternative — computing next-cycle IE and IMASK in the
   sequencer — needs more sequencer logic; the shadow is the behaviour of
   many CPUs, e.g. x86 STI, and guarantees progress after RTE.)
Verified: `p32uemu -C` models the I/O card's timer exactly (it ticks every
third microcycle and reloads one cycle after 0); all cross-checks between the
two emulators pass with the rule.

## D21 — The ALU is a Tang Nano 20K module; every part through-hole (2026-10-13, Ken)

Ken: "go ahead with the Nano 20K and DIP LVC parts".

1. **The ALU is an FPGA** rather than 74181/74182 logic; the rest of the
   machine is discrete 74-series logic. So the ALU's function set is not
   bound to the 74181's: barrel shifts and multiply/divide are single-cycle,
   as the microcode assumes. The flags are computed inside the FPGA, with the
   P8X lesson applied from day one: active-high carry and real signed
   overflow. The ALU is off the critical path; register and bus timing
   dominate. The emulator stays the golden model and the ALU's Verilog is
   checked against it.
2. **The FPGA is a Sipeed Tang Nano 20K** (GW2AR-18: 20,736 LUTs, 48
   multipliers, 2 PLLs) in two 1×20 female headers on the CPU card. The
   module brings its own 27 MHz crystal, configuration flash and USB
   programmer (no JTAG header), and runs from the bus's 5 V. Its pinout is
   published (Sipeed's pin diagram); the pin map is set with the CPU card.
3. **A 16-bit local bus to the module.** The module has 34 I/O pins; the ALU
   needs 53 with a 32-bit data path. Eight 74LVC574s split it: four capture
   `D0–31` at every `CLK` edge and give the FPGA one half at a time; four take
   the result a half at a time and drive the data bus while `DOE = ALU`. A
   GAL decodes `-ROE` (so the FPGA cannot drive the bus at all) and registers
   "this edge loaded A / B" for the FPGA. 31 of the 34 pins are used.
4. **Through-hole 3.3 V logic.** TI's SN74LVC574AN / SN74LVC244AN come in
   PDIP-20: 3.3 V parts whose inputs (and outputs when off) take 5 V, the only
   boundary between the 5 V logic and the 3.3 V FPGA. They drive at 3.3 V,
   above every receiver's TTL threshold (VIH 2.0 V). An LD1117V33 (TO-220)
   powers them. **No card has a surface-mount part.**
5. **Timing, verified in Verilog with models of the latches** (D17 timing,
   fields from the pipeline register at `CLK` falling, fclk 108 MHz
   unrelated to the bus clock): every function's result is on the bus at
   worst 270 ns after B's edge (the budget is 300 ns; division dominates:
   read-in ~60 ns, 16 + 2 divider cycles, write-out ~40 ns); sampling at
   250 ns fails 1,298 vectors, so the check is real.
6. **Branches take one more microcycle.** The compare flags (`EQ`, `LT`,
   `LTU`) are ready ~84 ns after B's edge; the sequencer's next-address
   lookup needs them ~55 ns after it (the pipeline register clocks at `CLK`
   falling, 133 ns, minus the 55 ns flash, D18). The six conditional branches
   test the condition one step after loading B: 4 microcycles not taken, 6
   taken.
7. **Still to confirm with the module in hand:** the header row spacing
   (0.7 in, from Sipeed's drawing) and position, and synthesis timing in Gowin
   EDA (or Yosys + nextpnr-himbaechel) at 108 MHz — the multiplier is
   pipelined two stages.

## D22 — P32W Unix: a multi-user, V6-style OS in C (2026-10-13, ACCEPTED by Ken 2026-10-13)

Ken: "a multitasking OS where each user is in a protected area with their
own memory space … a filesystem, system calls into OS and classic unix like
commands." Chosen with Ken: **C, with our own compiler**; **our own kernel in
the style of Unix V6**; **real multi-user** (logins, uids, permissions, one
terminal per UART); **a Unix inode filesystem** on the CF card. The design
and its review points are in [os-design.md](os-design.md): the toolchain
(relocatable assembler, linker, `p32cc`, a C library), the kernel (processes
in private address spaces (D34) with a one-load TLB refill, ~35 V6/V7
system calls, signals, terminal and disk drivers), the filesystem (1 KiB
blocks, 64-byte inodes, `p32fs` host tool), users and permissions, the
commands, emulator support and tests, and the phases M1–M6. It needs no
hardware beyond stages A–C (D10–D12).

## D23 — A real-time clock on the I/O card (2026-10-14, Ken; the part proposed)

Ken: "We need to add a realtime clock maybe on IO card then commands and
calls to read and set." Without one, P32W Unix would start every boot at
1 January 1970.

1. **The part: a DS12C887** (Maxim / Analog Devices): a 24-pin, 600-mil
   DIP module with the 32.768 kHz crystal and a lithium cell inside (about
   ten years), the MC146818 register set of the PC (seconds to century,
   alarms, periodic and update interrupts, binary or BCD, 12/24-hour) and
   114 bytes of battery-backed RAM. Through-hole, 5 V, no extra parts. (To
   check before ordering: the pin numbers, taken as the MC146818 /
   DS12887's.)
2. **On the I/O card's byte bus**, PC-style, two byte registers:
   **`FFFF_FF40`** the register number (write), **`FFFF_FF41`** the
   register (D52). Intel bus mode (`MOT` = GND). A write to `FF40` makes a
   pulse on the chip's `AS`, and the chip latches the register number from
   the byte bus as `AS` falls, the data still valid; `FF41` selects the chip,
   whose `RD` / `WR` are the card's read and write strobes. `-RESET` is the
   bus `-RES`; the chip's open-drain `-IRQ` drives its interrupt level
   (**IRQ 5** by default, D52).
3. **Software:** the kernel (`kernel/rtc.c`) keeps it in binary, 24-hour
   mode, UTC; reads it at boot (the system time is then the clock ticks on
   top); `stime()` (root) writes it. `date` prints the time and sets it,
   `date [cc]yymmddhhmm[.ss]` as in Unix V7. A card without the chip is
   detected (a RAM byte does not read back) and ignored.
4. **The emulators** model the registers (`emulator/p32io.h`): by default
   the host's clock; `-R SECONDS` starts it at a fixed time that follows
   emulated time, so the session tests stay repeatable.

## D24 — A native toolchain is the next milestone (2026-10-14, Ken)

Ken: "add the native toolchain as the next milestone." P32W Unix gets its
own assembler, linker, library tool (`ar`) and C compiler, written in C, so programs
can be written and built on the machine itself; the goal is self-hosting
(`cc` compiles itself on P32W Unix, identical to the host build). The plan
is M7 in [os-design.md](os-design.md): binary objects shared with the host
tools, a C version of `p32cc` with the same design and ABI (one test suite
for both), and register allocation for locals first.

## D25 — Moving files: Kermit, tar, gzip and zip (2026-10-14, Ken)

Ken: "We will need a kermit to get files onto the machine, and probably a
zip/unzip and a tar command." Done before M7 continues (Ken: "Now, then
M7"). All in C under `software/cmd`, all exchangeable with the Mac:

- **`tar`**: POSIX ustar, `c` / `t` / `x` with `v`, `f`, `z` (through
  gzip); reads macOS pax headers and GNU long names; keeps modes, owners
  (by name), times, hard links, devices.
- **Deflate** (RFC 1951) in a shared library, both directions — **`gzip` /
  `gunzip`** (RFC 1952; Ken: yes) and **`zip` / `unzip`** (PKZIP, CRC-32,
  stored and deflated entries, directories, DOS times), compatible with the
  Mac's zip / unzip and Finder.
- **`kermit`**: send, receive and **server mode**, with **long packets and
  sliding windows**; binary and text files. The Mac side is C-Kermit (Ken
  installs it); the automated tests use a small Python Kermit in `tools/`.
  Needs the terminal in raw mode and 1 KiB terminal queues.

Kernel support added for them: `utime` (V7 30) to restore modification
times, `times` (43) for CPU time. Checked by a test in `software/tests`:
host-made tar, gzip and zip files extracted and made again on P32W in the
emulator, then compared on the host (contents, modes, times, links).

## D26 — 16-bit CompactFlash transfers (2026-10-14, Ken)

Ken: "yes go to 16 bit cf". CompactFlash is driven 16 bits wide, not in
8-bit True-IDE mode: 8-bit transfers are optional in the CF standard, and
many newer and larger cards and most CF-to-SD / SSD adapters lack them;
16 bits is every card's default. It also halves the copy loop per sector.

- **The disk card** (D50): a halfword access to a port's data register (at
  `0xFFFF_EF00 + 16 p`, D52) is a 16-bit transfer on the drive's `DD0–DD15`.
  Every other task-file register is a byte.
- **Software:** the monitor's `cf_init` / `cf_read` / `cf_write` and the
  kernel driver move halfwords (buffers halfword-aligned); SET FEATURES 81h
  undoes an 8-bit mode left set by another host (a refusal is ignored). The
  kernel reads a 1 KiB block with one two-sector command.
- **Emulators:** the CF model answers halfword data accesses; byte
  accesses still model a card in either mode.

**D24 done (2026-10-14):** M7 complete. The native `as`, `ld`, `ar` and `cc`
write byte for byte what the host tools write, and `cc` compiles itself on
P32W Unix into a binary identical to the host-built one (os-design.md M7).

## D27 — BASIC's numbers: integers plus software floating point (2026-10-14, Ken)

Ken: "integers plus software float for BASIC" (over decimal fixed point).
P32W has no FPU, so the floating point is a C library, `softfp`
(`<softfp.h>`, `software/libc/softfp.c`):

- **IEEE 754 binary64** (C's `double`): 53-bit significand, about 15-16
  digits, exponents to 10^±308, with subnormals, infinities and NaNs.
  A wider format than the classic BASICs' 32/40-bit floats, chosen so a
  BASIC program's arithmetic agrees with any other machine's, and so the
  library could be tested bit for bit against the host's hardware.
- **Correctly rounded** `+ − × ÷` and square root (nearest, ties to
  even): identical to the host on 2 million random cases. Decimal
  conversion round-trips exactly at 17 digits. `exp`, `log` and `pow`
  work internally to 62 bits and are within about 1 ulp; `sin` `cos`
  `tan` `atan` within 3.
- **The interface takes pointers** (`fp_add(&r, &a, &b)`), so it needs no
  structures passed or returned by value. Only integer instructions are
  used; 64-bit quantities are word pairs, products are built from 16-bit
  pieces.
- `cc/tests/softfp.c` checks that P32W's build prints exactly what the
  host's does.

BASIC keeps whole numbers that fit in 32 bits as integers (exact and fast)
and switches to floating point when a result is not whole or overflows.
The C compiler's `double` (D30) is built on this library.

## D28 — ustat, and fsck -y on the mounted root (2026-10-14)

From the review of the administration commands (Ken: "lets address all in
order sh, kernel, commands, ...").

- **ustat (system call 74):** returns a mounted filesystem's blocks, free
  blocks, inodes and free inodes from the kernel's in-memory superblock,
  given the block device's number (st_rdev). df uses it, so any user gets
  current numbers without reading the root-only devices. mkfs and fsck use
  it to find out whether a device is mounted. The structure is P32W's own;
  V7's ustat returned free counts and a name.
- **fsck -y:** repairs, but refuses a mounted filesystem other than /. The
  root cannot be unmounted, and the kernel's buffer cache holds some of its
  blocks, which a repair makes stale. So after repairing / fsck restarts at
  once with `reboot(RB_REBOOT|RB_NOSYNC)`. The flag RB_NOSYNC (0x200)
  tells the kernel not to write the cache back or mark the filesystems
  clean. This is what V7's fsck did. Dropping the cache instead would need
  a kernel call to invalidate one device's buffers, and every open inode on
  it, so restarting is the simpler choice.

## D29 — The C compilers emit functions in definition order; cc frees as it goes (2026-10-14)

From the toolchain step (Ken: "lets address all in order sh, kernel,
commands, toolchain and library, documentation").

- **Order:** both compilers (p32cc and cc in C) write functions in the
  order they are *defined*, not in the order of first declaration
  (prototypes included). The output is identical between the two
  (cc_test), and self-hosting reproduces cc byte for byte. Only the layout
  of `-S` listings and objects is affected.
- **Why:** cc in C generates each function as soon as its body is parsed
  (a callback, `function_done`). It then frees that function's tree
  (nodes come from a pool released after each function) and its body's
  tokens. Closed block scopes and the preprocessor's temporary arrays are
  freed too, and each macro's output tokens are made once rather than
  copied two or three times.
- **Result on P32W:** peak memory falls by about half. `parse.c` goes from
  3.25 to 1.56 MB, and `sh.c` from out of memory to 1.78 MB. That is
  about 1 MB per 1000 lines, so files up to about 3000 lines fit in a
  process.
- **Next, if needed:** run the preprocessor and the parser together, a token
  at a time, so the whole token list never exists at once.
- **Related:** `volatile` locals are never kept in registers in either
  compiler, so longjmp cannot undo them.

## D30 — The C compiler upgrade: C99 pieces, bit-fields, structs by value, long long, double (2026-10-14)

Ken: "ok go ahead with the compiler upgrade" (the backlog's first step
towards Python). Both compilers (p32cc and cc in C) change together and
keep writing the same assembly.

1. **C99 pieces:**
   - designated initialisers (`[i] =`, `.m =`, chains like `.a.b[2] =`);
   - brace elision, following C's current-object walk;
   - compound literals: at file scope a static object, in a function a
     local one initialised where the literal is evaluated;
   - `__func__`.
   Declarations anywhere, `//`, `_Bool` and `inline` were already in.
2. **Bit-fields** follow the System V layout: from a unit's lowest bit,
   never across a unit the size of the field's type, and `:0` ends the
   unit. `op=` and `++` evaluate the place once.
3. **The calling convention for memory values.** A struct, a `long long`
   and a `double` are *memory values*: handled by their address, never
   in a register.
   - A function with any memory-value parameter takes **every** argument
     on the stack, as a variadic one does. Each memory value is copied
     into as many words as it takes. `va_arg` steps by the size rounded
     to words.
   - A memory-value result is written where a **hidden first argument**
     points (the caller's temporary), and that address comes back in r1.
   - Other functions are unchanged: arguments 1–4 in r1–r4.
4. **`long long`** is 64 bits, aligned to 4 (unlike most ABIs' 8: P32W's
   alignment never exceeds 4).
   - Its operations are calls of `libc/llong.c`, written on a struct of
     the same layout.
   - Constants are objects in the data, and constant expressions fold at
     compile time.
   - 32-bit literals keep their C89 types, so existing programs compile as
     before.
5. **`double`** is softfp (D27) behind helper calls (`libc/dbl.c`), so it
   is correctly rounded and agrees bit for bit with any IEEE machine.
   - **`float` is the same 8-byte type** (C allows this): one
     implementation, and only more precise than a 4-byte float.
   - Comparisons follow C's NaN rules.
   - Constants fold, except a division by zero or a NaN result, whose
     bits differ between machines.
   - **printf prints doubles only in programs that pass one.** The
     float formatting is about 56 KB, too much for every binary. printf
     calls it through a pointer, `__fp_hook`, which is null in
     `libc/fpdefault.c`. A compiler that sees a double passed to a
     variadic function emits a reference to `__printf_fp`, which pulls in
     `libc/fphook.c`, where the pointer is set. (The linker takes library
     members in name order, so the default must sort first.) `true` is
     about 50 KB.
6. **`#if` arithmetic is C99's** (added 2026-10-15): 64-bit intmax_t,
   uintmax_t when an operand is unsigned, in both compilers. Without
   it, `-1 < 0u` is true in `#if`, and Lua's limit checks go wrong.

**Cost:** a `long long` or `double` operation is a library call, so it
is tens of times slower than an int one. That is fine for programs that
only sometimes need 64-bit or real numbers (Lua, MicroPython, awk-like
tools). The 64-bit multiply could use the MULHU instruction later.

## D31 — switch: a jump table when the cases are dense (2026-10-15)

**Context:** both compilers turned every `switch` into a chain of compares,
two instructions per case (LI, BEQ). An interpreter's dispatch switch has
dozens of cases, so it paid for all of them on every instruction it ran.
Lua's main loop spent about 58% of its time there.

**Decision:** a switch with at least 4 cases, whose values span at most 3
times as many slots as there are cases, becomes a jump table. Other
switches keep the compare chain.

- **The dispatch** is a fixed 8 or 9 instructions, whatever the case:
  ```
  addi  d, v, -lo         (or LI + SUB when -lo does not fit 16 bits;
                           none when lo is 0)
  li    t, hi-lo+1
  bgeu  d, t, default     one unsigned compare: below lo wraps to big
  slli  d, d, 2
  la    t, table
  add   d, d, t
  lw    d, 0(d)
  jr    d
  table: .word case_lo, ..., case_hi   (default for each hole)
  ```
- **The table sits in .text** right after the JR, so it is read-only and
  shared, like the code. Each entry is an ordinary ABS32 relocation, so
  the assembler and linker need no change.
- **Both compilers apply the same rule** and write the same code (all
  197 C files identical). The switch value's register is never written,
  since the native cc may keep a variable in it.

**Effect on Lua** (software/lua/test/bench.lua, P32W CPU seconds, before
and after): fib(25) 240.5 to 161.9; an integer loop 37.2 to 15.9; the
whole benchmark 668 to 546. The float, string and sort parts barely
change: their time goes to softfp, malloc and the library, not dispatch.

**Not done (yet):** a binary search for large sparse switches, and
splitting a switch into dense clusters. No P32W program has needed either
so far.

## D32 — malloc: boundary tags and size bins (2026-10-15)

**Context:** libc's malloc was K&R's: one free list in address order,
first fit. Every `free` walked the list to find its place, so a program
with thousands of free blocks paid thousands of steps per free, and
`malloc` searched the same list. Lua had to bring its own allocator
(`lp32_alloc`) to work round it.

**Decision:** a small dlmalloc-style allocator (`libc/malloc.c`).

- **Boundary tags.** Each chunk starts with two words: the size of the
  chunk before (valid when that one is free) and its own size with two
  flags, "in use" and "the one before in use". A free chunk's size is
  also written at its end. So `free` finds both neighbours and merges
  with them in O(1).
- **Size bins.** Free chunks wait in doubly linked bins: one per size up
  to 512 bytes (8-byte steps, so a request takes the first chunk with no
  search), then one per power of two. A bitmap finds the next bin with
  chunks. Requests no bin can meet are carved from `top`, the free end of
  the heap, which `sbrk` extends.
- **`realloc` grows in place** into a free neighbour or into `top`, and
  gives back the tail when shrinking.
- **The break moved by someone else** (a program calling `sbrk` itself)
  starts a separate region; the earlier one ends in an in-use "fence"
  header so nothing merges across the gap.
- Overhead stays 8 bytes per block, blocks stay 8-byte aligned, and the
  interface is unchanged.

**Effect:** 100,000 random mallocs and frees over 10,000 slots: 288 million
P32W instructions before, 24 million after. Lua uses the standard
allocator (realloc and free) and is faster with it than with its own:
its benchmark 552 s to 494 s, a full GC of 10,000 tables 53.9 s to 19.3 s.
`cc/tests/malloc.c` checks it under load (random sizes, fills and checks
every block, realloc both ways, calloc, the break moved behind its back).

## D33 — The native cc's memory: freed expansions, shared hidesets, small tokens freed as parsed (2026-10-15)

**Context:** the native cc ran out of memory on five of Lua's files, so
Lua could not be rebuilt on P32W. D29 had assumed the cost was the token
list; measuring showed most of it was waste.

- **Macro expansion threw tokens away without freeing them:** the macro's
  name, a call's parentheses and commas, its arguments (only copies are
  used), `##`'s operands, directive lines and skipped `#if 0` text. For
  `lvm.c` (53,000 tokens out) there were 191,000 tokens alive.
- **Every expanded token had its own copy of its hideset:** 287,000 list
  cells.
- **One function can be huge.** `luaV_execute` is 35,500 tokens and 16,000
  tree nodes, and its tokens could not be freed while its tree pointed at
  them.

**Decision** (cc in C only; p32cc on the host is unchanged, and the two
still write the same code for all 223 C files):

1. **Expansion owns its tokens:** each one goes to the output or is
   freed. `#define`, `#if` and computed `#include` work on copies, and
   `process` frees every directive line.
2. **Hidesets are shared:** a (name, rest-of-list) cell is made once and
   found again in a table. When preprocessing ends, the macros and the
   hidesets are freed.
3. **Tokens are 28 bytes** (the flags are bit-fields, and a character's
   value shares space with a string's bytes), from 4 KB slabs without a
   malloc header each. Each slab counts its live tokens, and a slab with
   none goes back to the heap.
4. **Tree nodes keep a position, not a token:** one shared token per
   source line, which is all error messages and `-S` listings use. So
   the parser frees each statement's tokens as soon as it is parsed
   (string literals stay for the data). Node blocks are 4 KB too, so they
   reuse the slabs that come back.
5. **The generated lines** waiting for a function's prologue are packed
   into 4 KB chunks rather than one allocation each.

**Result on P32W:** all of Lua compiles on P32W, and the output is the
same as the host's. `lvm.c` takes 2.99 MB of heap, and `lstrlib.c` 1.23 MB
(2.4 MB before). The limit is about 3.1 MB, so `lvm.c` is close to it. If
a bigger single function appears, the next step is a smaller tree node
(60 bytes, of which `struct vec list` is 12) or streaming the preprocessor
into the parser (D29).

## D34 — An 8 MiB address space per process (2026-10-15)

From Ken, with the per-process window sized to the RAM a big program
needs.

- **8 MiB per process.** `USERTOP` is `0x0080_0000`. A page table is 2,048
  entries in **two physically contiguous frames**, so the TLB-miss fast
  path is one indexed load. `pt_new` finds two adjacent free frames from
  the frames' reference counts, a scan that happens only at fork and exec.
  If memory is too fragmented for that, fork fails with ENOMEM. A
  two-level table would avoid the contiguity at the cost of one more load
  per refill.
- **The RAM itself is set by D52:** 16 MiB memory cards, up to four (64
  MiB). One card gives 15 MiB above the boot ROM. The kernel's frame
  table, `sysinfo` and `/dev/mem` follow the RAM it finds at boot.
- `db`'s backtrace bound, the man pages and the docs say 8 MiB.

**Why 8 MiB:** big programs (the native cc on big files, Lua, MicroPython)
are the ones that need room, and 8 MiB covers them. One process can take a
large share of a one-card machine's RAM; swapping (backlog) is the answer
to that, not a smaller window.

## D35 — C and libc pieces for MicroPython (2026-10-15)

Porting MicroPython 1.29 ("Python for P32W", stage 2) found gaps in both C
compilers and in libc. Each is fixed in both compilers, which still write
the same code for all 228 C files, and each has a test in cc/tests.

**Compilers.**
- **`_Bool` is C99's.** As a plain `unsigned char`, `bool b = 0x100;`
  would store 0, the low byte. Any value converted to it, by assignment,
  initialiser, argument, return or cast, is 1 if it is not zero (or not a
  null pointer), else 0. *(bool.c)*
- **`offsetof` is an integer constant.** `(size_t)&((T *)0)->m` folds to
  a number when it is cast. So it works in array sizes, `case` labels,
  enum values and static initialisers, including the static assertions
  MicroPython builds from it. *(decl99.c)*
- **`restrict` is accepted, and ignored.** *(decl99.c)*
- **A parameter or local may be named like a typedef** (`qstr qstr`):
  after the specifiers, an identifier is the declarator's name.
  *(decl99.c)*
- **A flexible array member given elements in a static initialiser** (a
  GCC extension) makes the object run on past `sizeof`. Dropping its
  elements silently left MicroPython's qstr pools reading garbage.
  *(flexarray.c)*
- **`volatile` after a `*` (`T *volatile p`) makes `p` volatile.** So do
  `volatile` parameters, and each declarator in a list gets its own
  volatility. Volatile variables are never kept in registers, so
  `longjmp` cannot undo a change to them. MicroPython's VM keeps its
  exception-handler pointer like that; without this, `finally` blocks
  are skipped. *(volatile.c)*
- **Bit-fields wider than 32 bits** (a GCC extension on `uint64_t`) are
  a clear error, rather than laid out and given bad code.
- **Hexadecimal floating constants** (`0x1.8p3`, `0x1p-1022`, with an `f`
  or `l` suffix): the `p` exponent is required, so `0x1f` is still 31.
  p32cc reads them with `float.fromhex`; the native cc with `strtod`, and
  P32W libc's `strtod` already reads them exactly. *(c99lit.c)*
- **Designators reach into anonymous structs and unions**: `.lo = 1`
  where `lo` belongs to an unnamed member names the path to it, and
  initialisation carries on from there. *(c99lit.c)*
- **`__builtin_nan("")` and `__builtin_inf()`** are double constants, so
  `NAN` and `INFINITY` (defined with them) work in static initialisers.
  *(c99lit.c)*

**libc.**
- **`<stdint.h>`, `<stdbool.h>`; `SSIZE_MAX` in `<limits.h>`; `nan()`**
  (with `nanf` and `nanl`).
- **More `errno` names, with Linux's numbers:** `EWOULDBLOCK` (=
  `EAGAIN`), `EOPNOTSUPP` 95, and networking's `EADDRINUSE` 98,
  `ECONNABORTED` 103, `ECONNRESET` 104, `ENOBUFS` 105, `ENOTCONN` 107,
  `ETIMEDOUT` 110, `ECONNREFUSED` 111, `EHOSTUNREACH` 113, `EALREADY` 114,
  `EINPROGRESS` 115, with `strerror` messages. MicroPython's `errno`
  module lists them, and the network stack (D45) returns them.
- **`pow` is correctly rounded for whole exponents up to ±2047.** Its
  double-double path is renormalised after each product, so it does not
  overflow past ±64. A denormal result is rounded once, ties to even.
  Without this, about 30% of large powers were 1-3 ulps off, and
  MicroPython's float literals such as `1e301` parsed wrong. Checked
  against exact arithmetic: 0 wrong of 15,127 (every exponent from -1080
  to 1080, seven bases, 138 denormal results).

**Emulator.** `-w` also knows Python's prompts `>>> ` and `... `, so
scripted sessions can drive the REPL.

## D36 — Python for P32W: MicroPython 1.29 as /usr/bin/python (2026-10-15)

From Ken: "go ahead with the python port" (stage 2 of the backlog's
"Python for P32W"; D35 was the C it needed).

**What.** MicroPython 1.29.0 (the release's source distribution, without its
submodules: `software/micropython/README.P32W`) is `/usr/bin/python`, also
named `micropython`, with python(1). It runs a file, `-c` code or the
standard input, or gives a REPL at a terminal (line editing, history, Tab
completion, auto-indent). `sys.path` is the current directory, then
`/usr/lib/micropython`; `sys.argv` is as CPython's (`['-c', args...]`
for `-c`).

**Configuration** (the port's `mpconfigport.h`). MicroPython's "extra
features" level, as its Unix port, with arbitrary-size integers (mpz)
and IEEE double floats. Left out: `select` and `asyncio` (P32W has no
`poll`), `framebuf`, and `cmath` (a candidate to turn on; D39 does).
Added: `re`'s `match.groups/start/end/span`, and `os.getenv`, `putenv`,
`unsetenv` and `system` (the port's `modos.c`, as the Unix port has them).
Python objects live in a fixed 2 MiB heap; Python frames on a 64 KiB stack
of their own; C may use 448 KiB of the process's 512 KiB stack, which
stops a recursion about 700 calls deep with a `RuntimeError` (D39 and D41
deepen both).

**Built on the host,** by `software/Makefile` running
the port's `build.sh`: MicroPython's own scripts make the generated
tables (the sources preprocessed by the host's clang with only P32W's
headers), then p32cc compiles all 134 files (about a minute). The same
sources give the same 1.7 MB binary. Built on P32W with the native cc:
D39.

**Changes to imported files** are few and marked "P32W:" (README.P32W
lists them): the double's 64-bit bit-fields taken with shifts and masks
(D35), and 4-byte floats. P32W's C `float` is the 8-byte double (D30), so
`struct`/`array` `'f'`, half-precision `'e'` and `uctypes.FLOAT32` would
be 8 bytes, or read back as 0. They go through two libc functions in
`<math.h>`, `__f32_bits(double)` (rounded to nearest, ties to even) and
`__f32_value(bits)` (exact): the conversions Lua's `string.pack("f")`
already uses.

**The reference.** `build.sh` with `HOST=1` builds the same MicroPython,
same configuration, for the Mac. Its output is what P32W's must match,
since MicroPython's own expected results are CPython's. It is built with
`-ffp-contract=off`: clang otherwise fuses `a*b + c` into one rounding,
which changes `math.gamma(5.5)` in the last digit, while P32W rounds each
step as C says. Where P32W and the Mac still differ, P32W's libm is the
cause: `atan2(2, 1)` is 0.58 ulp off where the Mac's is correctly
rounded, so complex powers can differ in the last digit (D38 settles it).

**Tested.**
- MicroPython's own `tests/basics` and `tests/float`, 645 programs, run
  on P32W and on the reference: all 645 outputs identical (610 run to the
  end; 23 skip themselves for features left out; 12 stop the same way on
  both, needing t-strings or `unittest`). This found the 4-byte float
  bug, and a shell bug: a `for` list over more than 512 names was cut
  short without a word. `sh` says "too many names" (status 2) for that
  and for wildcards past its 16 KiB of names (`/usr/tests/sh/glob.sh`).
- `/usr/tests/cmd/python.sh` on P32W: the language, the library, files
  and `os`, and the command's options and exit statuses. Its three
  scripts' output is the reference's.
- `session15` in `make test`: the REPL at a terminal (blocks, an error,
  Tab completion, ^D).

**Costs.** Starting `python` took about 12 s of P32W time, nearly all of
it the kernel reading the 1.7 MB program (D37 brings it to 3.1 s). A
Python function call then takes about 2 ms, and a loop step 0.35 ms.

## D37 — Faster program loading, and heap pages made on first use (2026-10-15)

From Ken: "go ahead with 1, 2 and 3", 1 being the backlog's "starting
`python` takes 12 s". A profiler showed where the time went:
`p32emu -k FILE` writes how many instructions ran at each kernel address
(and in user mode), and `tools/kprof.py FILE` totals them by function.
For three starts of `python -c pass`, 50 million kernel instructions:
`wakeup` 23%, the CF loop 21%, `memset` 15%, `memcpy` 12%, the buffer
cache's lookup 8%. Little of it was the disk itself.

**What changed** (kernel; 12 s → 3.1 s for `python`, about 4 → 1.9 s
for `lua`; all tests unchanged):
- **`exec` reads the program straight into its pages.** `readi_pages`
  (fs.c) loads a file's bytes into a page table's pages. Runs of up to 64
  consecutive blocks that are not in the buffer cache go through an
  optional block-device entry, `readv` (`cf_readv`). It reads them in
  multi-sector commands, past the cache. Its scatter list of pieces copes
  with the a.out header's 32 bytes, which put every block across a page
  boundary. A block that is in the cache comes from there, since it may
  be newer than the disk (a program just compiled). The snapshot of the
  cached blocks holds because the file is locked. The leaf indirect
  block is kept while its 256 entries are walked, not read again for
  each block.
- **The CF transfer loop** is a small function of its own (`cf_in`,
  `cf_out`), so its two variables live in registers: 19 instructions
  per 16 bytes, against 22 per 8 with them in memory. Every disk read and
  write gains.
- **Pages the program fills whole are not zeroed first** (`map_page_raw`).
  A short file fails the `exec` before it can run, so no stale data shows.
- **`brelse` wakes waiters for a free buffer only when there are any**
  (rather than scanning all 64 process slots for every block).
- **Heap pages are made on first use.** `sbrk` promises pages without
  mapping them; a fault, or `copyin`/`copyout`, maps a zeroed frame then.
  Each promise holds a frame in reserve (`nreserved`), so `sbrk` still
  fails when memory is short, and a page it granted is always there
  later: no overcommit. `fork` reserves the child's untouched pages too;
  exit, exec and shrinking give back what was never touched. `ptrace`
  reads an untouched page as zeros. MicroPython's 2 MiB heap costs only
  the pages it uses.

What is left is mostly the CF loop itself: 1.7 MB through a 16-bit data
register, about 1.2 instructions a byte. Only DMA would remove it
(backlog, "Faster disk transfers").

## D38 — The maths library in 62 bits: correctly rounded 99.5% of the time (2026-10-15)

From Ken's "go ahead with 1, 2 and 3", 3 including "make `atan2`
correctly rounded". Measured first: against the Mac's libm, P32W's `sin`
and `cos` differed in about 22% of results, `atan2` 37%, `asin` 63%
(16% by more than an ulp). `exp` and `log` agreed 99%, because they were
already worked out in softfp's unpacked form, which carries 62 bits, and
rounded once. The others were plain double arithmetic, rounding at every
step.

**What changed** (`libc/softfp.c`, `libc/fpmath.c`). The rest of the
library works as `exp` and `log` do: in 62 bits, rounded once.
- **`sin`, `cos`, `tan`:** the argument is reduced by k·π/2 with π/2 in
  four parts (three of 33 bits, so k times each is exact), then a series
  to 13 terms. Arguments past 2^19 are reduced exactly first
  (Payne-Hanek).
- **`atan`:** past 1, use 1/t; then atan t = atan(j/8) + atan((t − j/8)/(1
  + t·j/8)), the nine atan(j/8) held to 106 bits as pairs of doubles; the
  series on the rest (< 1/16). **`atan2`, `asin`, `acos`** build on it,
  with sqrt((1 − a)(1 + a)) from exact factors.
- **`expm1`, `log1p`:** their own series for small arguments, so there
  is no cancellation. **`sinh`, `cosh`, `tanh`, `log2`, `log10`, `cbrt`**
  (one Newton step) follow, and **`asinh`, `acosh`, `atanh`** are added.
- **`exp`'s reduction** uses ln 2 in two parts (a single 62-bit ln 2
  loses bits for large k).

**Checked against exact values** (60-digit decimal arithmetic), 1,000
to 2,000 random arguments per function, on P32W and on the Mac. P32W's
result is the correctly rounded one for 99.5–100% of arguments in every
function above, and never more than 0.51 ulp off. The Mac's libm, for
comparison: `tan` 58%, `acos` 84–87%, `atan` 92%, `cbrt` 91–93%,
`asinh`/`acosh`/`atanh` 66–88%, with errors up to 2 ulp. So P32W and the
Mac still differ in the last digit now and then, and more often it is the
Mac that is off.

**Not changed here:** `pow` (correctly rounded for whole powers up to 2047,
D35; otherwise about 93%, within an ulp: y·log x needs more than 62 bits;
D42 settles it). `erf`, `erfc`, `tgamma` and `lgamma` are musl's (D39),
within a few ulp.

**Speed** (instructions per call on P32W, before → after): `sin` 29,500 →
29,300, `cos` 31,100 → 28,400, `atan` 35,300 → 17,700, `asin` 76,800 →
59,000, `sinh` 64,900 → 47,700, `expm1` 60,800 → 45,900, `exp` 37,700 →
38,700. No function got slower by more than 3%.

## D39 — Python: deeper recursion, cmath, and musl's functions in libc (2026-10-15)

From Ken's "go ahead with 1, 2 and 3" (2: deeper recursion; 3: the rest
of the Python follow-ons).
- **MicroPython is built stackless** (`MICROPY_STACKLESS`): a Python
  function calling another does not recurse in C, so recursion depth
  is set by the Python stack, 512 KiB from `malloc`. With D37 that
  costs only the pages used. Plain recursion goes from about 700 calls
  deep to about 10,000. A recursion that passes through C (a class's
  `__init__` making another instance) still uses C's stack, about 450
  deep (D41 raises the stack limit).
- **`cmath` is on.**
- **`erf`, `erfc`, `tgamma`, `lgamma` are in libc** (`libc/erf.c`,
  `libc/gamma.c`, musl 1.1.16's code under its MIT licence; sinpi uses
  libm's `sin` and `cos`). `asinh`, `acosh` and `atanh` are libc's own
  (D38). MicroPython's copy, `lib/libm_dbl`, is not used, and C programs
  get the functions too.
- **Built on P32W itself.** `/usr/src/micropython` holds the sources, the
  tables `build.sh` made in `genhdr/` (MicroPython makes them with Python
  scripts, which P32W cannot run), and the host's objects
  (`tools/mkmpsrc.py`); its Makefile is the port's `Makefile`. With no
  objects, `make` there compiles all 126 files with the native cc in
  about 20 hours of P32W time (67 minutes in the emulator), and links a
  `python` byte for byte the host's. Before that run, the two compilers
  were compared on all 126 files: the same assembly. Three things had to
  change on the way:
  - **`ld` ran out of memory** linking 1.7 MB from 126 objects, keeping
    every input file whole until the end and building the output in a
    second buffer. An object file's text and data are read again when
    they are copied into the output, and the output is written straight
    to the file. The peak on the host falls from 10.4 to 6.5 MB, and the
    output is the same, byte for byte.
  - **`-DNDEBUG`**, as MicroPython's own release builds: no assertions.
    `python` is 13% smaller (1.79 → 1.55 MB).
  - **Relative `-I` paths in `build.sh`,** so `__FILE__` puts none of the
    host's paths into the binary.

  The disk then held 26,262 of its 32,768 blocks (80%; D40).

## D40 — A 512 MB disk; boot checks it only after a crash (2026-10-16)

From Ken: "Raise to 512MB" (MicroPython's sources had filled a 32 MB disk
image to 80%).
- **The disk image is 512 MB** (`software/Makefile`'s `DISK_MB`, and the
  test sessions). The design allows far more: 32-bit block numbers, and
  LBA28 addressing in the CF driver (128 GiB). Any real CF card holds it.
- **16,384 inodes** (`DISK_INODES`, `p32fs build --inodes`): one per
  32 KB, more than ten times today's 1,400 files. The default, one per 8
  blocks, would give 65,536, a 4 MB inode table.
- **Boot checks the filesystem only after a crash.** A full `fsck` every
  boot at 512 MB takes boot plus login from 17 to 68 million instructions.
  So `/etc/rc` runs `fsck -q -c`: `-c` checks only a filesystem that was
  not shut down cleanly. The kernel writes the superblock's clean flag
  down at mount, so `ustat` (D28) has `f_clean`: the flag as it was when
  mounted. A clean boot takes 6.4 million instructions on 512 MB; after a
  crash, with the full check, 44.5 million (68 with 65,536 inodes).

## D41 — A process's stack may grow to 1 MiB (2026-10-16)

From Ken: "make it 1 MiB", after weighing the trade-offs (512 KiB before).
- **What it costs:** nothing for a program that does not go deep, since
  stack pages are made only as the stack grows, and the limit reserves no
  address space (the heap can still grow to within a page of the stack's
  actual bottom). A runaway recursion holds up to 1 MiB of RAM before
  it is stopped: one fifteenth of a one-card machine's RAM (D52).
  Stack growth is not reserved as heap pages are (D37), and the stack does
  not shrink once grown.
- **What it gains:** Python recursion through C (a class's `__init__`
  making another instance) about 950 deep against 450; awk's recursive
  functions about 2,500 deep against 1,000; and deeper recursion in cc
  and Lua's C code.
- **Changed:** `MAXSTACK` (kernel/param.h); MicroPython's `CSTACK`
  (960 KiB) and awk's own limit (1,008 KiB), each kept just short of the
  kernel's; sbrk(2), malloc(3), a.out(5), awk(1), python(1) and the
  kernel README.

## D42 — pow in 96 bits: correctly rounded (2026-10-16)

From Ken: "ok improve pow". `pow(x, y)` is e^(y log x). In 62 bits, a
relative error ε in log x becomes an absolute error |y log x|·ε in the
exponent, up to 745ε. Measured against exact values over 3,000 arguments
weighted toward large exponents, only 75% of results were correctly
rounded, and the worst was 3.4 ulp off. The "93%" of D38 came from
milder arguments.

**What changed** (`libc/softfp.c`):
- A three-word unpacked number, `w3`: 96 bits, with add, multiply and
  divide.
- **log x in 96 bits:** x = 2^e·f; with c = 1 + j/16 the nearest
  sixteenth, log x = e·ln 2 + log c + 2·atanh((f − c)/(f + c)). The
  remaining argument is below 1/64, so eight series terms are enough. The
  17 values log(1 + j/16) and ln 2 are held to about 159 bits, as three
  doubles each.
- **y·log x and the reduction by k·ln 2** are also done in 96 bits. Only
  e^r, |r| ≤ 0.35, is left to the 62-bit `exp`.
- Whole powers up to 2047 still go by squaring (D35).

**Result:** correctly rounded on all 3,000 arguments, never more than
0.50 ulp off. The Mac's libm got 99.9% of the same. `pow` costs 77,000
instructions against 62,000 (24% more); the table of sixteenths keeps
that down from the 106,000 of a plain series.

## D43 — USB storage: two CH376S on the I/O card, FAT by tools (2026-10-16)

From Ken, after the design study (`docs/usb-design.md`): "CH376S plan A,
FAT tools only".
- **Two CH376S on the I/O card** (D50), each a USB port: the first at
  `FF50` (data) and `FF51` (command and status), the second at `FF54` /
  `FF55`, both on interrupt level 6 (D52).
- **The CH376S** (WCH; datasheet CH376DS1) in parallel mode, as a module
  with 2.54 mm headers and a USB-A socket, used in its sector mode. It
  connects directly to the 5 V bus, needs little glue, and has working
  drivers to learn from (Fuzix). **Plan B:** a Raspberry Pi Pico running
  TinyUSB's mass storage host behind a mailbox interface, if too many
  drives fail with the CH376S. The driver keeps the controller behind one
  small layer, so a change touches only that layer.
- **FAT through user-space tools only:** `dosdir`, `doscp`, `dosrm`,
  `dosformat` on the raw device. A stick can also carry a P32W filesystem
  (`mkfs /dev/usb`, `mount`). No FAT in the kernel.
- **Order of work:** an emulator model of the CH376S (both emulators), the
  block driver and its tests, the FAT tools, then the hardware.
- **The module** (schematic from Ken) has every parallel signal on a 16-pin
  header, plus +5 V and GND; RSTI is not there (it has its own power-on
  reset), so the driver always starts with `RESET_ALL`. Its CH376 runs at
  3.3 V from an LM1117; **Ken: run it at 5 V** (the regulator bridged, or
  a 5 V jumper), as in the datasheet's circuit: no level shifting.

**The driver** (2026-10-16): `kernel/usb.c`.
- Block device 2 (`/dev/usb`), for `mkfs`, `mount` and `fsck`; character
  device 5 (`/dev/rusb`, mode 666 as a floppy's would be), raw sectors for
  the FAT tools. `ioctl USBIOSIZE` gives the size (`<sys/usb.h>`). usb(4)
  starts manual section 4 (devices); `man` and the index know it.
- **Polled,** with a time limit on every wait (2 s, or 5 s for
  `DISK_INIT`), counted in clock ticks. A drive is found at first open.
  Pulling it out drops its blocks from the cache, and the access fails
  with EIO.
- **Read errors reach the filesystem,** for every disk. A failed read
  filled with zeros and cached as valid would make a pulled-out drive's
  free-block bitmap look empty, and `cp` would carry on "writing". So
  `bread` leaves a failed block uncached with `B_ERROR` set, and
  `balloc`, `readi` and `writei` return EIO. The "disk error" message is
  printed once per device until it works again.
- **Tested:** session 17 (a blank 4 MB stick: `mkfs`, `mount`, copy,
  `fsck`, read back, `/dev/rusb`); session 18 (pulled out mid-copy: `cp`
  gets a write error, the system carries on, the root stays clean).

## D44 — The FAT tools: one program, sectors by number (2026-10-17)

From Ken: "go ahead with the FAT tools" (D43's third step).
- **One program, `software/cmd/dos.c`, under five names:** `dosdir`,
  `doscp`, `dosrm`, `dosmkdir`, `dosformat` (links to `/bin/dos`, as
  `gunzip` is to `gzip`). `dosmkdir` is beyond D43's four: making a
  directory is half of `doscp -r` already. mtools-style: drive paths start
  with `::` for `doscp`; names match without regard to case. dos(1).
- **FAT12, FAT16 and FAT32, with long names,** read and written; the volume
  in an MBR partition (the usual stick) or from sector 0 ("superfloppy").
  Long names are UTF-8 on P32W, UCS-2 on the drive. A name that fits
  8.3 in one case gets only a short entry (with the lower-case flags that
  Windows and the Mac honour); others get long-name entries and a
  `BASIS~N` short name. Times are local time, by TZ. The FAT is read
  through an 8-sector cache written back to both copies at the end; the
  FAT32 FSInfo free count is kept right.
- **`doscp` is careful with space:** a file's clusters are allocated and
  written first, and its directory entry made last, so a drive that fills
  up mid-copy loses nothing (the clusters go back). A long name over P32W's
  28 characters comes off the drive under its short name, with a message.
- **`dosformat`** writes an MBR with one partition from 1 MiB, then FAT32
  for 512 MB and more (Windows' cluster sizes) or FAT16 below (the
  smallest cluster that fits; 3 MB at least). `-F` overrides, `-v` labels.
  No questions asked: P32W's `mkfs` asks none either.
- **Two kernel ioctls on `/dev/rusb`:** `USBIOREAD` and `USBIOWRITE` move
  1 to 128 sectors by number (`struct usbio` in `<sys/usb.h>`), because a
  file offset stops at 2 GB and sticks are bigger. Four sectors go per
  CH376 command, staged in the driver's 2 KiB buffer, so a bad user
  address is found before the drive is touched. `USBIOWRITE` needs the
  descriptor open for writing. `lseek` works on `/dev/rusb`, as on
  the CF card's raw device.
- **`-i image`** runs the tools on an image file, so they also build and
  run on the Mac (`cc dos.c`), which is how they were first tested.
- **Tested against the Mac's own FAT code** (`software/tests/fat_test.sh`,
  in `make test`): a stick image the Mac formats and fills (`hdiutil`) is
  read and written by P32W in the emulator, then checked by `fsck_msdos`
  and a read-only mount; likewise sticks `dosformat` made (600 MB FAT32,
  40 MB FAT16) and one that fills up. Session 19 (a blank 40 MB stick:
  both formats, copies both ways, the error messages) is recorded, and is
  the same on p32uemu.

## D45 — Networking: Ethernet and Wi-Fi through a Pico 2 W and a W5500, before SLIP (2026-10-17)

From Ken, after the design study (`docs/net-design.md`): "B: Pico
coprocessor", and "Ethernet first".
- **A Raspberry Pi Pico 2 W with a W5500** on the I/O card (D50), beside
  the CH376S ports, behind a **mailbox**: latches, with flags, on
  interrupt level 7 (D52). The Pico's 3.3 V side is buffered to the 5 V
  card. Its registers follow the first CH376S's (`FF52` on).
- **The mailbox speaks network operations,** not W5500 registers: socket,
  connect, listen, accept, send, recv, close, DNS, status. The
  coprocessor does DHCP, DNS, ARP timing and keepalives, and **TLS**
  (mbedTLS), so P32W can reach `https://` itself. The deciding point: a
  TLS handshake on P32W (about 0.5 MIPS) would take minutes.
- **P32W keeps everything above the socket:** sockets as file
  descriptors (`FD_SOCKET`), BSD-style system calls, and the programs.
  A network-interface layer underneath leaves room for SLIP.
- **Ethernet before SLIP.** SLIP, with P32W's own TCP/IP stack, stays in
  the backlog as a later project, for learning; the W5500's MAC-raw mode
  could carry the same stack.
- **Order of work:** the mailbox model in both emulators, mapped onto
  the Mac's sockets (and the Mac's TLS); sockets in the kernel and the
  driver; the programs; the firmware; then the hardware.
- **Single latches, not FIFOs,** in the mailbox: the Pico answers within
  a microsecond, under one P32W instruction, so a FIFO would buy nothing
  the driver could use.

**The protocol and the emulator model** (2026-10-17; Ken: "go ahead with
the emulator model"):
- **`docs/net-mailbox.md`** is the reference for the model, the driver
  and the firmware.
  - Registers: `NDATA` at `FF52`, `NSTAT`/`NCTRL` at `FF53`, with the bits
    `RXF`, `TXE`, `ATTN`, `IEN` and `READY`, and `RESET`.
  - Messages: requests and replies with the same 4-byte header and up to
    1024 bytes of payload; 17 commands; 8 sockets of 4 types (TCP, UDP,
    TLS, DNS); socket states; 15 status codes.
  - `CONNECT` only starts a connection; `ATTN` and `EVENTS` say what
    changed, so one slow handshake never holds the mailbox.
- **The model** (`emulator/p32io.h`, `-N`, both emulators) carries the
  commands out with the Mac's sockets:
  - 10.0.2.2 is the Mac (QEMU's numbers);
  - listening ports below 1024 go up by 8000;
  - DNS is the Mac's resolver;
  - TLS is `tools/p32tls.py`, Python's `ssl` with the host-name check.
    LibreSSL's `s_client` was tried first; it cannot check the host name.
  - An idle P32W waits for the sockets in real time, so emulated time
    keeps pace with the network.
- **Tested** (`emulator/test/net_model.c` with `net_peer.py`'s servers,
  in `make test`, the same on both emulators): TCP there and back with
  shutdown and the end of the stream; a refused connection; DNS, and a
  name that does not exist; UDP; listen and accept, with `ATTN` and
  `EVENTS`; TLS with a test certificate, and its refusal for the wrong
  name; all 8 sockets in use; bad requests; `RESET`; no coprocessor
  without `-N`.

**Sockets in the kernel** (2026-10-22; Ken: "go ahead with the kernel
sockets and driver"): `kernel/net.c`.
- **A socket is a file** (`FD_SOCKET`): `read`, `write`, `close`, `dup`,
  `fork` and `select` work on it, and `fstat` says `S_IFSOCK`. Each is
  one of the coprocessor's 8 sockets; `socks[]` is indexed by its number.
- **BSD's calls at 4.2BSD's numbers:**
  - `socket`, `connect`, `bind`, `listen`, `accept`;
  - `sendto`/`recvfrom` (`send`/`recv` in libc), `shutdown`;
  - `setsockopt`, `getsockopt`, `getsockname`, `getpeername`;
  - `select`, and `fcntl` (`O_NONBLOCK`).
  A system call carries four words, so `sendto` and `recvfrom` pass an
  argument block, the options pass level and name in one word, and
  `select` drops `nfds` (an `fd_set` is one word: `NOFILE` is 20).
  `netctl` (160) is P32W's own: interface information and settings, and
  host names.
- **TLS** is `socket(AF_INET, SOCK_STREAM, IPPROTO_TLS)` (253, RFC 3692's
  experimental number), with the server's name given by `setsockopt`
  (`TLS_HOSTNAME`) before `connect`.
- **The mailbox** is used only in process context, an exchange at a time
  without sleeping (the kernel is not pre-empted), so processes never
  interleave on it. **Waiting:** a process checks its socket. If it
  cannot go on, it sleeps until `ATTN`, unless `ATTN` is already up. The
  interrupt handler masks `IEN` (the line stays up until `EVENTS`) and
  wakes every sleeper; each sends `EVENTS` and looks again. A wake every
  200 ms covers a missed event; signals end a wait with `EINTR`.
- **select** covers terminals, pipes and sockets. Its sleepers are woken
  by terminal input, by the network interrupt, and by every clock tick
  (timeouts, and pipes).
- **The protocol gained one detail:** `STATE`'s "bytes ready" counts the
  connections waiting on a listening socket.
- **libc:** the wrappers; `htons` and the rest; `inet_aton`, `inet_addr`
  and `inet_ntoa`; `gethostbyname` through `netctl`; `strerror` for the
  network errors. **Manual:** socket(2), send(2), getsockopt(2),
  select(2), fcntl(2), netctl(2), inet(3).
- **Tested:** `software/tests/nettest.c`, as ken under the kernel, with
  `net_test.py`'s servers on the Mac (`make test`; the same on both
  emulators). It covers TCP there and back, 4000 bytes, shutdown and the
  end; a refusal; a non-blocking connect with `select` and `SO_ERROR`;
  a blocking read ended by `SIGALRM`; DNS; UDP; `select` timing out; a
  server accepting the Mac's client; TLS, and the wrong name refused; all
  8 sockets in use; and the errors.

**The first programs** (2026-10-23; Ken: "go ahead with ifconfig, ping,
telnet and fetch"):
- **`ifconfig`** shows the interface, and for root sets a fixed address
  or DHCP (`netctl`). Manual page in section 8, as BSD's is.
- **`ping`** builds ICMP echo requests itself (checksum, id, sequence)
  and sends them through a socket kind of its own,
  `socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)`. That is socket type 5 in
  the protocol, the W5500's IP-raw mode. The model uses the Mac's
  unprivileged ICMP socket and strips the IP header the Mac returns.
  Times are in 20 ms clock ticks.
- **`telnet`** is RFC 854: line mode until the server offers to echo,
  then raw. It answers TERMINAL-TYPE (VT100), NAWS (80 x 24) and SGA;
  `^]` closes.
- **`fetch`** does HTTP/1.1 with `Connection: close`, redirections (5),
  chunked replies, `-o`, `-O` and `-v`. Its HTTPS is the coprocessor's
  TLS socket: fetch speaks only HTTP.
- **Tested** (`software/tests/netcmd_test.py`, in `make test`, the same
  on both emulators): every case against servers on the Mac (HTTP,
  HTTPS with the test certificate, a telnet server that negotiates).
  By hand, P32W in the emulator fetched `https://example.com/` (the
  certificate checked against the Mac's store) and pinged it.

## D46 — Remote logins, FTP and the network clock (2026-10-23)

From Ken: "go ahead with telnetd, ftp and NTP".
- **Pseudo-terminals in the kernel** (`kernel/tty.c`), four pairs. The
  slaves, `/dev/ttyp0-3`, are tty minors 5-8 (D53): the same line
  discipline, signals and modes as tty0, so `getty`, `login`, `sh` and `vi`
  run on them unchanged. The masters, `/dev/ptyp0-3`, are character device
  6: writing one feeds the slave's input path (as a UART's interrupt does),
  reading it takes the slave's output queue (which no UART empties).
  - Closing a master hangs the slave up: SIGHUP to its processes, and
    their reads end.
  - A master's read ends once the slave has been opened and then closed
    by everyone.
  - `select` covers both ends.
  - The four pairs cost about 48 KiB of kernel memory (three 4 KiB
    queues each).
- **utmp:** `ttyp0-3` are records 5-8 (`utmp_slot()` in `<utmp.h>`), so
  `who` lists remote users.
- **telnetd** (`/etc/telnetd`, started by `/etc/rc`):
  - It detaches itself, as `cron` does, and with no network it exits
    without a word.
  - Each connection takes the first free master, runs `getty` on the
    slave, and carries bytes with `select`: telnet commands out, CR LF and
    CR NUL as one Return, 255 doubled.
  - It offers WILL ECHO and WILL SGA (character mode, P32W echoes).
  - The session belongs to init (a double fork). Logout or the client
    leaving closes the master and clears the utmp record.
  - It holds one of the coprocessor's 8 sockets while it listens.
- **ftp** (`/bin/ftp`): an interactive client, passive mode only. The
  data connection goes to the control connection's address, not the one
  in the PASV reply (a server behind NAT names a private one), as curl
  does. Binary by default; ascii converts CR LF and LF. `-n` skips the
  automatic login.
- **ntpdate** (`/etc/ntpdate`): one SNTP request (three tries), the
  transmit time plus half the round trip. Root sets the clock, which
  `stime` also writes to the battery-backed clock; `-q` only asks.
  `/etc/rc` runs it if `/etc/ntp.conf` exists. None is shipped, so no
  test depends on the internet.
- **Tested** (`software/tests/netsvc_test.py`, in `make test`, the same on
  p32uemu), against the Mac:
  - two telnet logins in a row on `ttyp0`, `who` during and after;
  - an FTP server: a wrong password; `ls`, `nlist`, `cd`; `get` binary
    (5000 bytes checked on the Mac) and ascii (CR LF to LF); `put`,
    `delete`, `mkdir`; a missing file;
  - an SNTP server answering 2030: `-q` as ken, refused as ken, set as
    root, `date`.
- **The emulator flushes its output when P32W goes idle,** so a session
  that hangs shows how far it got.

## D47 — Bypass capacitors on a 0.1" footprint, on every board (2026-10-23)

Ken's rule for every board on every system: each 100 nF bypass capacitor
uses a 0.1" (2.54 mm) lead spacing, never a 0.2" (5 mm) disc.
- **The footprint:** `Capacitor_THT:C_Disc_D3.0mm_W2.0mm_P2.50mm` (KiCad
  calls 2.54 mm "2.50"), for `C_DISC` in `generators/p32net.py` and the
  backplane's bypass capacitors.
- **The small part's label** goes beside it (2.3 mm), not below:
  between its 0.1" pads there is no room, and below it the label meets the
  next part's silk.
- **Routing is re-tried:** small footprints change the DSN's order,
  and Freerouting is sensitive to it (the P8X lesson: re-roll the
  placement, not the route). `build_board.sh` places and routes again,
  up to `ROUTE_TRIES` (4) times, until DRC finds no unconnected pads.

## D48 — Board layout rules for every system: the front edge, readable labels, placement drawings (2026-10-24)

From Ken, for all systems (P32W, P8X, YACC1, ...): "switches and LEDS should
be near edge farthest from bus connector. Connectors such as video and DB9
should also be on edge farthest from bus connector"; part numbers "should
be visible once chips are installed"; switches and LEDs labelled with their
function, "if say a toggle switch has two positions label each option";
"for cards with an addon like a pico or nano ... near any edge (other then
bus connector) with USB port facing edge"; and the placement drawing must
show the board and its parts in one orientation. (Also in Ken's global
instructions, ~/.claude/CLAUDE.md, so they carry to the other projects.)
- **The front edge** is the edge opposite the backplane connectors.
  Switches, buttons and LEDs go there, and so do the connectors for
  outside cables (video, D-subs, USB, network), pointing outwards so a
  cable plugs straight in. A plug-in module (Pico, Tang Nano, the CH376S)
  goes at an edge other than the bus edge, its USB port facing out.
  Jumper blocks and cable headers go near a board edge, never in the
  middle.
- **Labels stay readable with the parts in.** A chip's reference and type
  sit on one line above the top pin row, the reference over pin 1's end
  and the type over the other, never under the chip. A socketed PLCC chip
  or a module gets its label above its socket, and an axial diode above
  its body.
- **Function labels.** An LED's function sits right under it. A button
  carries its function above it (RESET, STEP). A switch's positions are
  labelled at both ends with what each selects, ON at the end the part
  marks "on" (for example RUN / HALT, or INSTR / CYCLE for what STEP
  steps: an instruction or a microcycle).
- **Placement drawings** take the board's orientation (portrait for a tall
  board), with the board centred above the title block (A4, or A3 if it
  does not fit).
- Internal cables are not covered: a drive port whose cable stays inside
  the case sits beside its logic.

## D49 — The frame: 9U × 340 mm cards, three connectors, ten slots 1 inch apart (2026-10-24, Ken: "go with 366.7 x 340 and 3 connectors")

**Decided:**
- **Card size 366.7 × 340 mm:** 9U tall (the rack's height, the connector
  edge) and 340 mm deep, about 1,250 cm² per card.
- **Three 96-pin DIN 41612 connectors per card,** J1–J3 (288 pins), down
  the 366.7 mm edge.
- **A 10-slot backplane, slots 1 inch (25.4 mm, 5 HP) apart** (Ken: "lets
  set card spacing at 1\" (25.4mm)"). 5 HP is a whole Eurocard unit, so
  standard card guides and front panels fit (one 5 HP panel per card).
  25.4 mm less the 1.6 mm board leaves about 23.8 mm to the next card's
  solder side; allowing about 2.5 mm for that card's pins and joints and
  1 mm of air, **parts up to about 20 mm tall** fit on the component side.
  That covers DIP chips in sockets, standing 10 mm electrolytics and
  crystals; anything taller lies down.

**Why:**
- **Room for the CPU's datapath on one card.** A 340 mm-deep card holds
  the sequencer and control store, the instruction register and decode,
  the register file, the PC and the ALU module together, with the MMU and
  the data path on a second card beside it. The CPU's signals then stay on
  its own boards rather than crossing the backplane.
- **A system bus that DMA masters and several memory cards can share.**
  The bus (D50) carries only physical memory cycles, interrupts,
  arbitration, clock and reset, with room for page swapping, several
  memory cards, DMA, and a wait line that stretches a read or a write. J1
  carries the cycle, J2 control.
- **A private local link between the CPU card and the MMU / data card.**
  J3 in slots 1–2 only joins those two cards, so the microcode's memory
  controls never become bus signals other cards must respect.
- **Cost:** JLCPCB, 5 boards at 366.7 × 340: $171.70 at 6 layers, $254.00
  at 8 (2026-10-24).

## D50 — The system bus, the cards, and the choices under them (2026-10-24, Ken: "agree with all your recommendations")

Ken accepted every recommendation put to him with D49. In full in
[architecture.md](architecture.md); the bus in [bus.md](bus.md), generated
by `generators/p32bus.py`.
- **A system bus:** the backplane carries only physical memory cycles,
  interrupts, arbitration, clock and reset (J1 the cycle: D0–31, A2–31,
  BE0–3, `-MRD`, `-MWR`, `-ACK`, `-WAIT`; J2 control: CLK, `-CLK`, `-RES`,
  PWROK, `-LOCK`, `-IRQ0–7`, `-BREQ` / `-BGNT`). The CPU's microcode fields
  stay on the CPU card; J3 in slots 1–2 is the local link between the CPU
  card and the MMU / data card (LD0–31, DOE, DLD, MEM, SIZE, SIGNED, SUPER,
  TLBW, CRSEL, FAULT, FC, `-WSTB`, STALL, LOCKREQ).
- **The CPU card** in slot 1: the sequencer and control store, the
  instruction register and decode, the register file, the PC, the ALU
  module (D21), the clock and the front panel.
- **MMU / data on its own card** in slot 2: MAR, the split I/D TLB (D13),
  the fault checks, MDR and byte steering, BADADDR, the bus master
  interface, the arbiter and `-LOCK`. **GALs and TTL**, no CPLDs.
- **Clock:** designed for **5 MHz**, 3 MHz the safe setting.
- **Bus timing:** synchronous, one clock a cycle, **`-WAIT`** adds clocks;
  no `-ACK` within the first clock is a BUSERROR.
- **Arbitration: radial** (`-BREQ` / `-BGNT` per slot to the arbiter in
  slot 2), the slot setting only the priority. **Interrupts:** 8 shared,
  open-drain levels. **`-LOCK`** for atomic read-modify-write (D51, D54).
- **Memory cards:** 1–4, 16 MiB of SRAM each, the boot ROM on one; placed
  by **jumpers** (16 MiB windows, ROM enable).
- **Disk:** 2 CF + 2 IDE; PIO first, DMA-capable.
- **I/O:** four UARTs, the timer, the real-time clock (D23), ports, two
  USB ports (CH376S, D43), and Ethernet and Wi-Fi through **one Pico 2 W +
  W5500** (D45).
- **Video:** VGA 640 × 480, 80 × 25 text and a 640 × 480 one-bit overlay;
  **PS/2** keyboard and mouse. Ken confirmed **25 rows** over 30 the same day
  (asked: "25"): the classic PC console size. 25 rows of 8 × 16 cells fill
  400 of the 480 lines; the text is centred with a 40-line band above and
  below (the overlay still covers all 480).
- **Debug:** a host link (a Pico), a logic analyzer on probe inputs, and a
  fast clock; and a **bus master**, so it loads and reads memory by DMA
  with the CPU halted.
- **10 slots**, 1 inch apart (D49). I/O cards, like memory cards, are
  placed by jumpers, not by slot (D52).
- **ISA kept,** with an atomic swap (D51) and any new MMU fault causes
  added.

## D51 — SWAP, the atomic exchange (2026-10-24; under D50's "ISA kept, plus an atomic swap")

- **`SWAP rd, imm(rs1)`**, opcode **0x4C**, format LD: `t = mem32; mem32 =
  rd; rd = t` at an aligned word. The lock primitive: `SWAP` a 1 into the
  lock word; a 0 back means the lock was free and is now held.
- **Faults** as a load and then a store of the same word: MISALIGNED,
  TLBMISS_R, then PROTECT if the page is read-only. Commit-after-access
  (D14) holds: the old word goes to scratch T0 and reaches `rd` only after
  the store succeeded, so a faulting `SWAP` changes nothing and is
  restarted.
- **Microcode** (10 cycles with the fetch): address to MAR, `LOADW` into
  the MDR, MDR to T0, `rd` to the MDR, `STOREW`, T0 to `rd`.
- **Atomicity:** interrupts are taken only between instructions, but DMA
  masters share the bus, so the read and the write must be one bus tenure.
  The microcode marks the read as locked (`LOCKREQ` on the local link,
  held through the store), and the MMU / data card holds `-LOCK` over both
  (D54).
- Rejected for now: compare-and-swap (needs a third operand and a compare
  in the middle of a locked tenure) and load-linked / store-conditional (a
  reservation register on the MMU / data card). `SWAP` is enough for spin
  locks and semaphores on one processor with DMA.

## D52 — The address map: every card placed by jumpers, nothing by slot (2026-10-24, Ken: "Address jumpers. Not slot specific")

Device cards are placed by **address jumpers**, as memory cards are, and
nothing in the system depends on which slot a card is in (apart from slots
1–2, the CPU and MMU / data cards and their local link). Following from
that:

- **The bus carries no slot-number pins and no per-slot interrupt lines:**
  a slot number is no use to software that does not know which card is in
  which slot. 162 signal pins, 48 spare. The radial `-BREQ` / `-BGNT`
  pairs are there because they are how a card asks for the bus; the slot
  sets only its priority.
- **Interrupts:** each device's level is chosen by a **jumper** on its card;
  devices may share a level (`-IRQn` is open-drain). Each card has an
  **interrupt status register** so the kernel can tell which device on a
  shared level is asking.
- **Physical map:**

  | Range | What |
  |---|---|
  | `0x0000_0000`–`0x000F_FFFF` | boot ROM (1 MiB), on the memory card with its ROM jumper fitted; hides the RAM under it; a write is a BUSERROR |
  | `0x0000_0000`–`0x03FF_FFFF` | RAM: up to 4 memory cards of 16 MiB, window n = `n × 16 MiB` (2 jumpers) |
  | `0x0400_0000`–`0xFDFF_FFFF` | nothing (BUSERROR) |
  | `0xFE00_0000`–`0xFE07_FFFF` | **video memory**: the video card's text, font and overlay RAM, 64 KiB at `0xFE00_0000 + n × 64 KiB` (n = its register window's jumpers) |
  | `0xFFFF_8000`–`0xFFFF_FFFF` | **device registers**: 8 windows of 4 KiB, window n at `0xFFFF_8000 + n × 4 KiB` (3 jumpers per card) |

  Device windows are 4 KiB and sit in the top 32 KiB so that every device
  register is one instruction away, as an offset from `r0`
  (`lbu r1, 0xFFFFFF05(r0)`: the offset is encoded in 16 bits, `0xFF05`,
  and sign-extends). A 1 MiB window per card would need a base register
  for every access.
- **A card's registers are the top 256 bytes of its window** (offset
  `0xF00`–`0xFFF`), so the I/O card, at its default window 7, has `UART0`
  at `0xFFFF_FF00`, the timer at `FF10`, the USB ports at `FF50` and
  `FF54`, the network mailbox at `FF52`. Every card has, at offsets
  `0xFF0` / `0xFF1`, a read-only **card ID** (1 = I/O, 2 = disk, 3 =
  video, 4 = debug) and **revision**, and at `0xFF4` its interrupt status
  (a word, a bit per device); the kernel finds the cards by reading each
  window's ID (an empty window is a BUSERROR).
- **Default jumpers** (the settings the documents and the kernel assume):
  window 7 (`0xFFFF_F000`) the I/O card, 6 (`0xFFFF_E000`) the disk card,
  5 (`0xFFFF_D000`) the video card, 4 (`0xFFFF_C000`) the debug card; 0–3
  free. Interrupt levels: 0 timer, 1 disk (all four ports), 2 UART 0 and
  2, 3 UART 1 and 3, 4 PS/2 keyboard and mouse, 5 RTC, 6 USB (both), 7
  network.
- **The disk card** at window 6: port p at `0xFFFF_EF00 + 16 p` (0 = CF 0,
  1 = CF 1, 2 = IDE channel A, 3 = IDE channel B).
- **RAM sizing:** a missing memory card does not answer, so the kernel
  probes each 16 MiB window at boot with a load that may take a BUSERROR,
  and marks the frames of a window that does not answer as in use for
  good. The boot line says "memory: N MiB of RAM, …", and `sysinfo` (so
  `top`) reports the RAM found. No jumper tells the kernel the size.

## D53 — The video console (2026-10-24, Ken: "1")

Asked whether the video card's screen and PS/2 keyboard should be the
console when the card is fitted, or one more terminal beside UART 0's,
Ken chose the console.

- **The screen and keyboard are a terminal, `/dev/tty4`** (kernel
  `video.c`). When the video card answers at boot (its ID read with a
  probe), the kernel's messages go to the screen as well as to UART 0, and
  `init` keeps a getty on tty4 as on tty0 and tty1. Without the card,
  tty4 does not open and nothing changes: UART 0 is the console.
- **Terminal numbering** (minors and `/etc/utmp` slots): tty0–tty3 the
  I/O card's four UARTs, tty4 the screen, ttyp0–ttyp3 (the
  pseudo-terminals, D46) 5–8. `ps` shows them as `0`–`4` and `p0`–`p3`.
- **The screen** understands what P32W Unix's programs send (vi, top,
  BASIC): CR, LF with scrolling, BS, TAB, and `ESC [ row ; col H`,
  `ESC [ J` / `2J`, `ESC [ K`, `ESC [ n A`–`D`, `ESC [ … m` (normal, bright,
  underline, inverse). The last column wraps late, as a VT100's.
- **The keyboard:** set-2 scan codes decoded in the kernel, with shift,
  ctrl and caps lock; the arrows type `ESC [ A`–`D`, Delete DEL.
- **In the emulators** the card is fitted only with `-V FILE` (the screen
  written at exit) or `-K FILE` (the keyboard types the file), so every
  other run is a machine without one.
- Found on the way, and fixed: a mode change (`TIOCSETP`) left typed-ahead
  input counted in the old mode's units, so the shell's line editor
  (CBREAK) read three characters of a typed-ahead line and waited. The
  driver recounts it.

## D54 — The bus lock in the microcode (2026-10-24, under D51; Ken: "Go ahead with the microcode")

- **A one-bit microword field, `LOCK`** (bit 42: the microword is 43 bits,
  in six flash chips). It drives `LOCKREQ` on the local link. A `MEM` code
  would also work, but the field's one spare value (7) is kept free.
- **The rule, checked by `p32ucode.py`:** `LOCK` only on a `LOADW`, and the
  next bus cycle on every path after it is its `STOREW`. The MMU / data card
  holds `-LOCK` from the locked read to the end of that store, or until
  either faults; while it is held the arbiter grants nothing.
- **`SWAP`'s read is the one locked cycle.** Nothing else in the ISA needs
  the lock.
- **`STALL`** needs no microcode: the MMU / data card holds the sequencer
  while a cycle waits (`-WAIT`) or the bus is lent to a DMA master, and the
  microword's meaning does not change.
- **Tested in `p32uemu`** with a DMA master (`-D ADDR:PERIOD`) that keeps
  adding 1 to the word that a program empties with `SWAP`: with the lock no
  addition is lost; with the arbiter told to ignore it (`-Dnolock`), up to
  1,800 of 19,800 are.

## D55 — Every card designed for the best result; the ISA may change with it (2026-10-25, Ken: "GO with all the Alternatives now open ... I am fine starting as much from scratch as need for the best results")

- **Every card is designed from first principles** for the best result:
  the CPU's datapath, the clock, the register file, the ALU, the
  microword, the control logic's parts, and the MMU / data card and the
  rest with them. No circuit is kept for having been drawn before.
- **The ISA may change** where the new hardware runs better with it (Ken:
  "adding to or changing ISA to better operate in the new hardware is
  fine"). The software follows: the assembler and the opcode header are
  generated from microcode/p32isa.py, the compiler and P32W Unix are
  rebuilt, and the instruction-level emulator (p32emu) defines each
  instruction's meaning.
- **The three bus connectors are used to the full** (Ken: "Take full
  advantage of the 3 bus connectors"): the bus is redesigned with the
  datapath, not kept for its pin list.
- **The layout rules apply to every new card** (Ken): D19, D47, D48 and the
  global rules -- switches, LEDs and cable connectors at the front edge,
  pointing outwards; jumpers and headers at an edge; every reference
  readable with the parts in, ICs with their type; every control and each
  switch position labelled; placement drawings on a sheet that matches the
  board.
- **The order:** the datapath and microword first, as a microcode-level
  emulator model running P32W Unix and measured against today's (6.28
  cycles an instruction at 3 MHz, 0.48 MIPS); then the bus; then the
  cards, the CPU card first.

## D56 — The three-bus datapath (2026-10-25, under D55)

D55 asked for the best result from first principles. The datapath is the
lever on every other choice, so it came first, built and measured in the
microcode-level emulator before any card (docs/microarch.md).

- **Three buses:** A and B carry the operands (the register file's two read
  ports, the PC, the immediate unit), R the result (the ALU, memory data,
  the immediate unit, MAR, the store data, CAUSE, BADADDR, a hardware
  register) to one destination a cycle. **The register file reads two
  registers and writes one every cycle** (two SRAM copies written together).
- **A pipelined ALU:** a word latches A, B and the function; the result is
  on R from the next cycle, and the comparison of the latched operands is a
  condition from that edge.
- **Fetch overlaps the instruction before:** every instruction starts with
  MAR = PC and its last step sets MAR to the next PC; the generator checks
  it on every path.
- **MDR (the store data) on the CPU card, loaded from B**; a load's data
  comes back on R already steered. J3 carries R (`LR0`–`LR31`) and its
  source and destination codes.
- **The microword: 53 bits, seven flash chips.**
- **Measured** (P32W Unix booted, root logged in, `halt`; 7,490,575
  instructions on both): **3.57 cycles an instruction** against 6.28 for one
  internal bus — 1.76× at the same clock: 0.84 MIPS at 3 MHz, 1.00 MIPS at
  5 MHz with a wait state on every bus cycle, 1.40 MIPS without. Bus cycles
  are now 40% of all cycles, so a memory path fast enough for 5 MHz without
  a wait state is the next lever.
- Every test program runs on it instruction for instruction as on the
  reference emulator (`make test`); the ISA did not change.

## D57 — The bus's spare pins: more power, vectored interrupts (2026-10-25, Ken: "Power Pins and Vectored interrupts")

Of the options for the bus's spare pins (D55; a CPU trace bus to every
slot, more power pins, vectored interrupts, bus parity, a 64-bit memory
path), Ken chose two:

- **More power pins.** J2's row B even pins 4–30 are VCC, on every card and
  every slot: with row B's odd pins (GND) they alternate VCC and GND down
  the middle of J2. **VCC 18 → 32 pins a card**, GND 60. (DIN 41612 contacts
  carry about 1.5 A each when warm: 32 × 1.5 A = 48 A of 5 V a card, far
  above any card's need; the gain is the margin and a lower-inductance
  supply into the card.)
- **Vectored interrupts.** One new bused line, **`-IACK`** (J2 A18). When the
  CPU takes level *n*, its interrupt entry runs an **acknowledge cycle**: the
  MMU / data card puts `A4:A2` = *n* and pulls `-IACK` instead of `-MRD`; the
  card driving level *n* answers `-ACK` and puts its **vector** on `D7:D0` —
  its window number (D52) × 16 + the asking device's bit in its interrupt
  status register (the lowest asking one on that level). Slot-independent,
  like everything else on the bus: the vector comes from the window jumpers
  the card already has.
  - **One card a level**: two cards' jumpers must not put devices on the
    same level (they would both answer). One card may use several levels.
  - **No answer is not a bus error**: the vector is `0xFF`, and the kernel
    falls back to reading the cards' interrupt status registers.
  - **`CAUSE` = `0x8000_0000` | vector << 8 | *n*** for an interrupt
    (isa-system.md B2). The kernel dispatches on the vector.
  - **The microcode:** a new memory cycle type `IACK` (the eighth of the
    3-bit `MEM` field): interrupt entry puts `CAUSE` (with *n*) into MAR, and
    the acknowledge cycle's data — MAR with bits 15:8 replaced by the vector
    — goes into `CAUSE`. One cycle more per interrupt (six).
- Not chosen: the trace bus, bus parity, a 64-bit memory path. 33 pins a
  card stay spare (J2 rows A and C, J3).

## D58 — The CPU card's open choices (2026-10-25, Ken: "the first three, not the trace bus")

docs/cpu-card.md §3 is decided:

1. **Register-file addresses in GALs** (three ATF22V10s for the A, B and
   write ports, with the r15 bank rule and the r0 write sink), not
   74HCT151 muxes.
2. **Two copies of the register file in CY7C199 SRAM** (one per read port,
   written together at `-WSTB`), not dual-port SRAM.
3. **A dedicated comparator** (74HCT688s and a 74HCT85 tree, latched at the
   ALUGO edge), so a branch's condition is ready the cycle after its
   operands; not a two-cycle branch.
4. **No trace bus** (D57): the debug card watches the CPU through J2 and its
   own probe header.

## D59 — The address adder, indexed and post-increment loads and stores, LI32 (2026-10-25, Ken: "Go with the address adder and the three cheap additions")

From the measurements in [isa-measurements.md](isa-measurements.md):

- **An address adder (AGU) on the CPU card** puts A + an offset on R in the
  cycle the operands are read: the IR's sign-extended 16-bit offset, the
  post-increment format's 8-bit offset, or B shifted left by 0–3 (an
  index). A load or store's address reaches MAR without the ALU's cycle:
  **loads and stores take 3 cycles instead of 4**, `SWAP` 5 instead of 6.
  The microword gains `AOFF` (2 bits), `MARINC` (1) and the source code AGU
  in `RSRC`: **56 bits, still seven flash chips**.
- **Indexed loads and stores** (format X, 0x80–0x8A): address = rs1 + rs2 ×
  1, 2, 4 or 8. Loads 3 cycles; stores 4 (three registers to read, two
  ports).
- **Post-increment loads and stores** (format P, 0x90–0x9A): the access at
  rs1 + off8, then rs1 += inc8. 4 cycles. rs1 is written after the access
  has succeeded, so a fault leaves the instruction restartable (swap, D51).
- **`LI32 rd, value`** (0x2E, two words): 3 cycles instead of `LUI` +
  `ORI`'s 5, the same 8 bytes. MAR becomes a counter on the MMU / data card
  (`MARINC`, a new J3 line). The assemblers' `LA` and 32-bit `LI` are
  `LI32`. rd is in field B so the decode table keeps eight must-be-zero
  classes (the X format took the last one).
- **The compilers** (`cc/cgen.py` and P32W's own `software/cc/gen.c`, which
  must produce the same code) use indexed accesses for `p[i]` and
  post-increment for `*p++`; the native assembler and the debugger know the
  new formats.
- **Measured** (`tools/isamix.sh`, each workload the same before and
  after): **13–18% fewer cycles** — the shell session 91.6 M → 75.9 M, the
  compile 2,147 M → 1,799 M, Python 62.2 M → 54.1 M, SQL 157.4 M → 129.6 M;
  the boot benchmark 26.74 M → 22.43 M (7.49 M → 7.05 M instructions):
  **1.19× at the same clock**, 2.10× the single-bus datapath.
- **The adder's timing** is the new critical path at 5 MHz (≈ 195 ns with
  carry lookahead, microarch.md §8), to be confirmed in the CPU card's
  gate-level simulation; at 3 MHz it has room.
- Not taken (measured, Ken's choice): link-register calls, multiple
  load / store, compare with a constant and branch.

## D60 — Memory cards may be partly populated (2026-10-26, Ken: "I would like option to not fully populate cards fully"; "for now stick to Static")

- **A memory card is eight banks of 2 MiB**, each bank four AS6C4008
  (512K × 8, one per byte lane): the smallest step for a 32-bit memory. The
  banks are fitted in order from bank 0: a card holds 2, 4, … 16 MiB.
- **A 3-bit jumper on the card says how many banks are fitted.** The card
  answers (`-ACK`) only inside its fitted banks: an address above them is a
  bus error, as a missing card is.
- **The kernel probes in 2 MiB steps across all four windows** (D52) and
  uses every bank it finds; the memory may have holes (a half-fitted card
  in window 0 and a full one in window 1). The emulators' `-M` takes a
  size per card (`-M 16,6`).
- Static RAM for now; a DRAM card for density is in the backlog, for later
  (Ken).
- Done (2026-10-26): the emulators' `-M S0,S1,…` (MiB per window), the
  kernel's probe in 2 MiB steps (saving and restoring the word it tests)
  and its frame pool with holes, `/dev/mem` by bank; the boot monitor's
  stack moved to the top of the first 2 MiB bank (`0x001F_FC00`), which
  every machine has; the memory card's netlist (D63).

## D61 — The CPU card as built (2026-10-26, under D55-D59)

docs/cpu-card.md; `generators/card_cpu.py`, verified in the gate-level
simulator (`generators/sim_cpu.py`).

- **One commit edge, eight phases.** A 24 MHz oscillator, eight phases a
  3 MHz microcycle from one GAL; every destination and the pipeline
  register take their values at one edge (`CCLK`). The next microaddress
  is computed during the cycle from the cycle's own signals (the live
  comparison of A and B, the opcode on R during the fetch, faults), which
  keeps the microcode's meaning with one edge.
- **The register file is written late**, in phases 0–2 of the next cycle,
  before its reads (phases 3–7): no bypass.
- **196 ICs** (23 GALs, each checked exhaustively against its spec),
  through-hole; the ALU on the Tang Nano 20K behind its LVC latches.
- **Verified:** the card as wired runs the 17 cross-check programs with
  the state at every instruction equal to `p32emu`'s and the cycle count
  equal to `p32uemu`'s; `make test` runs five.
- **3 MHz** is this card's clock; 5 MHz needs a redesign of the read and
  condition paths and earlier translation (backlog).
- **The microcode changed with it:** the register-select codes are
  ordered for the card's field mux (A, B, C, SP, CONST, CRMAP), and the
  generator forbids MARPC in a word that loads the PC (the MMU / data card's
  shadow PC would give MAR the old PC).

## D62 — The MMU / data card as built (2026-10-26, under D55; Ken: "continue building hardware")

docs/mmu-card.md; `generators/card_mmu.py`.

- **130 ICs, 8 GALs**, through-hole; no clock of its own (CCLK, CLK and
  `-WSTB` from the CPU card).
- **MAR and the shadow PC are counters** (74HCT161): MARINC and PCINC count,
  RDST / MARPC load.
- **The TLB in seven CY7C199**, both sides (64 + 64 entries) in one array,
  compared by four 74HCT688s; **TLBW is written late** at the next cycle's
  `-WSTB`, as the register file is.
- **Faults in the order of `translate()`**, split over two GALs and checked
  against it for every input; a bus error is no `-ACK`, except in the
  acknowledge cycle (vector FF: pull-ups on D0–7).
- **Verified with the CPU card at gate level** (`generators/sim_sys.py`):
  the two netlists joined at J1–J3, memory and devices as bus slaves; the
  cross-check programs run with every instruction's state as `p32emu` and
  every cycle count as `p32uemu`; `make test` runs three.
- To do: a DMA master and a slow slave in the simulation (arbiter, lock,
  STALL), timing against the datasheets, the layouts.

## D63 — The memory card as built (2026-10-26, under D60)

docs/mem-card.md; `generators/card_mem.py`.

- **48 ICs, 3 GALs**: eight banks of four AS6C4008 (D60), the boot ROM in
  four SST39SF020A, address buffers and data transceivers, the select,
  bank / `-ACK` and write-strobe GALs.
- **Jumpers:** the window (2), the banks fitted (3), the ROM (1). The card
  answers only in fitted banks and, for reads, the ROM; each lane's `-WE`
  in the bus clock's second half.
- **Verified with the CPU and MMU / data cards at gate level**
  (`generators/sim_sys.py --mem`), every instruction and cycle as the
  emulators.
