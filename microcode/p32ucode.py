#!/usr/bin/env python3
"""P32W microcode generator (docs/microarch.md, D14).

Builds the control store for the microarchitecture from the ISA's single
source of truth (p32isa.py), in the lineage of P8X's genucode.py:

  * the microword fields and their codes (section 6.2),
  * the microcode of every instruction and of the internal sequences
    (reset, trap entry, interrupt entry),
  * a static check of the commit-after-access rule (section 5),
  * the flash images and the C header the microcode-level emulator
    (emulator/p32uemu.c) is built from.

    python3 p32ucode.py [-o DIR] [--header FILE] [--stats]

Outputs (build artifacts -- edit this generator, never the outputs):
  DIR/p32ucode.bin       16384 words x 8 bytes, little-endian (emulator image)
  DIR/u0.bin .. u5.bin   one 16 KiB image per flash chip (byte n of each word)
  --header FILE          field layout, codes and tables for p32uemu.c

Microaddress = SEQ (bit 13) | OPCODE (bits 12:5) | STEP (bits 4:1) | COND (bit 0).
Every step is a pair (cond-0 word, cond-1 word); the COND field of a word
selects the condition that picks the twin for the NEXT step (P8X scheme).
The condition is evaluated on the state after the word's clock edge.
"""
import os, sys, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32isa import OPC, PRIVILEGED, CREGS, CAUSES, FMT_ZERO  # noqa: E402

# ---------------------------------------------------------------- word layout
FIELDS = [("DOE", 5), ("DLD", 5), ("RSEL", 3), ("RCONST", 5), ("ALU", 5),
          ("IMM", 3), ("MEM", 3), ("PCINC", 1), ("SEQ", 2), ("COND", 3),
          ("MODE", 3), ("RAISE", 4), ("LOCK", 1)]
SHIFT, pos = {}, 0
for name, width in FIELDS:
    SHIFT[name] = pos
    pos += width
WORD_BITS = pos                                  # 43
WORD_BYTES = (WORD_BITS + 7) // 8                # 6 flash chips

def enum(*names):
    return {n: i for i, n in enumerate(names)}

# Who drives the data bus / who latches it (section 6.2).
DOE = enum("NONE", "REG", "ALU", "IMM", "PC", "MEM", "MDRX", "MDRS", "MAR",
           "CAUSE", "BADADDR", "CRHW")
DLD = enum("NONE", "REG", "ALUA", "ALUB", "IR", "PC", "MAR", "MDR", "CRHW")
# Register-file address source: an IR field (A, B, C -- with the r15 bank
# rule), a constant (physical, no bank rule), the CR decode table, or SP
# (= 15 through the bank rule, for CALL/RET).
RSEL = enum("A", "B", "C", "CONST", "CRMAP", "SP")
ALU = enum("ADD", "SUB", "AND", "OR", "XOR", "NOR", "SLL", "SRL", "SRA", "SLT",
           "SLTU", "MUL", "MULH", "MULHU", "DIV", "DIVU", "REM", "REMU", "PASSA")
IMM = enum("SEXT16", "ZEXT16", "LUI", "BR", "J", "FOUR", "ZERO")
MEM = enum("NONE", "FETCH", "LOAD", "STORE", "LOADW", "STOREW", "CHECKW")
SEQ = enum("NEXT", "END", "TRAP")
COND = enum("ZERO", "EQ", "LT", "LTU", "CRILL", "CRHW", "FFAULT", "MMUF")   # MMUF: the trap came from the MMU / data card (D17)
MODE = enum("NONE", "TRAPENTER", "RTE", "TLBW", "HALT", "WAIT")

# Physical register-file addresses (section 2.1).
R_EPC, R_CAUSE, R_BADADDR, R_VBR = 16, 17, 18, 19
T0, T1 = 20, 21                                  # microcode scratch T0..T7 = 20..27
R_USP, R_SSP = 15, 31
R_SINK = 28                                      # writes to r0 land here (the write sink)

