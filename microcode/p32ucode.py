#!/usr/bin/env python3
"""P32W microcode generator (docs/microarch.md, D14, D55).

Builds the control store for the three-bus datapath from the ISA's single
source of truth (p32isa.py):

  * the microword fields and their codes,
  * the microcode of every instruction and of the internal sequences
    (reset, trap entry, interrupt entry),
  * static checks: commit after access, the next fetch's address, each
    word's own consistency, the bus lock,
  * the flash images and the C header the microcode-level emulator
    (emulator/p32uemu.c) is built from.

    python3 p32ucode.py [-o DIR] [--header FILE] [--stats]

Outputs (build artifacts -- edit this generator, never the outputs):
  DIR/p32ucode.bin       16384 words x 8 bytes, little-endian (emulator image)
  DIR/u0.bin .. u6.bin   one 16 KiB image per flash chip (byte n of each word)
  --header FILE          field layout, codes and tables for p32uemu.c

The datapath (docs/microarch.md) has three buses:
  A  the first operand: register-file read port A, or the PC
  B  the second operand: read port B, or the immediate unit
  R  the result: the ALU, memory data (a load's steered to bit 0), the
     immediate unit, MAR, the store data (MDR, replicated), CAUSE, BADADDR,
     a hardware control register; written to one destination: the register
     file's write port, PC, MAR, PC and MAR together, IR, MDR or a hardware
     control register.
The register file reads two registers and writes one in every cycle. The
ALU is pipelined: a word with ALUGO latches A, B and its function at the
clock edge; the result is on R (source ALU) from the next cycle until the
next ALUGO, and the comparison of the latched operands (EQ, LT, LTU) is a
condition from that edge. MDR (the store data) loads from B, or from R.

Every instruction starts with MAR = PC: its first step (FETCH) reads the
instruction into IR and adds 4 to the PC; its last step sets MAR to the
next PC (MARPC, or PC and MAR loaded together) -- so the next fetch needs
no cycle of its own.

Microaddress = SEQ (bit 13) | OPCODE (bits 12:5) | STEP (bits 4:1) | COND (bit 0).
Every step is a pair (cond-0 word, cond-1 word); the COND field of a word
selects the condition that picks the twin for the NEXT step. The condition
is evaluated on the state after the word's clock edge.
"""
import os, sys, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32isa import OPC, PRIVILEGED, CREGS, CAUSES, FMT_ZERO  # noqa: E402

# ---------------------------------------------------------------- word layout
FIELDS = [("ASRC", 2), ("BSRC", 2), ("RSRC", 4), ("RDST", 3), ("MDRB", 1), ("MARPC", 1),
          ("PCINC", 1), ("ALUGO", 1), ("ALU", 5), ("RSA", 3), ("RSB", 3), ("RSW", 3),
          ("RCONST", 5), ("IMM", 3), ("MEM", 3), ("LOCK", 1), ("SEQ", 2), ("COND", 3),
          ("MODE", 3), ("RAISE", 4), ("AOFF", 2), ("MARINC", 1)]
SHIFT, pos = {}, 0
for name, width in FIELDS:
    SHIFT[name] = pos
    pos += width
WORD_BITS = pos                                  # 56
WORD_BYTES = (WORD_BITS + 7) // 8                # 7 flash chips

def enum(*names):
    return {n: i for i, n in enumerate(names)}

ASRC = enum("NONE", "REG", "PC")
BSRC = enum("NONE", "REG", "IMM")
RSRC = enum("NONE", "ALU", "MEM", "IMM", "MAR", "MDRS", "CAUSE", "BADADDR", "CRHW", "AGU")
# The address adder (AGU, D59): R = A + an offset, in the cycle the operands
# are read, so a load or store's address reaches MAR without the ALU's cycle.
# Its offset (AOFF): the IR's 16-bit offset sign-extended, the post-increment
# format's 8-bit offset (IR 15:8), or B shifted by IR 1:0 (an index).
AOFF = enum("SEXT16", "OFF8", "INDEX")
RDST = enum("NONE", "REG", "PC", "MAR", "PCMAR", "IR", "MDR", "CRHW")
# Register-file address source, for each of the three ports: an IR field (A,
# B, C -- with the r15 bank rule), the constant RCONST (physical, no bank
# rule; one per word, for whichever ports name it), the CR decode table, or
# SP (= 15 through the bank rule, for CALL/RET). The order is the CPU card's:
# codes 0-3 pick a 4-bit field in a 4:1 mux (SP = 15), code 4 or 5 the 5-bit
# constant or the CR table's address.
RSEL = enum("A", "B", "C", "SP", "CONST", "CRMAP")
ALU = enum("ADD", "SUB", "AND", "OR", "XOR", "NOR", "SLL", "SRL", "SRA", "SLT",
           "SLTU", "MUL", "MULH", "MULHU", "DIV", "DIVU", "REM", "REMU", "PASSA")
