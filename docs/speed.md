# Speeding up the CPU: where it can be done

**Status:** ANALYSIS (2026-10-27; Ken: "look where there can be changes to
speed up cpu"). Nothing here is built. The figures come from the timing
analysis (docs/timing.md, `generators/p32timing.py`) with what-if changes,
and from `tools/isamix.sh` on two recorded P32W Unix sessions.

Speed is the clock times the work done per cycle. Today: **3 MHz
guaranteed** (the datasheets' worst case, D72), about **4.9 MHz at typical
delays**, and **3.2 microcycles an instruction**: about 0.94 MIPS at
3 MHz.

## 1. The clock

What the timing analysis says each step would give (worst case: every
chip at its datasheet maximum, 85 °C; typical: 25 °C):

| Change | Worst case | Typical | Cost |
|---|---|---|---|
| As built (D72) | 3.09 MHz | 4.93 MHz | — |
| **A faster oscillator, chosen by a jumper** | — | run at 4 MHz with 20% in hand, 4.5 MHz with 10% | an oscillator socket and a jumper on the CPU card; the debug card's alone clock likewise |
| Swap every remaining 74HCT part on the failing paths for 74ACT / 74F (about 93 more chips, the device cards' decoders and the MMU / data card's muxes included) | about 3.6 MHz | — | many chips, more supply current, faster edges |
| The control store and the decode tables in 15 ns SRAM, loaded from the flash at reset | 3.09 MHz (other paths limit) | 5.50 MHz | 7 + 2 × CY7C199 and a loader (a counter and a GAL) |
| Both of the above together | 4.09 MHz | 6.44 MHz | both |
| **5 MHz at worst case** | needs the structure changed, below | | |

**Why worst case stops near 4 MHz:** one path crosses the whole machine in
one microcycle, from IR through the decode table's flash (the access size)
to the byte enables, the addressed card's decode and `-ACK`, the bus-error
decision (FAULT), the next microaddress and the control store's flash.
Two flash reads and a bus round trip don't fit 200 ns. Structural fixes:

1. **Latch the decode table's outputs with IR** (look it up during FETCH,
   from R, as the opcode mux already does): the size and the class are
   ready when the cycle starts.
2. **Decide a bus error a cycle later:** a fault latch that the next
   microcycle traps on, so `-ACK` no longer feeds the microaddress in the
   same cycle. The microcode's abort discipline (D10) has to stay exact:
   the faulting cycle's writes must still be cancelled.
3. **Translation a cycle earlier** (microarch.md 8): the TLB looked up as
   MAR loads, so the bus cycle starts with its physical address.

With those and the SRAM control store, 5 MHz is in reach at worst case;
measured, the work per cycle is unchanged, so **5 MHz is 1.67× today**.

**The cheapest step by far is the jumper:** most machines will run at
4–4.5 MHz at room temperature. `make test` can't prove that; a bench run of
the test programs at the higher clock (the debug card's listen mode shows
any late signal) would.

## 2. The work per cycle

`tools/isamix.sh` on two sessions (cycles without `WAIT`):

| Session | Instructions | Cycles an instruction |
|---|---|---|
| shell, files (session1) | 24.2 M | 3.20 |
| MicroPython (session15) | 17.5 M | 3.18 |

| Change | Cycles saved | Cost |
|---|---|---|
| **A DMA disk card** (D50: the disk card is built able to be a master) | the CPU's copy loop: `LHU` / `SH` are **12.7%** of the shell's cycles and **21.4%** of Python's (with the loop's `ADDI` / `BNE` more) | a DMA engine on the disk card, a driver change |
| **An overlapped fetch**: the next instruction fetched in an instruction's last cycle when that cycle doesn't use the bus | **11–12%** (every ALU op, `LUI`, `LI32`, a branch not taken: 3 cycles → 2) | a second path from the MMU / data card's data into IR (today the fetched word reaches IR over R, which the last cycle also uses for its result), the shadow PC ahead by one, the microcode's END words; exceptions must stay precise |
| Leaf calls with a link register (`JAL` / `JR lr`) | 1.3% | an ISA and compiler change |
| Post-increment used more by the compiler | 1.2% | compiler |
| Compare with a constant and branch | 0.7% | ISA, compiler |
| `LDM` / `STM` | 0.6% | a register-list sequencer |

The ISA additions left over from D59 together are under 4%; the DMA disk
and the overlapped fetch are the two that matter.

## 3. In order of value for the effort

1. **A clock jumper** (4 or 4.5 MHz besides 3): +33–50%, a part or two.
2. **DMA on the disk card:** 13–21% fewer cycles where the disk is busy.
3. **The control store in SRAM:** typical 5.5 MHz; a step towards 5 MHz
   worst case.
4. **The structural changes for 5 MHz at worst case** (§1, 1–3): 1.67×.
5. **The overlapped fetch:** 11–12%, the largest datapath change of these.

Together: 5 MHz with the fetch overlapped and DMA is about **2× today's
speed** (1.67 × 1.13 × about 1.1), about 2 MIPS.
