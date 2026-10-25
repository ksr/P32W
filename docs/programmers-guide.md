# P32W programmer's guide

How to write programs for the P32W: the registers, the memory map, the
instructions, the devices on the I/O card, exceptions and interrupts, virtual
memory, the boot ROM — and the tools that check your program before any
hardware exists. The authorities behind this guide: [isa-v0.md](isa-v0.md)
(instructions and encodings), [isa-system.md](isa-system.md) (traps,
interrupts, the MMU), [instruction-reference.md](instruction-reference.md)
(generated from the ISA table), and the decision log
[decisions.md](decisions.md). How the machine does it in hardware:
[architecture.md](architecture.md) (the cards and the address map) and
[microarch.md](microarch.md) (the microcode).

## 1. The machine in one page

- **32-bit**, load/store, 16 general registers, fixed 32-bit instructions,
  **byte addressing, little-endian**, aligned accesses only (a misaligned
  word or halfword traps).
- **No condition flags:** branches compare two registers (`BEQ`, `BLT`, …).
- **Two modes:** supervisor (reset, the kernel) and user. User programs run
  in a **virtual** address space of 2 GiB, translated by a software-managed
  TLB; supervisor code sees physical memory directly.
- **Memory:** 1 MiB boot ROM at address 0, RAM above it on one to four
  16 MiB memory cards (up to 64 MiB), the I/O page at the top of the address
  space (D52).
- **Speed:** a 3 MHz microcycle, about 6–7 microcycles per instruction —
  roughly 0.5 MIPS. Every instruction, including multiply and divide, takes a
  fixed, small number of cycles.

## 2. Registers

| Register | Use |
|---|---|
| `r0` (`zero`) | always reads 0; writes are discarded |
| `r1`–`r14` | general purpose |
| `r15` (`sp`) | stack pointer — **banked**: the supervisor SP in supervisor mode, the user SP in user mode; switching mode switches the bank |
| `PC` | instruction address (a multiple of 4) |

**Control registers** — read with `MFCR rd, cr`, written with `MTCR cr, rs`,
supervisor mode only:

| # | Name | Contents |
|---|---|---|
| 0 | `STATUS` | bit 0 **S** (supervisor), 1 **IE** (interrupts enabled), 2 **PS** (previous S), 3 **PIE** (previous IE) |
| 1 | `EPC` | the PC of the instruction that trapped (or the next one, for an interrupt) |
| 2 | `CAUSE` | exception code; bit 31 set for an interrupt (then bits 2:0 = the IRQ line) |
| 3 | `BADADDR` | the faulting address (misaligned, bus error, privilege, TLB, protection); 0 otherwise |
| 4 | `VBR` | address of the one exception handler (multiple of 4) |
| 5 | `USP` | the user-mode `r15`, as seen from supervisor mode |
| 6 | `IPEND` | read-only: bit n = interrupt line n asserted |
| 7 | `IMASK` | bit n = 1 enables interrupt line n |
| 8 | `ASID` | the current address-space ID (8 bits) |
| 9 | `TLBHI` | a TLB entry's tag: VPN (31:12), I (8: instruction side), ASID (7:0) |
| 10 | `TLBLO` | a TLB entry's data: PFN (31:12), X (2), W (1), V (0) |

**Reset state:** supervisor mode, interrupts disabled (`IE` = 0, `IMASK` = 0),
`VBR` = 0, PC = 0. **No other register is initialised** — set `sp` first.

## 3. Memory map (physical)

| Range | Size | What |
|---|---|---|
| `0x0000_0000`–`0x000F_FFFF` | 1 MiB | **boot ROM** (flash on the memory card); writing it is a bus error |
| `0x0010_0000`–`0x00FF_FFFF` | 15 MiB | **RAM** on the first memory card (window 0) |
| `0x0100_0000`–`0x03FF_FFFF` | up to 48 MiB | **RAM** on more memory cards, 16 MiB each (windows 1–3, by jumpers; D52) |
| `0xFE00_0000`–`0xFE07_FFFF` | 64 KiB a card | **video memory**, at `0xFE00_0000 + n × 64 KiB` (default `0xFE05_0000`; §6.6) |
| `0xFFFF_8000`–`0xFFFF_FFFF` | 32 KiB | **I/O page** (§6): eight 4 KiB card windows |
| everything else | — | nothing answers: **bus error** |

