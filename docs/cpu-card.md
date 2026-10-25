# The CPU card — design plan

**Status:** PROPOSED (2026-10-25). The plan for the CPU card's netlist,
from the three-bus datapath ([microarch.md](microarch.md), D56) and the
bus's J1–J3 ([bus.md](bus.md)). Slot 1, 9U × 340 mm (D49); every part
through-hole (D21); 4 layers unless it needs 6 (D19); the layout rules of
D48 (D55). Chip counts are *estimates* until the netlist exists.

## 1. Blocks

| Block | What it does | Parts | ICs (*estimate*) |
|---|---|---|---|
| **Clock and control** | a socketed oscillator (15 MHz → 3 MHz; 25 MHz → 5 MHz), INT OSC / EXT CLK jumper; GAL SYSCLK: the tick counter and the system clock (J2's CLK, always running); GAL CPUCLK: CCLK, the CPU's clock — an edge only in cycles that commit (running, not STALLed, a STEP allowed) — and `-WSTB`; RUN / HALT, STEP (microcycle or instruction), RESET, a reset while PWROK is low | oscillator, 2 × ATF22V10, 74HCT14, diode | 4 |
| **Control store** | microaddress {SEQ, opcode, STEP, COND} → 7 flash chips (53 bits) → pipeline register on CCLK's falling edge | 7 × SST39SF010A-55, 7 × 74HCT374 | 14 |
| **Sequencer** | STEP counter, SEQ / internal-sequence state, END / TRAP / interrupt decisions, the opcode mux (IR or internal sequence id), the condition mux | GALs (SEQ, FLAGS), 2 × 74HCT157 | 5 |
| **Instruction unit** | IR (from R), the opcode and CR decode tables (flash), the must-be-zero check → decode trap | 4 × 74HCT377, 2 × flash, zero detectors, GAL DECODE | 12 |
| **Register addresses** | three ports (A, B, write), each from IR.A / IR.B / IR.C / RCONST / the CR table / SP, with the r15 bank rule and the r0 write sink | 3 × 5-bit muxes (GALs, or 74HCT151s) | 6–15 |
| **Register file** | two copies, one per read port, written together from R at `-WSTB`; each copy's address is its read port's during the cycle and the write port's in the write strobe | 8 × CY7C199 (15 ns), per copy 4 × 74HCT244 R → SRAM and 4 × 74HCT244 SRAM → A / B, 2 × 5-bit address muxes | 28 |
| **PC** | bits 31:2, +4 (PCINC), load from R; onto A | 8 × 74HCT161, 4 × 74HCT244 | 12 |
| **Immediate unit** | the IR fields shaped (sext16, zext16, LUI, branch, jump, 4, 0) onto B, and onto R (LUI, the zeros of reset and trap entry) | 74HCT541 field groups, fill-bit buffers, GAL IMM | 16 |
| **ALU** | the Tang Nano 20K module (D21): A and B latched at the CCLK edge (8 × 74LVC574), read by the FPGA 16 bits at a time, the result written back (4 × 74LVC574) onto R when RSRC = ALU; 3.3 V regulator, level buffers | module, 12 × 74LVC574, 2 × 74LVC244, GAL, LD1117 | 17 |
| **Comparator** | EQ, LT, LTU of A and B, latched at the ALUGO edge: the branch's condition must be ready right after that edge (the FPGA's flags come too late) | 4 × 74HCT688 (EQ), a 74HCT85 tree (LT / LTU), flag register | 12 |
| **MDR** | the store data, from B (MDRB) or R, onto R for a store (RSRC = MDRS) | 4 × 74HCT377, 4 × 74HCT244 | 8 |
| **Status and interrupts** | STATUS (S, IE, PS, PIE), IMASK, IPEND, priority, the cause latch, the hardware control registers onto R | GALs (STATUS, IMASK, CAUSE), 74HCT540, 74HCT148, 74HCT00s, drivers | 10 |
| **Field drivers** | RSRC, RDST, MEM, SIZE, SIGNED, SUPER, TLBW, CRSEL, LOCKREQ, PCINC, MARPC onto J3; abort gating (a fault or decode trap suppresses the cycle) | 74HCT541s, 74HCT08s | 5 |
| **Front panel** | RUN / HALT LEDs, RESET and STEP buttons, the RUN / STEP switch, all at the front edge and labelled (D48) | | — |
| **Total** | | | **~150** |

## 2. The cycle (5 MHz: 200 ns; 3 MHz: 333 ns)

- **0–35 ns:** the pipeline register's outputs settle (loaded on CCLK's
  falling edge of the cycle before, so the word is ready at the start).
- **Operands:** register addresses (≈ 20 ns) → SRAM (15 ns) → buffers onto A
  and B (≈ 15 ns): A and B valid by ≈ 90 ns; the comparator by ≈ 150 ns.
- **Result:** whatever drives R (the ALU's output latches, memory data from
  J3, the immediate unit, MDR, …) by ≈ 120 ns; the destination latches at
  the CCLK edge; the register file is written in the cycle's last part
  (`-WSTB`), its address switched to the write port's.
- **The ALU:** the FPGA reads A and B (≈ 40 ns), computes (a few of its
  clocks; division 18, 167 ns), and writes the result back (≈ 20 ns): ready
  for the next cycle at 3 MHz always; at 5 MHz a division needs one more
  cycle (`busy` → the result cycle held, backlog).
- **Memory:** a bus cycle at 5 MHz takes one wait state until translation
  moves earlier (architecture.md §5).

## 3. Open choices

1. **Register addresses in GALs or muxes.** Three 5-bit ports with the bank
   rule and the write sink: three ATF22V10s (fast, fewer chips) or 15
   74HCT151s (no programming). *Recommended: GALs.*
2. **The register file's write timing.** Written in the cycle's last part
   at the write port's address: the read ports must not need their data
   then (they don't: A and B are latched by the ALU, MDR or the comparator
   before). Alternative: dual-port SRAM (IDT7134-class, through-hole, 4K × 8)
   — one copy instead of two, no address switching, but 48-pin DIPs and
   costlier. *Recommended: two copies of CY7C199.*
3. **The comparator** (12 chips) or a two-cycle branch (no comparator: the
   FPGA's flags a cycle later, +1 cycle on every branch). *Recommended: the
   comparator — branches are a sixth of all instructions.*
4. **The bus's spare pins** (backlog, "the bus: all three connectors"): a
   CPU trace bus for the debug card would add ~6 driver chips here.

## 4. Verification

- The netlist is written in Python like every P32W card
  (`generators/card_cpu.py`), checked electrically (`p32net.check`), every
  GAL exhaustively against its spec.
- **Gate-level simulation of the card** (`p32sim`) running the real control
  store, with the MMU / data card and memory as a behavioural model on J3:
  every test program's state at every instruction compared with `p32emu`,
  and every microcycle with `p32uemu`.
