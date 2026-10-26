# The debug card (bus tester)

**Status:** DESIGN AGREED (2026-10-26, §7 decided); netlist to do. D50 (the
host link, DMA with the CPU halted, a fast clock), D64 (listen or drive,
never both; every signal; the dedicated slot 0).

## 1. What it is for

Bring-up and test, in two modes selected by a switch at its front edge:

- **LISTEN**: a logic analyzer on **every backplane signal, the CPU's
  local link (J3) included**. Every one of its drivers is held off, by
  wiring and not by software, so it can never disturb a running machine.
- **DRIVE**: it is the machine's master. It runs bus cycles and sets every
  line a card other than the CPU card reads:
  - **with the CPU and MMU / data cards present**, it borrows the bus by
    DMA (slot 0's own request line, the arbiter's highest priority): it
    loads programs, reads and writes memory and devices, with the CPU
    halted or running;
  - **with them pulled**, it also makes `CLK`, `-CLK` and `-RES`, so a
    memory, I/O, disk or video card can be tested alone. It plays the
    other masters' `-BREQ` lines to test the arbiter, and plays the CPU
    card's side of J3 (R, `RSRC`, `RDST`, `MEM`, `SIZE`, `CCLK`, `-WSTB` …)
    to test the MMU / data card alone, reading back `FAULT`, `FC`, `STALL`
    and R.

A Pico 2 is the **host link**: USB to the Mac, a small command set
(read / write memory, run a bus cycle, arm a capture, read it back).

## 2. Slot 0 (D64)

An eleventh backplane position beside slot 1, wired to:
- **J1**: the whole bus cycle (70 lines);
- **J2**: the system lines (`CLK`, `-CLK`, `-RES`, `PWROK`, `-LOCK`,
  `-IRQ0–7`, `-IACK`), the eight radial `-BREQ3–10` / `-BGNT3–10` pairs as
  slot 2 sees them, and **its own pair**, `-BREQ0` / `-BGNT0`, to the
  arbiter;
- **J3**: the local link (62 lines), a third drop on lines that today run
  only between slots 1 and 2.

**162 signals in all.** This needs two changes elsewhere, on cards still
netlist-only (none built or ordered):
- **the backplane** (its generator): the eleventh position, J3 and the
  fan-in carried to it;
- **the MMU / data card's arbiter**: a ninth requester, slot 0, at the
  highest priority. GAL ARB grows from 9 outputs to 10, its limit.

## 3. LISTEN: capture

- **What is sampled:** all 162 lines, into **21 × 74HCT574** input
  latches. The sample clock is one of:
  - **`CLK`'s rise**, one sample per bus cycle (and per microcycle: `CCLK`
    is the same edge), the state at the cycle's end. This is the normal
    mode, and its samples read like the emulators' traces.
  - **the card's own fast oscillator** (24 MHz: eight samples per
    microcycle), a timing view for glitches and set-up problems.
- **Storage:** 21 SRAM chips side by side, one 8-bit slice each, with a
  counter for the address: a circular buffer that keeps running until
  the trigger, then stops after a chosen number of samples (pre- and
  post-trigger).
- **The trigger:** a pattern in **trigger RAM**. The address lines and the
  control lines each address an SRAM whose stored bit says "this value
  matches". The host fills it, so any mask or range of values costs no
  extra logic: 8 independent trigger patterns, combined by a GAL with
  qualifiers (a read, a write, an acknowledge cycle, no `-ACK`, `FAULT`, an
  interrupt line). It can also simply start and stop by command.
- **Read-out:** after the capture, the Pico reads the SRAMs slice by slice
  over the card's local bus and sends them to the Mac. A Mac tool turns
  them into a trace (`p32emu -s`'s format for the bus view, or a VCD file
  for GTKWave).

## 4. DRIVE: playing the master

- **Output registers:** **21 × 74HCT574**, whose 3-state outputs reach
  every line it may drive. The register for a line is enabled only in
  DRIVE mode, and only for the lines that mode's sub-setting allows:
  - *bus master*: A, BE, strobes, D, `-BREQ0`;
  - *system*: `CLK`, `-CLK`, `-RES`, `-BREQ3–10`, `-IRQ0–7`;
  - *CPU side of J3*.
  The mode switch's LISTEN position ties all three enables off.
- **One bus cycle at a time:** the Pico loads a cycle (address, byte
  enables, read / write, data) into the registers and says "go". A GAL
  sequencer clocked by `CLK` requests the bus (`-BREQ0`), waits for the
  grant, runs **exactly one bus cycle** in step with `CLK`, honours
  `-WAIT`, latches D and `-ACK` at the cycle's end, and lets the bus go.
  The Pico reads the result. At USB speed that is a few hundred kilobytes a
  second: a kernel loads in well under a second.
- **Alone on the bus** (the CPU and MMU / data cards pulled): the card's
  oscillator, divided, makes `CLK` and `-CLK`; `-RES` is a command. The
  cycle sequencer then needs no grant.
- **Testing the MMU / data card alone:** the Pico sets the J3 lines for one
  microcycle, pulses `CCLK` and `-WSTB` as the CPU card would, and reads
  back R, `FAULT`, `FC` and `STALL`. A microcycle at a time, so any TLB,
  fault or steering case can be checked.

## 5. The host link and the rest

- **A Raspberry Pi Pico 2** at the front edge, its USB port facing out
  (D48): 3.3 V, buffered to the card's 5 V by 74LVC245s; an 8-bit local bus
  with a few address lines and strobes to the card's registers.
- **The boot ROM, in place:** with a **PROG** jumper fitted, the drive
  mode can write the memory card's flash chips (the SST39SF020A's
  programming sequence as bus cycles). The monitor can then be replaced
  without pulling chips.
- **The card's own window** (window 4, D52): its ID register (4), and a
  mailbox, so that P32W software can talk to the Mac through it.

## 6. Size

About **110 ICs** (21 capture latches, 21 capture SRAMs, 21 drive
registers, the trigger RAMs, counters, GALs, buffers, the Pico) on a 9U
card. It is the biggest card after the CPU card, and the one that makes
every other card testable.

## 7. Decided (Ken, 2026-10-26)

1. **Capture depth: 32 K samples** in 21 × CY7C199 (15 ns), so the
   **24 MHz timing mode stays**: one sample per bus cycle by default (about
   11 ms of machine time), or eight per microcycle for timing work.
2. **Both changes on the not-yet-built cards:** the backplane's eleventh
   position (slot 0: J1, J2 with the request / grant fan-in, J3), and the
   MMU / data card's arbiter taking slot 0 as a ninth requester at the
   highest priority.