A memory card that is not there does not answer either, so a program that
sizes memory must be ready for a bus error (the kernel's probe is
`kernel/vm.c`).

The I/O page is the top 32 KiB so that a sign-extended 16-bit offset from
`r0` reaches it: `lbu r2, 0xFFFFFF05(r0)` is one instruction. The assembler
accepts the full address as the offset.

## 4. Instructions

The full table, generated from the ISA source, is
[instruction-reference.md](instruction-reference.md). In outline:

| Group | Instructions |
|---|---|
| Arithmetic, logic (register) | `ADD SUB AND OR XOR NOR SLL SRL SRA SLT SLTU` |
| … with an immediate | `ADDI SLTI SLTIU` (sign-extended), `ANDI ORI XORI` (zero-extended), `SLLI SRLI SRAI`, `LUI` |
| Multiply, divide | `MUL MULH MULHU DIV DIVU REM REMU` — division by zero does not trap: `DIV`/`DIVU` give all ones, `REM`/`REMU` the dividend |
| Load, store | `LB LBU LH LHU LW`, `SB SH SW` — `rd, offset(rs)` |
| Branch | `BEQ BNE BLT BGE BLTU BGEU ra, rb, label` (±128 KiB) |
| Jump, call | `JMP label`, `CALL label` (±32 MiB), `JR rb`, `CALLR rb`, `RET` |
| System | `SYSCALL BREAK RTE MFCR MTCR WAIT TLBW HALT` |

**Assembler pseudo-instructions** (`assembler/p32asm.py`): `NOP`, `MOV rd,rs`,
`NEG`, `NOT`, `LI rd,value` (1 or 2 instructions), `LA rd,address` (always 2),
`PUSH rs`, `POP rd`, `BEQZ`/`BNEZ ra,label`, and `BGT BLE BGTU BLEU` (the
operands swapped).

**Calls.** `CALL` pushes the return address and jumps; `RET` pops it. The stack
is **full-descending**: `sp` points at the last word pushed, and `PUSH`
decrements before storing. Keep `sp` a multiple of 4.

**A calling convention** (used by the library and the examples; the ISA does
not impose one): arguments in `r2`–`r5`, the result in `r1`; a routine may
clobber `r10`–`r14`; it preserves `r1`–`r9` except the result. System calls
(the test kernels): number in `r1`, arguments `r2`–`r5`, result `r1`.

**Large constants and addresses:** `LI r1, 0x12345678` becomes
`LUI r1, 0x1234` + `ORI r1, r1, 0x5678`; `LA` always uses two words so a
label's address may change between assembler passes.

### Example: print a string

```asm
        li    sp, 0x01000000    ; top of the first memory card
        la    r2, msg
        call  uart_puts
        halt
msg:    .asciz "Hello, P32W\n"
        .include "../software/lib/p32io.inc"
```

## 5. Programs, the boot ROM and reset

At reset the CPU fetches from address 0 in supervisor mode: **the boot ROM
is the first code that runs.** A ROM program must:
1. set `sp` (e.g. `li sp, 0x01000000`, the top of the first memory card);
2. initialise the console UART (`uart_init`, §6.1) — the emulators work
   without it, the hardware does not;
3. set `VBR` before anything can trap, if it wants to handle exceptions.

The assembler's output is a flat image starting at address 0: that is the
ROM image, for the emulators (`p32emu image.bin`) and for the flash
programmer alike.

**The boot ROM monitor** ([software/monitor](../software/README.md)) is the
standard ROM program: a console prompt to dump and modify memory, call a
program, load sectors from the CompactFlash card, and **boot** a program from
it (sector 0: `"P32B"`, sector count, load address, entry point). Its
exception handler reports `CAUSE`, `EPC` and `BADADDR` and returns to the
prompt.

**Programming the ROM** on the real machine: the four flash chips (SST39SF0x0) on
the memory card hold byte lanes 0–3 of each word. Either program them in a
TL866-class programmer (split the image into lanes), or fit the memory card's
**PROG** jumper and let the debug card's host link write them in place (D50).

## 6. The devices

