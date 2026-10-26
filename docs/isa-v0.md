# P32W ISA v0

**Status:** ACCEPTED (2026-10-11, D8). Implements decisions
D5–D10 in [decisions.md](decisions.md). Scope: what the instruction-level
reference emulator v1 (D7) needs. Privileged instructions, the MMU,
interrupts and exceptions-as-traps come in a later revision.

## 1. Machine state (v1)

| State | Notes |
|---|---|
| r0 | Always 0; writes discarded (D4) |
| r1–r14 | General purpose, 32 bits |
| r15 | Stack pointer (D6). Banked: supervisor SP / user SP. v1 runs in supervisor mode only, so only the supervisor SP is used |
| PC | 32 bits, always a multiple of 4 |

No flags register (D5). Memory is byte-addressed and little-endian (D4).

## 2. Instruction formats

Every instruction is 32 bits, word-aligned (D3). The fields never move:

```
 31      24 23  20 19  16 15  12 11                 0
+----------+------+------+------+--------------------+
|    OP    |  A   |  B   |  C   |      unused (0)    |   R  register-register
+----------+------+------+------+--------------------+
|    OP    |  A   |  B   |           IMM16           |   I  immediate / load / store / branch
+----------+------+------+---------------------------+
|    OP    |                 OFF24                    |   J  jump / call
+----------+------------------------------------------+
|    OP    |  A   |  B   |  C   |  0 (10 bits)  | S  |   X  indexed load / store (D59)
+----------+------+------+------+--------------------+
|    OP    |  A   |  B   |  OFF8 (15:8) | INC8 (7:0)|   P  post-increment load / store (D59)
+----------+------+------+---------------------------+
|    OP    |  0   |  rd  |          0 (16 bits)      |   L32  LI32, followed by the value's word (D59)
+----------+------+------+---------------------------+
```

- **OP** — 8-bit opcode. The microcode will be addressed by this field (D3).
- **A** — destination register (rd); for stores, the data register; for
  branches, the first compared register.
- **B** — first source register (rs1); base register for loads/stores; second
  compared register for branches; target register for JR/CALLR.
- **C** — second source register (rs2), R format only.
- **IMM16** — 16-bit immediate, sign- or zero-extended per instruction.
- **OFF24** — 24-bit signed word offset.
- Unused bits must be 0. The emulator reports a nonzero unused field as an
  illegal instruction, keeping those bits free for later revisions.

Because fields sit on 4-bit boundaries, a hex dump reads directly:
`ADD r1,r2,r3` = `0x10123000`, `ADDI r1,r0,42` = `0x2010002A`.

**Branch and jump targets** are relative to the *next* instruction:
`target = PC + 4 + offset × 4`.
- Conditional branches (IMM16): ±128 KiB. (D5 estimated ±512 KiB for a
  6-bit opcode; an 8-bit opcode trades that for simpler decode.)
- JMP/CALL (OFF24): ±32 MiB.
- JR/CALLR reach anywhere.

## 3. Opcode map

`sext` = sign-extend, `zext` = zero-extend, `mem32[x]` = word at byte address
x. All arithmetic is modulo 2³². `rd` = A, `rs1` = B, `rs2` = C.

### 0x00–0x0F — Special

| OP | Mnemonic | Fmt | Operation |
|---|---|---|---|
| 0x00 | — | — | **Illegal** (so executing zeroed memory stops at once) |
| 0x01 | `HALT` | — | Stop. Emulator exit status = r1 & 0xFF. (Privileged in later revisions) |
| 0xFF | — | — | **Illegal** (so executing erased EEPROM stops at once) |

### 0x10–0x1F — Register-register ALU (R)

| OP | Mnemonic | Operation |
|---|---|---|
| 0x10 | `ADD rd,rs1,rs2` | rd = rs1 + rs2 |
| 0x11 | `SUB rd,rs1,rs2` | rd = rs1 − rs2 |
| 0x12 | `AND rd,rs1,rs2` | rd = rs1 & rs2 |
| 0x13 | `OR  rd,rs1,rs2` | rd = rs1 \| rs2 |
| 0x14 | `XOR rd,rs1,rs2` | rd = rs1 ^ rs2 |
| 0x15 | `SLL rd,rs1,rs2` | rd = rs1 << (rs2 & 31) |
| 0x16 | `SRL rd,rs1,rs2` | rd = rs1 >> (rs2 & 31), zero fill |
| 0x17 | `SRA rd,rs1,rs2` | rd = rs1 >> (rs2 & 31), sign fill |
| 0x18 | `SLT rd,rs1,rs2` | rd = (signed rs1 < signed rs2) ? 1 : 0 |
| 0x19 | `SLTU rd,rs1,rs2` | rd = (unsigned rs1 < unsigned rs2) ? 1 : 0 |
| 0x1A | `NOR rd,rs1,rs2` | rd = ~(rs1 \| rs2) |

