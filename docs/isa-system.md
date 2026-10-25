# P32W System Architecture — Stage A: Traps and Privilege

**Status:** stage A ACCEPTED (2026-10-11, D10); stage B ACCEPTED (2026-10-11, D11); stage C
ACCEPTED (2026-10-11, D12).
Extends
[isa-v0.md](isa-v0.md) for stage A of D9. Stage B (timer interrupt) and
stage C (MMU) will add to this document. Everything here is designed so
interrupts and page faults use the same mechanism later.

## 1. Modes

| Mode | Can do |
|---|---|
| **Supervisor** (S = 1) | everything |
| **User** (S = 0) | ordinary instructions only; privileged instructions and the I/O page trap |

Reset starts in supervisor mode with interrupts disabled. The only ways from
user to supervisor mode are traps (exceptions, SYSCALL, and later
interrupts); the only way back is `RTE`.

r15 is banked (D6): in supervisor mode r15 is the **supervisor SP (SSP)**, in
user mode the **user SP (USP)**. The switch happens automatically with the
mode. Supervisor code reads and writes the user SP through control register
USP.

## 2. Control registers

Accessed only with the privileged `MFCR` / `MTCR` instructions (§4).

| # | Name | Contents |
|---|---|---|
| 0 | `STATUS` | bit 0 **S** (supervisor), bit 1 **IE** (interrupt enable, used from stage B), bit 2 **PS** (previous S), bit 3 **PIE** (previous IE). Other bits read 0, ignore writes |
| 1 | `EPC` | exception PC — address of the instruction that trapped |
| 2 | `CAUSE` | exception code (§3); bit 31 = 1 for interrupts (stage B) |
| 3 | `BADADDR` | faulting address for misaligned, bus-error, upper-half/I/O and page-fault exceptions; **0** for exceptions without an address (illegal, privileged instruction, SYSCALL, BREAK) and for interrupts (D15) |
| 4 | `VBR` | vector base: address of the single exception handler; must be a multiple of 4; reset value 0 |
| 5 | `USP` | the user-mode r15, readable/writable from supervisor mode |
| 6–15 | — | reserved (timer in stage B, MMU in stage C); access traps as illegal |

## 3. Exception causes

| Code | Name | Raised by | BADADDR |
|---|---|---|---|
| 1 | `ILLEGAL` | undefined opcode or nonzero must-be-zero bits | — |
| 2 | `PRIVILEGE` | privileged instruction, or I/O-page access, in user mode | address (I/O case) |
| 3 | `MISALIGNED` | misaligned load/store, or JR/CALLR/RET to an address not a multiple of 4 | the address |
| 4 | `BUSERROR` | access to an unmapped physical address, write to ROM | the address |
| 5 | `SYSCALL` | `SYSCALL` instruction | — |
| 6 | `BREAK` | `BREAK` instruction (debugger breakpoint) | — |
| 8–11 | — | reserved: page faults (stage C) | |
| bit 31 set | — | reserved: interrupts (stage B) | |

Divide by zero never traps (isa-v0.md).

## 4. New instructions (opcodes 0x70–0x7F)

New format **CR**: `OP | A = general register | B = control-register number | bits 15:0 = 0`.

| OP | Mnemonic | Fmt | Privileged | Operation |
|---|---|---|---|---|
| 0x70 | `SYSCALL` | N | no | trap with CAUSE = SYSCALL |
| 0x71 | `RTE` | N | yes | return from exception (§5) |
| 0x72 | `MFCR rd, cr` | CR | yes | rd = control register cr |
| 0x73 | `MTCR cr, rs` | CR | yes | control register cr = rs |
| 0x74 | `BREAK` | N | no | trap with CAUSE = BREAK |

The assembler accepts control-register names: `STATUS`, `EPC`, `CAUSE`,
`BADADDR`, `VBR`, `USP`.

**`HALT` becomes privileged** (traps with PRIVILEGE in user mode). Opcodes
0x75–0x7F stay reserved (e.g. `WAIT` for stage B, TLB/page-table control for
stage C).

## 5. Trap entry and return

**Trap entry** (any exception; later also interrupts), done by the hardware:

1. `EPC` ← address of the trapping instruction
2. `CAUSE` ← code; `BADADDR` ← faulting address (if any)
3. `PS` ← `S`, `PIE` ← `IE`; then `S` ← 1, `IE` ← 0
4. `PC` ← `VBR`

Trap entry **writes no memory** and changes no general register except that
r15 now selects the supervisor SP. So it cannot itself fault, and a user
program with a bad SP cannot crash trap entry. The handler saves whatever
registers it uses.

The trapping instruction has **no effect**: no register, SP, PC or memory
change (the D6 commit-after-access rule). So after a fault, `RTE` re-executes
it — this is what page faults in stage C rely on.