All I/O registers are in the top 32 KiB, in **card windows** of 4 KiB
(window n at `0xFFFF_8000 + n × 0x1000`, chosen by jumpers on the card;
D52). A card's registers are the top 256 bytes of its window, and at
`+FF0` / `+FF1` / `+FF4` every card answers with its **card ID** (byte:
1 I/O, 2 disk, 3 video, 4 debug), its **revision** (byte) and its
**interrupt status** (word, a bit per device). The default jumpers: the
I/O card in window 7 (`FFFF_F000`), the disk card in window 6
(`FFFF_E000`), the video card in window 5 (`FFFF_D000`).

**Byte registers** (UARTs, ports, RTC, USB, network; the CF data register
is a halfword)
must be accessed with `LB`/`LBU`/`SB`; **timer registers** with `LW`/`SW`.
Any other size, or an address no device uses, is a **bus error** — and the
I/O page traps in user mode, so only the kernel touches devices.

| Address | Device | IRQ |
|---|---|---|
| `FFFF_FF00`–`FF07` | UART 0 — the console | 2 |
| `FFFF_FF08`–`FF0F` | UART 1 | 3 |
| `FFFF_FF30`–`FF37` | UART 2 | 2 |
| `FFFF_FF38`–`FF3F` | UART 3 | 3 |
| `FFFF_FF10` | `TCOUNT` — timer count | 0 |
| `FFFF_FF14` | `TRELOAD` — timer period | |
| `FFFF_FF18` | `TCTRL` — bit 0 enable, bit 1 pending (write 1 to clear) | |
| `FFFF_FF20` | input port (8 bits) | |
| `FFFF_FF21` | output port (8 bits, reads back) | |
| `FFFF_FF40` / `FF41` | real-time clock (DS12C887): register number / register | 5 |
| `FFFF_FF50` / `FF51` | USB 0 (CH376S): data / command, status | 6 |
| `FFFF_FF52` / `FF53` | network coprocessor: data / status, control | 7 |
| `FFFF_FF54` / `FF55` | USB 1, as `FF50` / `FF51` | 6 |
| `FFFF_FFF0` / `FFF1` / `FFF4` | the I/O card's ID (1), revision, interrupt status (bit 0 timer, 1 UART 0, 2 UART 1, 3 UART 2, 4 UART 3, 6 USB 0, 7 USB 1, 8 network) | |

**The disk card** (window 6): four IDE ports of 16 bytes, port p at
`FFFF_EF00 + 16 p` (0 CF 0, 1 CF 1, 2 IDE A, 3 IDE B; §6.4).

| Address | Device | IRQ |
|---|---|---|
| `FFFF_EF00`–`EF07` | port 0, the first CompactFlash card: the True-IDE task file | 1 |
| `FFFF_EF08`–`EF0F` | port 0's control block (`EF0E` alternate status) | |
| `FFFF_EF10`–`EF3F` | ports 1–3, the same layout | 1 |
| `FFFF_EFF0` / `EFF1` / `EFF4` | the disk card's ID (2), revision, interrupt status (bit p = port p) | |

The interrupt levels are jumpers on the cards; the defaults are in the IRQ
columns above and in §7.

`software/lib/p32io.inc` has tested routines for everything below; its
symbols (`U0`, `ULSR`, `CFCMD`, …) are used here.

### 6.1 The UARTs (TL16C550)

Each UART has the PC-standard 16550 registers at consecutive bytes:

| Offset | Read | Write | With LCR bit 7 (DLAB) set |
|---|---|---|---|
| 0 | RBR: received byte | THR: byte to send | DLL: divisor low |
| 1 | IER: interrupt enable | IER | DLM: divisor high |
| 2 | IIR: interrupt identification | FCR: FIFO control | |
| 3 | LCR: line control | LCR | |
| 4 | MCR: modem control | MCR | |
| 5 | **LSR: bit 0 = data ready, bit 5 = transmitter ready** | — | |
| 6 | MSR: modem status | — | |
| 7 | SCR: scratch | SCR | |

**Baud rate** = 1.8432 MHz / (16 × divisor): 12 → 9600, 6 → 19200,
1 → 115200. **Set-up** (`uart_init`): LCR = 0x80, DLL/DLM = divisor,
LCR = 0x03 (8 data bits, no parity, 1 stop), FCR = 0x07 (FIFOs on and
cleared), IER = 0.

**Polled I/O:**

