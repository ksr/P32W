# P32W Microarchitecture

**Status:** ACCEPTED (2026-10-11, D14). How the ISA
([isa-v0.md](isa-v0.md), [isa-system.md](isa-system.md)) is built from
74HCT logic, GALs, SRAM, flash and the FPGA ALU module (D21), on the two
cards that make up the processor: the **CPU card** (slot 1) and the **MMU /
data card** (slot 2), joined by the J3 local link (D49, D50;
[architecture.md](architecture.md), [bus.md](bus.md)). It keeps the P8X
conventions wherever they fit: one shared data bus with encoded
source/destination fields decoded where each source and destination sits, a
pipelined microcode word, and microcode generated from Python tables that
share the ISA's single source of truth.

## 1. Overview

```
 CPU card (slot 1)                                       MMU / data card (slot 2)
┌──────────────────────────────────────────────┐        ┌───────────────────────────┐
│  ┌───────────┐ ┌──────────┐ ┌─────────────┐  │  J3    │  ┌─────────────────────┐  │
│  │ control   │ │ register │ │ ALU module  │  │ local  │  │ MAR, split I/D TLB, │  │
│  │ store,    │ │ file     │ │ (FPGA)      │  │ link   │  │ fault checks,       │  │
│  │ sequencer,│ │ 32×32    │ │ A, B →      │  │        │  │ MDR, byte steering, │  │
│  │ IR, imm,  │ │ SRAM,    │ │ result,     │  │        │  │ BADADDR             │  │
│  │ STATUS,   │ │ PC       │ │ compare     │  │        │  └──────────┬──────────┘  │
│  │ traps     │ │ counter  │ │             │  │        │             │             │
│  └─────┬─────┘ └────┬─────┘ └──────┬──────┘  │        │  ┌──────────┴──────────┐  │
│        │            │              │         │        │  │ bus master interface│  │
│  ══════╧════════════╧══════════════╧═════════╪═LD0–31═╪══╡ arbiter, -LOCK      │  │
│       internal data bus (one driver a cycle) │ DOE,DLD│  └──────────┬──────────┘  │
└──────────────────────────────────────────────┘ MEM,...└─────────────┼─────────────┘
                                                                      │ J1 + J2
  ════════════════════════════════════════════════════════════════════╧════════════
     the system bus: D0–D31, A2–A31, BE0–BE3, -MRD, -MWR, -ACK, -WAIT, ...
     (memory cards, disk, I/O, video, debug)
```

- **One shared 32-bit internal data bus,** exactly one driver per
  microcycle (P8X rule: encoded `DOE`/`DLD` fields, decoded where each
  source and destination sits, so contention is impossible by
  construction). It runs on the CPU card and is extended to the MMU / data
  card over the local link (`LD0`–`LD31`), with the microword's `DOE`,
  `DLD` and memory-cycle fields beside it.
- **The CPU reaches memory only through the MMU / data card.** That card
  translates the address and puts the **physical** address (`A2`–`A31`,
  `BE0`–`BE3`) on the system bus (D12: the bus carries physical addresses).
  The microcode fields never leave the two cards.
- **One microcycle = one clock.** The source drives the bus during the
  cycle; the destination latches at the clock edge that ends it. A bus
  cycle that a slow card stretches with `-WAIT`, or that waits for a DMA
  master, holds the microcycle through `STALL`; nothing else changes.

## 2. Registers and where they live

### 2.1 Register file: 32 × 32-bit fast SRAM (CPU card)

Four 8-bit fast SRAMs (≤ 15 ns), one read **or** write per microcycle. Only
32 words are used:

| Physical | Contents |
|---|---|
| 0 | r0 — holds 0 (see below) |
| 1–14 | r1–r14 |
| 15 | r15 in **user** mode = **USP** |
| 16 | `EPC` |
| 17 | `CAUSE` |
| 18 | `BADADDR` |
| 19 | `VBR` |
| 20–27 | **T0–T7**: microcode scratch, invisible to programs |
| 28 | **write sink**: writes to r0 land here (see below) |
| 29–30 | spare |
| 31 | r15 in **supervisor** mode = **SSP** |

