# The CPU card

**Status:** NETLIST DONE AND VERIFIED (2026-10-26); the board routed (D83: 0
unconnected, 0 DRC violations; `backlog.md`, "The board layouts"). Slot 1,
9U × 340 mm (D49); every part through-hole (D21); 6 layers (D77); the
layout rules of D48 and D55. The design: D56 (the three-bus datapath), D58
(its choices), D59 (the address adder), D61 (this card). The
netlist is `generators/card_cpu.py`; the card runs in the gate-level
simulator through `generators/sim_cpu.py`.

How it works, block by block: [the theory of operation](cpu-card-theory.md).

**195 ICs, 35 of them GALs** (ATF22V10C), 9 flash chips, 8 SRAMs, the ALU
module; 429 parts in all (`python3 generators/card_cpu.py --bom`).

**Faster parts on the critical paths (D72):** to meet 3 MHz at the
datasheets' worst case (docs/timing.md), the register file's address muxes
(74ACT157), write latches and the pipeline register (74ACT574), its read
buffers and the address adder onto R (74ACT541), IR (74ABT377: no 74ACT or
74F 377 is made in DIP), the opcode mux (74F153), R's destination decode
(74F138), the local link's gates (74ACT08), the pending-interrupt ORs
(74ACT32) and the `-IRQ` synchroniser (74ACT574); the comparator in GALs.
Same pinouts as the 74HCT parts they replace.

## 1. The cycle

A 24 MHz oscillator; the GAL **CLKGEN** counts **eight phases** a microcycle
(3 MHz). Every clock on the card is a register output of CLKGEN on the same
oscillator edge, so none can glitch.

**The clock jumper (D74).** A second oscillator, 32 MHz, in a socket; GAL
**CKSEL** puts one of the two on OSC (D76, below). The **CLOCK** jumper (JP1, at a
board edge, labelled `3 MHz` open / `4 MHz` fitted) selects it and pulls the
bus line `-FAST` low, so the I/O card's timer divides by 4 instead of 3 and
still ticks at 1 MHz. 3 MHz is guaranteed over the datasheets' worst case;
4 MHz meets every path at the parts' typical delays with about 20% in hand
([timing.md](timing.md)), which most boards at room temperature will run,
but which is not guaranteed. Move the jumper only with the power off: a
change while running could put a short pulse on OSC.

**Stepping the clock itself (D76).** Besides STEP (one microcycle or one
instruction, the oscillator running on), the machine's clock can be
stopped and advanced **one oscillator edge at a time: one of the eight
phases** a press, so `CLK`, `CCLK`, `-WSTB` and every bus cycle can be
watched phase by phase. GAL **CKSEL** (the -7 grade) picks OSC:

| Source | When |
|---|---|
| the debug card's `XCLK` (bus, J2) | `-XCLKEN` low: the debug card's host asks (CTRL1 bit 7): its 12 MHz (a 1.5 MHz machine), or one edge per host STEP |
| the **CLOCK STEP** button | the **CLOCK** switch at STEP (front edge, labelled RUN / STEP at its ends) |
| the 32 MHz oscillator | the speed jumper fitted (`-FAST` low) |
| the 24 MHz oscillator | otherwise |

The CLOCK STEP button is debounced as RESET and STEP are (an RC, 10 ms,
into a 74HCT14), so a press is one clean edge. CKSEL is a plain
multiplexer: **change the source with RESET held**; a change in the middle
of a phase can cut it short. With no debug card, the card's own resistors
hold `XCLK` low and `-XCLKEN` high. Everything is static, so the machine
waits indefinitely between edges; the devices with their own clocks (the
video card, the UARTs' baud rate, the clock chip, the USB modules, the
Pico, the drives) run on in real time, which every interface allows (they
wait or are polled), but a UART receiving at full speed will overrun.

| Phase | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| `CLK` (the bus clock, J2) | high | high | high | high | low | low | low | low |
| `CCLK` (the commit, J3) | rises at 0 unless held | | | | falls | | | |
| register file | write address | **write** (`-WSTB`) | write address | read | read | read | read | read |
| `-IRQ` synchroniser | | | | | samples (`-CLK` rises) | | | |

- **One commit edge.** At phase 0's start `CCLK` rises: the pipeline
  register takes the next microword and every destination takes R (IR, PC,
  MDR, STATUS, IMASK, the cause latch, the flags; on the MMU / data card
  MAR and its shadow PC). `ACLK` rises with it in an ALUGO word: the ALU's
  operand and function latches.