IMM = enum("SEXT16", "ZEXT16", "LUI", "BR", "J", "FOUR", "ZERO", "INC8")   # INC8: IR 7:0, sign-extended (D59)
MEM = enum("NONE", "FETCH", "LOAD", "STORE", "LOADW", "STOREW", "CHECKW", "IACK")
# IACK (D57): the interrupt acknowledge cycle. MAR holds CAUSE (IRQ | n); the
# MMU / data card runs the bus cycle with -IACK and A4:A2 = n, and returns MAR
# with bits 15:8 = the vector the interrupting card puts on D7:0 (FF: none).
SEQ = enum("NEXT", "END", "TRAP")
COND = enum("ZERO", "EQ", "LT", "LTU", "CRILL", "CRHW", "FFAULT", "MMUF")   # MMUF: the trap came from the MMU (D17)
MODE = enum("NONE", "TRAPENTER", "RTE", "TLBW", "HALT", "WAIT")

# Physical register-file addresses.
R_EPC, R_CAUSE, R_BADADDR, R_VBR = 16, 17, 18, 19
T0, T1 = 20, 21                                  # microcode scratch T0..T7 = 20..27
R_USP, R_SSP = 15, 31
R_SINK = 28                                      # writes to r0 land here

# Control-register decode table: CR number -> SRAM address or hardware register.
HWCR = enum("STATUS", "IPEND", "IMASK", "ASID", "TLBHI", "TLBLO")
CR_SRAM = {"EPC": R_EPC, "CAUSE": R_CAUSE, "BADADDR": R_BADADDR, "VBR": R_VBR, "USP": R_USP}
CRMAP = {}                                       # cr -> ("sram", addr) | ("hw", id)
for name, n in CREGS.items():
    CRMAP[n] = ("sram", CR_SRAM[name]) if name in CR_SRAM else ("hw", HWCR[name])

# Internal sequences (SEQ = 1).
ISEQ = enum("RESET", "TRAP", "INTR")

# Memory accesses that can fault; those that use the bus.
FAULTING = {MEM["FETCH"], MEM["LOAD"], MEM["STORE"], MEM["LOADW"], MEM["STOREW"], MEM["CHECKW"]}
BUSCYCLES = {MEM["FETCH"], MEM["LOAD"], MEM["STORE"], MEM["LOADW"], MEM["STOREW"], MEM["IACK"]}

TABLES = dict(ASRC=ASRC, BSRC=BSRC, RSRC=RSRC, RDST=RDST, RSEL=RSEL, ALU=ALU, IMM=IMM, MEM=MEM,
              SEQ=SEQ, COND=COND, MODE=MODE, AOFF=AOFF)

def w(a="NONE", b="NONE", r="NONE", to="NONE", rsa="A", rsb="A", rsw="A", rconst=0, alu=None,
      imm="SEXT16", mem="NONE", mdrb=0, marpc=0, pcinc=0, seq="NEXT", cond="ZERO", mode="NONE",
      raise_=0, lock=0, aoff="SEXT16", marinc=0):
    """One microword. a / b / r: the sources of the three buses; to: R's
    destination; alu: the function latched (ALUGO) this cycle, or None;
    aoff: the address adder's offset (with r="AGU"); marinc: MAR += 4 at the
    edge (MAR is a counter on the MMU / data card, D59)."""
    vals = dict(ASRC=ASRC[a], BSRC=BSRC[b], RSRC=RSRC[r], RDST=RDST[to], MDRB=mdrb, MARPC=marpc,
                PCINC=pcinc, ALUGO=int(alu is not None), ALU=ALU[alu or "ADD"], RSA=RSEL[rsa],
                RSB=RSEL[rsb], RSW=RSEL[rsw], RCONST=rconst, IMM=IMM[imm], MEM=MEM[mem], LOCK=lock,
                SEQ=SEQ[seq], COND=COND[cond], MODE=MODE[mode], RAISE=raise_,
                AOFF=AOFF[aoff], MARINC=marinc)
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
FETCH = both(w(r="MEM", mem="FETCH", to="IR", pcinc=1))      # step 0: IR <- M[MAR], PC += 4