**`RTE`**: `PC` ← `EPC`; `S` ← `PS`; `IE` ← `PIE`. The r15 bank follows the
restored mode. `PS` and `PIE` are left unchanged.

**`MTCR STATUS`** takes effect at once: writing S = 0 switches to user mode
(and the user r15) for the next instruction.

**`VBR` and `EPC` read back exactly as written;** the PC holds only bits
31:2, so trap entry and `RTE` ignore their low 2 bits (D15).

**SYSCALL and BREAK** also leave `EPC` pointing at themselves; the handler
adds 4 to `EPC` before `RTE` to continue after them. (One rule for every
exception keeps the microcode simple.)

**Nesting:** a trap overwrites `EPC`, `CAUSE`, `BADADDR` and the previous-mode
bits. A handler that may trap again (or, in stage B, re-enable interrupts)
must first save `EPC` and `STATUS` on the supervisor stack.

## 6. Protection in stage A

Without an MMU, user programs can still read and write any RAM — real memory
protection arrives with stage C. Stage A enforces what it can cheaply:
- privileged instructions trap in user mode;
- **the I/O page traps in user mode** (one address comparison: A31–A15 all
  ones and S = 0), so user programs must reach devices through SYSCALL.

## 7. Emulator v2 behaviour

- Exceptions are taken as traps (§5) instead of stopping the emulator.
- **While `VBR` = 0 (no handler installed), an exception still stops the
  emulator with the register dump**, as in v1, so bare-metal test programs
  keep working. (Hardware would jump to address 0.)
- `HALT` in supervisor mode stops the emulator as before.
- Trace output shows the mode (S/U) and reports each trap.

## 8. Software conventions (kernel ABI, not ISA)

Proposed for the first kernel, recorded here for reference:
- `SYSCALL` number in r1, arguments in r2–r5, result in r1.
- First calls: `exit(status)`, `putc(ch)`, `getc()`, `write(buf, len)`.

## 9. Review outcome (2026-10-11)

All five review points were accepted as proposed (D10): trap entry writes no
memory; one handler entry point at VBR that dispatches on CAUSE; EPC is the
address of the trapping instruction for every exception; user-mode I/O-page
access traps in stage A; VBR = 0 stops the emulator on an exception.

---

# Stage B: Interrupts and the Timer

**Status:** ACCEPTED (2026-10-11, D11). Adds what preemptive
multitasking needs: hardware interrupts, a programmable timer, and a way to
idle. Interrupts reuse the stage A trap mechanism unchanged.

## B1. Interrupt lines

- **8 interrupt request lines**, IRQ0–IRQ7 (backplane signals; each device's
  line is set by a jumper, and devices may share one). Level-triggered: a line stays asserted until the device
  is acknowledged.
- **IRQ0 = timer** (B4). IRQ1–IRQ7 reserved (e.g. console receive, disk).
  The default jumpers (D50, D52): 1 disk, 2 UARTs 0 and 2, 3 UARTs 1
  and 3, 4 PS/2, 5 real-time clock (D23), 6 USB, 7 network.
- Fixed priority: the lowest-numbered pending, enabled line is taken first.

New control registers:

| # | Name | Contents |
|---|---|---|
| 6 | `IPEND` | read-only: bit n = IRQn currently asserted. Writes ignored |
| 7 | `IMASK` | bit n = 1 enables IRQn. Reset value 0 |

8–15 stay reserved (MMU in stage C). `IMASK` ignores bits 8–31.

## B2. When an interrupt is taken

**Between instructions:** before fetching the next instruction, if
`STATUS.IE` = 1 and `(IPEND & IMASK)` ≠ 0, the machine takes an interrupt
instead of fetching. Trap entry is exactly stage A §5, with:
- `EPC` ← address of the next instruction, **not yet executed** — so `RTE`
  executes it, and "EPC = the instruction that was interrupted" holds for
  traps and interrupts alike;
- `CAUSE` ← `0x8000_0000 | n` for IRQn;
- `IE` ← 0 (as for every trap), so interrupts do not nest unless the handler
  saves `EPC`/`STATUS` and sets `IE` again.

An instruction is never interrupted half-way.

**Interrupt shadow (D20, 2026-10-13):** the check after an instruction uses
`IE` and `IMASK` as they were *before* that instruction if it was `MTCR` or
`RTE` — so after enabling interrupts, or returning from a handler, one more
instruction always runs before a pending interrupt is taken (as in the
hardware, which decides in the instruction's last microcycle, before its
writes take effect at the closing clock edge). After `RTE` this guarantees
the interrupted program makes progress. Trap and interrupt entry never take
an interrupt at their end.

## B3. `WAIT` (opcode 0x75, privileged, format N)

