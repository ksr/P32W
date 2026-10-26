# The CPU card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-26),
`generators/card_cpu.py` (every chip and GAL equation comes from there),
running the microcode of `microcode/p32ucode.py`. The card's summary, its
parts list by block and its verification are in [the CPU card](cpu-card.md).
The datapath it implements is [the microarchitecture](microarch.md) (D56).
This chapter explains how the circuit works, block by block, and then
follows three instructions through it cycle by cycle.

## 1. What the card does

The CPU card is the whole processor **except memory access**. It holds:
- the microcode, and the sequencer that steps through it;
- the instruction register and decode;
- the 32 registers and the PC;
- the ALU (a Tang Nano 20K FPGA module, D21);
- the address adder, the comparator, the immediate unit;
- STATUS, IMASK and the interrupt logic;
- the clock and the front panel.

Everything to do with memory (MAR, translation, faults, the bus cycle, the
byte steering of loads) is on the **MMU / data card** in slot 2. The two
cards talk over **J3, the local link**, which exists only in slots 1 and 2.
The CPU card tells the MMU / data card what each cycle needs (`MEM`,
`SIZE`, `RSRC`, `RDST`, `PCINC` …). The MMU / data card answers with load
data on R, `FAULT` and its cause, and `STALL`.

## 2. The three buses

The card is built around three 32-bit buses (D56):

| Bus | Nets | Driven by | Read by |
|---|---|---|---|
| **A** | `AB0–31` | register file copy A; the PC | the ALU's operand A latches; the address adder; the comparator |
| **B** | `BB0–31` | register file copy B; the immediate unit | the ALU's operand B latches; the address adder's index; the comparator; the MDR |
| **R** (the result) | `LR0–31` | the ALU result latches; the address adder; the MMU / data card (load data, MAR, BADADDR, its CRs); the immediate unit's LUI and zero fill; the store data lanes; CAUSE, STATUS, IPEND, IMASK | the register file (write); the PC; the IR; the MDR; STATUS, IMASK; the MMU / data card (MAR, the shadow PC, its CRs, store data) |

R is the same net as J3's `LR0–31`: the local link carries the result bus
itself. Each bus has 10k pull-downs (twelve 8-resistor networks), so in a
cycle that drives nothing a bus reads 0 rather than floating.

**Who drives a bus** is decided by the microword: `ASRC` (none, register,
PC), `BSRC` (none, register, immediate) and `RSRC` (16 codes). Decoders turn
each field into one active-low enable. Exactly one source drives a bus in a
cycle, and the simulator checks every cycle for two drivers fighting.

## 3. The clock: eight phases, one commit edge

A **24 MHz oscillator** clocks **GAL CLKGEN**, a 3-bit phase counter
(`PH0–2`) whose other outputs are all registered on the same oscillator
edge. None of them can glitch, because none is decoded combinationally.

```
phase      7  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  0
            ──┐     ┌─────────────────┐                       ┌──
CLK           └─────┘                 └───────────────────────┘      high in 0-3 (the bus clock, J2)
CCLK        __┌───────────────────────┐__________________┌─────     rises at 0 (the commit), falls at 4
WPH         __┌─────────────────┐________________________┌─────     0-2: the register file at its write address
-WSTB       ‾‾‾‾‾‾‾‾‾‾‾‾└─────┘‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾     phase 1: the write strobe
reads                         ├── register file read: phases 3-7 ──┤
```

One microcycle is eight phases of 41.7 ns, which is **333 ns at 3 MHz**.

- **`CCLK`, the commit**, rises at the start of phase 0, and everything
  that holds state changes on that one edge: the pipeline register (the
  next microword), the microaddress register, IR, PC, MDR, STATUS, IMASK,
  the cause latch, the comparison flags, the register file's write latches,
  and, on the MMU / data card, MAR and its shadow PC. **`ACLK`** rises with
  it, but only in a word with `ALUGO`: the ALU's operand and function
  latches.