U = {}       # (seq, opcode) -> list of steps after fetch (seq 0) / from step 0 (seq 1)

def alu_rr(op):
    return [both(w(a="REG", rsa="B", b="REG", rsb="C", alu=op)),
            both(w(r="ALU", to="REG", rsw="A", marpc=1, seq="END"))]

def alu_ri(op, imm):
    return [both(w(a="REG", rsa="B", b="IMM", imm=imm, alu=op)),
            both(w(r="ALU", to="REG", rsw="A", marpc=1, seq="END"))]

def load():
    """The address adder puts rs + offset into MAR in the cycle the base is
    read (D59), so a load is the fetch and two cycles."""
    return [both(w(a="REG", rsa="B", r="AGU", to="MAR")),
            both(w(r="MEM", mem="LOAD", to="REG", rsw="A", marpc=1, seq="END"))]   # may fault; commits with it

def store():
    return [both(w(a="REG", rsa="B", r="AGU", to="MAR", b="REG", rsb="A", mdrb=1)),  # the address; the data from B
            both(w(r="MDRS", mem="STORE", marpc=1, seq="END"))]                    # may fault; the only effect

def load_x():
    """D59: rs + rt * scale (B shifted by IR 1:0 in the address adder)."""
    return [both(w(a="REG", rsa="B", b="REG", rsb="C", r="AGU", aoff="INDEX", to="MAR")),
            both(w(r="MEM", mem="LOAD", to="REG", rsw="A", marpc=1, seq="END"))]

def store_x():
    """D59: three registers (base, index, data) and two read ports: the data
    a cycle later."""
    return [both(w(a="REG", rsa="B", b="REG", rsb="C", r="AGU", aoff="INDEX", to="MAR")),
            both(w(b="REG", rsb="A", mdrb=1)),
            both(w(r="MDRS", mem="STORE", marpc=1, seq="END"))]

def load_p():
    """D59: the access at rs + off8; the ALU adds inc8 to rs meanwhile, and
    rs is written after the access has succeeded (commit after access)."""
    return [both(w(a="REG", rsa="B", r="AGU", aoff="OFF8", to="MAR", b="IMM", imm="INC8", alu="ADD")),
            both(w(r="MEM", mem="LOAD", to="REG", rsw="A")),                          # may fault
            both(w(r="ALU", to="REG", rsw="B", marpc=1, seq="END"))]

def store_p():
    return [both(w(a="REG", rsa="B", r="AGU", aoff="OFF8", to="MAR", b="REG", rsb="A", mdrb=1)),
            both(w(r="MDRS", mem="STORE", a="REG", rsa="B", b="IMM", imm="INC8", alu="ADD")),  # may fault
            both(w(r="ALU", to="REG", rsw="B", marpc=1, seq="END"))]

def li32():
    """D59: the next word is the value. The fetch left MAR at this
    instruction and the PC past it: MAR steps to the value's word, which is
    read as a fetch is (execute permission); then PC and MAR both step past it."""
    return [both(w(marinc=1)),
            both(w(r="MEM", mem="FETCH", to="REG", rsw="B", pcinc=1, marinc=1, seq="END"))]   # may fault

def swap():
    """D51: t = M[a]; M[a] = rd; rd = t. The old word waits in scratch T0
    until the store has succeeded (commit after access, D14); the read and
    the write are one bus tenure (LOCK, D54)."""
    return [both(w(a="REG", rsa="B", r="AGU", to="MAR", b="REG", rsb="A", mdrb=1)),
            both(w(r="MEM", mem="LOADW", to="REG", rsw="CONST", rconst=T0, lock=1)),        # may fault
            both(w(r="MDRS", mem="STOREW", a="REG", rsa="CONST", rconst=T0, alu="PASSA")),  # may fault
            both(w(r="ALU", to="REG", rsw="A", marpc=1, seq="END"))]

