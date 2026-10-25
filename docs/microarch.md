# P32W Microarchitecture

**Status:** PROPOSED (2026-10-25, D55, D56), running in the microcode-level
emulator. How the ISA ([isa-v0.md](isa-v0.md), [isa-system.md](isa-system.md))
is built from 74HCT logic, GALs, SRAM, flash and the FPGA ALU module (D21),
on the two cards that make up the processor: the **CPU card** (slot 1) and
the **MMU / data card** (slot 2), joined by the J3 local link (D49, D50;
[architecture.md](architecture.md), [bus.md](bus.md)). The microcode is
generated from Python tables that share the ISA's single source of truth
(`microcode/p32ucode.py`, which is the authority for everything below).

## 1. Overview

```
 CPU card (slot 1)                                                MMU / data card (slot 2)
┌───────────────────────────────────────────────────────────┐    ┌──────────────────────────┐
│ register file      ┌──────────┐                           │    │ MAR, split I/D TLB,      │
│ 2 read ports ──A───┤          │                           │    │ fault checks, BADADDR,   │
│              ──B───┤ ALU      ├──┐  (result, next cycle)  │ J3 │ load steering,           │
│ PC ──────────A     │ (FPGA,   │  │                        │    │ bus master interface,    │
│ immediate ───B     │ latched) │  │                        │    │ arbiter, -LOCK           │
│ B ──→ MDR          └──────────┘  │                        │    └────────────┬─────────────┘
│  ═══════════════════ R ══════════╧═════════════════════════╪═LR0–31═════════╡
│   → write port, PC, MAR, IR, MDR, control registers        │ RSRC, RDST,     │
└───────────────────────────────────────────────────────────┘ MEM, ...        │ J1 + J2
                                                                             │
  ═══════════════════════════════════════════════════════════════════════════╧════════
     the system bus: D0–D31, A2–A31, BE0–BE3, -MRD, -MWR, -ACK, -WAIT, ...
```

- **Three buses.** **A** carries the first operand (register-file read
  port A, or the PC), **B** the second (read port B, or the immediate
  unit), **R** the result (the ALU, memory data, the immediate unit, MAR,
  the store data, CAUSE, BADADDR or a hardware control register). Each has
  one driver a cycle, chosen by an encoded field decoded where its sources
  sit, so contention is impossible by construction.
- **The register file reads two registers and writes one every cycle.** R
  goes to one destination a cycle: the register file's write port, PC, MAR,
  PC and MAR together, IR, MDR or a hardware control register.
- **The ALU is pipelined.** A word that names an ALU function latches A, B
  and the function at the clock edge; the result is on R from the next
  cycle, and the comparison of the latched operands (EQ, LT, LTU) is a
  condition from that edge. So the ALU takes new operands every cycle while
  the previous result is being used.
- **Fetch overlaps the end of the instruction before.** Every instruction
  starts with MAR = PC; its first step reads it into IR and adds 4 to the
  PC; its last step sets MAR to the next PC. The generator checks the rule
  on every path.
- **R is the local link.** It runs on the CPU card and is extended to the
  MMU / data card over J3 (`LR0`–`LR31`) with the fields that say who
  drives it and who takes it. The CPU reaches memory only through the MMU /
  data card, which translates and puts the **physical** address on the
  system bus (D12). A load's data comes back on R, already steered and
  extended; a store's data goes out on R from MDR, replicated.
- **One microcycle = one CPU clock (CCLK).** Sources drive during the
  cycle; destinations latch at the edge that ends it. A bus cycle that a
  slow card stretches (`-WAIT`), or that waits for a DMA master, has no
  CCLK edge until it ends (`STALL`); nothing else changes.

## 2. Registers and where they live

### 2.1 Register file: 32 × 32-bit fast SRAM (CPU card)

Two read ports and one write port: two copies of the file (each four 8-bit
fast SRAMs, ≤ 15 ns), written together from R, read one per port. Only 32
words are used:

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

- **Register addresses** (5 bits each, for port A, port B and the write
  port) are formed on the CPU card: each port's select picks IR field A, B
  or C, the word's constant, the CR decode table, or SP; then the **bank
  rule** maps register 15 to physical 31 when S = 1 (D6's banked SP in one
  gate). The three ports share the word's one constant.
- **r0 = 0:** when a write's register address comes from an IR field equal
  to 0, the address logic sends physical **28** (the write sink) instead;
  the reset microcode writes 0 into physical 0 once (D4, D16).