# Control-register decode table: CR number -> SRAM address or hardware register.
HWCR = enum("STATUS", "IPEND", "IMASK", "ASID", "TLBHI", "TLBLO")
CR_SRAM = {"EPC": R_EPC, "CAUSE": R_CAUSE, "BADADDR": R_BADADDR, "VBR": R_VBR, "USP": R_USP}
CRMAP = {}                                       # cr -> ("sram", addr) | ("hw", id)
for name, n in CREGS.items():
    CRMAP[n] = ("sram", CR_SRAM[name]) if name in CR_SRAM else ("hw", HWCR[name])

# Internal sequences (SEQ = 1).
ISEQ = enum("RESET", "TRAP", "INTR")

# Memory accesses that can fault (section 5).
FAULTING = {MEM["FETCH"], MEM["LOAD"], MEM["STORE"], MEM["LOADW"], MEM["STOREW"], MEM["CHECKW"]}

TABLES = dict(DOE=DOE, DLD=DLD, RSEL=RSEL, ALU=ALU, IMM=IMM, MEM=MEM, SEQ=SEQ,
              COND=COND, MODE=MODE)

def w(doe="NONE", dld="NONE", rsel="A", rconst=0, alu="ADD", imm="SEXT16",
      mem="NONE", pcinc=0, seq="NEXT", cond="ZERO", mode="NONE", raise_=0, lock=0):
    vals = dict(DOE=DOE[doe], DLD=DLD[dld], RSEL=RSEL[rsel], RCONST=rconst,
                ALU=ALU[alu], IMM=IMM[imm], MEM=MEM[mem], PCINC=pcinc,
                SEQ=SEQ[seq], COND=COND[cond], MODE=MODE[mode], RAISE=raise_, LOCK=lock)
    word = 0
    for name, width in FIELDS:
        v = vals[name]
        assert 0 <= v < (1 << width), (name, v)
        word |= v << SHIFT[name]
    return word

def field(word, name):
    width = dict(FIELDS)[name]
    return (word >> SHIFT[name]) & ((1 << width) - 1)

def both(word):
    """A step whose word does not depend on the condition."""
    return (word, word)

# ---------------------------------------------------------------- microcode
FETCH = [both(w(doe="PC", dld="MAR")),                                   # 0  MAR <- PC
         both(w(doe="MEM", dld="IR", mem="FETCH", pcinc=1))]             # 1  IR <- M[MAR], PC += 4

U = {}       # (seq, opcode) -> list of steps after fetch (seq 0) / from step 0 (seq 1)

def alu_rr(op):
    return [both(w(doe="REG", rsel="B", dld="ALUA")),
            both(w(doe="REG", rsel="C", dld="ALUB")),
            both(w(doe="ALU", alu=op, dld="REG", rsel="A", seq="END"))]

def alu_ri(op, imm):
    return [both(w(doe="REG", rsel="B", dld="ALUA")),
            both(w(doe="IMM", imm=imm, dld="ALUB")),
            both(w(doe="ALU", alu=op, dld="REG", rsel="A", seq="END"))]

def load():
    return [both(w(doe="REG", rsel="B", dld="ALUA")),
            both(w(doe="IMM", imm="SEXT16", dld="ALUB")),
            both(w(doe="ALU", alu="ADD", dld="MAR")),
            both(w(doe="MEM", mem="LOAD", dld="MDR")),                   # may fault
            both(w(doe="MDRX", dld="REG", rsel="A", seq="END"))]          # commit

def store():
    return [both(w(doe="REG", rsel="B", dld="ALUA")),
            both(w(doe="IMM", imm="SEXT16", dld="ALUB")),
            both(w(doe="ALU", alu="ADD", dld="MAR")),
            both(w(doe="REG", rsel="A", dld="MDR")),
            both(w(doe="MDRS", mem="STORE", seq="END"))]                 # may fault; the only effect