Stops fetching instructions until some line in `IPEND & IMASK` is asserted,
then continues with the next instruction (and, if `IE` = 1, takes the
interrupt first). The kernel's idle loop uses it instead of spinning.

## B4. Timer device (IRQ0)

A memory-mapped device in the I/O page (on the I/O card in hardware):

| Address | Name | Access | Meaning |
|---|---|---|---|
| `0xFFFF_FF10` | `TCOUNT` | read/write | current count; counts down by 1 per tick |
| `0xFFFF_FF14` | `TRELOAD` | read/write | period: loaded into `TCOUNT` when it reaches 0 |
| `0xFFFF_FF18` | `TCTRL` | read/write | bit 0 = enable (counting); bit 1 = pending (IRQ0 asserted) — write 1 to bit 1 to acknowledge |

When enabled, `TCOUNT` decrements each tick; on reaching 0 it sets pending
(asserting IRQ0) and reloads from `TRELOAD`. The handler acknowledges by
writing `TCTRL` with bit 1 set (and bit 0 set to keep counting).

**Tick:** in hardware, a fixed divided clock (rate chosen with the clock,
open decision 8). **In the emulator, one tick per instruction executed**, so
interrupt timing is exactly repeatable and tests can check exact output.
During `WAIT` the emulator advances the timer straight to its next expiry.

## B5. Emulator v3 behaviour

- Interrupts as B2; `IPEND` bit 0 mirrors the timer's pending flag.
- `WAIT` with no possible wake-up (`IPEND & IMASK` = 0 and the timer disabled
  or masked) stops the emulator with a message instead of hanging.
- `-t` trace reports each interrupt.

## B6. Kernel (software, not ISA)

The stage B kernel test: a few tasks in one address space, each in user mode
with its own stack. The timer interrupt saves the running task's registers,
`EPC` and user SP into its task record, picks the next task round-robin,
restores that task's state and `RTE`s into it. Tasks never yield on their
own, so their interleaved output proves preemption.

## B7. Review outcome (2026-10-11)

All five review points were accepted as proposed (D11): interrupts only
between instructions (`EPC` = next instruction, `CAUSE` = bit 31 | line);
8 level-triggered lines with fixed priority, `IMASK` plus `STATUS.IE`; the
timer as a memory-mapped device; one emulator tick per instruction; a
privileged `WAIT`.

---

# Stage C: Virtual Memory (MMU)

**Status:** ACCEPTED (2026-10-11, D12). Completes D1 and D9:
each process gets its own address space, and cannot reach another process's
memory, the kernel, or the devices.

## C1. Design in one paragraph

User-mode addresses are **virtual** and go through a small **TLB**
(translation lookaside buffer) that maps 4 KiB virtual pages to physical
pages. The TLB is **managed by software**: when an access finds no matching
entry, the CPU raises a *TLB miss* exception and the kernel loads the right
entry from its own page tables, then `RTE` re-executes the access (stage A
already guarantees this is safe). Supervisor mode is **untranslated** — the
kernel uses physical addresses directly — so the miss handler itself can
never miss. This is the MIPS approach: the least hardware, with the page-table
format left entirely to software.

**Why not a hardware page-table walker?** A walker means extra microcode and
datapath to read page tables on every miss, and fixes the page-table format in
hardware. On a microcoded TTL machine the software refill costs roughly 15–20
instructions per miss, and the hardware stays small: an SRAM, a comparator
and a few gates.

## C2. Address spaces

| Mode | Addresses | Translation |
|---|---|---|
| User | `0x0000_0000`–`0x7FFF_FFFF` (2 GiB) | through the TLB |
| User | `0x8000_0000` and above | **trap** (PRIVILEGE, BADADDR = address) — this includes the stage A I/O-page rule |
| Supervisor | everything | **none**: virtual = physical |

The backplane address bus always carries **physical** addresses (settles the
D1 question). The physical address is 32 bits; this machine populates 1 MiB of ROM and up to 64 MiB of RAM, in
16 MiB memory cards (D52).

## C3. Pages and the TLB

- **Page size 4 KiB.** A virtual address = virtual page number
  **VPN** (bits 31:12) + offset (bits 11:0).
- **Split instruction / data TLB (D13):** an **I-TLB** for instruction
  fetches and a **D-TLB** for loads and stores, each **64 entries,
  direct-mapped**: entry index = VPN bits 5:0. In hardware both halves are one
  128-entry SRAM whose top address line selects fetch or data, plus one tag
  comparator — one lookup per access.
- **Why split:** one instruction may need two translations, its fetch and its
  data access. In a single direct-mapped TLB, when both pages fall in the
  same slot each refill evicts the other and the instruction can never
  complete (livelock; found in emulator testing, 2026-10-11). With the split,
  every instruction needs at most one entry in each half, so it always
  completes.