- **Control registers stored in SRAM** (`EPC`, `CAUSE`, `BADADDR`, `VBR`, and
  `USP` = physical 15) cost no hardware. `MFCR`/`MTCR` find them through a
  small **CR decode table** (CR number → SRAM address, or a hardware
  register, or illegal), generated from `p32isa.py`.

### 2.2 Hardware registers

| Register | Card | Why it is not in SRAM |
|---|---|---|
| `IR` (32 bits) | CPU | its fields drive decode every cycle |
| `PC` (bits 31:2) | CPU | a **counter**: +4 at the clock edge, no ALU cycle |
| `MDR` (store data) + replication | CPU | loaded from B (or R); replicates a byte or halfword onto every lane for a store |
| `STATUS` (S, IE, PS, PIE) | CPU | S and IE gate hardware every cycle |
| `IMASK`, `IPEND` | CPU | interrupt priority logic |
| ALU operand latches, function, result | CPU (the ALU module) | at the FPGA |
| `ASID`, `TLBHI`, `TLBLO` | MMU / data | used by the TLB hardware |
| `MAR` (virtual address) | MMU / data | addresses every memory access |
| load steering | MMU / data | aligns and extends load data onto R |
| `BADADDR` latch | MMU / data | holds the faulting address until trap entry copies it (D17) |

The hardware registers on the MMU / data card are read and written over the
local link: `CRSEL` names the register for `RSRC` / `RDST = CRHW`.

## 3. Cards

| Card | Contents |
|---|---|
| **CPU** (slot 1) | control-store flash + pipeline register, sequencer, IR, instruction decode, immediate unit, condition logic, STATUS, interrupt priority / IMASK, trap logic, CR decode table; the dual-read-port register file, the PC counter, MDR; the ALU module (FPGA, D21: operand latches, every ALU function incl. multiply / divide, compare outputs); the clock, reset and the front panel |
| **MMU / data** (slot 2) | MAR, split I/D TLB, ASID / TLBHI / TLBLO, alignment and upper-half checks, the BADADDR latch, load steering; the bus master interface (physical address and data drivers, `-ACK` timeout = BUSERROR, `-WAIT` → `STALL`), the arbiter and `-LOCK`. Raises MISALIGNED, PRIVILEGE (upper half), TLB miss, PROTECT, BUSERROR on `FAULT` + `FC0`–`FC3` |
| **Memory** (1–4), **I/O**, **disk**, **video**, **debug** | [architecture.md](architecture.md) §3 |

## 4. Instructions, cycle by cycle

One line per microcycle; `R[x]` the register file. Step 0 is the fetch,
shared by every instruction; MAR already holds the PC.

**Fetch:** `IR ← M[MAR]` (R = memory), `PC += 4` — commits only if no fault.

**ADD rd, rs1, rs2** — 3 cycles:
```
0  fetch
1  A = R[rs1], B = R[rs2], ALU ← ADD
2  R[rd] ← ALU, MAR ← PC, end
```

**LW rd, imm(rs1)** — 4 cycles:
```
0  fetch
1  A = R[rs1], B = imm, ALU ← ADD
2  MAR ← ALU
3  R[rd] ← M[MAR] (steered), MAR ← PC, end     — may fault: then nothing is written
```

**SW rs, imm(rs1)** — 4 cycles:
```
0  fetch
1  A = R[rs1], B = imm, ALU ← ADD
2  MAR ← ALU; MDR ← B = R[rs]
3  M[MAR] ← MDR (R = MDR, replicated), MAR ← PC, end   — may fault: the store is the only effect
```

**BEQ ra, rb, label** — 3 cycles not taken, 4 taken:
```
0  fetch
1  A = R[ra], B = R[rb], ALU ← SUB; the condition: EQ of the latched operands
2  not taken: MAR ← PC, end   |   taken: A = PC, B = offset, ALU ← ADD
3  (taken) PC, MAR ← ALU, end
```

**CALL label** — 7 cycles, nothing visible before the store (commit after
the access, D6):
```
0  fetch
1  A = SP, B = 4, ALU ← SUB
2  MAR ← ALU (SP − 4); A = PC, ALU ← PASSA
3  MDR ← ALU (the return address); A = SP, B = 4, ALU ← SUB
4  M[MAR] ← MDR                      — may fault: SP and PC still unchanged
5  SP ← ALU (SP − 4); A = PC, B = offset, ALU ← ADD
6  PC, MAR ← ALU, end
```