def swap():
    """D64: t = M[a]; M[a] = rd; rd = t. The old word waits in the ALU's A
    latch (not visible state) until the store has succeeded (commit after
    access, D6); rd gets it through PASSA."""
    return [both(w(doe="REG", rsel="B", dld="ALUA")),
            both(w(doe="IMM", imm="SEXT16", dld="ALUB")),
            both(w(doe="ALU", alu="ADD", dld="MAR")),
            both(w(doe="MEM", mem="LOADW", dld="MDR", lock=1)),          # may fault; locked: the bus is ours to the STOREW (D51, D54)
            both(w(doe="MDRX", dld="ALUA")),
            both(w(doe="REG", rsel="A", dld="MDR")),
            both(w(doe="MDRS", mem="STOREW")),                           # may fault: nothing committed
            both(w(doe="ALU", alu="PASSA", dld="REG", rsel="A", seq="END"))]

def branch(cond, taken_on):
    """Compare A with B; the condition picks the twin after the compare."""
    taken = [w(doe="PC", dld="ALUA"),                                    # (twin of step 4)
             w(doe="IMM", imm="BR", dld="ALUB"),
             w(doe="ALU", alu="ADD", dld="PC", seq="END")]
    not_taken = w(seq="END")
    s4 = (taken[0], not_taken) if taken_on == 0 else (not_taken, taken[0])
    return [both(w(doe="REG", rsel="A", dld="ALUA")),
            both(w(doe="REG", rsel="B", dld="ALUB")),
            both(w(cond=cond)),         # condition on the new A, B: a step later (D21 -- the
                                        # ALU module's compare is ready ~90 ns after B's edge,
                                        # the sequencer needs it ~55 ns after)
            s4, both(taken[1]), both(taken[2])]

def call_tail(final):
    """Shared by CALL and CALLR: T0 = SP - 4; M[T0] = PC; SP = T0; then `final`."""
    return [both(w(doe="REG", rsel="SP", dld="ALUA")),
            both(w(doe="IMM", imm="FOUR", dld="ALUB")),
            both(w(doe="ALU", alu="SUB", dld="REG", rsel="CONST", rconst=T0)),
            both(w(doe="REG", rsel="CONST", rconst=T0, dld="MAR")),
            both(w(doe="PC", dld="MDR")),
            both(w(doe="MDRS", mem="STOREW")),                           # may fault: nothing committed
            both(w(doe="REG", rsel="CONST", rconst=T0, dld="ALUA")),
            both(w(doe="ALU", alu="PASSA", dld="REG", rsel="SP"))] + final  # commit SP

