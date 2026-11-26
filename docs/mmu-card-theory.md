# The MMU / data card: theory of operation

**Status:** DESCRIBES THE NETLIST AS BUILT AND VERIFIED (2026-10-26),
`generators/card_mmu.py`. The card's summary, its parts by block and its
verification are in [the MMU / data card](mmu-card.md). The behaviour it
implements is `p32uemu`'s model of it (`translate()`, `mem_cycle()`,
`load_extract()`), which the gate-level simulation checks it against. The
architecture it serves: `isa-system.md` (virtual memory, faults), D12, D13,
D17, D51, D54, D57, D59, D62.

## 1. What the card does

The MMU / data card stands between the CPU card and the system bus. In
every microcycle it does what the CPU card's microword asks for over J3,
the local link:

- it keeps **MAR**, the memory address register, and a **shadow PC**;
- it **translates** MAR from a virtual to a physical address through the
  **TLB**, and checks the access (alignment, privilege, a TLB miss,
  protection);
- it runs the **bus cycle**: the address, the strobes and the byte
  enables on the backplane; the store data out; the load data back,
  **steered** to the bottom of R and sign- or zero-extended;
- it reports a **fault** (and its cause) in the same cycle, so the CPU card
  can abort it;
- it holds **TLBHI, TLBLO, ASID and BADADDR**, the control registers of
  the memory system;
- it runs the **interrupt acknowledge cycle** that fetches a vector (D57);
- it is the bus's **arbiter** for DMA masters, and holds the bus **lock**
  for `SWAP` (D51, D54).

It has **no clock of its own**. Its registers change at the CPU card's
`CCLK` (the commit), its arbiter at the bus's `CLK`, and its TLB is
written at the CPU card's `-WSTB`.

**With no CPU card in** nothing drives J3's controls (a test with the
debug card, or the bare backplane). RN3, 10k × 8 to ground, holds RSRC0–3,
MEM0–2 and CCLK low (D136): RSRC and MEM none, so the R source decoders
put nothing on R and no bus cycle starts, and no commit. Floating, RSRC
could have put two of the card's registers on R at once. The CPU card
drives the lines through the 10k when it is in.

## 2. The microcycle, seen from this card

| When | What happens here |
|---|---|
| **The commit** (`CCLK` rising, phase 0) | MAR, the shadow PC, TLBHI / TLBLO / ASID, BADADDR and the lock take their new values. A TLB write requested by the word just finished (`TLBW`) becomes *pending*. |
| **Phases 0–2** | A pending TLB write happens, at `-WSTB` (phase 1). |
| **During the cycle** | The new word's `MEM`, `SIZE`, `RSRC` arrive on J3. MAR goes through the TLB and the comparators; the fault GALs decide; the physical address goes onto the bus with the strobes; the addressed card answers. |
| **By the end of the cycle** | Load data is steered onto R, or `FAULT` is up with its cause. The CPU card takes R, or aborts the cycle, at the next commit. |

Everything from MAR to R happens **within one microcycle**: one memory
access per cycle, with no wait state at 3 MHz.

## 3. MAR and the shadow PC

**MAR** is bits 31:2 in **eight 74HCT161 counters**, plus bits 1:0 in **GAL
MMUCTL**. At the commit it:
- loads **from R** when `RDST = MAR` (an address the CPU computed: the
  address adder, the ALU, CAUSE for the acknowledge cycle);
- loads **from the shadow PC** when `MARPC` is set (the next instruction's
  fetch), through eight 74HCT157s. Bits 1:0 become 0. With `RDST = PC+MAR`
  (a jump), R goes to both the PC and MAR.
- **counts by 4** when `MARINC` is set (`LI32` steps from the instruction
  to its value word and past it, D59).

**The shadow PC** is a second set of **eight 74HCT161s**. It loads from R
whenever the CPU card's PC does (`RDST = PC` or `PC+MAR`) and counts with
it (`PCINC`). It is an exact copy of the PC, kept here so that "MAR ← PC"
for the next fetch needs neither a cycle nor the R bus (D56).