```asm
putc:   lbu   r10, 0xFFFFFF05(r0)  ; LSR
        andi  r10, r10, 0x20       ; transmitter ready?
        beqz  r10, putc
        sb    r2, 0xFFFFFF00(r0)   ; THR
        ret
getc:   lbu   r10, 0xFFFFFF05(r0)
        andi  r10, r10, 0x01       ; data ready?
        beqz  r10, getc
        lbu   r1, 0xFFFFFF00(r0)   ; RBR
        ret
```

**Interrupts:** IER bit 0 interrupts when a byte arrives, bit 1 when the
transmitter is empty. Read IIR to find out which (bits 3:0: `0x4` data
received, `0x2` transmitter empty, `0x1` nothing pending); reading IIR clears
a transmitter-empty interrupt, reading RBR a receive interrupt. Then enable
the line in `IMASK` (bit 2 for UARTs 0 and 2, bit 3 for UARTs 1 and 3) and set `STATUS.IE`.

On the I/O card each UART has a DB9 connector at the front edge.

### 6.2 The timer

A 32-bit down-counter: `TCOUNT` counts down once per tick **while
`TCTRL` bit 0 is set**; when it reaches 0 it reloads from `TRELOAD` and sets
**pending** (`TCTRL` bit 1), which raises IRQ0 until acknowledged by writing
`TCTRL` with bit 1 set (keep bit 0 set to keep counting).

```asm
        li    r2, 10000             ; 10 ms at 1 MHz
        sw    r2, 0xFFFFFF14(r0)    ; TRELOAD
        sw    r2, 0xFFFFFF10(r0)    ; TCOUNT
        li    r2, 1
        sw    r2, 0xFFFFFF18(r0)    ; enable
; in the IRQ0 handler:
        li    r2, 3
        sw    r2, 0xFFFFFF18(r0)    ; acknowledge, keep counting
```

**The tick:** 1 MHz on the hardware, divided from the bus clock, so it stops
when the CPU is halted from the front panel. **In the emulators one tick
per instruction**, so interrupt timing is exactly repeatable in tests. On the
hardware `TCOUNT` may read 0 for one microcycle at expiry, just before the
reload; in the emulators it never reads 0 while counting.

### 6.3 The ports

`FFFF_FF20` reads the 8-bit input port; `FFFF_FF21` is the 8-bit output port
(reads back what was written). Both go to a connector on the I/O card; with
nothing connected the input port reads `0xFF` (pull-ups). In the emulators
`-p N` sets the input value and `-t` traces writes to the output port.

### 6.4 The CompactFlash disk

The disk card's port 0: a CF card in a socket at the card's front edge, in
**True-IDE mode**; the data
register is **16 bits wide** (D26); 512-byte sectors addressed by a 28-bit
**LBA**.

| Address | Read | Write |
|---|---|---|
| `EF00` | data: a **halfword** (`lhu` / `sh`), the first byte in bits 7:0 | data |
| `EF01` | error | features |
| `EF02` | sector count | sector count |
| `EF03`–`EF05` | LBA 7:0, 15:8, 23:16 | same |
| `EF06` | drive/head | `0xE0` \| LBA 27:24 (LBA mode, drive 0) |
| `EF07` | **status**: 7 BSY, 6 DRDY, 3 DRQ, 0 ERR | command |

Cards start in 16-bit mode; `cf_init` sends features = `0x81`, command
`0xEF` (SET FEATURES: no 8-bit transfers) in case earlier software chose
8-bit mode, and ignores a refusal. **Read a sector** (`cf_read`): count = 1,
LBA, head, command `0x20`; wait for DRQ; read the data register as a
halfword 256 times (into a halfword-aligned buffer). **Write** (`cf_write`):
command `0x30`, wait for DRQ, write 256 halfwords, wait for BSY clear. A
count of n moves n sectors with one command (wait for DRQ before each).
Byte accesses to `EF00` reach only `DD0–7`: they work after SET FEATURES
`0x01` on a card that offers 8-bit mode (optional in the CF standard). `0xEC` (IDENTIFY) returns 512 bytes of
device information, word 0 = `0x848A` for a CF card. Reading the status
register clears the disk interrupt (IRQ1), raised when a sector is ready
and when a write or command completes. An empty port reads status `0x00`
(not ready). The other ports work the same way, 16 bytes further on each.

In the emulators `-d image.img` gives a disk backed by a file of raw
sectors (`dd if=/dev/zero of=disk.img bs=512 count=2048` for 1 MiB); `-d1`,
`-d2`, `-d3` fill the other ports.