def build():
    for m, (op, fmt) in OPC.items():
        k = (0, op)
        if m in ("ADD", "SUB", "AND", "OR", "XOR", "NOR", "SLL", "SRL", "SRA", "SLT",
                 "SLTU", "MUL", "MULH", "MULHU", "DIV", "DIVU", "REM", "REMU"):
            U[k] = alu_rr(m)
        elif m in ("ADDI", "SLTI", "SLTIU"):
            U[k] = alu_ri({"ADDI": "ADD", "SLTI": "SLT", "SLTIU": "SLTU"}[m], "SEXT16")
        elif m in ("ANDI", "ORI", "XORI", "SLLI", "SRLI", "SRAI"):
            U[k] = alu_ri(m[:-1], "ZEXT16")
        elif m == "LUI":
            U[k] = [both(w(doe="IMM", imm="LUI", dld="REG", rsel="A", seq="END"))]
        elif m == "SWAP":
            U[k] = swap()
        elif fmt == "LD":
            U[k] = load()
        elif fmt == "ST":
            U[k] = store()
        elif m == "BEQ":  U[k] = branch("EQ", 1)
        elif m == "BNE":  U[k] = branch("EQ", 0)
        elif m == "BLT":  U[k] = branch("LT", 1)
        elif m == "BGE":  U[k] = branch("LT", 0)
        elif m == "BLTU": U[k] = branch("LTU", 1)
        elif m == "BGEU": U[k] = branch("LTU", 0)
        elif m == "JMP":
            U[k] = [both(w(doe="PC", dld="ALUA")),
                    both(w(doe="IMM", imm="J", dld="ALUB")),
                    both(w(doe="ALU", alu="ADD", dld="PC", seq="END"))]
        elif m == "CALL":
            U[k] = call_tail([both(w(doe="PC", dld="ALUA")),
                              both(w(doe="IMM", imm="J", dld="ALUB")),
                              both(w(doe="ALU", alu="ADD", dld="PC", seq="END"))])
        elif m == "JR":
            U[k] = [both(w(doe="REG", rsel="B", dld="MAR")),
                    both(w(doe="MAR", mem="CHECKW", dld="PC", seq="END"))]   # misaligned: fault, no effect
        elif m == "CALLR":
            U[k] = [both(w(doe="REG", rsel="B", dld="MAR")),
                    both(w(doe="MAR", mem="CHECKW", dld="REG", rsel="CONST", rconst=T1))] + \
                   call_tail([both(w(doe="REG", rsel="CONST", rconst=T1, dld="PC", seq="END"))])
        elif m == "RET":
            U[k] = [both(w(doe="REG", rsel="SP", dld="MAR")),
                    both(w(doe="MEM", mem="LOADW", dld="MDR")),               # may fault
                    both(w(doe="MDRX", dld="MAR")),
                    both(w(doe="MAR", mem="CHECKW", dld="REG", rsel="CONST", rconst=T0)),  # may fault
                    both(w(doe="REG", rsel="SP", dld="ALUA")),
                    both(w(doe="IMM", imm="FOUR", dld="ALUB")),
                    both(w(doe="ALU", alu="ADD", dld="REG", rsel="SP")),     # commit SP
                    both(w(doe="REG", rsel="CONST", rconst=T0, dld="PC", seq="END"))]
        elif m == "HALT":
            U[k] = [both(w(mode="HALT"))]
        elif m == "SYSCALL":
            U[k] = [both(w(seq="TRAP", raise_=CAUSES["SYSCALL"]))]
        elif m == "BREAK":
            U[k] = [both(w(seq="TRAP", raise_=CAUSES["BREAK"]))]
        elif m == "RTE":
            U[k] = [both(w(doe="REG", rsel="CONST", rconst=R_EPC, dld="PC", mode="RTE", seq="END"))]
        elif m == "WAIT":
            U[k] = [both(w(mode="WAIT", seq="END"))]
        elif m == "TLBW":
            U[k] = [both(w(mode="TLBW", seq="END"))]
        elif m == "MFCR":
            illegal = w(seq="TRAP", raise_=CAUSES["ILLEGAL"])
            U[k] = [both(w(cond="CRILL")),
                    (w(cond="CRHW"), illegal),
                    (w(doe="REG", rsel="CRMAP", dld="ALUA"),                 # SRAM-backed CR
                     w(doe="CRHW", dld="REG", rsel="A", seq="END")),          # hardware CR
                    both(w(doe="ALU", alu="PASSA", dld="REG", rsel="A", seq="END"))]
        elif m == "MTCR":
            illegal = w(seq="TRAP", raise_=CAUSES["ILLEGAL"])
            U[k] = [both(w(cond="CRILL")),
                    (w(cond="CRHW"), illegal),
                    (w(doe="REG", rsel="A", dld="ALUA"),
                     w(doe="REG", rsel="A", dld="CRHW", seq="END")),
                    both(w(doe="ALU", alu="PASSA", dld="REG", rsel="CRMAP", seq="END"))]
        else:
            raise SystemExit("no microcode for %s" % m)

    # Internal sequences (SEQ = 1), from step 0.
    U[(1, ISEQ["RESET"])] = [
        both(w(doe="IMM", imm="ZERO", dld="REG", rsel="CONST", rconst=0)),   # r0 = 0 (D4)
        both(w(doe="IMM", imm="ZERO", dld="PC", seq="END"))]                 # PC = 0
    U[(1, ISEQ["TRAP"])] = [
        both(w(doe="PC", dld="ALUA", cond="FFAULT")),
        (w(doe="IMM", imm="FOUR", dld="ALUB"),                               # fault after fetch: EPC = PC - 4
         w(doe="IMM", imm="ZERO", dld="ALUB")),                              # fault in the fetch: EPC = PC
        both(w(doe="ALU", alu="SUB", dld="REG", rsel="CONST", rconst=R_EPC, cond="MMUF")),
        (w(doe="IMM", imm="ZERO", dld="REG", rsel="CONST", rconst=R_BADADDR),   # raised by the sequencer: 0 (D15)
         w(doe="BADADDR", dld="REG", rsel="CONST", rconst=R_BADADDR)),          # MMU fault: the MMU / data card's latch (D17)
        both(w(doe="CAUSE", dld="REG", rsel="CONST", rconst=R_CAUSE)),
        both(w(doe="REG", rsel="CONST", rconst=R_VBR, dld="PC", mode="TRAPENTER", seq="END"))]
    U[(1, ISEQ["INTR"])] = [
        both(w(doe="PC", dld="REG", rsel="CONST", rconst=R_EPC)),           # EPC = next instruction
        both(w(doe="IMM", imm="ZERO", dld="REG", rsel="CONST", rconst=R_BADADDR)),
        both(w(doe="CAUSE", dld="REG", rsel="CONST", rconst=R_CAUSE)),
        both(w(doe="REG", rsel="CONST", rconst=R_VBR, dld="PC", mode="TRAPENTER", seq="END"))]