def branch(cond, taken_on):
    """Compare A with B (the comparison is a condition from the latching
    edge, so it picks the next step); taken: PC + offset into PC and MAR;
    not taken: done, the next fetch at MAR = PC."""
    taken = w(a="PC", b="IMM", imm="BR", alu="ADD")
    not_taken = w(marpc=1, seq="END")
    return [both(w(a="REG", rsa="A", b="REG", rsb="B", alu="SUB", cond=cond)),
            (not_taken, taken) if taken_on == 1 else (taken, not_taken),
            both(w(r="ALU", to="PCMAR", seq="END"))]

def push_pc(final):
    """SP - 4 to MAR, the PC (the return address) to MDR, the store; then
    `final`, which commits SP and the new PC -- nothing visible before the
    store. The ALU holds SP - 4 through the store for `final`."""
    return [both(w(a="REG", rsa="SP", b="IMM", imm="FOUR", alu="SUB")),
            both(w(r="ALU", to="MAR", a="PC", alu="PASSA")),
            both(w(r="ALU", to="MDR", a="REG", rsa="SP", b="IMM", imm="FOUR", alu="SUB")),
            both(w(r="MDRS", mem="STOREW"))] + final                               # may fault: nothing committed

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
            U[k] = [both(w(r="IMM", imm="LUI", to="REG", rsw="A", marpc=1, seq="END"))]
        elif m == "SWAP":
            U[k] = swap()
        elif fmt == "LD":
            U[k] = load()
        elif fmt == "X":
            U[k] = store_x() if m[0] == "S" else load_x()
        elif fmt == "P":
            U[k] = store_p() if m[0] == "S" else load_p()
        elif fmt == "L32":
            U[k] = li32()
        elif fmt == "ST":
            U[k] = store()
        elif m == "BEQ":  U[k] = branch("EQ", 1)
        elif m == "BNE":  U[k] = branch("EQ", 0)
        elif m == "BLT":  U[k] = branch("LT", 1)
        elif m == "BGE":  U[k] = branch("LT", 0)
        elif m == "BLTU": U[k] = branch("LTU", 1)
        elif m == "BGEU": U[k] = branch("LTU", 0)
        elif m == "JMP":
            U[k] = [both(w(a="PC", b="IMM", imm="J", alu="ADD")),
                    both(w(r="ALU", to="PCMAR", seq="END"))]
        elif m == "CALL":
            U[k] = push_pc([both(w(r="ALU", to="REG", rsw="SP", a="PC", b="IMM", imm="J", alu="ADD")),
                            both(w(r="ALU", to="PCMAR", seq="END"))])
        elif m == "JR":
            U[k] = [both(w(a="REG", rsa="B", alu="PASSA")),
                    both(w(r="ALU", to="MAR")),
                    both(w(r="MAR", mem="CHECKW", to="PC", seq="END"))]           # misaligned: fault, no effect
        elif m == "CALLR":
            U[k] = [both(w(a="REG", rsa="B", alu="PASSA")),
                    both(w(r="ALU", to="MAR")),
                    both(w(r="MAR", mem="CHECKW", to="REG", rsw="CONST", rconst=T1))] + \
                   push_pc([both(w(r="ALU", to="REG", rsw="SP", a="REG", rsa="CONST", rconst=T1, alu="PASSA")),
                            both(w(r="ALU", to="PCMAR", seq="END"))])
        elif m == "RET":
            U[k] = [both(w(a="REG", rsa="SP", alu="PASSA")),
                    both(w(r="ALU", to="MAR", a="REG", rsa="SP", b="IMM", imm="FOUR", alu="ADD")),
                    both(w(r="MEM", mem="LOADW", to="MAR")),                       # may fault
                    both(w(r="ALU", mem="CHECKW", to="REG", rsw="SP")),             # may fault; SP commits with it
                    both(w(r="MAR", to="PC", seq="END"))]                          # (MAR holds the target)
        elif m == "HALT":
            U[k] = [both(w(mode="HALT"))]
        elif m == "SYSCALL":
            U[k] = [both(w(seq="TRAP", raise_=CAUSES["SYSCALL"]))]
        elif m == "BREAK":
            U[k] = [both(w(seq="TRAP", raise_=CAUSES["BREAK"]))]
        elif m == "RTE":
            U[k] = [both(w(a="REG", rsa="CONST", rconst=R_EPC, alu="PASSA")),
                    both(w(r="ALU", to="PCMAR", mode="RTE", seq="END"))]
        elif m == "WAIT":
            U[k] = [both(w(mode="WAIT", marpc=1, seq="END"))]
        elif m == "TLBW":
            U[k] = [both(w(mode="TLBW", marpc=1, seq="END"))]
        elif m == "MFCR":
            illegal = w(seq="TRAP", raise_=CAUSES["ILLEGAL"])
            U[k] = [both(w(cond="CRILL")),
                    (w(cond="CRHW"), illegal),
                    (w(a="REG", rsa="CRMAP", alu="PASSA"),                         # SRAM-backed CR
                     w(r="CRHW", to="REG", rsw="A", marpc=1, seq="END")),           # hardware CR
                    both(w(r="ALU", to="REG", rsw="A", marpc=1, seq="END"))]
        elif m == "MTCR":
            illegal = w(seq="TRAP", raise_=CAUSES["ILLEGAL"])
            U[k] = [both(w(cond="CRILL")),
                    (w(cond="CRHW"), illegal),
                    both(w(a="REG", rsa="A", alu="PASSA", cond="CRHW")),
                    (w(r="ALU", to="REG", rsw="CRMAP", marpc=1, seq="END"),
                     w(r="ALU", to="CRHW", marpc=1, seq="END"))]
        else:
            raise SystemExit("no microcode for %s" % m)

    # Internal sequences (SEQ = 1), from step 0. Each ends with PC and MAR
    # loaded together (the next fetch's address).
    U[(1, ISEQ["RESET"])] = [
        both(w(r="IMM", imm="ZERO", to="REG", rsw="CONST", rconst=0)),          # r0 = 0 (D4)
        both(w(r="IMM", imm="ZERO", to="PCMAR", seq="END"))]                    # PC = MAR = 0
    U[(1, ISEQ["TRAP"])] = [
        both(w(r="CAUSE", to="REG", rsw="CONST", rconst=R_CAUSE, cond="FFAULT")),
        (w(a="PC", b="IMM", imm="FOUR", alu="SUB", cond="MMUF"),                # fault after fetch: EPC = PC - 4
         w(a="PC", b="IMM", imm="ZERO", alu="SUB", cond="MMUF")),               # fault in the fetch: EPC = PC
        (w(r="IMM", imm="ZERO", to="REG", rsw="CONST", rconst=R_BADADDR),        # raised by the sequencer: 0 (D15)
         w(r="BADADDR", to="REG", rsw="CONST", rconst=R_BADADDR)),               # MMU fault: the MMU's latch (D17)
        both(w(r="ALU", to="REG", rsw="CONST", rconst=R_EPC)),
        both(w(a="REG", rsa="CONST", rconst=R_VBR, alu="PASSA")),
        both(w(r="ALU", to="PCMAR", mode="TRAPENTER", seq="END"))]
    U[(1, ISEQ["INTR"])] = [
        both(w(r="CAUSE", to="MAR", a="PC", alu="PASSA")),                      # EPC = next instruction
        both(w(r="ALU", to="REG", rsw="CONST", rconst=R_EPC)),
        both(w(r="MEM", mem="IACK", to="REG", rsw="CONST", rconst=R_CAUSE)),     # CAUSE with the vector (D57)
        both(w(r="IMM", imm="ZERO", to="REG", rsw="CONST", rconst=R_BADADDR)),
        both(w(a="REG", rsa="CONST", rconst=R_VBR, alu="PASSA")),
        both(w(r="ALU", to="PCMAR", mode="TRAPENTER", seq="END"))]