Every instruction (from `p32ucode.py --stats`): ALU and immediate ops 3,
`LUI` 2, loads and stores 4, `SWAP` 6, branches 3 (not taken) / 4 (taken),
`JMP` 3, `JR` 4, `CALL` 7, `CALLR` 10, `RET` 6, `RTE` 3, `MFCR` / `MTCR`
3–5, the rest 2.

**Measured** (`p32uemu`, P32W Unix booted, root logged in, `halt`:
7,490,575 instructions, the same run on both datapaths):

| Datapath | Cycles | Cycles / instruction | 3 MHz | 5 MHz, a wait state on every bus cycle | 5 MHz, no wait |
|---|---|---|---|---|---|
| one internal bus | 47,030,356 | 6.28 | 0.48 MIPS | 0.65 MIPS | 0.80 MIPS |
| **three buses** | **26,738,361** | **3.57** | **0.84 MIPS** | **1.00 MIPS** | **1.40 MIPS** |

The three-bus datapath does the same work in 57% of the cycles (1.76× at
the same clock). Bus cycles are now 40% of all cycles (one wait state on
each adds 10.8 million), so memory and translation fast enough for 5 MHz
without a wait state is the next lever.

## 5. Precise exceptions

The ISA promises that a faulting instruction has **no effect** (D10). The
hardware gets there by **microcode discipline plus one abort mechanism**:

1. **Commit after access.** In every instruction's microcode, all writes to
   programmer-visible state (registers, PC, SP, memory, STATUS) come **after
   the instruction's last possibly-faulting memory access**, or in the
   access cycle itself; earlier results go to the scratch registers T0–T7
   or wait in the ALU. The generator **checks this rule statically** on
   every path of every opcode and refuses to build images that break it.
2. **Abort.** A fault found by the MMU / data card (MISALIGNED, PRIVILEGE,
   TLB miss, PROTECT) or a missing `-ACK` on the system bus (BUSERROR) is
   detected **in the cycle of the access** and signalled on the local link
   (`FAULT`, the cause on `FC0`–`FC3`); the CPU card suppresses that cycle's
   loads and forces the next microaddress to the **trap-entry sequence**,
   latching the cause (D17).
3. **Trap entry** (6 cycles): `CAUSE` ← the cause latch; `EPC` ← PC (fetch
   fault) or PC − 4 (fault after fetch); `BADADDR` ← the MMU / data card's
   latch (0 for traps the CPU card raises itself); the STATUS mode bits in
   one step (PS←S, PIE←IE, S←1, IE←0); PC and MAR ← `VBR` (bits 31:2).
   Nothing is written to memory (D10).
4. **Interrupts** are checked by the sequencer at the end of each instruction:
   if `STATUS.IE` and `IPEND & IMASK`, the next microaddress is the
   interrupt-entry sequence (5 cycles) instead of the fetch (D11, D20).

## 6. Microcode

### 6.1 Address

```
microaddress = { SEQ (1) | OPCODE (8) | STEP (4) | COND (1) }  = 14 bits, 16K words
```

- `SEQ` = 0: the instruction in IR (OPCODE = IR[31:24]); `SEQ` = 1: an
  internal sequence (reset, trap entry, interrupt entry).
- `STEP` counts microcycles within the sequence (up to 16); `COND` is the
  condition the previous word chose, so every word has a taken / not-taken
  twin.
- Privileged opcodes in user mode, and illegal opcodes, are caught by a
  decode table (from `p32isa.py`) in step 1, the first after the fetch,
  whose effects are suppressed; trap entry follows (D18). Step 1 never
  starts a memory cycle.

### 6.2 Word format (**53 bits, 7 flash chips**)