# ---------------------------------------------------------------- commit-after-access check
def writes_state(word):
    """Does this word change programmer-visible state (if it completes)?"""
    dld, rsel, rconst = field(word, "DLD"), field(word, "RSEL"), field(word, "RCONST")
    if dld == DLD["REG"]:
        if rsel == RSEL["CONST"] and 20 <= rconst <= 27:
            return False                                 # T0..T7 are scratch
        return True
    if dld in (DLD["PC"], DLD["CRHW"]):
        return True
    if field(word, "MEM") in (MEM["STORE"], MEM["STOREW"]):
        return True
    return field(word, "MODE") in (MODE["TRAPENTER"], MODE["RTE"], MODE["TLBW"])

def check(steps, name):
    """On every path through an instruction (after fetch), nothing visible may
    be written before the last possibly-faulting access. The access cycle
    itself is safe: a fault suppresses its effects."""
    errors = []
    def walk(i, path):
        if i == len(steps):
            return finish(path)
        w0, w1 = steps[i]
        prev_cond = field(path[-1], "COND") if path else COND["ZERO"]
        choices = {w0} if prev_cond == COND["ZERO"] else {w0, w1}
        for word in choices:
            p = path + [word]
            if field(word, "SEQ") in (SEQ["END"], SEQ["TRAP"]) or field(word, "MODE") == MODE["HALT"]:
                finish(p)
            else:
                walk(i + 1, p)
    def finish(path):
        last = max((j for j, wd in enumerate(path) if field(wd, "MEM") in FAULTING), default=-1)
        for j, wd in enumerate(path[:last]):
            if writes_state(wd):
                errors.append("%s: step %d writes visible state before the last access (step %d)"
                              % (name, j + 2, last + 2))
    walk(0, [])
    return errors

# ---------------------------------------------------------------- the bus lock (D51, D54)
BUSCYCLES = {MEM["FETCH"], MEM["LOAD"], MEM["STORE"], MEM["LOADW"], MEM["STOREW"]}