- **The next microaddress is worked out during the cycle**, from the
  cycle's own signals, so the microcode's meaning ("the condition on the
  state after the edge") holds with one edge: a branch is tested on the
  **live** comparison of A and B in the word that latches them, and on the
  latched flags after; the opcode of step 1 comes from R while the
  instruction is being fetched (the opcode mux); a fault, a decode trap or
  an interrupt picks the internal sequence.
- **The register file is written late:** R, the write address and the
  enable are latched at the commit and written in phases 0–2 of the next
  cycle (the strobe in phase 1, the address steady either side), before
  that cycle's reads (phases 3–7). A register written by one word and read
  by the next needs no bypass.
- **Held cycles:** `STALL` (the MMU / data card: arbitration, `-WAIT`), WAIT
  with nothing pending, HALT, the front panel's STEP mode: no `CCLK` edge;
  the cycle repeats with the same word, and its late write again (the same
  data to the same register).
- **Timing at 3 MHz** (333 ns; 74HCT ≈ 15–25 ns a level, flash 55 ns, SRAM
  15 ns, 74F283 ≈ 8 ns a stage), *estimates to check against the
  datasheets*: reads begin at phase 3 (125 ns); A and B ≈ 40 ns later; the
  address adder to MAR ≈ 110 ns more (≈ 275 ns); the comparison through the
  condition, the flash and the pipeline register's setup ≈ 150 ns more
  (≈ 315 ns: the tightest path). The fetch's opcode path (memory data on R,
  the opcode mux, the flash) fits if the MMU / data card has the data back
  by ≈ 220 ns. **5 MHz does not fit this card as designed**: it needs the
  reads to start earlier (a separate write port), faster parts on the
  condition path and translation moved earlier (backlog).

## 2. Blocks