- **A held cycle** has no `CCLK` edge. CLKGEN suppresses it when **`STALL`**
  (from the MMU / data card: a DMA master has the bus, or a slow card holds
  `-WAIT`) or **`HOLD`** (from GAL PANEL: WAIT with nothing pending, HALT,
  the front panel's STEP mode) is high at the end of phase 7. The same
  microword then runs again. Its effects are idempotent: the late write
  writes the same value to the same register again.
- **`CLK`**, the backplane's bus clock, is high in phases 0–3. Every card
  on the bus works from it ([the bus](bus.md)).

## 4. The control store and the microword

The microword is **56 bits** in **seven SST39SF010A flash chips** (128 K × 8,
55 ns), one byte each. Their outputs go into **seven 74HCT574s, the
pipeline register**, clocked by `CCLK`. So the word that controls a cycle
is the one the flash produced during the previous cycle, latched at the
commit. Its fields (`p32ucode.py`, low bit first):

| Field | Bits | What it controls |
|---|---|---|
| `ASRC` | 2 | A's source: none, the register file, the PC |
| `BSRC` | 2 | B's source: none, the register file, the immediate unit |
| `RSRC` | 4 | R's source: none, ALU, MEM (the MMU / data card), IMM, MAR, MDRS (store data), CAUSE, BADADDR, CRHW (a hardware CR), AGU (the address adder) |
| `RDST` | 3 | R's destination at the commit: none, REG, PC, MAR, PC+MAR, IR, MDR, CRHW |
| `MDRB` | 1 | the MDR loads from B instead of R |
| `MARPC`, `PCINC`, `MARINC` | 1 each | MAR ← the shadow PC; PC += 4; MAR += 4 (D59) |
| `ALUGO` | 1 | latch A, B and the function into the ALU at this commit |
| `ALU` | 5 | the ALU function |
| `RSA`, `RSB`, `RSW` | 3 each | each register port's address source: the IR's field A, B or C, SP, a constant, the CR table |
| `RCONST` | 5 | the constant register address (EPC, CAUSE, scratch …) |
| `IMM` | 3 | the immediate unit's mode |
| `MEM` | 3 | the memory cycle: none, FETCH, LOAD, STORE, LOADW, STOREW, CHECKW, IACK |
| `LOCK` | 1 | `LOCKREQ`: this access and the next are atomic (SWAP, D51, D54) |
| `SEQ` | 2 | NEXT step, END of the instruction, TRAP |
| `COND` | 3 | the condition that selects between a step's two words |
| `MODE` | 3 | none, TRAPENTER, RTE, TLBW, HALT, WAIT |
| `RAISE` | 4 | the cause a `SEQ = TRAP` word raises |
| `AOFF` | 2 | the address adder's offset: the 16-bit offset, the 8-bit one, the index |

**The microaddress** is 14 bits on the flash chips' address lines:

| Address bit | 0 | 1–4 | 5–12 | 13 |
|---|---|---|---|---|
| | **the condition** | **the step** (0–15) | **the opcode**, or the internal sequence's number | **USEQ**: an instruction (0) or an internal sequence (1) |

So each instruction owns 16 steps of two words: a word for "condition
false" and a word for "condition true". A step that does not depend on a
condition has the same word in both. Step 0 of every opcode is the same
**FETCH** word. The internal sequences (USEQ = 1) are RESET, TRAP and INTR.

## 5. The sequencer: choosing the next microaddress

The next microaddress is **worked out during the cycle**, from the cycle's
own signals, and latched at the same `CCLK` edge as everything else
(**74HCT574 microaddress register**: USEQ, the step, the internal
sequence's number). The microcode's convention is "the condition is tested
on the state **after** this cycle's edge". The hardware gets that with one
edge by computing the condition from the signals that will be latched.

**GAL NEXT** computes the next sequence, step and opcode source:

| This cycle | Next microaddress |
|---|---|
| RESET | USEQ = 1, step 0, sequence RESET |
| a trap being entered (`GOTRAP`) | USEQ = 1, step 0, sequence TRAP |
| an interrupt being taken (`GOINTR`) | USEQ = 1, step 0, sequence INTR |
| `SEQ = NEXT` | the same sequence, step + 1 |
| `SEQ = END` | USEQ = 0, step 0: the FETCH word (the same for every opcode) |

**The opcode mux** (four 74HCT153s) supplies address bits 5–12 from one of
three places (`OPS`):
- **IR[31:24]**, normally;
- **R[31:24]** in the step after FETCH. The instruction is still on R,
  arriving from memory, and has not reached IR yet, so its opcode comes
  straight off the bus. This saves a cycle on every instruction.
- **the internal sequence's number**, for RESET, TRAP and INTR.

**GAL COND** computes address bit 0, the condition (`NCOND`), only in a
`SEQ = NEXT` word:

| `COND` | Condition |
|---|---|
| `EQ`, `LT`, `LTU` | the comparison of A and B. **In the word that latches them** (`ALUGO`) it is the comparator's **live** output, the comparison of what is on the buses now; in a later word, the flags COND **latched** at that ALUGO's commit (`EQF`, `LTF`, `LTUF`) |
| `CRILL`, `CRHW` | from the control-register decode table: the CR number is illegal; it is a hardware CR, not one kept in the register file |
| `FFAULT`, `MMUF` | for the trap sequence: the fault came from a fetch; it came from the MMU / data card (D17) |
| `ZERO` | 0 |

COND also keeps the two fault flags, latched when a trap is entered.

**GAL EVENTS** decides what kind of cycle this is:
- **`ABORT_N`**: the cycle is cancelled when there is a reset, a decode
  trap (`DECTRAP`) or a fault from the MMU / data card (`FAULT`). R's
  destination decoder is disabled, and the local link's `RDST`, `PCINC`,
  `MARPC` and `MARINC` are gated off. `ALUGO`, `MODE` and the MDR load have
  no effect. Nothing the cycle would have changed changes, so an
  instruction that faults can be restarted (commit after access, D14).
- **`GOTRAP`**: a decode trap, a fault, or a `SEQ = TRAP` word.
- **`GOINTR`**: an `END` word, not aborted, with STATUS.IE set and an
  enabled interrupt pending, and not the end of trap entry itself (D20).
- **`SEQNEXT`**, **`ALUGOK`** (an ALUGO that commits), **`WAITHOLD`**
  (`MODE = WAIT` with nothing pending: GAL PANEL holds the clock),
  **`HALTMODE`**.
- **The cause latch's source** (`CSEL`, below) and its load (`CLOAD_N`).

A **74HCT138** decodes the step into `FIRST_N` (step 0, the fetch) and
`FIRST1_N` (step 1, when the decode trap is checked).

## 6. The instruction unit

- **IR**: four 74HCT377s loaded from R at the commit when `RDST = IR` (the
  FETCH word).
- **The opcode decode table**: an SST39SF010A addressed by IR[31:24]. For
  each opcode it gives `LEGAL`, `PRIV` (privileged), a 3-bit **zero class**
  (which bits of the instruction must be 0), `SIZE` (byte, halfword, word)
  and `SIGNED`. `SIZE` and `SIGNED` go to the MMU / data card on J3.
- **The control-register decode table**: an SST39SF010A addressed by
  IR[19:16] (an `MFCR` / `MTCR`'s CR number). It gives `CRILL` (no such
  CR), `CRHW` (a hardware CR: STATUS, IPEND, IMASK, ASID, TLBHI, TLBLO),
  `CRSEL` (which one; it goes to the MMU / data card for its three) and,
  for CRs kept in the register file (EPC, CAUSE, BADADDR, VBR, USP), the
  register address.
- **The must-be-zero check** (D18): four 74HCT4002 (dual 4-input NOR)
  detect seven groups of IR bits that are all zero. **GAL DECODE**
  compares them with the opcode's zero class. In step 1 it raises
  **`DECTRAP`** for an illegal opcode or a must-be-zero field that is not
  zero (cause ILLEGAL), or a privileged opcode in user mode (cause
  PRIVILEGE). The step-1 word is then aborted and the trap entered.

## 7. The register file

**32 physical registers** of 32 bits:

| Address | Register |
|---|---|
| 0 | r0: written 0 by the reset microcode, then never written again |
| 1–14 | r1–r14 |
| 15 | r15 in user mode: the user stack pointer (USP) |
| 16–19 | EPC, CAUSE, BADADDR, VBR (control registers kept here) |
| 20–27 | T0–T7: the microcode's scratch registers |
| 28 | the sink: a write to r0 lands here |
| 31 | r15 in supervisor mode: the supervisor stack pointer (SSP, D6) |

**Two copies** (D58), each four **CY7C199** (32 K × 8, 15 ns; only five
address bits used): copy A drives bus A, copy B drives bus B. Both are
written together with the same data. Together they give **two read ports
and one write port** from single-ported SRAM.

**The port addresses.** For each port (A, B, write) two 74HCT153s pick an
IR field: field A (IR[23:20]), B (IR[19:16]), C (IR[15:12]) or 15 (SP),
under `RSA` / `RSB` / `RSW`. Then a GAL (**REGA, REGB, REGW**) makes the
5-bit physical address:
- **the bank rule**: field value 15 in supervisor mode becomes 31 (SSP);
- or the microword's **constant** (`RCONST`: EPC, a scratch register …);
- or the **CR table's** address (`MFCR` / `MTCR` of a CR kept in the file);
- and for the write port only, register 0 becomes the **sink, 28**. r0
  stays 0 without any gating.

**The late write.** A result is on R at the end of its cycle, but the
register file is written **in the next cycle's first three phases**:
1. At the commit, a 74HCT574 latches the write address and its enable
   (`RDST = REG`), and eight 74HCT574s (four per copy) latch R itself.
2. In phases 0–2 (`WPH` high), 74HCT157s switch both copies' address lines
   from the read address to the latched write address. The SRAMs' outputs
   are off (`-OE = WPH`), and the latched data drives their data pins.
3. `-WSTB` (phase 1) ORed with the latched enable makes `-WE`: the write
   happens in the middle of the window, with the address steady on either
   side of it.
4. In phases 3–7 the address lines carry the read addresses. The SRAMs
   drive their 74HCT541s onto A and B, which GAL ABDEC enables outside
   `WPH`.

So a register written by one word and read by the next one **needs no
bypass**: the write happens before that cycle's reads begin. The cost is
that reads start at phase 3 (125 ns into the cycle), and that is what
limits the clock (§17).

## 8. The PC

The PC is bits 31:2 (instructions are word-aligned): **eight 74HCT161
counters**, a carry chain for `PCINC` (PC += 4 at the commit), and a load
from R for `RDST = PC` or `PC+MAR`. Four 74HCT541s put it on A (bits 1:0
as 0) for branch targets, `CALL`'s return address and EPC.

The MMU / data card keeps a **shadow PC** that loads and counts with this
one, so the next fetch's address (`MARPC`: MAR ← PC) costs no cycle on R.

## 9. The immediate unit

Six GALs (**IMM0–5**) drive B from the IR, under `IMM`, when `BSRC = IMM`:

| Mode | Value on B |
|---|---|
| `SEXT16` | IR[15:0] sign-extended |
| `ZEXT16` | IR[15:0] zero-extended |
| `LUI` | IR[15:0] << 16 |
| `BR` | IR[15:0] sign-extended, × 4 (a branch offset) |
| `J` | IR[23:0] sign-extended, × 4 (a jump offset) |
| `FOUR`, `ZERO` | 4, 0 |
| `INC8` | IR[7:0] sign-extended (post-increment, D59) |

Each GAL covers a group of B's bits and has as inputs only the IR bits that
can reach them, generated from `imm_value` (the same function `p32uemu`
uses).

Two more R sources use the IR: **LUI** puts IR[15:0] on R[31:16] (two
74HCT541s, with zero fill below), and the **zero fill** (four 74HCT541s
with grounded inputs) fills the bytes of R that a narrower source (CAUSE,
STATUS, IPEND, IMASK, an 8-bit immediate) does not drive. **GAL RDEC**
decides which bytes.

## 10. The address adder (D59)

Loads and stores compute their address in the cycle the base register is
read, without the ALU. That makes a load three cycles, not four.

- **The offset**, `AOFF`:
  - `SEXT16`: the 16-bit offset, IR[15:0], through four 74HCT257s, with
    IR[15] spread to bits 31:16 by two 74HCT541s;
  - `OFF8`: the 8-bit offset IR[15:8], through the same 74HCT257s
    (post-increment, D59);
  - `INDEX`: B shifted left by IR[1:0] (scale 1, 2, 4, 8), from four GALs
    **IDX0–3**.
- **The adder**: eight **74F283** (4-bit, fast carry) add A and the offset.
- **Onto R**: four 74HCT541s when `RSRC = AGU`. Usually `RDST = MAR`, so
  the address goes to the MMU / data card at the commit.

## 11. The comparator (D58)

Fourteen **74HCT85** magnitude comparators in a tree compare A with B
directly, without waiting for the ALU:
- eight compare bits 3:0 … 31:28 (A > B, A < B per nibble);
- one more compares bits 31:28 with both sign bits inverted (GAL ABDEC
  inverts them), for the signed result;
- two compare the eight nibble results in two halves (bits 15:0, 31:16),
  and one more does the signed upper half;
- the last two give **`CMPEQ`**, **`CMPLTU`** (unsigned) and **`CMPLT`**
  (signed).

Branches use them through GAL COND (§5). So does `SLT`, which latches them
as flags.

## 12. The MDR and the store data

- **The MDR** (four 74HCT377s) loads at the commit from R or, with `MDRB`,
  from B, through eight 74HCT157s. A store's data is usually register `rd`
  on B, loaded in the same cycle as the address.
- **Store data onto R** (`RSRC = MDRS`): the bytes are **replicated by
  size** so that each byte lane of the bus carries the right byte whatever
  the address. Lane 0 is always MDR[7:0]. Lane 1 is MDR[7:0] for a byte,
  else MDR[15:8] (74HCT257). Lane 2 is MDR[7:0] for a byte or halfword,
  else MDR[23:16]. Lane 3 is MDR[7:0], MDR[15:8] or MDR[31:24] by size
  (74HCT253). The MMU / data card's byte enables then pick the lanes that
  are written.

## 13. The ALU module (D21)

The ALU is an FPGA on a **Tang Nano 20K** module, at 3.3 V behind 74LVC
latches (5 V tolerant):
1. At `ACLK` (an ALUGO word's commit), **eight 74LVC574s** take A and B,
   and one more takes the 5-bit function and **toggles GO** (a 74HCT14
   inverter feeds GO's complement back into its own D input).
2. The FPGA sees GO change. It reads the four operand halves over its
   16-bit **FD bus**, enabling one latch pair at a time (`SELAL_N`,
   `SELAH_N`, `SELBL_N`, `SELBH_N`), computes, and writes the result halves
   into **four 74LVC574 result latches** (`CKLO`, `CKHI`).
3. The result latches drive R when `RSRC = ALU`, in the next cycle or
   later. A division takes 18 FPGA clocks, about 167 ns, so even the
   slowest result is ready well inside the next 333 ns cycle.

The module's reset comes from `-RES` through a 74LVC244, and an LD1117
makes 3.3 V for the latches. The FPGA's side of the FD-bus protocol is
still to be written, and the module's pin map is to be checked against
Sipeed's diagram before layout.

## 14. STATUS, IMASK and interrupts

**GAL STATUS** holds S (supervisor), IE (interrupts enabled), PS and PIE
(their previous values). At the commit:
- `MTCR STATUS` writes it from R[3:0];
- **trap entry** (`MODE = TRAPENTER`) saves S and IE into PS and PIE, then
  sets S = 1 and IE = 0;
- **`RTE`** restores S and IE from PS and PIE.

Reset sets S = 1 and IE = 0. STATUS also makes **`TLBW`** for the MMU /
data card (`MODE = TLBW`) and drives `SUPER` on J3.

**GAL IMASK** holds the interrupt mask **inverted** (`IMN`: 1 = disabled),
so reset (all registers 0 in the GAL, the outputs inverted) masks
everything.

**The interrupt path:**
1. A **74HCT574 synchroniser** samples `-IRQ0–7` at `-CLK`'s rise (phase
   4), so an asynchronous request can never be seen half-changed.
2. Three 74HCT32s OR each synchronised line with its `IMN` bit: a low
   output means "pending and enabled".
3. A **74HCT148** priority encoder gives `ANYP_N` (something is pending and
   enabled) and the **lowest-numbered** such level (IRQ0, the timer, is the
   highest priority).
4. At an `END` word with IE set, GAL EVENTS raises **`GOINTR`**, and the
   next microaddress is the INTR sequence.

**The cause latch** (a 74HCT377 with a 74HCT153 mux) loads at trap or
interrupt entry. It takes one of four sources: the microcode's `RAISE`
(SYSCALL, BREAK, …), the MMU / data card's fault cause (`FC`), the decode
trap's cause (ILLEGAL / PRIVILEGE), or the interrupt level, with bit 4
marking an interrupt (CAUSE bit 31). It drives R for the trap sequence's
first word, which stores it in the CAUSE register (address 17). Further
74HCT541 / 540 buffers put CAUSE, STATUS, IPEND and IMASK on R for `MFCR`.

**The interrupt sequence** (INTR, six words) puts CAUSE in MAR and saves
the PC in EPC. It runs the **acknowledge cycle** (`MEM = IACK`, D57): the
MMU / data card puts `-IACK` on the bus, and the card driving that level
answers with its vector, which comes back on R in CAUSE bits 15:8 and goes
into the CAUSE register. Then BADADDR = 0, PC = MAR = VBR, and trap entry
switches STATUS. **The trap sequence** is the same without the acknowledge
cycle, with EPC chosen by whether the fault was in the fetch, and BADADDR
from the MMU / data card.

## 15. R's sources and destinations

- **Sources**: two 74HCT138s decode `RSRC` into enables for the ALU, IMM,
  MDRS, CAUSE, CRHW and AGU. The MMU / data card decodes the same field on
  J3 for MEM, MAR, BADADDR and its own CRs. GAL RDEC adds LUI, the zero
  fill and which of the CPU's hardware CRs to drive.
- **Destinations**: a 74HCT138 decodes `RDST` with **`ABORT_N` as its
  enable**, so an aborted cycle writes nothing: REG (the late-write latch),
  PC, PC+MAR, IR, MDR, CRHW (STATUS, IMASK; the MMU / data card's CRs).
  74HCT08s gate the local link's `RDST`, `PCINC`, `MARPC` and `MARINC` the
  same way, and the PC's and MDR's loads.

## 16. Reset, the front panel and stepping

**GAL PANEL** (on the 24 MHz clock) turns the front panel and the events
into the clock's `HOLD`:

| Control | Effect |
|---|---|
| **RESET** button, or `PWROK` low | `RESET` → `-RES` on the backplane; the sequencer goes to the RESET sequence ([boot](boot.md) §1–2) |
| **RUN / STEP** switch | RUN: free running. STEP: `HOLD` except when STEP allows a commit |
| **STEP** button | MICRO mode: one commit (one microcycle). INSTR mode: commits up to the next instruction boundary (`NBOUND`, from GAL NEXT) |
| **INSTR / MICRO** switch | what one STEP press does |
| HALT instruction | `HALTED` latched at its commit; `HOLD` until reset |
| WAIT with nothing pending | `WAITHOLD` from EVENTS: the WAIT word repeats until an enabled interrupt arrives |
| **RUN** LED | lit while the clock commits (not `HOLD`) |
| **HALT** LED | lit after a `HALT` |

The buttons are debounced by an RC and a 74HCT14 Schmitt trigger.

## 17. Three instructions, cycle by cycle

Each row is one 333 ns microcycle. "At the commit" is the `CCLK` edge that
ends it.

**`ADD r1, r2, r3`** (3 cycles, 1 µs):

| Cycle | Word | What happens | At the commit |
|---|---|---|---|
| 1 | FETCH | `MEM = FETCH`: the MMU / data card reads the word at MAR, and it arrives on R. The opcode mux takes R[31:24] for the next microaddress. | IR ← R; PC += 4 (and the shadow PC) |
| 2 | step 1 | A = r2 (field B), B = r3 (field C); `ALUGO`, function ADD. DECODE checks the opcode (legal, no stray bits). | the ALU latches A, B, ADD; the FPGA starts |
| 3 | step 2, END | R = the ALU's result; `RDST = REG`, write port = field A (r1). `MARPC`: MAR ← the PC. No interrupt pending. | the write latched; MAR = PC; next microaddress: FETCH |
| (4) | the next FETCH | phases 0–2: r1 written | |

**`LW r1, 8(r2)`** (3 cycles):

| Cycle | Word | What happens | At the commit |
|---|---|---|---|
| 1 | FETCH | as above | IR, PC |
| 2 | step 1 | A = r2; the address adder adds the 16-bit offset 8; `RSRC = AGU`, `RDST = MAR` | MAR ← r2 + 8 |
| 3 | step 2, END | `MEM = LOAD`, `SIZE` = word: the MMU / data card translates MAR, runs the bus cycle and puts the word on R; `RDST = REG` (r1); `MARPC` | r1's write latched; MAR = PC |

If the load **faults** (a TLB miss, say), the MMU / data card raises
`FAULT` in cycle 3. `ABORT_N` cancels the cycle's register write and its
`MARPC`. The cause latch takes the fault's cause, and the next microaddress
is the TRAP sequence. EPC will be this instruction, so it runs again after
the handler.

**`BEQ r1, r2, label`, taken** (4 cycles; 3 when not taken):

| Cycle | Word | What happens | At the commit |
|---|---|---|---|
| 1 | FETCH | as above | IR, PC |
| 2 | step 1 | A = r1, B = r2; `ALUGO` SUB; `COND = EQ`. The comparator's **live** EQ goes to the flash's address bit 0, so step 2's two words are chosen now. | flags latched; the next word is step 2's "true" word |
| 3 | step 2 (true) | A = the PC, B = the branch offset (IMM `BR`); `ALUGO` ADD | the ALU starts PC + offset |
| 4 | step 3, END | R = the ALU; `RDST = PC+MAR` | PC = MAR = the target |

Not taken, step 2's "false" word is `MARPC`, `END`: three cycles.

## 18. Timing

At 3 MHz a cycle is 333 ns. The estimates below are from typical part
delays and are **to be checked against the datasheets** (the simulator has
zero delay):

| Path | Approximate time |
|---|---|
| the register file's read address valid | phase 3, 125 ns |
| A and B valid (SRAM 15 ns + 541) | ≈ 165 ns |
| the address adder (74F283 chain) onto R and to MAR's set-up | ≈ 275 ns |
| **the comparison → COND → the flash (55 ns) → the pipeline register's set-up** | **≈ 315 ns: the tightest path** |
| a fetch: the MMU / data card's data on R by ≈ 220 ns → the opcode mux → the flash | fits |

5 MHz (200 ns) does not fit this design as it stands. It would need the
reads to start earlier (a separate write port), faster parts on the
condition path, and translation moved earlier (backlog).