def check_lock(steps, name):
    """LOCK (LOCKREQ on the local link) marks a LOADW whose bus tenure lasts
    to the next bus cycle, which must be a STOREW: the MMU / data card holds
    -LOCK from the one to the end of the other (or a fault). Any other word
    with LOCK, or a locked read not followed by its STOREW, is an error."""
    errors = []
    for i, pair in enumerate(steps):
        for wd in pair:
            if not field(wd, "LOCK"):
                continue
            if field(wd, "MEM") != MEM["LOADW"]:
                errors.append("%s step %d: LOCK on a cycle that is not LOADW" % (name, i + 2))
                continue
            nxt = [field(x, "MEM") for p in steps[i + 1:] for x in {p[0], p[1]} if field(x, "MEM") in BUSCYCLES]
            if not nxt or nxt[0] != MEM["STOREW"]:
                errors.append("%s step %d: a locked LOADW whose next bus cycle is not its STOREW" % (name, i + 2))
    return errors

# ---------------------------------------------------------------- decode tables (D17, D18)
# Must-be-zero classes, as the CPU card's zero detectors test them.
ZCLASS = {0: 0, 0x00000FFF: 1, 0x0000FFE0: 2, 0x000F0000: 3, 0x00F0FFFF: 4, 0x00FFFFFF: 5, 0x0000FFFF: 6}

def decode_tables():
    """Two 256-byte flash images for the CPU card's decode tables.
    opcode table, address = opcode:  bit 0 legal, 1 privileged, 4:2 must-be-zero
      class, 6:5 access size (0 = byte, 1 = half, 2 = word), 7 signed load.
    CR table, address = IR bits 19:16: bit 0 illegal (reserved CR), 1 hardware
      register (else SRAM), 6:2 SRAM address or hardware-register number."""
    from p32isa import FMT_ZERO
    op = bytearray(256)
    for m, (code, fmt) in OPC.items():
        size = {"LB": 0, "LBU": 0, "SB": 0, "LH": 1, "LHU": 1, "SH": 1}.get(m, 2)
        signed = m in ("LB", "LH")
        op[code] = (1 | (m in PRIVILEGED) << 1 | ZCLASS[FMT_ZERO[fmt]] << 2
                    | size << 5 | signed << 7)
    cr = bytearray(256)
    for n in range(16):
        if n not in CRMAP:
            cr[n] = 1                                    # reserved: illegal
        else:
            kind, a = CRMAP[n]
            cr[n] = (2 if kind == "hw" else 0) | a << 2
    return bytes(op), bytes(cr)

# ---------------------------------------------------------------- images
def image():
    words = [0] * (1 << 14)
    def addr(seq, op, step, cond):
        return seq << 13 | op << 5 | step << 1 | cond
    for op in range(256):                       # fetch is step 0-1 of every opcode
        for step, (w0, w1) in enumerate(FETCH):
            words[addr(0, op, step, 0)], words[addr(0, op, step, 1)] = w0, w1
    for (seq, op), steps in U.items():
        base = 2 if seq == 0 else 0
        assert base + len(steps) <= 16, ("too many steps", seq, op)
        for i, (w0, w1) in enumerate(steps):
            words[addr(seq, op, base + i, 0)], words[addr(seq, op, base + i, 1)] = w0, w1
    return words

def cycles(steps):
    """(shortest, longest) cycles including the 2 fetch cycles."""
    lens = []
    def walk(i, n, prev_cond):
        w0, w1 = steps[i]
        for word in ({w0} if prev_cond == COND["ZERO"] else {w0, w1}):
            if field(word, "SEQ") in (SEQ["END"], SEQ["TRAP"]) or field(word, "MODE") == MODE["HALT"] \
               or i + 1 == len(steps):
                lens.append(n + 1)
            else:
                walk(i + 1, n + 1, field(word, "COND"))
    walk(0, 2, COND["ZERO"])
    return min(lens), max(lens)