The CPU card gates `RDST`, `MARPC`, `MARINC` and `PCINC` with its abort, so
an aborted cycle changes neither register.

## 4. The TLB

**The organisation** (D13, isa-system.md): **split** into an instruction
half and a data half of **64 entries each**, **direct-mapped**. A virtual
page's entry is at index VPN[5:0] (MAR[17:12]) in the half the access
uses. An entry holds:

| Bits | Field |
|---|---|
| 0–19 | the VPN, MAR[31:12] (the tag) |
| 20–27 | the ASID |
| 28–47 | the PFN, the physical page |
| 48, 49, 50 | V (valid), W (writable), X (executable) |

51 bits, in **seven CY7C199** SRAMs (32 K × 8, 15 ns) side by side. Only 7
of their address lines are used: the 6-bit index and the half, `ISFETCH`.

**A lookup** happens in every cycle, whether or not the access is
translated. MAR[17:12] and `ISFETCH` (from GAL FAULTA: this is a fetch)
address the SRAMs. Their 56 outputs (the entry, `E0–E50`) feed:
- **four 74F521 identity comparators**: the entry's VPN against
  MAR[31:12], and its ASID against the ASID register. GAL LOCKG combines
  their four outputs into **`MATCH`**;
- **the physical address**: five 74HCT157s choose the entry's PFN (user
  mode) or MAR[31:12] (supervisor mode: untranslated, `SUPER` from the CPU
  card) for address bits 31:12. Bits 11:2 are MAR's, untranslated in
  either mode;
- **the fault GALs**: V, W and X.

**A TLB write** (the `TLBW` instruction) is written **late**, like the CPU
card's register file:
1. At `TLBW`'s commit, GAL MMUCTL registers **`TLBWP`**, a write pending.
2. In the next cycle, two 74ACT157s switch the SRAMs' index from MAR to
   TLBHI's (bits 17:12 and the I bit, 8). The SRAMs' outputs turn off
   (`-OE = TLBWP`), and seven 74ACT541s drive the new entry from TLBHI and
   TLBLO onto their data pins.