### 0x20–0x2F — Immediate ALU (I)

The low nibble matches the register form where one exists.

| OP | Mnemonic | Operation |
|---|---|---|
| 0x20 | `ADDI rd,rs1,imm` | rd = rs1 + sext(imm) |
| 0x22 | `ANDI rd,rs1,imm` | rd = rs1 & zext(imm) |
| 0x23 | `ORI  rd,rs1,imm` | rd = rs1 \| zext(imm) |
| 0x24 | `XORI rd,rs1,imm` | rd = rs1 ^ zext(imm) |
| 0x25 | `SLLI rd,rs1,sh` | rd = rs1 << sh (imm bits 4:0; bits 15:5 must be 0) |
| 0x26 | `SRLI rd,rs1,sh` | logical right shift |
| 0x27 | `SRAI rd,rs1,sh` | arithmetic right shift |
| 0x28 | `SLTI rd,rs1,imm` | rd = (signed rs1 < signed sext(imm)) ? 1 : 0 |
| 0x29 | `SLTIU rd,rs1,imm` | rd = (unsigned rs1 < unsigned sext(imm)) ? 1 : 0 |
| 0x2F | `LUI rd,imm` | rd = imm << 16 (B must be 0) |

There is no SUBI (use ADDI with a negative immediate). A full 32-bit constant
is `LUI rd,hi` + `ORI rd,rd,lo`; the assembler provides `LI rd,value`, which
emits one instruction when the value fits and two otherwise.

### 0x30–0x3F — Multiply / divide (R)

Implemented by the FPGA ALU in hardware (D21); the emulator models them directly.

| OP | Mnemonic | Operation |
|---|---|---|
| 0x30 | `MUL rd,rs1,rs2` | rd = low 32 bits of rs1 × rs2 |
| 0x31 | `MULH rd,rs1,rs2` | rd = high 32 bits, signed × signed |
| 0x32 | `MULHU rd,rs1,rs2` | rd = high 32 bits, unsigned × unsigned |
| 0x34 | `DIV rd,rs1,rs2` | signed quotient, rounded toward zero |
| 0x35 | `DIVU rd,rs1,rs2` | unsigned quotient |
| 0x36 | `REM rd,rs1,rs2` | signed remainder (sign of rs1) |
| 0x37 | `REMU rd,rs1,rs2` | unsigned remainder |

Divide never traps:
- Division by zero: quotient = 0xFFFFFFFF, remainder = rs1.
- Signed overflow (−2³¹ ÷ −1): quotient = −2³¹, remainder = 0.

These are the RISC-V results, chosen so that software checks the cases it
cares about and the hardware needs no divide trap.

### 0x40–0x4F — Loads and stores (I)

Address = rs1 + sext(imm). Misaligned halfword/word access → exception (D4).

| OP | Mnemonic | Operation |
|---|---|---|
| 0x40 | `LB  rd,imm(rs1)` | rd = sext(mem8) |
| 0x41 | `LH  rd,imm(rs1)` | rd = sext(mem16) |
| 0x42 | `LW  rd,imm(rs1)` | rd = mem32 |
| 0x44 | `LBU rd,imm(rs1)` | rd = zext(mem8) |
| 0x45 | `LHU rd,imm(rs1)` | rd = zext(mem16) |
| 0x48 | `SB  ra,imm(rs1)` | mem8 = ra & 0xFF |
| 0x49 | `SH  ra,imm(rs1)` | mem16 = ra & 0xFFFF |
| 0x4A | `SW  ra,imm(rs1)` | mem32 = ra |
| 0x4C | `SWAP rd,imm(rs1)` | t = mem32; mem32 = rd; rd = t — atomic (D51) |

(`ra` = the A field = the data register.)

**`SWAP`** (D51) exchanges a register with an aligned word in memory
as one indivisible operation: the read and the write are one bus
tenure under `-LOCK` ([bus.md](bus.md)), so no DMA master (or,
later, another processor) can come between them. It is the primitive for
locks: `SWAP` a 1 into the lock word; a 0 back means the lock was free and is
now yours. Faults are taken as for a load and then a store of the same word
(MISALIGNED, TLBMISS_R, then PROTECT if the page is read-only): the memory
and `rd` change only when both accesses succeed.