def header(path):
    L = ["/* GENERATED by microcode/p32ucode.py -- do not edit. */",
         "#ifndef P32UC_H", "#define P32UC_H", "#include <stdint.h>", "",
         "#define UC_WORDS %d" % (1 << 14), "#define UC_BITS  %d" % WORD_BITS, ""]
    for name, width in FIELDS:
        L.append("#define UF_%s(w) ((unsigned)((w) >> %d) & 0x%Xu)" % (name, SHIFT[name], (1 << width) - 1))
    for tname, table in TABLES.items():
        L.append("")
        L += ["#define %s_%s %d" % (tname, n, v) for n, v in table.items()]
    L.append("")
    L += ["#define ISEQ_%s %d" % (n, v) for n, v in ISEQ.items()]
    L += ["", "#define PR_EPC %d" % R_EPC, "#define PR_CAUSE %d" % R_CAUSE,
          "#define PR_BADADDR %d" % R_BADADDR, "#define PR_VBR %d" % R_VBR,
          "#define PR_USP %d" % R_USP, "#define PR_SSP %d" % R_SSP,
          "#define PR_SINK %d" % R_SINK, ""]
    L += ["#define HWCR_%s %d" % (n, v) for n, v in HWCR.items()]
    L += ["", "/* CR decode table: kind 0 = illegal, 1 = SRAM (addr), 2 = hardware (HWCR id) */",
          "static const unsigned char uc_crkind[16] = {"]
    L += ["    [%d] = %d," % (n, 1 if k == "sram" else 2) for n, (k, a) in sorted(CRMAP.items())]
    L += ["};", "static const unsigned char uc_craddr[16] = {"]
    L += ["    [%d] = %d," % (n, a) for n, (k, a) in sorted(CRMAP.items())]
    L += ["};", "", "#endif", ""]
    with open(path, "w") as fh:
        fh.write("\n".join(L))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", default=HERE, help="directory for the images")
    ap.add_argument("--header")
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()
    build()
    errors = []
    names = {op: m for m, (op, f) in OPC.items()}
    for (seq, op), steps in U.items():          # step 2 may be suppressed by a decode trap
        if seq == 0 and steps and any(field(wd, "MEM") != MEM["NONE"] for wd in steps[0]):
            errors.append("%s: step 2 starts a memory cycle (it must be suppressible, D18)" % names[op])
    for (seq, op), steps in U.items():          # the register file has one port
        for i, pair in enumerate(steps):
            for wd in pair:
                if field(wd, "DOE") == DOE["REG"] and field(wd, "DLD") == DLD["REG"]:
                    errors.append("%s step %d reads and writes the register file in one cycle"
                                  % (names.get(op, "internal %d" % op) if seq == 0 else "internal %d" % op, i))
    for (seq, op), steps in U.items():
        if seq == 0:
            errors += check(steps, names[op])
            errors += check_lock(steps, names[op])
    if errors:
        sys.exit("commit-after-access violations:\n  " + "\n  ".join(errors))
    words = image()
    with open(os.path.join(args.o, "p32ucode.bin"), "wb") as fh:
        for wd in words:
            fh.write(wd.to_bytes(8, "little"))
    dop, dcr = decode_tables()
    for fname, data in (("dec_op.bin", dop), ("dec_cr.bin", dcr)):
        with open(os.path.join(args.o, fname), "wb") as fh:
            fh.write(data)
    for chip in range(WORD_BYTES):
        with open(os.path.join(args.o, "u%d.bin" % chip), "wb") as fh:
            fh.write(bytes((wd >> (8 * chip)) & 0xFF for wd in words))
    if args.header:
        header(args.header)
    if args.stats:
        print("microword: %d bits (%d flash chips); %d instructions; commit-after-access: OK"
              % (WORD_BITS, WORD_BYTES, len([k for k in U if k[0] == 0])))
        for m, (op, f) in sorted(OPC.items(), key=lambda kv: kv[1][0]):
            lo, hi = cycles(U[(0, op)])
            print("  0x%02X %-7s %s cycles" % (op, m, lo if lo == hi else "%d-%d" % (lo, hi)))

if __name__ == "__main__":
    main()