# ---------------------------------------------------------------- static checks
def writes_state(word):
    """Does this word change programmer-visible state (if it completes)?"""
    to, rsw, rconst = field(word, "RDST"), field(word, "RSW"), field(word, "RCONST")
    if to == RDST["REG"]:
        return not (rsw == RSEL["CONST"] and 20 <= rconst <= 27)          # T0..T7 are scratch
    if to in (RDST["PC"], RDST["PCMAR"], RDST["CRHW"]):
        return True
    if field(word, "MEM") in (MEM["STORE"], MEM["STOREW"]):
        return True
    return field(word, "MODE") in (MODE["TRAPENTER"], MODE["RTE"], MODE["TLBW"])

def paths(steps):
    """Every path through an instruction's steps (lists of words)."""
    out = []
    def walk(i, path):
        if i == len(steps):
            out.append(path)
            return
        w0, w1 = steps[i]
        prev_cond = field(path[-1], "COND") if path else COND["ZERO"]
        for word in ({w0} if prev_cond == COND["ZERO"] else {w0, w1}):
            p = path + [word]
            if field(word, "SEQ") in (SEQ["END"], SEQ["TRAP"]) or field(word, "MODE") == MODE["HALT"]:
                out.append(p)
            else:
                walk(i + 1, p)
    walk(0, [])
    return out

