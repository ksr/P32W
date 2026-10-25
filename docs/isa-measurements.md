# ISA additions: measured candidates

**Status:** MEASURED (2026-10-25), for Ken's decision (D55 allows ISA
changes where the new hardware runs better with them). Nothing here is in
the ISA yet.

## How it was measured

`tools/isamix.sh SESSION` runs a recorded session on `p32uemu` (the real
control store, the three-bus datapath, D56) from a fresh image and streams
its instruction trace (`p32uemu -I`: per instruction its PC, IR and
microcycles) into `tools/isamix.c`. For each candidate the analyzer finds
the instruction sequences a new instruction would replace, in the dynamic
trace, and adds up the **cycles the replaced instructions actually took**,
less what the new instruction would add. A register a fused instruction
would no longer write must be **dead** afterwards (the trace writes it
before reading it), so only sequences the compiler could really fuse count.
The savings are estimates: the new instructions' cycle counts are assumed
(below), and the compiler would have to emit them.

| Workload | Session | Instructions | CPI | Kernel |
|---|---|---|---|---|
| Shell, files, kernel heavy | session1 | 25.5 M | 3.58 | 90% |
| BASIC, `make basic` (the C compiler on P32W) | session12 | 599 M | 3.58 | 17% |
| MicroPython | session15 | 17.2 M | 3.62 | 84% |
| SQL | session11 | 43.6 M | 3.61 | 68% |

(Cycles without `WAIT`, the idle loop.)

## Results: cycles saved, % of all cycles

| Candidate | Assumed cost | shell | compile | Python | SQL |
|---|---|---|---|---|---|
| **Post-increment** `LW rd, off(rs)+k`: the access, then `rs += k` | one cycle more than the load / store (the second register write) | 1.0 | **2.8** | 1.1 | 1.8 |
| **Scaled indexed** `LW rd, [rs + rt << k]` (k = 0–3) | as a load / store; the ALU adds the shifted index (a new ALU function) | **3.5** | 1.5 | **2.0** | 1.7 |
| **Unscaled indexed** `LW rd, [rs + rt]` | as a load / store | 0.2 | 0.7 | 0.3 | 0.3 |
| **Leaf calls with a link register** (`JAL` 3 cycles, `JR lr` 4, instead of `CALL` 7, `RET` 6) | a register given up as the link | 1.0 | 2.3 | 1.0 | 1.7 |
| **Compare with a constant and branch** `BEQ ra, simm8, label` | as a branch; the label's reach shrinks to ±2 K words (a 12-bit offset) | 1.0 | 1.9 | 0.8 | 0.9 |
| **LDM** (epilogue: load registers, 2 cycles each) | a register-list sequencer on the CPU card | 1.0 | 1.6 | 1.0 | 1.2 |
| **STM** (prologue: store registers, 2 cycles each) | as LDM | 0.8 | 1.3 | 0.8 | 1.0 |
| **LI32**: `LUI` + `ORI` as one two-word instruction | microcode only (the second word read like a fetch) | 1.0 | 1.3 | 1.0 | 1.3 |
| `ADDSL rd = rs + (rt << k)` (an ALU function alone) | — | 0.0 | 0.0 | 0.1 | 0.0 |
| *(compiler only)* `LA` + `LW 0` → `LUI` + `LW lo` | none | 0.3 | 0.6 | 0.4 | 0.4 |
| *(compiler only)* `ADDI` folded into the offset | none | 0.0 | 0.0 | 0.0 | 0.0 |

Calls: 62–71% of all calls are to **leaf** functions (no call inside).

**Together** (they overlap little): **about 10% fewer cycles on the
compile, 8–11% on the others.** No single addition is worth more than 3.5%.

## What the measurements say besides

- **Loads and stores take 42–54% of all cycles** (4 cycles each: the fetch,
  the address into the ALU, the ALU's result into MAR, the access). An
  **address adder** that puts `rs + offset` into MAR in the cycle the
  operands are read would make every load and store 3 cycles: **11–13%
  fewer cycles** on every workload, more than all the ISA additions
  together, with no ISA or compiler change. It is a datapath question for
  the CPU and MMU / data cards (a 32-bit adder on B and A into MAR, its
  carry chain inside the cycle at 5 MHz), to measure next.
- **Disk transfers by the CPU**: in the shell session `LHU` / `SH` (the
  CompactFlash's 16-bit data register into a buffer) are 13% of all
  instructions. A **DMA disk card** (already planned, for swap) takes
  those away entirely.
- `ADDI` is the most frequent instruction (18–22%): loop counters and
  pointer steps; post-increment absorbs part of it.