### 6.5 The real-time clock (D23)

A DS12C887 (the PC's MC146818 registers) with its own crystal and battery.
Write the register number to `FF40`, then read or write it at `FF41`:

| Reg | | Reg | |
|---|---|---|---|
| `00` | seconds | `07` | day of the month |
| `02` | minutes | `08` | month |
| `04` | hours | `09` | year (00–99) |
| `06` | day of the week (1 = Sunday) | `32` | century |
| `0A` | A: bit 7 UIP (update in progress — wait while set); bits 6:4 = `010` runs the oscillator | `0B` | B: bit 7 SET (stop to set), bit 2 binary (else BCD), bit 1 24-hour |
| `0C` | C: interrupt flags (reading clears them) | `0D` | D: bit 7 = the battery is good |
| `0E`–`7F` | battery-backed RAM (`32` is the century) | | |

P32W Unix sets B = `0x06` (binary, 24-hour, no interrupts) and keeps UTC
(`kernel/rtc.c`). The chip ships with its oscillator off: write A = `0x20`
once. Its interrupt (alarm, periodic, update) is IRQ5. In the emulators the
clock follows the host's clock, or `-R SECONDS` (a fixed start).

### 6.6 The video card and PS/2 (D53)

VGA, 640 × 480: an **80 × 25 text** layer and a **640 × 480 one-bit
overlay**, each switched on and off, and two PS/2 ports (keyboard and
mouse). The full description is [architecture.md](architecture.md) §4a.

**Memory** at `0xFE00_0000 + n × 64 KiB` (default `0xFE05_0000`), read and
written at any time, in words, halfwords or bytes:

| Offset | What |
|---|---|
| `0x0000`–`0x0F9F` | **text**: 25 rows × 80 cells, 2 bytes a cell: the character, then its attribute (bit 0 inverse, 1 underline, 2 bright, 3 blink) |
| `0x8000`–`0x95FF` | **overlay**: 480 lines × 80 bytes, bit 7 the leftmost dot |

**Registers** (window 5, byte accesses):

| Address | Read | Write |
|---|---|---|
| `FFFF_DF00` | control | bit 0 TEXT on, 1 OVERLAY on, 2 XOR (else OR), 3 CURSOR on |
| `FFFF_DF01` | status: bit 0 VBLANK, 1 DE | — |
| `FFFF_DF02` / `DF03` | cursor row (0–24) / column (0–79) | the same |
| `FFFF_DF08` / `DF09` | keyboard: the next scan code (set 2) / status (bit 0 FULL, 1 BUSY) | a command byte / — |
| `FFFF_DF0C` / `DF0D` | mouse: as `DF08` / `DF09` | as `DF08` |
| `FFFF_DF10` | interrupt enables | bit 0 keyboard, 1 mouse (IRQ4 by default) |
| `FFFF_DFF0` / `DFF1` / `DFF4` | card ID (3), revision, interrupt status (bit 0 keyboard, 1 mouse) | — |

At reset everything is off (a black screen), the cursor at 0, 0.

## 7. Exceptions and interrupts

**One handler.** Every exception and interrupt enters supervisor mode at the
address in `VBR`:
1. `EPC` ← the trapping instruction (an interrupt: the next instruction);
2. `CAUSE` ← the code, `BADADDR` ← the faulting address (or 0);
3. `PS` ← S, `PIE` ← IE; S ← 1, IE ← 0 (and `r15` becomes the supervisor SP);
4. PC ← `VBR`.

Entry writes no memory and no register, so it cannot fault. **The trapping
instruction has had no effect** — return with `RTE` to re-execute it (after
fixing the cause, e.g. a TLB refill), or add 4 to `EPC` first to skip it
(`SYSCALL` and `BREAK` leave `EPC` pointing at themselves).

| Code | Cause | `BADADDR` |
|---|---|---|
| 1 | ILLEGAL — undefined opcode or non-zero reserved bits | 0 |
| 2 | PRIVILEGE — privileged instruction in user mode, or a user access to ≥ `0x8000_0000` | 0 / the address |
| 3 | MISALIGNED — a load/store or jump not aligned | the address |
| 4 | BUSERROR — nothing answered (or wrong access size in the I/O page, or a ROM write) | the **physical** address |
| 5 | SYSCALL | 0 |
| 6 | BREAK | 0 |
| 8 / 9 / 10 | TLB miss on fetch / load / store | the virtual address |
| 11 | PROTECT — store to a read-only page, fetch from a non-executable one | the virtual address |
| `0x8000_0000` \| v << 8 \| n | interrupt on line n; v = the card's vector (its window × 16 + the device's interrupt status bit; `0xFF`: none answered, D57) | 0 |