def check_commit(steps, name):
    """On every path through an instruction (after fetch), nothing visible may
    be written before the last possibly-faulting access. The access cycle
    itself is safe: a fault suppresses its effects."""
    errors = []
    for path in paths(steps):
        last = max((j for j, wd in enumerate(path) if field(wd, "MEM") in FAULTING), default=-1)
        for j, wd in enumerate(path[:last]):
            if writes_state(wd):
                errors.append("%s: step %d writes visible state before the last access (step %d)"
                              % (name, j + 1, last + 1))
    return errors

def check_next_fetch(steps, name):
    """The END word of every path leaves MAR = the next PC: MARPC, PC and MAR
    loaded together, or (JR, RET) the PC loaded from MAR."""
    errors = []
    for path in paths(steps):
        wd = path[-1]
        if field(wd, "SEQ") != SEQ["END"]:
            continue
        to = field(wd, "RDST")
        stepped = field(wd, "PCINC") and field(wd, "MARINC")             # (LI32: MAR = PC, both step)
        if not (field(wd, "MARPC") or to == RDST["PCMAR"] or (to == RDST["PC"] and field(wd, "RSRC") == RSRC["MAR"])
                or stepped):
            errors.append("%s: an END word that does not set MAR to the next PC" % name)
        if field(wd, "PCINC") and not stepped:
            errors.append("%s: an END word that adds to the PC" % name)
    return errors

def check_word(wd, name):
    """One word's own consistency."""
    errors = []
    to, r, mem = field(wd, "RDST"), field(wd, "RSRC"), field(wd, "MEM")
    if to != RDST["NONE"] and r == RSRC["NONE"]:
        errors.append("%s: R has a destination and no source" % name)
    if mem in (MEM["FETCH"], MEM["LOAD"], MEM["LOADW"], MEM["IACK"]) and r != RSRC["MEM"]:
        errors.append("%s: a read whose data is not on R" % name)
    if mem in (MEM["STORE"], MEM["STOREW"]) and r != RSRC["MDRS"]:
        errors.append("%s: a write whose data is not on R (MDRS)" % name)
    if r == RSRC["MEM"] and mem not in (MEM["FETCH"], MEM["LOAD"], MEM["LOADW"], MEM["IACK"]):
        errors.append("%s: memory data on R without a read" % name)
    if field(wd, "MARPC") and to == RDST["PC"]:
        errors.append("%s: MAR from the PC in the word that loads the PC (the shadow PC has the old one)" % name)
    if (field(wd, "MARPC") + field(wd, "MARINC") + (to in (RDST["MAR"], RDST["PCMAR"]))) > 1:
        errors.append("%s: MAR loaded twice" % name)
    if field(wd, "AOFF") != AOFF["SEXT16"] and r != RSRC["AGU"]:
        errors.append("%s: an address-adder offset with the adder not on R" % name)
    if r == RSRC["AGU"] and field(wd, "ASRC") != ASRC["REG"]:
        errors.append("%s: the address adder without a base register on A" % name)
    if r == RSRC["AGU"] and field(wd, "AOFF") == AOFF["INDEX"] and field(wd, "BSRC") != BSRC["REG"]:
        errors.append("%s: an indexed address with no index register on B" % name)
    if field(wd, "PCINC") and to in (RDST["PC"], RDST["PCMAR"]):
        errors.append("%s: PC loaded and incremented" % name)
    if field(wd, "MDRB") and field(wd, "BSRC") == BSRC["NONE"]:
        errors.append("%s: MDR from B with nothing on B" % name)
    if field(wd, "MDRB") and to == RDST["MDR"]:
        errors.append("%s: MDR loaded twice" % name)
    if field(wd, "BSRC") == BSRC["IMM"] and r == RSRC["IMM"]:
        errors.append("%s: the immediate unit on B and R at once" % name)
    return errors