- **Register address** (5 bits) is formed on the CPU card: the microword
  picks IR field A, B or C, or a 5-bit constant; then the **bank rule** maps
  register 15 to physical 31 when S = 1 (D6's banked SP in one gate).
- **r0 = 0:** when a write's register address comes from an IR field equal
  to 0, the register address logic sends physical **28** (the write sink)
  instead; the reset microcode writes 0 into physical 0 once, through a
  constant address (D4). The register file itself needs no extra logic
  (D16).
- **Control registers stored in SRAM** (`EPC`, `CAUSE`, `BADADDR`, `VBR`, and
  `USP` = physical 15) cost no hardware. `MFCR`/`MTCR` find them through a
  small **CR decode table** (CR number → SRAM address, or a hardware
  register, or illegal), generated from `p32isa.py`.

### 2.2 Hardware registers

| Register | Card | Why it is not in SRAM |
|---|---|---|
| `IR` (32 bits) | CPU | its fields drive decode every cycle |
| `PC` (bits 31:2) | CPU | a **counter**: +4 at the clock edge, no ALU cycle |
| `STATUS` (S, IE, PS, PIE) | CPU | S and IE gate hardware every cycle |
| `IMASK`, `IPEND` | CPU | interrupt priority logic |
| `ASID`, `TLBHI`, `TLBLO` | MMU / data | used by the TLB hardware |
| `MAR` (virtual address) | MMU / data | addresses every memory access |
| `MDR` + byte-lane steering | MMU / data | aligns and extends load data, replicates store data |
| `BADADDR` latch | MMU / data | holds the faulting address until trap entry copies it (D17) |
| ALU operand latches A, B | CPU (the ALU module) | at the FPGA |

The hardware registers on the MMU / data card are read and written over the
local link: `CRSEL` names the register for `DOE`/`DLD = CRHW`.

## 3. Cards

| Card | Contents | Notes |
|---|---|---|
| **CPU** (slot 1) | control-store flash + pipeline register, sequencer, IR, instruction decode, immediate unit, condition mux, STATUS, interrupt priority/IMASK, trap logic, CR decode table; the 32 × 32 SRAM register file and the PC counter; the ALU module (FPGA, D21, with 5 V ↔ 3.3 V buffering); the clock, reset and the front panel | the "brain"; generates register addresses. The ALU: A/B latches, all ALU ops incl. multiply/divide, compare outputs EQ/LT/LTU |
| **MMU / data** (slot 2) | MAR, split I/D TLB (one 128-entry SRAM + 22-bit tag comparator), ASID/TLBHI/TLBLO, alignment and upper-half checks, the BADADDR latch, MDR with byte-lane steering; the bus master interface (physical address and data drivers, `-ACK` timeout = BUSERROR, `-WAIT` → `STALL`), the arbiter and `-LOCK` | raises MISALIGNED, PRIVILEGE (upper half), TLB miss, PROTECT, BUSERROR on `FAULT` + `FC0`–`FC3` |
| **Memory** (1–4) | boot ROM (5 V flash, on one card) + RAM (fast SRAM, 16 MiB a card) | asserts `-ACK` when it decodes an address; none → BUSERROR |
| **I/O**, **disk**, **video**, **debug** | the devices ([architecture.md](architecture.md) §3) | memory-mapped in the device windows (D52) |

**FPGA voltage (D21):** 74HCT inputs accept 3.3 V logic levels (VIH = 2.0 V),
so FPGA outputs can drive the bus directly; FPGA **inputs** need 5 V-tolerant
buffering (74LVC245-class, powered at 3.3 V) on the 32 data lines and the
ALU control lines. This is one reason 74HCT (not 74HC) suits the design.
D21 puts the FPGA on a Tang Nano 20K module, behind latches and buffers on
the CPU card; their circuit is designed with the CPU card.

## 4. Data paths per cycle (examples)

`R[x]` = register file, `D` = data bus. One line per microcycle. These are
the design sketches made before the microcode was written; the sequences
as built are in `microcode/p32ucode.py`, and their cycle counts in §6.4
(ADD and LW are as sketched; branches and CALL came out shorter).

**Fetch** (step 0 of every instruction, shared):
```
1  MAR ← PC                        (DOE=PC,  DLD=MAR)
2  IR ← MEM[MAR] (fetch), PC += 4  (DOE=MEM, DLD=IR, PC INC)  — PC commits only if no fault
```

**ADD rd, rs1, rs2:**
```
3  ALUA ← R[B]
4  ALUB ← R[C]
5  R[A] ← ALU(ADD), end            → 5 cycles in all
```

**LW rd, imm(rs1):**
```
3  ALUA ← R[B]
4  ALUB ← IMM(sext16)
5  MAR ← ALU(ADD)
6  MDR ← MEM[MAR] (load, word)       — may fault: nothing committed yet
7  R[A] ← MDR (steered/extended), end → 7 cycles
```

**BEQ ra, rb, label:**
```
3  ALUA ← R[A]
4  ALUB ← R[B]
5  (wait: the compare outputs settle; condition mux selects EQ — D21)
6  if not taken: end
7  ALUA ← PC
8  ALUB ← IMM(branch: sext16 << 2)
9  PC ← ALU(ADD), end   → 6 cycles not taken, 9 taken (sketch; as built 5 / 7)
```

**CALL label** (commit after the access, D6):
```
3  ALUA ← R[15]
4  ALUB ← CONST(4)
5  T0 ← ALU(SUB)                     new SP, not yet committed
6  MAR ← T0
7  MDR ← PC
8  MEM[MAR] ← MDR (store)            — may fault: SP and PC still unchanged
9  R[15] ← T0                        commit SP
10 ALUA ← PC ; 11 ALUB ← IMM(jump: sext24 << 2) ; 12 PC ← ALU(ADD), end
                                     (as built: 13 cycles)
```

**Estimated average ≈ 6–7 microcycles per instruction.** As built and
measured (emulators.md §1): **5.0** on a C loop and **6.1** booting P32W Unix.
At the 3 MHz microcycle (D17) that is roughly **0.5–0.6 MIPS**. Later
speedups (a second bus, an ALU path for PC + offset) are possible without
changing the ISA.

## 5. Precise exceptions (stage A rule in hardware)

The ISA promises that a faulting instruction has **no effect** (D10). The
hardware gets there by **microcode discipline plus one abort mechanism**:

1. **Commit after access.** In every instruction's microcode, all writes to
   programmer-visible state (registers, PC, SP, memory, STATUS) come **after
   the instruction's last possibly-faulting memory access**; earlier results
   go to the scratch registers T0–T7. The microcode generator **checks this
   rule statically** for every opcode and refuses to build images that break
   it.
2. **Abort.** A fault found by the MMU / data card (MISALIGNED, PRIVILEGE,
   TLB miss, PROTECT) or a missing `-ACK` on the system bus (BUSERROR) is
   detected **in the cycle of the access** and signalled on the local link
   (`FAULT`, the cause on `FC0`–`FC3`); the CPU card suppresses that cycle's
   destination load and forces the next microaddress to the **trap-entry
   sequence**, latching the cause (D17).
3. **Trap entry** (microcode, ≈ 6 cycles): `EPC` ← PC (fetch fault or
   interrupt) or PC − 4 (fault after fetch); `BADADDR` ← the MMU / data
   card's BADADDR latch (0 for traps the CPU card raises itself); `CAUSE` ←
   the cause latch; the STATUS mode bits update in one step (PS←S, PIE←IE,
   S←1, IE←0); `PC` ← `VBR`. Nothing is written to memory (D10).
4. **Interrupts** are checked by the sequencer at the end of each instruction
   (step "end"): if `STATUS.IE` and `IPEND & IMASK`, the next microaddress is
   the interrupt-entry sequence instead of fetch (D11, D20).

## 6. Microcode

### 6.1 Address

```
microaddress = { SEQ (1) | OPCODE (8) | STEP (4) | COND (1) }  = 14 bits, 16K words
```

- `SEQ` = 0: the instruction in IR (OPCODE = IR[31:24]); `SEQ` = 1: an
  internal sequence (OPCODE field = reset, trap entry, interrupt entry, …).
- `STEP` counts microcycles within the sequence (up to 16); `COND` is the
  condition-mux output, so every word has a taken / not-taken twin (P8X
  scheme).
- Privileged opcodes in user mode, and illegal opcodes, are caught in the
  shared fetch path by a decode table (from `p32isa.py`) and sent to trap
  entry (D18).

### 6.2 Word format (as built: **43 bits, 6 flash chips** — `microcode/p32ucode.py` is the authority)

| Field | Bits | Purpose |
|---|---|---|
| `DOE` | 5 | who drives D: REG, ALU, IMM, PC, MEM, MDR, MAR, CAUSE, STATUS, CR-hw, … |
| `DLD` | 5 | who latches D: REG, ALUA, ALUB, IR, PC, MAR, MDR, STATUS, CR-hw, MEMW, … |
| `RSEL` | 2 | register address source: IR.A, IR.B, IR.C, constant |
| `RCONST` | 5 | constant register address (T0–T7, EPC, …) |
| `ALUOP` | 5 | ALU function |
| `IMM` | 3 | immediate unit: sext16, zext16, lui, branch, jump, small constant |
| `MEM` | 3 | cycle type: none, fetch, load, store; size from IR or forced word |
| `PC` | 2 | none, +4, load |
| `SEQ` | 3 | next step, end (→ fetch / interrupt), branch on COND, wait while BUSY, trap |
| `COND` | 3 | EQ, LT, LTU (inverted by the taken/not-taken twin), BUSY, … |
| `MODE` | 3 | trap-enter, RTE, other STATUS actions |
| `RAISE` | 4 | microcode-raised cause (SYSCALL, BREAK, ILLEGAL, PRIVILEGE, …) |
| misc | 3 | TLBW, HALT, WAIT |
| `LOCK` | 1 | D54: this LOADW and the STOREW after it are one bus tenure — `LOCKREQ` on the local link, `-LOCK` on the system bus (for `SWAP`, D51). Bit 42, on the sixth chip |

### 6.3 Store and pipeline

- **Six 5 V parallel flash chips** (55 ns, DIP-32, D18), in-system
  reprogrammable later; the outputs go through a **pipeline register**
  clocked on the opposite edge (P8X's non-negotiable rule — no ROM
  glitches on the datapath). The decode tables (opcode, control register)
  are generated from `p32isa.py` into their own flash images (D17).
- Built by a generator in the P8X lineage (`microcode/p32ucode.py`) that
  imports `p32isa.py`, emits the flash images, and **feeds a
  microcode-level emulator** (the P8X rule "the emulator runs the real ROM
  images") which is checked against the instruction-level reference
  emulator (D7) at every instruction.

### 6.4 As built (2026-10-11)

`microcode/p32ucode.py` implements the fields above with these refinements:
`RSEL` gained an `SP` input (register 15 through the bank rule, for
CALL/RET) and a `CRMAP` input (the CR decode table); `DOE` gained `MDRX`
(load steering), `MDRS` (store replication), `CAUSE` and `BADADDR` (the
trap latches) and `CRHW` (hardware control registers); the memory field
gained `LOADW`/`STOREW` (forced word) and `CHECKW` (alignment check only,
for JR/CALLR/RET targets). `ALU` has no BUSY/multi-cycle path yet: the FPGA
is modelled as single-cycle. The condition is evaluated on the state after
a word's clock edge (ROM output latched on the opposite edge, as on P8X).
The generator also checks the `LOCK` bit: only on a `LOADW`, and always
followed by its `STOREW`.

Cycles per instruction, including the 2 fetch cycles: ALU and immediate
ops 5, `LUI` 3, loads/stores 7, branches 5 (not taken) / 7 (taken), `JMP`
5, `JR` 4, `CALL`/`CALLR` 13, `RET` 10, `MFCR`/`MTCR` 4–6, the rest 3.
Measured over the test programs: about **6 cycles per instruction**
(5.04 on a stand-alone C loop, 6.12 for a P32W Unix boot; emulators.md §1).

The microcode-level emulator `emulator/p32uemu.c` runs these images over a
model of the CPU card and the MMU / data card and is compared with the
reference emulator on every test program, instruction by instruction
(`make test`; [emulators.md](emulators.md) §13–14).

## 7. The local link (J3, D50)

The microcode's datapath crosses between the two cards on J3 in slots 1 and
2 ([bus.md](bus.md), "J3"):

| Group | Direction | Signals |
|---|---|---|
| Internal data bus `LD0`–`LD31` | either (`DOE` picks the driver) | 32 |
| `DOE`, `DLD` (encoded) | CPU → MMU / data | 10 |
| Memory cycle `MEM0`–`MEM2`, size `SIZE0`–`SIZE1`, `SIGNED` (from IR) | CPU → MMU / data | 6 |
| `CRSEL0`–`CRSEL2` (hardware control register), `SUPER` (STATUS.S), `TLBW` | CPU → MMU / data | 5 |
| `-WSTB` (the SRAM write strobe, D16), `LOCKREQ` | CPU → MMU / data | 2 |
| `FAULT` + cause `FC0`–`FC3` | MMU / data → CPU | 5 |
| `STALL` (the bus is not yet ours, or the cycle is waiting) | MMU / data → CPU | 1 |
| **Total** | | **61**, with 9 spare pins |

Register addresses and the ALU function stay on the CPU card. The system
bus (J1, J2) carries only physical memory cycles, interrupts, arbitration,
the clock and reset.

## 8. Timing estimate

Critical paths at 74HCT speeds (≈ 15–25 ns per gate level, 15 ns SRAM,
55–70 ns memory):

- **Register/ALU cycle:** pipeline register → DOE decode → SRAM read → bus →
  ALU latch setup ≈ 100 ns.
- **Memory cycle** (the slowest): MAR → TLB SRAM → tag compare → physical
  address on the system bus → 55–70 ns memory → data back over the local
  link → MDR setup ≈ 200 ns.

**The first estimate: a 4 MHz microcycle (250 ns)** (D14), every memory
access in one cycle. **Settled at 3 MHz** (D17: a 15 MHz oscillator divided
by 5; the control-store lookup takes the first 2 ticks, 133 ns, and the
datapath the last 3, 200 ns, so the memory cycle above fits in one
microcycle). The system bus is designed for 5 MHz, with 3 MHz as the safe
setting (D50); a slow card adds wait states with `-WAIT`, and the microcode
does not change. The FPGA build of P8X suggests the same microcode will also
run much faster in an FPGA.

## 9. What follows acceptance

1. `microcode/p32ucode.py`: microword fields, every instruction and internal
   sequence, the commit-after-access checker, flash images.
2. A **microcode-level emulator** that runs those images and is compared,
   instruction by instruction, with `p32emu` (the golden model) on the whole
   test suite.
3. The bus definition ([bus.md](bus.md), from `generators/p32bus.py`), then
   the card netlists, starting with the CPU card; gate-level simulation of
   the CPU card's netlist comes with that netlist.

## 10. Review outcome (2026-10-11)

The review points were accepted as proposed (D14): one shared internal data
bus with encoded DOE/DLD, the physical address put on the system bus by the
MMU / data card; the 32-word SRAM register file; PC as a counter; microcode
in flash with a pipeline register and address {SEQ, opcode, step, cond};
precise exceptions by generator-checked commit-after-access plus fault
abort; a 4 MHz first estimate of the microcycle, since set at 3 MHz (D17).
The cards that carry it are in D50.
