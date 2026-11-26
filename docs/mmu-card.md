# The MMU / data card

**Status:** NETLIST DONE AND VERIFIED with the CPU card (2026-10-26).
**THE BOARD IS ROUTED** (since 2026-10-30; again 2026-11-18 for D136, RN3
on J3): 6 layers, 543 nets, nothing left open, 111.9 m of 0.2 mm track and
406 vias; the pre-order check (`pcb-check`) passed with no failures, and
since 2026-11-23 fails the card on purpose: it is held (`hold =` in its
`pcb-check.ini`, D150) until its silkscreen reads upright with the card
standing. Slot
2, 9U × 340 mm (D49), through-hole (D21). The CPU card's partner
on the local link (J3) and its way onto the system bus (J1, J2): D12, D17,
D50, D52, D54, D57, D59, D62. The netlist is `generators/card_mmu.py`.

How it works, block by block: [the theory of operation](mmu-card-theory.md).

**"An MMU / data card is in": `-MMUIN`** (J3 B30, D102): the pin is tied
to the card's ground plane, with no part. The debug card in slot 0 pulls
the line up and reads it: while it is low, the debug card alone leaves D,
the bus cycle's lines and the grants to this card, whose address drivers,
strobes and arbiter drive them (debug-card-theory §9). Nothing on this
card reads it; J3 B28, the CPU card's `-CPUIN`, is not connected here.

**130 ICs, 8 of them GALs**; 269 parts (`python3 generators/card_mmu.py --bom`).

It has no clock of its own: `CCLK` (the commit), `CLK` (the bus) and
`-WSTB` (the CPU card's write strobe, phase 1) come from the CPU card.

## 1. Blocks

| Block | Parts | ICs |
|---|---|---|
| **MAR** | 8 × 74F161 (D72; bits 31:2: load, +4 for LI32's MARINC), 8 × 74HCT157 (from R or the shadow PC); bits 1:0 and the controls in GAL MMUCTL | 17 |
| **Shadow PC** (D56) | 8 × 74HCT161 (loaded with the PC from R, +4 with PCINC): `MAR ← PC` needs no R cycle | 8 |
| **TLB** (split I / D, 64 + 64, direct-mapped) | 7 × CY7C199 (an entry: VPN, ASID, PFN, V, W, X); 2 × 74ACT157 (the index: MAR[17:12] and the side, or TLBHI's for TLBW); 7 × 74ACT541 (the write data from TLBHI / TLBLO); 4 × 74F521 (VPN and ASID against MAR and ASID; the '688's pinout, D72); a 74HCT32 gate (`-WE`) | 21 |
| **Faults** (as `translate()`) | GAL FAULTA (the access: kind, misaligned, privilege, translated?), GAL FAULTB (miss, protection, the cause; TLBHI takes the fault; whether the bus cycle runs), GAL FAULT2 (bus error: no `-ACK`; FAULT and its cause to the CPU; STALL) | 3 |
| **The physical address** | 5 × 74HCT157 (the PFN, or MAR in supervisor mode); 74HCT157 (A4:A2 = the level in the acknowledge cycle); 4 × 74ACT541 onto A2–A31 (D72) | 10 |
| **The bus cycle** | GAL STROBE (`-MRD`, `-MWR`, `-IACK`, BE0–3; off the bus while a DMA master has it); 4 × 74HCT541 (the store data onto D) | 5 |
| **Load steering** (as `load_extract()`) | GAL STEER; 4 × 74HCT253 (byte 0: the addressed lane), 4 × 74HCT253 (byte 1: the next lane, the vector, the fill), 4 × 74HCT257 (bytes 2–3: the data or the fill) | 13 |
| **Hardware CRs** | TLBHI: 8 × 74HCT157 (from R, or the fault's address, side and ASID) and 4 × 74ABT377; TLBLO 3 × 74ABT377 (D72); ASID 74HCT377; BADADDR: 5 × 74HCT157 (MAR, or the physical address for a bus error) and 4 × 74HCT377 | 25 |
| **R's sources** | 2 × 74HCT138 (MEM, MAR, BADADDR; which CR); 74HCT138 (CR writes); onto R: MAR, BADADDR, ASID, TLBHI, TLBLO (20 × 74HCT541) | 23 |
| **Lock and arbiter** | GAL LOCKG (the lock: a locked LOADW to its STOREW, D54; the TLB tag match); GAL ARB (slot 0, the debug card, then slots 3–10: the highest request at a free, unlocked edge, kept while asked; D64); 74HC05 (`-LOCK`, open drain) | 3 |
| **Glue, pull-ups** | 74HCT08, 74HCT00, 74HCT32; 10k on `-BREQ3–10` (empty slots) and on D0–7 (no vector reads FF); 10k down on J3's RSRC0–3, MEM0–2 and CCLK (no CPU card in: R idle, D136) | 2 |

## 2. How it works

- **MAR** loads at the commit from R (RDST = MAR), from R with the PC (PCMAR),
  or from the shadow PC (MARPC), and counts by 4 (MARINC). The CPU card gates
  all of these off in a cycle that aborts.
- **Translation.** In supervisor mode the physical address is MAR. In user
  mode the TLB is read in the same cycle (its index MAR[17:12], its side
  the fetch / data): VPN and ASID must match and V be set, else a TLB miss
  (TLBHI takes the faulting page, side and ASID); a store needs W, a fetch X,
  else PROTECT; an address in the upper half is PRIVILEGE. Misalignment is
  checked first, in either mode.
- **The bus cycle** runs only when no fault was found before it: the
  strobes for the microcycle, the store data from R (already replicated by
  the CPU card), the byte enables from SIZE and MAR[1:0]. No `-ACK` is a
  BUSERROR (BADADDR = the physical address); in the acknowledge cycle it is
  vector FF.
- **TLBW is written late,** like the register file: latched at the commit
  and written at the next cycle's `-WSTB` — that cycle is always a fetch in
  supervisor mode, which does not use the TLB.
- **STALL** holds the CPU while `-WAIT` is low in its cycle or a DMA master
  has the bus.

## 3. Verification

- Every GAL against its spec, exhaustively; FAULTA followed by FAULTB
  against `translate()`'s order for every input (`check_faults()`). The
  netlist's electrical checks.
- **Both cards together at gate level** (`generators/sim_sys.py`): the two
  netlists joined at J1–J3 as the backplane joins them; only memory and the
  devices are a model (`p32uemu`'s, as bus slaves on J1). The cross-check
  programs, the TLB's (`vm_test`, `vm_demo`) and interrupts' (`irq_test`:
  the acknowledge cycle's vector) included, run with **the state at every
  instruction identical to `p32emu`'s and the cycles to `p32uemu`'s**.
- Not yet exercised: STALL (no slow slave, no DMA master in the
  simulation), the arbiter and the lock against a DMA master.

## 4. Next

The schematic and board; a DMA master and a slow slave in the simulation
(the arbiter, the lock, STALL); timing against the datasheets — the
translation and the bus cycle inside one 333 ns microcycle (MAR at the
commit, the TLB 15 ns, the comparators, the address on the bus, a 55 ns
memory, the data back through the steering by ≈ 220 ns).