def check_lock(steps, name):
    """LOCK (LOCKREQ on the local link) marks a LOADW whose bus tenure lasts
    to the next bus cycle, which must be a STOREW: the MMU / data card holds
    -LOCK from the one to the end of the other (or a fault)."""
    errors = []
    for i, pair in enumerate(steps):
        for wd in pair:
            if not field(wd, "LOCK"):
                continue
            if field(wd, "MEM") != MEM["LOADW"]:
                errors.append("%s step %d: LOCK on a cycle that is not LOADW" % (name, i + 1))
                continue
            nxt = [field(x, "MEM") for p in steps[i + 1:] for x in {p[0], p[1]} if field(x, "MEM") in BUSCYCLES]
            if not nxt or nxt[0] != MEM["STOREW"]:
                errors.append("%s step %d: a locked LOADW whose next bus cycle is not its STOREW" % (name, i + 1))
    return errors

# ---------------------------------------------------------------- decode tables (D17, D18)
# Must-be-zero classes, as the CPU card's zero detectors test them.
ZCLASS = {0: 0, 0x00000FFF: 1, 0x0000FFE0: 2, 0x000F0000: 3, 0x00F0FFFF: 4, 0x00FFFFFF: 5, 0x0000FFFF: 6,
          0x00000FFC: 7}                         # (all eight: D59)

def decode_tables():
    """Two 256-byte flash images for the CPU card's decode tables.
    opcode table, address = opcode:  bit 0 legal, 1 privileged, 4:2 must-be-zero
      class, 6:5 access size (0 = byte, 1 = half, 2 = word), 7 signed load.
    CR table, address = IR bits 19:16: bit 0 illegal (reserved CR), 1 hardware
      register (else SRAM), 6:2 SRAM address or hardware-register number."""
    op = bytearray(256)
    for m, (code, fmt) in OPC.items():
        memfmt = fmt in ("LD", "ST", "X", "P") and m != "SWAP"
        size = min(code & 3, 2) if memfmt else 2         # the low opcode bits, in every group (D59)
        signed = memfmt and not code & 0xC and code & 3 < 2  # LB, LH, LBX, LHX, LBP, LHP
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
    for op in range(256):                       # fetch is step 0 of every opcode
        words[addr(0, op, 0, 0)], words[addr(0, op, 0, 1)] = FETCH
    for (seq, op), steps in U.items():
        base = 1 if seq == 0 else 0
        assert base + len(steps) <= 16, ("too many steps", seq, op)
        for i, (w0, w1) in enumerate(steps):
            words[addr(seq, op, base + i, 0)], words[addr(seq, op, base + i, 1)] = w0, w1
    return words

def cycles(steps):
    """(shortest, longest) cycles including the fetch cycle."""
    lens = [len(p) + 1 for p in paths(steps)]
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
    def label(seq, op):
        return names[op] if seq == 0 else "internal %s" % [k for k, v in ISEQ.items() if v == op][0]
    for (seq, op), steps in U.items():          # step 1 may be suppressed by a decode trap
        if seq == 0 and steps and any(field(wd, "MEM") != MEM["NONE"] for wd in steps[0]):
            errors.append("%s: step 1 starts a memory cycle (it must be suppressible, D18)" % names[op])
    for (seq, op), steps in U.items():
        for pair in steps:
            for wd in pair:
                errors += check_word(wd, label(seq, op))
        errors += check_next_fetch(steps, label(seq, op))
        if seq == 0:
            errors += check_commit(steps, names[op])
            errors += check_lock(steps, names[op])
    for wd in FETCH:
        errors += check_word(wd, "FETCH")
    if errors:
        sys.exit("microcode errors:\n  " + "\n  ".join(sorted(set(errors))))
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
        print("microword: %d bits (%d flash chips); %d instructions; checks: OK"
              % (WORD_BITS, WORD_BYTES, len([k for k in U if k[0] == 0])))
        for m, (op, f) in sorted(OPC.items(), key=lambda kv: kv[1][0]):
            lo, hi = cycles(U[(0, op)])
            print("  0x%02X %-7s %s cycles" % (op, m, lo if lo == hi else "%d-%d" % (lo, hi)))

if __name__ == "__main__":
    main()