| Block | Parts | ICs |
|---|---|---|
| **Clock and control** | 24 MHz oscillator, a 32 MHz one in a socket, GAL CKSEL picking OSC from them, the CLOCK STEP button or the debug card's XCLK (D74, D76); GAL CLKGEN; GAL PANEL (reset from PWROK and the RESET button, RUN / STEP, MICRO / INSTR stepping, HALT, the LEDs); 74HCT14 (debounce, the ALU's GO toggle) | 6 |
| **Control store** | 7 × SST39SF010A (the 56-bit microword, D59), 7 × 74ACT574 pipeline register | 14 |
| **Sequencer** | 74HCT574 microaddress register (sequence, step, internal sequence id); GAL NEXT (the next step and sequence, the opcode source); 4 × 74F153 opcode mux (IR, R, the sequence id); GAL COND (the condition: live or latched compare, CR table, fault flags); GAL EVENTS (abort, trap / interrupt entry, ALUGO's commit, WAIT, HALT, the cause source); 74HCT138 step decode | 9 |
| **Instruction unit** | 4 × 74HCT377 IR; 2 × SST39SF010A decode tables (opcode: legal, privileged, zero class, size, signed; CR: illegal, hardware, address); 4 × 74HCT4002 must-be-zero groups; GAL DECODE (the decode trap, D18) | 11 |
| **Register addresses** | per port (A, B, write): 2 × 74HCT153 (the IR field A / B / C or SP) and a GAL (the bank rule, the constant, the CR table's address; the write port's r0 sink) | 9 |
| **Register file** (two copies, D58) | 8 × CY7C199; 74HCT574 write address and enable; 4 × 74HCT157 read / write address; 8 × 74HCT574 write data; 8 × 74HCT541 onto A and B; a 74HCT32 gate (`-WE`) | 29 |
| **PC** | 8 × 74HCT161 (bits 31:2: count, load from R); 4 × 74HCT541 onto A | 12 |
| **Immediate unit** | 6 GALs IMM0–5 onto B (sext16, zext16, LUI, branch, jump, 4, 0, inc8) | 6 |
| **Address adder** (D59) | 4 × 74HCT257 and 2 × 74HCT541 (the IR's 16- or 8-bit offset); 4 GALs IDX0–3 (B << IR[1:0]); 8 × 74F283; 4 × 74HCT541 onto R | 22 |
| **Comparator** (D58, D72) | GALs CMP0–CMP7 (each nibble's GT and LT; CMP7 also signed), CMPH (bits 16–31), CMPF (bits 0–15, then EQ, LTU, LT): three GAL levels, about 40 ns where the 74HCT85 tree took 150 | 10 |
| **MDR** (store data) | 8 × 74HCT157 (from B or R), 4 × 74HCT377; onto R replicated by size: 74HCT541, 4 × 74HCT257, 4 × 74HCT253 | 21 |
| **ALU** (D21) | Tang Nano 20K; 8 × 74LVC574 operands, 74LVC574 function + GO, 4 × 74LVC574 result onto R; 74LVC244 (its reset); LD1117V33 | 16 |
| **Status and interrupts** | GAL STATUS (S, IE, PS, PIE; TLBW); GAL IMASK; 74ACT574 `-IRQ` synchroniser; 3 × 74ACT32 (pending); GAL ANYP (any pending, D72); 74HCT148 priority (the level's number); 2 × 74HCT153 cause mux; 74HCT377 cause latch; onto R: 3 × 74HCT541 (CAUSE, STATUS), 2 × 74HCT540 (IPEND, IMASK) | 15 |
| **R's other sources and destinations** | 2 × 74HCT138 (RSRC); GAL RDEC (LUI, zero fill, CPU CRs); 4 × 74HCT541 zero fill; 2 × 74HCT541 LUI; GAL ABDEC (A / B enables, size, sign bits); 74HCT138 (RDST, gated by the abort); 2 × 74HCT08, 74HCT00 (the local link's gated lines, PC and MDR loads) | 14 |
| **Front panel** (front edge, labelled, D48) | RESET and STEP buttons, RUN / STEP and INSTR / MICRO switches, RUN and HALT LEDs; the CLOCK switch (RUN / STEP) and CLOCK STEP button (D76); the POWER LED | — |
| **Pull-downs** | 12 × 10k networks on R, A, B | — |

**The abort:** reset, a decode trap (step 1) or a fault from the MMU /
data card suppresses the cycle — R's destination decoder is disabled,
the local link's RDST, PCINC, MARPC and MARINC are gated off, ALUGO's
latches don't clock, MODE has no effect — and the sequence goes to trap
entry with the cause latched.

**The local link (J3)** carries R itself (`LR0–31`, the card's R bus),
RSRC, RDST (gated), MEM, SIZE / SIGNED (from the opcode table), CRSEL,
SUPER, TLBW, `-WSTB`, LOCKREQ, CCLK, PCINC / MARPC / MARINC (gated), and
back FAULT, FC and STALL.

**"A CPU card is in": `-CPUIN`** (J3 B28, D102). The pin is tied to the
card's ground plane, with no part. On the backplane the line runs to the
debug card in slot 0, which pulls it up: low there means this card is in
its slot, and the debug card then refuses to make `CLK`, `-RES` or the
local link's lines itself (debug-card-theory §9). Nothing on this card
reads it. J3 B30 is the MMU / data card's line, `-MMUIN`, not connected
here.

## 3. The ALU module (D21)

The operand latches take A and B at `ACLK`; the function latch takes the
5-bit function and toggles **GO**. The FPGA sees GO change, reads the four
operand halves over its 16-bit `FD` bus (one latch pair's outputs at a time,
`SELAL_N` … `SELBH_N`), computes, and writes the result halves into the
result latches (`CKLO`, `CKHI`), which drive R when RSRC = ALU — well
inside the next microcycle at 3 MHz (a division, 18 fclk = 167 ns, D21).
The module's 34 GPIOs: 29 used. **Which module pin is which GPIO is to
verify against Sipeed's pin diagram before layout** (the netlist's pads are
provisional); the FPGA's side of this protocol is still to be written
(`fpga/alu`: the core exists).

## 4. Verification

- **Every GAL** is generated from a behavioural spec (`minimize`, or a hand
  equation for the wide ones) and checked exhaustively against it, with
  the 22V10's product-term limits (`p32gal.py`).
- **The netlist** passes the electrical checks (`p32net.check`: every pin
  connected or declared unused, no floating input, no two drivers, power).
- **The card as wired runs programs** (`generators/sim_cpu.py`): every
  chip simulated, the real GAL equations, the control store and decode
  tables in its flash chips; the MMU / data card, memory and the devices
  are `p32uemu`'s own model (`emulator/p32busmodel.c`) on J3. All 17 test
  programs of the cross-check (`hello`, `echo`, `isa_test`, `d59_test`,
  `fib`, `primes`, `trap_test`, `kernel_hello`, `kernel_fault`,
  `irq_test`, `wait_nowake`, `mt_demo`, `vm_test`, `vm_demo`, the
  exception tests) run with **the state at every instruction identical to
  `p32emu`'s and the cycle count identical to `p32uemu`'s**; `make test`
  runs five of them. About 300 microcycles a second: too slow to boot Unix.
- **Not simulated:** timing (the simulator is zero-delay; the static timing
  pass, [timing.md](timing.md) (D71, D72), times the card against the
  datasheets), the FPGA's FD-bus protocol,
  STALL from a real MMU / data card.

## 5. Next

The schematic and the board (the `p32kicad_*` generators, placement by the
layout rules, 4 or 6 layers), the module's pin map, the FPGA's bus
protocol; then the MMU / data card, designed the same way, against the
same model.
