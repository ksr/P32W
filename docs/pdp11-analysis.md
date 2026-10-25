# A PDP-11/23 on P32W hardware — analysis

Status: **an idea, parked** (Ken, 2026-10-14): analysis only. Not a decision; nothing is
built. Question: could this hardware, or something very close to it, be a
PDP-11/23 clone that runs the 11/23's operating systems and their software?

## Short answer

Yes, and P32W is well placed for it. P32W is **microcoded**, as the 11/23
was (the F-11 chipset ran PDP-11 microcode on a general datapath). The
natural route is a **second microcode set** on the same backplane, memory,
disk and I/O cards, with **variants of three parts**:

- the **CPU card's decode and sequencer**: PDP-11 instruction decode, a
  sequencer that can chain operand sequences;
- the **MMU / data card**: PDP-11 relocation (PAR / PDR), not a TLB;
- the **ALU FPGA**: 8- / 16-bit condition codes, the PSW flags, PDP-11
  MUL / DIV / ASH / ASHC / SWAB / SXT.

The hard parts are not the instruction set. They are the PDP-11 MMU's
64-byte relocation, DMA disk controllers, and making the devices look
exactly like DEC's.

## Three ways to do it

| | What | Verdict |
|---|---|---|
| **A** | PDP-11 microcode on P32W's datapath (other control-store images, some card variants) | **Recommended**: "this hardware or very close" |
| B | A PDP-11 emulator in software, under P32W Unix (SIMH-style) | Easy but ~50–100 P32W instructions per PDP-11 instruction: thousands of PDP-11 instructions a second. Too slow. |
| C | A PDP-11 core in the Tang Nano 20K (open cores exist, e.g. pdp2011) | Works, but the TTL becomes a backplane. A reference, not the goal. |

## What already fits

- **Register file:** 32 × 32-bit with a banked stack pointer. PDP-11 needs
  R0–R7 and separate kernel / user R6 — the same idea as P32W's banked r15
  (the bank rule moves from 15 to 6). T0–T7 are microcode scratch.
- **Memory:** byte addressing, byte enables, byte / halfword load steering.
  16-bit words and byte operations map directly; the PDP-11 odd-address
  trap is P32W's MISALIGNED.
- **22-bit addressing** (4 MB) fits P32W's RAM.
- **Traps and interrupts:** microcode pushes PS and PC and loads the new PC
  and PS from a vector (4, 10, 14, 20, 24, 30, 34, 250, devices). P32W's
  eight IRQ lines map to vectors and priorities through a small ROM.
- **T-bit trace trap:** a test at the end of each instruction.
- **The tools:** `p32ucode.py` and `p32uemu` (the emulator runs the real
  flash images) are what a second microcode set needs.

## What changes

| Area | P32W today | PDP-11 needs | Change |
|---|---|---|---|
| **Decode** | dispatch on IR[31:24]; 16 steps per instruction | 16-bit instructions, irregular layout, 8 addressing modes *per operand* (up to two memory indirections each) | a decode ROM from the instruction word to an entry; a sequencer that chains source-operand → destination-operand → execute sequences (microcode subroutines). **The biggest change on the CPU card.** |
| **PC** | a +4 counter, not in the register file | R7 *is* the PC (`(PC)+` immediates, PC-relative modes), +2 | a +2 counter variant; register 7 aliased to the PC |
| **r0** | always 0 (write sink) | an ordinary register | the zero rule off in PDP mode (a gate in the CPU card's decode) |
| **Condition codes** | none: branches compare registers | PSW N Z V C at **byte or word** width, set by most instructions | **in the FPGA:** 8 / 16-bit flag modes, the flags held inside, a "branch taken" output. The FPGA ALU (D21) pays off; its pins are tight — to check. |
| **MMU** | 4 KiB-page TLB, refilled by software | 8 × 8 KB pages per mode; PAR = base in **64-byte units**; PDR = length in 64-byte blocks, expansion direction, access | **The main obstacle.** OSes place pages on 64-byte boundaries (Unix allocates in 64-byte "clicks"), which 4 KiB frames cannot represent. A real relocation unit: a small PAR / PDR file indexed by mode and VA[15:13], a 22-bit adder, a length comparator — ~15–20 chips, an **MMU / data card variant**. (In microcode it would cost cycles on every access.) |
| **I/O** | P32W UART, timer, RTC, CF | DEC registers at DEC addresses: DL11 console (177560), KW11-L clock (177546), a disk controller (RL11 / RLV12 for RL02, or MSCP) | **The other hard part.** DL11: a UART with a GAL rearranging the status bits. **DEC disk controllers are DMA bus masters.** P32W's bus has bus masters too (radial `-BREQ` / `-BGNT` to the arbiter, D50), so a Pico-based device card can emulate the controllers over an SD / CF image and do the transfers by DMA. |
| **Console** | the monitor ROM | console ODT in microcode; a boot ROM | ODT in microcode or ROM; a small boot ROM |
| **Floating point** | none | the optional KEF11 (FP11 instructions, six 64-bit accumulators) | optional, as on the real 11/23; RT-11 and RSX run without it; V7 Unix's `cc` uses FP11 for `float`. Later, in the FPGA. |

## Software

- **Operating systems** that ran on the 11/23: RT-11 (SJ / FB / XM),
  RSX-11M and M-Plus, RSTS/E, Unix V6 and V7, Ultrix-11, 2.9BSD.
  (2.11BSD prefers separate I and D space, the 11/44 / 11/73 class — to
  check whether it runs on an 11/23.)
- **Licences:** the old Unix versions are free under Caldera's 2002
  licence; DEC's own systems have historically been available under
  hobbyist licences — Ken's call.
- **Disk images** for all of them exist for SIMH, mostly RL02 and MSCP: the
  emulated disk controller decides what boots. RL02 is the simplest; MSCP is
  more work and more common.
- **Checking:** SIMH's PDP-11 (set to an 11/23) as the reference,
  instruction by instruction (as `p32uemu` against `p32emu`); DEC's XXDP
  diagnostics for the KDF11 to test the CPU as DEC did.

## Speed (an estimate, to be measured)

P32W averages ~6 microcycles per instruction at a 3–5 MHz clock (D50). A
PDP-11 register-to-register `MOV` would be ~4–5 cycles; memory modes add
~2–3 per operand. Roughly **0.3–0.5 million PDP-11 instructions a second
— the same class as a real 11/23**, with the MMU in hardware.

## A path, if taken up

1. A decision (D-number): the target (11/23, 22-bit, no FPU first), the
   devices, dual-mode cards or separate "P11" card variants.
2. **Emulator first:** a PDP-11 microcode set in `p32uemu` over a card
   model with the PDP-11 MMU and devices; checked against SIMH; XXDP.
3. Boot Unix V6 / V7 and RT-11 from an emulated RL02.
4. Hardware variants last: the CPU card's decode / sequencer, the MMU /
   data card, the FPGA bitstream, a device card.

Steps 1–3 need no PCB work.
