#!/usr/bin/env python3
"""P32W ISA opcode table -- the single source of truth (D7).
ISA v0: docs/isa-v0.md; stage A system additions: docs/isa-system.md.

The assembler (assembler/p32asm.py) imports OPC and FMT_ZERO from here;
generators/gen_p32opc.py emits emulator/p32opc.h from the same data, so the
assembler and the emulator can never disagree about an encoding. The later
microcode generator will import this module too.

Instruction word:  OP[31:24]  A[23:20]  B[19:16]  C[15:12]  IMM16[15:0] / OFF24[23:0]

Formats (operand syntax as written in assembly):
  R    rd, rs1, rs2        A, B, C            bits 11:0 must be 0
  I    rd, rs1, simm16     A, B, IMM16        immediate sign-extended
  IU   rd, rs1, uimm16     A, B, IMM16        immediate zero-extended
  SH   rd, rs1, shamt      A, B, IMM16[4:0]   bits 15:5 must be 0
  LUI  rd, uimm16          A, IMM16           B must be 0
  LD   rd, simm16(rs1)     A, B, IMM16
  ST   ra, simm16(rs1)     A = data register, B = base
  BR   ra, rb, label       A, B, IMM16 = word offset from PC + 4
  J    label               OFF24 = word offset from PC + 4
  JR   rb                  B only             A and IMM16 must be 0
  N    (no operands)                          bits 23:0 must be 0
  CR   MFCR rd, cr / MTCR cr, rs   A = general register, B = control register,
                                    bits 15:0 must be 0
  X    rd, rs, rt[, scale]  A, B, C, bits 1:0 = log2(scale)   address = rs + rt * scale
                           (scale 1, 2, 4 or 8); bits 11:2 must be 0 (D59)
  P    rd, off(rs), inc    A, B, IMM16 = off8 (15:8) | inc8 (7:0), both signed:
                           the access at rs + off, then rs += inc (D59)
  L32  rd, value32         B = rd; A and bits 15:0 must be 0 (as JR, so the
                           decode table's zero classes stay eight); the next
                           word is the value (D59)
"""

# mnemonic: (opcode, format)
OPC = {
    "HALT":  (0x01, "N"),

    "ADD":   (0x10, "R"),  "SUB":  (0x11, "R"),  "AND":  (0x12, "R"),
    "OR":    (0x13, "R"),  "XOR":  (0x14, "R"),  "SLL":  (0x15, "R"),
    "SRL":   (0x16, "R"),  "SRA":  (0x17, "R"),  "SLT":  (0x18, "R"),
    "SLTU":  (0x19, "R"),  "NOR":  (0x1A, "R"),

    "ADDI":  (0x20, "I"),  "ANDI": (0x22, "IU"), "ORI":  (0x23, "IU"),
    "XORI":  (0x24, "IU"), "SLLI": (0x25, "SH"), "SRLI": (0x26, "SH"),
    "SRAI":  (0x27, "SH"), "SLTI": (0x28, "I"),  "SLTIU": (0x29, "I"),
    "LUI":   (0x2F, "LUI"),  "LI32": (0x2E, "L32"),        # D59: a 32-bit constant, two words

    "MUL":   (0x30, "R"),  "MULH": (0x31, "R"),  "MULHU": (0x32, "R"),
    "DIV":   (0x34, "R"),  "DIVU": (0x35, "R"),  "REM":  (0x36, "R"),
    "REMU":  (0x37, "R"),

    "LB":    (0x40, "LD"), "LH":   (0x41, "LD"), "LW":   (0x42, "LD"),
    "LBU":   (0x44, "LD"), "LHU":  (0x45, "LD"),
    "SB":    (0x48, "ST"), "SH":   (0x49, "ST"), "SW":   (0x4A, "ST"),
    "SWAP":  (0x4C, "LD"),                       # atomic exchange with memory (D51)

    # D59: indexed (rs + rt * scale) and post-increment (rs + off, then
    # rs += inc) loads and stores; the low bits as in the 0x40 group
    "LBX":   (0x80, "X"),  "LHX":  (0x81, "X"),  "LWX":  (0x82, "X"),
    "LBUX":  (0x84, "X"),  "LHUX": (0x85, "X"),
    "SBX":   (0x88, "X"),  "SHX":  (0x89, "X"),  "SWX":  (0x8A, "X"),
    "LBP":   (0x90, "P"),  "LHP":  (0x91, "P"),  "LWP":  (0x92, "P"),
    "LBUP":  (0x94, "P"),  "LHUP": (0x95, "P"),
    "SBP":   (0x98, "P"),  "SHP":  (0x99, "P"),  "SWP":  (0x9A, "P"),

    "BEQ":   (0x50, "BR"), "BNE":  (0x51, "BR"), "BLT":  (0x54, "BR"),
    "BGE":   (0x55, "BR"), "BLTU": (0x56, "BR"), "BGEU": (0x57, "BR"),

    "JMP":   (0x60, "J"),  "CALL": (0x61, "J"),  "JR":   (0x62, "JR"),
    "CALLR": (0x63, "JR"), "RET":  (0x64, "N"),

    # stage A: traps and privilege (docs/isa-system.md)
    "SYSCALL": (0x70, "N"), "RTE":  (0x71, "N"),  "MFCR": (0x72, "CR"),
    "MTCR":  (0x73, "CR"), "BREAK": (0x74, "N"),

    # stage B: interrupts (docs/isa-system.md)
    "WAIT":  (0x75, "N"),

    # stage C: virtual memory (docs/isa-system.md)
    "TLBW":  (0x76, "N"),
}