3. `-WSTB` (the CPU card's phase-1 strobe) ORed with `TLBWPN` makes `-WE`:
   the write happens in the middle of the window.

The cycle after a `TLBW` is always the next instruction's fetch in
supervisor mode (`TLBW` is privileged and ends its instruction). That
fetch is untranslated and does not need the TLB, so borrowing the SRAMs
for one cycle costs nothing.

## 5. Faults: translate(), in three GALs

The checks are made in the order `p32uemu`'s `translate()` makes them, and
`check_faults()` proves that for every combination of inputs.

**GAL FAULTA** decodes the access from `MEM`, `SIZE`, MAR[1:0], MAR[31] and
`SUPER`:

| Output | Meaning |
|---|---|
| `MIS` | **misaligned**: a halfword at an odd address, a word not at a multiple of 4 (any mode, any kind of access) |
| `PRV` | **privilege**: a user-mode access to the upper half, `0x8000_0000` and above (device registers, the kernel's addresses) |
| `XL` | **translated**: a user-mode access that is neither of those; the TLB decides |
| `ISFETCH`, `ISSTORE` | the kind of access (the TLB half; W or X) |
| `BUSK` | the cycle is a bus kind (fetch, load, store, the locked pair, the acknowledge cycle) |

**GAL FAULTB** adds the TLB's verdict:

| Order | Fault | Cause | TLBHI loaded? |
|---|---|---|---|
| 1 | misaligned | 3 MISALIGNED | no |
| 2 | user access to the upper half | 2 PRIVILEGE | no |
| 3 | translated, and not (`MATCH` and V) | 8 TLBMISS_X (fetch), 10 TLBMISS_W (store), 9 TLBMISS_R (load) | **yes** |
| 4 | translated, a store without W or a fetch without X | 11 PROTECT | **yes** |

Its outputs are the cause (`C3–C0`), **`PRE`** (a fault found before the
bus), **`TLBF`** (TLBHI takes the fault) and **`BUSCYC`**: the bus cycle
runs only for a bus kind with no fault found (and not during reset). A
faulting access never reaches the bus, so a store that faults writes
nothing.

**GAL FAULT2** adds what the bus says. A bus cycle that gets **no `-ACK`**
(no card at that address, a write to the ROM, a wrong size for a device
register) is a **bus error**, cause 4. The exception is the acknowledge
cycle, where no answer means vector `0xFF` (D57). FAULT2 drives **`FAULT`**
and **`FC3–FC0`** to the CPU card on J3, and **`BEF`** (a bus error: BADADDR
takes the *physical* address).

**What a fault leaves behind**, at the commit:
- **BADADDR** (four 74HCT377s, loaded when `FAULT` is up) takes the
  faulting address. Bits 11:0 are MAR's. Bits 31:12 are MAR's, or the
  physical page for a bus error (five 74HCT157s choose).
- **TLBHI**, for a TLB miss or a protection fault, takes the faulting
  page's VPN, the I bit (a fetch) and the current ASID, through eight
  74HCT157s. The refill handler then only has to write TLBLO and execute
  `TLBW` (isa-system.md).

## 6. The bus cycle

**The address** goes onto A2–A31 through four 74ACT541s:
- bits 31:12: the physical page (§4);
- bits 11:5: MAR;
- bits 4:2: MAR, or, in the acknowledge cycle, MAR[2:0], the **interrupt
  level** being acknowledged (a 74HCT157). MAR holds CAUSE then, whose low
  bits are the level.

**GAL STROBE** drives the strobes for the whole cycle when `BUSCYC` is
set: **`-MRD`** for a fetch, load or `LOADW`; **`-MWR`** for a store or
`STOREW`; **`-IACK`** for the acknowledge cycle. It also drives the **byte
enables** **BE0–3** from `SIZE` and MAR[1:0]: one lane for a byte, two for
a halfword, all four for a word, lane 0 alone for the acknowledge cycle.

**Store data** reaches the bus through four 74HCT541s from R. The CPU card
has already replicated the bytes into every lane (cpu-card-theory §12), so
the data is right whichever lanes the byte enables select.

**The answer:** the addressed card pulls **`-ACK`** low within the first
clock (no answer is the bus error above). A slow card also pulls **`-WAIT`**.
FAULT2 then raises **`STALL`**: the CPU card holds its commit, and the
cycle repeats with everything unchanged until `-WAIT` is released.

## 7. Load steering

A load's data comes back on D0–31 in the lanes it was read from. The CPU
wants it **right-aligned on R**, sign- or zero-extended (`load_extract()`).
**GAL STEER** sets up three groups of multiplexers onto R:

| R's byte | Comes from | Through |
|---|---|---|
| 0 (R7:0) | the addressed lane: MAR[1:0]'s for a byte, lane 0 or 2 for a halfword, lane 0 for a word | four 74HCT253 (dual 4:1) |
| 1 (R15:8) | lane 1 (a halfword at 0, a word), lane 3 (a halfword at 2), the fill (a byte), or **the vector** D7:0 (the acknowledge cycle) | four 74HCT253 |
| 2–3 (R31:16) | lanes 2–3 (a word), or the fill | four 74HCT257 |

**The fill** is 0, or for a signed load the sign bit (R7 for a byte, R15
for a halfword), which STEER reads back from R itself.

**In the acknowledge cycle** R carries **MAR with the vector in byte 1**:
MAR's drivers come on for bytes 0, 2 and 3, and byte 1 takes D7:0. The
INTR microcode writes that word into CAUSE, so CAUSE bits 15:8 hold the
vector (D57). With no card answering, ten-kilohm pull-ups on D0–7 make the
vector read `0xFF`.

## 8. The control registers

| Register | Parts | Written | Read (`RSRC = CRHW`, CRSEL) |
|---|---|---|---|
| **TLBHI** (bits 31:12, 8, 7:0) | 4 × 74HCT377, 8 × 74HCT157 | `MTCR` from R, or by a TLB fault (§5) | 4 × 74HCT541 |
| **TLBLO** (bits 31:12, 2:0) | 3 × 74HCT377 | `MTCR` | 4 × 74HCT541 |
| **ASID** (8 bits) | 74HCT377 | `MTCR` (the kernel at every context switch) | 74HCT541 |
| **BADADDR** | 4 × 74HCT377, 5 × 74HCT157 | by a fault | 4 × 74HCT541 |
| **MAR** (`RSRC = MAR`) | — | — | 4 × 74HCT541 |

The CPU card's control-register decode table sends **CRSEL** for the
hardware CRs. One 74HCT138 decodes the writes (`RDST = CRHW`), another the
reads. A further 74HCT138 decodes the `RSRC` codes this card serves:
`MEM`, `MAR`, `BADADDR`.

## 9. The lock and the arbiter

**The bus lock** (D51, D54): `SWAP` reads with `LOADW` and `LOCKREQ`, then
writes with `STOREW`, and no DMA master may come between the two. GAL LOCKG
registers **`LOCKED`** at the locked `LOADW`'s commit and clears it at the
`STOREW`'s, or at a fault (a `SWAP` that faults is restarted whole). A
74HC05 puts it on the backplane's open-drain **`-LOCK`**.

**The arbiter** (D50, D64), **GAL ARB**, clocked by `CLK`. Each slot from
3 to 10, and the debug card's slot 0, has a radial pair, **`-BREQn`** in
and **`-BGNTn`** out. Ten-kilohm pull-ups keep an empty slot's request off.
- At a `CLK` edge with no grant out and `-LOCK` high, the requesting slot
  of the **highest priority** gets its grant: slot 0 first (the debug card
  can always borrow the bus), then the highest-numbered slot.
- A master **keeps the bus as long as it asks**. When it releases
  `-BREQ`, its grant drops at the next edge.
- While a grant is out (**`DMAOWN`**), this card's address drivers,
  strobes and store data are all off the bus. If the CPU wants a bus cycle
  meanwhile, FAULT2's `STALL` holds it.

## 10. A load, end to end

`LW r1, 8(r2)` in user mode, the page in the TLB, the second word of the
instruction (cpu-card-theory §17):

1. **The commit before:** MAR ← r2 + 8 (`RDST = MAR` from the CPU card's
   address adder).
2. **During the cycle:** the word on J3 says `MEM = LOAD`, `SIZE = word`,
   `RSRC = MEM`. MAR[17:12] and `ISFETCH = 0` index the data half of the
   TLB; the 521s compare the VPN and ASID; `MATCH` and V are set. FAULTA
   says aligned, user, translated; FAULTB finds no fault and raises
   `BUSCYC`. The physical address (PFN, MAR[11:2]) goes onto A2–A31 with
   `-MRD` and all four byte enables.
3. The memory card in that window answers `-ACK`, and its SRAMs put the
   word on D0–31. STEER passes the lanes straight through to R.
4. **The commit:** the CPU card latches R for r1.

Had the page been missing, FAULTB would have raised `PRE` with TLBMISS_R,
`BUSCYC` would have stayed low (no bus cycle), and FAULT2 would have
raised `FAULT`. At the commit, BADADDR and TLBHI would have taken the
address and page, and the CPU card would have aborted the cycle and
entered the trap.

## 11. Timing

At 3 MHz the whole path above must fit one 333 ns cycle (*first estimates;
the static timing pass, [timing.md](timing.md) (D71, D72, D149), times the
card as wired against the datasheets and finds every path meeting 3 MHz at
the worst case*):

| Step | Approximately |
|---|---|
| MAR valid after the commit (74HCT161) | 25 ns |
| the TLB's SRAMs (15 ns) | 40 ns |
| the 521s, LOCKG's `MATCH`, FAULTB's `BUSCYC` (GALs ≈ 15 ns each) | 90 ns |
| the address and strobes on the backplane | 110 ns |
| the memory card: decode, a 55 ns SRAM, the transceivers | ≈ 200 ns |
| the steering onto R | ≈ 220 ns |
| the CPU card's set-up before the commit | ample |

The tightest part is the fault decision before the strobes. It is also why
5 MHz would need translation moved earlier (backlog).