**Interrupts** (8 levels, set by jumpers; the defaults: 0 timer, 1 disk,
2 UARTs 0 and 2, 3 UARTs 1 and 3, 4 PS/2, 5 real-time clock, 6 USB,
7 network) are level-triggered and taken **between
instructions** when `STATUS.IE` = 1 and `IPEND & IMASK` ≠ 0, lowest line
first. The handler must clear the device's request, or it will trap again
at once. **Interrupt shadow:** after an `MTCR` (e.g. setting `IE`) or an
`RTE`, one more instruction runs before a pending interrupt is taken — so
`RTE` always lets the interrupted program make progress.

**A handler skeleton:**

```asm
        la    r2, handler
        mtcr  VBR, r2
handler:                        ; supervisor SP in effect
        push  r10
        push  r11
        mfcr  r10, CAUSE
        blt   r10, r0, irq      ; bit 31 set: an interrupt
        ...                     ; an exception: CAUSE, BADADDR, EPC
irq:    andi  r10, r10, 7       ; the line
        ...                     ; acknowledge the device
        pop   r11
        pop   r10
        rte
```

A handler that may itself trap, or that re-enables interrupts, must first
save `EPC` and `STATUS` (they are overwritten by the next trap). **`WAIT`**
idles until an enabled interrupt line is asserted — the kernel's idle loop.

## 8. User mode and virtual memory

User programs run with `STATUS.S` = 0: privileged instructions and the I/O
page trap, and **every address is translated**. User addresses
`0x0000_0000`–`0x7FFF_FFFF` go through the TLB; anything above traps
(PRIVILEGE). Supervisor addresses are physical.

**Pages are 4 KiB.** The **TLB** is split: 64 entries for instruction fetches
(I-TLB), 64 for loads and stores (D-TLB), each **direct-mapped** — the entry
for virtual page number VPN is entry `VPN mod 64` — and **tagged with an
ASID**, so switching processes is just `MTCR ASID` (no flush).

**The TLB is managed by software.** On a miss the hardware has already put the
faulting VPN, the side (I) and the current ASID into `TLBHI`; the handler
looks the page up in its own page table, writes `TLBLO`, executes `TLBW`, and
returns with `RTE` to retry the access:

```asm
tlbmiss:                        ; CAUSE 8, 9 or 10
        mfcr  r10, TLBHI        ; VPN | I | ASID, loaded by the hardware
        srli  r10, r10, 12      ; the VPN
        slli  r10, r10, 2       ; index into a linear table of TLBLO words
        la    r11, pagetable    ; (this process's table)
        add   r10, r10, r11
        lw    r10, 0(r10)       ; PFN | X W V
        beqz  r10, segfault     ; not mapped: the process dies
        mtcr  TLBLO, r10
        tlbw                    ; write the entry TLBHI selects
        rte                     ; retry the instruction
```

`TLBLO` = `PFN << 12 | X << 2 | W << 1 | V`. A store needs W, a fetch X; V = 0
entries never match. See `emulator/test/vmkernel.inc` for a complete kernel
(processes in separate address spaces, preemption by the timer, system
calls that check user pointers).

## 9. Tools

| Tool | |
|---|---|
| `assembler/p32asm.py src.asm -o img.bin -l listing.lst` | two-pass assembler; flat ROM image at 0 |
| `emulator/p32emu [-t] [-d disk] [-a aux] [-p n] img.bin` | instruction-level emulator (the reference) |
| `emulator/p32uemu ...` | microcode-level emulator: runs the real control store, cycle by cycle |
| `make -C emulator test` | every test: both emulators must agree instruction by instruction |

The console is the emulators' stdin/stdout; UART 1 goes to files (`-a`, `-A`);
`HALT` ends the run with exit status `r1 & 0xFF`. With `VBR` = 0 an exception
stops the emulators with a register dump (on the hardware it would jump to
0). `-t` traces every instruction, trap and interrupt.