### 0x80–0x9F — Indexed and post-increment loads and stores (D59)

Added for the three-bus datapath with its address adder (D59): a load or
store's address goes straight into MAR, so these take no longer than a plain
load. The low opcode bits mean what they do in 0x40–0x4F (size, sign,
store), in every group.

**Indexed (X):** address = rs1 + rs2 × scale, scale 1, 2, 4 or 8 (`S` =
log2 scale). Written `LWX rd, rs1, rs2, 4` (`, 1` may be left out).

| OP | Mnemonic | Operation |
|---|---|---|
| 0x80 | `LBX  rd,rs1,rs2,s` | rd = sext(mem8) |
| 0x81 | `LHX  rd,rs1,rs2,s` | rd = sext(mem16) |
| 0x82 | `LWX  rd,rs1,rs2,s` | rd = mem32 |
| 0x84 | `LBUX rd,rs1,rs2,s` | rd = zext(mem8) |
| 0x85 | `LHUX rd,rs1,rs2,s` | rd = zext(mem16) |
| 0x88 | `SBX  ra,rs1,rs2,s` | mem8 = ra & 0xFF |
| 0x89 | `SHX  ra,rs1,rs2,s` | mem16 = ra & 0xFFFF |
| 0x8A | `SWX  ra,rs1,rs2,s` | mem32 = ra |

**Post-increment (P):** the access at rs1 + sext(OFF8), then rs1 += sext(INC8)
(both −128…127). Written `LWP rd, off(rs1), inc` (`(rs1)` for offset 0). rs1
is written **after** the access has succeeded, and last: a fault leaves rd
and rs1 unchanged (the instruction can be restarted), and with rd = rs1 a
load's value is overwritten by the increment.

| OP | Mnemonic | Operation |
|---|---|---|
| 0x90 | `LBP  rd,off(rs1),inc` | rd = sext(mem8); rs1 += inc |
| 0x91 | `LHP  rd,off(rs1),inc` | rd = sext(mem16); rs1 += inc |
| 0x92 | `LWP  rd,off(rs1),inc` | rd = mem32; rs1 += inc |
| 0x94 | `LBUP rd,off(rs1),inc` | rd = zext(mem8); rs1 += inc |
| 0x95 | `LHUP rd,off(rs1),inc` | rd = zext(mem16); rs1 += inc |
| 0x98 | `SBP  ra,off(rs1),inc` | mem8 = ra & 0xFF; rs1 += inc |
| 0x99 | `SHP  ra,off(rs1),inc` | mem16 = ra & 0xFFFF; rs1 += inc |
| 0x9A | `SWP  ra,off(rs1),inc` | mem32 = ra; rs1 += inc |

**`LI32 rd, value`** (0x2E, two words): rd = the word that follows the
instruction, read as instructions are (execute permission); the next
instruction is the one after it. rd is in field **B** (A and the low 16 bits
are 0), which keeps the CPU card's decode table at eight must-be-zero
classes. The assembler's `LA`, and `LI` of a value that needs 32 bits, are
`LI32` (the same 8 bytes as `LUI` + `ORI`, in 3 cycles instead of 5).

### 0x50–0x5F — Conditional branches (I)

`target = PC + 4 + sext(imm) × 4`. Compares A with B (D5).

| OP | Mnemonic | Taken if |
|---|---|---|
| 0x50 | `BEQ ra,rb,label` | ra == rb |
| 0x51 | `BNE ra,rb,label` | ra != rb |
| 0x54 | `BLT ra,rb,label` | signed ra < rb |
| 0x55 | `BGE ra,rb,label` | signed ra ≥ rb |
| 0x56 | `BLTU ra,rb,label` | unsigned ra < rb |
| 0x57 | `BGEU ra,rb,label` | unsigned ra ≥ rb |

Assembler aliases: `BGT a,b` = `BLT b,a`; `BLE a,b` = `BGE b,a` (and the
unsigned forms); `BEQZ a` = `BEQ a,r0`; `BNEZ a` = `BNE a,r0`.

### 0x60–0x6F — Jumps, calls, return

The stack is full-descending, SP = r15 (D6). Calls push the return address
(D6). SP and PC are committed only after the memory access succeeds, so a
faulted CALL or RET can be restarted.