| Field | Bits | Purpose |
|---|---|---|
| `ASRC` | 2 | A's source: none, register port A, PC |
| `BSRC` | 2 | B's source: none, register port B, immediate |
| `RSRC` | 4 | R's source: none, ALU, memory, immediate, MAR, MDR (store data), CAUSE, BADADDR, hardware CR |
| `RDST` | 3 | R's destination: none, register write port, PC, MAR, PC and MAR, IR, MDR, hardware CR |
| `MDRB` | 1 | MDR ← B |
| `MARPC` | 1 | MAR ← PC (the next fetch's address) |
| `PCINC` | 1 | PC += 4 |
| `ALUGO`, `ALU` | 1 + 5 | latch A, B and this ALU function |
| `RSA`, `RSB`, `RSW` | 3 + 3 + 3 | each port's register address: IR.A, IR.B, IR.C, constant, CR table, SP |
| `RCONST` | 5 | the constant register address (T0–T7, EPC, …) |
| `IMM` | 3 | immediate unit: sext16, zext16, lui, branch, jump, 4, 0 |
| `MEM`, `LOCK` | 3 + 1 | cycle type: none, fetch, load, store, load word, store word, alignment check; `LOCK`: this load word and the store word after it are one bus tenure (`-LOCK`, D54) |
| `SEQ` | 2 | next step, end (→ fetch or interrupt), trap |
| `COND` | 3 | the condition for the next step: EQ, LT, LTU, CR illegal, CR hardware, fetch fault, MMU fault |
| `MODE` | 3 | trap-enter, RTE, TLBW, HALT, WAIT |
| `RAISE` | 4 | microcode-raised cause (SYSCALL, BREAK, ILLEGAL) |

### 6.3 Store and pipeline

- **Seven 5 V parallel flash chips** (55 ns, DIP-32, D18); the outputs go
  through a **pipeline register** clocked on the opposite edge, so no ROM
  glitch reaches the datapath. The decode tables (opcode, control register)
  are generated from `p32isa.py` into their own flash images (D17).
- The generator (`microcode/p32ucode.py`) imports `p32isa.py`, emits the
  images, checks every word (R's source and destination, a memory read's
  data on R, a store's data from MDR, MAR not loaded twice, …), every END
  (MAR left at the next PC), commit after access and the lock; and **feeds
  the microcode-level emulator** `emulator/p32uemu.c`, which runs the real
  images over a model of the two cards and is compared with the
  instruction-level reference emulator (D7) at every instruction on the
  whole test suite ([emulators.md](emulators.md)).

## 7. The local link (J3, D50)

The microcode's datapath crosses between the two cards on J3 in slots 1 and
2 ([bus.md](bus.md), "J3"):

| Group | Direction | Signals |
|---|---|---|
| R: `LR0`–`LR31` | either (`RSRC` picks the driver) | 32 |
| `RSRC`, `RDST` (encoded) | CPU → MMU / data | 7 |
| Memory cycle `MEM0`–`MEM2`, size `SIZE0`–`SIZE1`, `SIGNED` (from IR) | CPU → MMU / data | 6 |
| `CRSEL0`–`CRSEL2` (hardware control register), `SUPER` (STATUS.S), `TLBW` | CPU → MMU / data | 5 |
| `-WSTB` (the write strobe), `LOCKREQ`, `CCLK` (the CPU's clock) | CPU → MMU / data | 3 |
| `PCINC`, `MARPC`: the MMU / data card's **shadow PC** loads from R with the PC and counts with it, so `MAR ← PC` is local to that card and needs no R cycle | CPU → MMU / data | 2 |
| `FAULT` + cause `FC0`–`FC3` | MMU / data → CPU | 5 |
| `STALL` (the bus is not yet ours, or the cycle is waiting) | MMU / data → CPU | 1 |
| **Total** | | **61** |

A and B, the register addresses and the ALU function stay on the CPU card.
The system bus (J1, J2) carries only physical memory cycles, interrupts,
arbitration, the clock and reset.

## 8. Timing

Critical paths at 74HCT speeds (≈ 15–25 ns per gate level, 15 ns SRAM,
55 ns memory), for a 200 ns (5 MHz) microcycle:

- **Operand cycle:** pipeline register → register addresses → SRAM read →
  A, B → ALU latch setup ≈ 90 ns.
- **Result cycle:** the ALU's result (from the latches, inside the FPGA) →
  R → the register file's write (in the cycle's last part, `-WSTB`) or MAR
  ≈ 120 ns.
- **Memory cycle:** MAR → TLB SRAM → tag compare → physical address on the
  system bus → 55 ns memory → data back on R, steered → destination setup
  ≈ 200 ns: one wait state at 5 MHz until translation moves earlier.

3 MHz (a 15 MHz oscillator divided by 5, D17) needs no wait state; the
system bus is designed for 5 MHz (D50).

## 9. What follows

1. The bus definition's J3 ([bus.md](bus.md), `generators/p32bus.py`):
   done with this datapath; the rest of the bus is redesigned to use all
   three connectors (D55).
2. The card netlists, starting with the CPU card; gate-level simulation of
   the CPU card's netlist comes with that netlist.