# Privileged: trap with PRIVILEGE in user mode.
PRIVILEGED = {"HALT", "RTE", "MFCR", "MTCR", "WAIT", "TLBW"}

# Control registers (docs/isa-system.md sections 2, B1, C4); 11-15 reserved.
CREGS = {"STATUS": 0, "EPC": 1, "CAUSE": 2, "BADADDR": 3, "VBR": 4, "USP": 5,
         "IPEND": 6, "IMASK": 7, "ASID": 8, "TLBHI": 9, "TLBLO": 10}

# Exception causes (docs/isa-system.md section 3).
CAUSES = {"ILLEGAL": 1, "PRIVILEGE": 2, "MISALIGNED": 3, "BUSERROR": 4,
          "SYSCALL": 5, "BREAK": 6,
          "TLBMISS_X": 8, "TLBMISS_R": 9, "TLBMISS_W": 10, "PROTECT": 11}

FORMATS = ("R", "I", "IU", "SH", "LUI", "LD", "ST", "BR", "J", "JR", "N", "CR", "X", "P", "L32")

# Bits that must be 0 in each format; a set bit makes the word illegal.
FMT_ZERO = {
    "R":   0x00000FFF,
    "I":   0, "IU": 0, "LD": 0, "ST": 0, "BR": 0, "J": 0,
    "SH":  0x0000FFE0,
    "LUI": 0x000F0000,
    "JR":  0x00F0FFFF,
    "N":   0x00FFFFFF,
    "CR":  0x0000FFFF,
    "X":   0x00000FFC,
    "P":   0,
    "L32": 0x00F0FFFF,
}

# Register names accepted by the assembler.
REGS = {"r%d" % i: i for i in range(16)}
REGS.update({"zero": 0, "sp": 15})

def _check():
    ops = [op for op, _ in OPC.values()]
    assert len(ops) == len(set(ops)), "duplicate opcode"
    assert 0x00 not in ops and 0xFF not in ops, "0x00/0xFF are reserved illegal"
    for m, (op, f) in OPC.items():
        assert f in FORMATS, (m, f)
        assert 0 < op < 0xFF, m
    assert PRIVILEGED <= set(OPC), PRIVILEGED - set(OPC)
_check()

if __name__ == "__main__":
    for m, (op, f) in sorted(OPC.items(), key=lambda kv: kv[1][0]):
        print("0x%02X  %-6s %s" % (op, m, f))