| OP | Mnemonic | Fmt | Operation |
|---|---|---|---|
| 0x60 | `JMP label` | J | PC = PC + 4 + sext(off) × 4 |
| 0x61 | `CALL label` | J | mem32[SP−4] = PC + 4; SP −= 4; PC = target |
| 0x62 | `JR rb` | I (B only) | PC = rb (must be a multiple of 4) |
| 0x63 | `CALLR rb` | I (B only) | mem32[SP−4] = PC + 4; SP −= 4; PC = rb |
| 0x64 | `RET` | — | PC = mem32[SP]; SP += 4 |

Assembler aliases:

| Alias | Expands to |
|---|---|
| `NOP` | `ADD r0,r0,r0` |
| `MOV rd,rs` | `ADD rd,rs,r0` |
| `NEG rd,rs` | `SUB rd,r0,rs` |
| `NOT rd,rs` | `NOR rd,rs,r0` |
| `PUSH rs` | `ADDI sp,sp,-4` + `SW rs,0(sp)` |
| `POP rd` | `LW rd,0(sp)` + `ADDI sp,sp,4` |

### 0x70–0x7F — System (reserved in v0)

Reserved for `SYSCALL`, return-from-exception, status/MMU register access and
user-SP access (D1, D6). In v1 they decode as illegal.

All other opcodes are illegal.

## 4. Reset state (v1)

- PC = 0x0000_0000.
- Supervisor mode.
- r1–r15 = 0 in the emulator (undefined in hardware). Startup code must set SP.

## 5. Memory map (emulator v1, physical, no MMU)

| Range | Size | Use |
|---|---|---|
| 0x0000_0000 – 0x000F_FFFF | 1 MiB | ROM (program image loaded here; writes are an error) |
| 0x0010_0000 – 0x03FF_FFFF | up to 63 MiB | RAM: one to four 16 MiB memory cards in windows 0–3 (D52); the ROM hides the first MiB of window 0 |
| 0xFE00_0000 – 0xFE07_FFFF | 64 KiB a card | video memory (D52, D53) |
| 0xFFFF_8000 – 0xFFFF_FFFF | 32 KiB | I/O page: eight 4 KiB device windows (D52) |
| everything else | — | unmapped → exception |

The I/O page is the top 32 KiB, so `LB rd,-256(r0)` reaches `0xFFFF_FF00`
with no extra instructions: a sign-extended 16-bit offset from r0 covers
0xFFFF_8000–0xFFFF_FFFF, echoing the P8X $FF00 I/O page.

**I/O page (D50, D52):** each device card answers in a 4 KiB window chosen
by its jumpers, its registers in the window's top 256 bytes; the defaults
put the I/O card in window 7 and the disk card in window 6. Byte
registers answer byte accesses only; the timer, word accesses only; anything
else gets no `-ACK` → BUSERROR. Full register descriptions: the programmer's
guide ([programmers-guide.md](programmers-guide.md)).

| Address | Device |
|---|---|
| 0xFFFF_FF00–07 | UART 0, the console (TL16C550 registers 0–7): data at FF00, line status at FF05 (bit 0 = receive data ready, bit 5 = transmit ready) |
| 0xFFFF_FF08–0F | UART 1 (TL16C550) |
| 0xFFFF_FF10/14/18 | timer TCOUNT / TRELOAD / TCTRL (isa-system.md B4) |
| 0xFFFF_FF20 | input port (8 bits) |
| 0xFFFF_FF21 | output port (8 bits, reads back) |
| 0xFFFF_FF30–3F | UARTs 2 and 3 (TL16C550) |
| 0xFFFF_FF40/41 | real-time clock (D23) |
| 0xFFFF_FF50/51, FF54/55 | USB ports 0 and 1 (CH376S, D43) |
| 0xFFFF_FF52/53 | network coprocessor (D45) |
| 0xFFFF_EF00–07 | the disk card's port 0, CompactFlash, True-IDE task file (16-bit data register, D26); ports 1–3 follow, 16 bytes each |

## 6. Exceptions (v1)

The emulator stops and prints the PC, the offending instruction and all
registers on:
- illegal instruction
- misaligned access, or a JR/CALLR target that is not a multiple of 4
- unmapped address
- write to ROM

## 7. Review outcome (2026-10-11)

All four review questions were accepted as proposed:
1. 8-bit opcode; conditional branches reach ±128 KiB.
2. Branch and jump offsets are relative to PC + 4 (the next instruction).
3. Memory map as in §5.
4. `NOR` added at 0x1A; `NOT rd,rs` = `NOR rd,rs,r0`.