- **Address-space ID (ASID), 8 bits.** Each entry is tagged with the ASID of
  its process, and matches only when it equals the current `ASID` register.
  A context switch just changes `ASID`; the TLB needs no flush (until ASIDs
  are reused).

**Entry format** (as seen through `TLBHI` / `TLBLO`):

```
TLBHI:  VPN [31:12]                      | 0 [11:9] | I [8] | ASID [7:0]
TLBLO:  PFN [31:12] (physical page)      | 0 [11:3] | X [2] | W [1] | V [0]
```

- **V** valid · **W** writable · **X** executable. Read is allowed whenever
  V = 1. (X is checked by the I-TLB, W by the D-TLB.)
- **I** (in `TLBHI` only) selects the half: 1 = I-TLB, 0 = D-TLB.
- No hardware accessed/dirty bits: the kernel can track dirty pages by
  mapping them read-only first and catching the protection fault.

## C4. New control registers and instruction

| # | Name | Contents |
|---|---|---|
| 8 | `ASID` | current address-space ID (bits 7:0) |
| 9 | `TLBHI` | entry tag for `TLBW`; **set by the hardware on every TLB miss or protection fault** to the faulting VPN, I = 1 for a fetch (0 for a load/store), and the current ASID |
| 10 | `TLBLO` | entry data for `TLBW` |

11–15 stay reserved.

| OP | Mnemonic | Privileged | Operation |
|---|---|---|---|
| 0x76 | `TLBW` | yes | write `TLBHI`/`TLBLO` into the entry at index `TLBHI` bits 17:12 (VPN bits 5:0) of the I-TLB if `TLBHI.I` = 1, else of the D-TLB |

Because the hardware pre-loads `TLBHI` on a miss (including the I bit), the
refill handler only looks up the page-table entry, writes `TLBLO`, executes
`TLBW` and `RTE` — the entry lands in the half that missed. To invalidate the
whole TLB, write all 128 entries (64 with I = 1, 64 with I = 0) with V = 0. TLB contents are
undefined at reset, so the kernel invalidates it before first entering user
mode. For a BUSERROR in user mode, BADADDR is the *physical* address.

## C5. Exceptions (codes 8–11)

| Code | Name | Raised when | BADADDR | TLBHI |
|---|---|---|---|---|
| 8 | `TLBMISS_X` | instruction fetch finds no valid matching entry | virtual address | set |
| 9 | `TLBMISS_R` | load finds no valid matching entry | virtual address | set |
| 10 | `TLBMISS_W` | store finds no valid matching entry | virtual address | set |
| 11 | `PROTECT` | store to an entry with W = 0, or fetch from X = 0 | virtual address | set |

A matching entry with V = 0 counts as a miss. As for every exception, the
faulting instruction has no effect and `EPC` points at it.

Order of checks for a user-mode access: alignment (MISALIGNED) → upper half
(PRIVILEGE) → TLB (miss / PROTECT) → physical bus (BUSERROR).

## C6. Emulator v4 behaviour

- Models the split 64 + 64-entry direct-mapped TLB exactly, so a kernel that
  works in the emulator works on the hardware.
- Supervisor mode is untranslated (all existing tests keep working
  unchanged; they never enter user mode with translation, except the
  stage A/B kernels, which are updated to map their user pages).
- `-t` trace reports TLB misses and writes.

## C7. Kernel (software, not ISA)

Proposed for the stage C kernel test:
- **Page tables:** per process, kernel-defined (first version: one linear
  table covering the first 16 MiB of user space, entries in `TLBLO` format, so
  a refill is a single indexed load).
- **Processes:** each has its own ASID, page table, stack and data pages.
  Program code stays in ROM, mapped read-only + executable into every
  process; data and stack pages are private RAM frames — two processes use
  the **same virtual addresses for different physical memory**.
- **System calls translate user pointers** through the caller's page table
  and check permissions (closing the stage A `write()` hole).
- **Tests:** two processes at the same virtual addresses keep separate data;
  a process that touches an unmapped page, writes a read-only page, or
  reaches above 2 GiB is killed while the others continue; the TLB is shared
  between processes without flushing (ASIDs).

## C8. Review outcome (2026-10-11)

All six review points were accepted as proposed (D12): software-managed TLB;
translation in user mode only; 4 KiB pages with a 64-entry direct-mapped,
ASID-tagged TLB; user space is the lower 2 GiB; V/W/X permission bits only;
physical addresses on the backplane.

**Amended by D13 (2026-10-11):** the TLB is split into a 64-entry I-TLB and a
64-entry D-TLB (see C3), after emulator testing showed that a single
direct-mapped TLB can livelock.
