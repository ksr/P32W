#!/usr/bin/env python3
"""The P32W CPU card (slot 1): docs/cpu-card.md, D56, D58, D59.

The three-bus datapath of docs/microarch.md in 5 V through-hole logic,
designed for this card from the microcode's semantics (microcode/p32ucode.py,
emulator/p32uemu.c). Every GAL is defined here with a behavioural spec and
verified exhaustively (p32gal.py); the whole card runs the real control
store in the gate-level simulator against the reference emulators
(generators/sim_cpu.py).

The cycle (docs/cpu-card.md, "The cycle"): a 24 MHz oscillator, eight
phases a microcycle (3 MHz). At phase 0's start CCLK rises (unless the
cycle is held): the pipeline register takes the next microword and every
destination takes R -- the ONE commit edge. The next microaddress is worked
out during the cycle from that cycle's own signals (the comparison of A and
B, a fault, the opcode arriving on R), so it is ready at the edge. The
register file is written LATE: R and the write address are latched at the
commit and written in phases 0-2 of the next cycle (the write strobe in
phase 1), before that cycle's reads (phases 3-7) -- a write and a read of the
same register in consecutive cycles need no bypass.

    python3 card_cpu.py            # build, check (GALs exhaustively, the netlist electrically), BOM
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "microcode"))
from p32net import Card, CARDS, check, gal_device, bom          # noqa: E402
from p32gal import Gal, sop, spec, minimize, eval_sop, TERMS    # noqa: E402
import p32ucode as UC                                           # noqa: E402

E = UC                                                          # the microword's codes
SEQ, MODE, COND, IMM, ASRC, BSRC, RSRC, RDST = E.SEQ, E.MODE, E.COND, E.IMM, E.ASRC, E.BSRC, E.RSRC, E.RDST
HWCR, ISEQ = E.HWCR, E.ISEQ

# ---------------------------------------------------------------- the microword's nets
# Fields the local link carries are named as their J3 lines (bus.md); the
# others u<FIELD><bit> (u<FIELD> for one bit).
J3_FIELDS = {"RSRC": "RSRC", "MEM": "MEM"}

def fnet(name, i=0):
    width = dict(UC.FIELDS)[name]
    if name in J3_FIELDS:
        return "%s%d" % (J3_FIELDS[name], i)
    if name == "LOCK":
        return "LOCKREQ"
    return "u%s" % name if width == 1 else "u%s%d" % (name, i)

def bitnet(k):
    """The net of microword bit k."""
    for name, width in UC.FIELDS:
        if UC.SHIFT[name] <= k < UC.SHIFT[name] + width:
            return fnet(name, k - UC.SHIFT[name])
    return None

def num(v, names):
    """The number the named bits spell (an absent bit -- one an output does not depend on -- is 0)."""
    return sum((1 if v.get(n) else 0) << i for i, n in enumerate(names))

# ---------------------------------------------------------------- GALs
OUT_PINS = sorted(TERMS, key=lambda p: (-TERMS[p], p))           # the pins with the most terms first

class GB:
    """A GAL from specs: outputs get the pins with the terms they need,
    inputs the rest; equations from minimize() unless given."""
    def __init__(self, name, function, clock=None, reset=None):
        self.name, self.function, self.clock, self.reset = name, function, clock, reset
        self.ins, self.outs = [], []

    def inp(self, *names):
        self.ins += [n for n in names if n not in self.ins]
        return self

    def _eq(self, deps, f, eq, invert=False):
        s = spec(deps, f)
        if eq is not None:
            return sop(eq) if isinstance(eq, str) else eq, s
        g = (lambda v: None if f(v) is None else not f(v)) if invert else f
        return minimize(spec(deps, g)), s

    def reg(self, name, deps, f, reset_high=False, eq=None):
        e, s = self._eq(deps, f, eq, invert=reset_high)
        self.outs.append(("reg", name, e, s, reset_high))
        return self

    def com(self, name, deps, f, eq=None):
        e, s = self._eq(deps, f, eq)
        self.outs.append(("com", name, e, s, None))
        return self

    def tri(self, name, deps, f, oe, eq=None):
        e, s = self._eq(deps, f, eq)
        self.outs.append(("tri", name, e, s, oe))
        return self

    def build(self):
        assert len(self.outs) <= 10, (self.name, len(self.outs))
        g = Gal(self.name, self.function, clock=self.clock, reset=self.reset)
        outs = sorted(self.outs, key=lambda o: -len(o[2]))
        pins = list(OUT_PINS)
        used = []
        for kind, name, eq, s, extra in outs:
            pin = pins.pop(0)
            used.append(pin)
            if kind == "reg":
                g.reg(name, pin, eq, s, reset_high=extra)
            elif kind == "com":
                g.com(name, pin, eq, s)
            else:                                       # extra: the output enable, one product term
                oe = sop(extra)
                g.tri(name, pin, eq, s, oe, spec(sorted({nm for t in oe for nm, _ in t}),
                                                 lambda v, oe=oe: eval_sop(oe, v)))
        free = ([] if self.clock else [1]) + list(range(2, 12)) + [13] + sorted(pins)
        ins = [n for n in self.ins if n != self.clock]
        if self.reset and self.reset not in ins:
            ins.append(self.reset)
        assert len(ins) <= len(free), (self.name, "too many inputs", len(ins), len(free))
        for n, pin in zip(ins, free):
            g.inp(n, pin)
        errs = g.check()
        if errs:
            raise SystemExit("GAL %s:\n  %s" % (self.name, "\n  ".join(errs[:20])))
        return g

PH = ["P0", "P1", "P2"]
def phase(v):
    return num(v, PH)

def gal_clkgen():
    """The clock: an 8-phase counter on the 24 MHz oscillator; CLK (the bus's
    clock, high in phases 0-3), CCLK (the commit clock: a rising edge at
    phase 0 unless STALL or HOLD), ACLK (CCLK in an ALUGO word that commits:
    the ALU's operand and function latches), WPH (phases 0-2: the register
    file at its write address) and -WSTB (phase 1: the write strobe). Every
    output is a register on the same oscillator edge: no clock glitches."""
    b = GB("CLKGEN", "clock: phases, CLK, CCLK, ACLK, WPH, -WSTB", clock="OSC")
    b.inp("STALL", "HOLD", "ALUGOK")
    b.reg("P0", PH, lambda v: not v["P0"])
    b.reg("P1", PH, lambda v: v["P1"] ^ v["P0"])
    b.reg("P2", PH, lambda v: v["P2"] ^ (v["P1"] & v["P0"]))
    b.reg("CLK", PH, lambda v: phase(v) in (7, 0, 1, 2))
    b.reg("CLKN", PH, lambda v: phase(v) not in (7, 0, 1, 2))
    b.reg("CCLK", PH + ["STALL", "HOLD", "CCLK"],
          lambda v: (phase(v) == 7 and not v["STALL"] and not v["HOLD"]) or (phase(v) in (0, 1, 2) and v["CCLK"]))
    b.reg("ACLK", PH + ["STALL", "HOLD", "ALUGOK", "ACLK"],
          lambda v: (phase(v) == 7 and not v["STALL"] and not v["HOLD"] and v["ALUGOK"])
          or (phase(v) in (0, 1, 2) and v["ACLK"]))
    b.reg("WPH", PH, lambda v: phase(v) in (7, 0, 1))
    b.reg("WPHN", PH, lambda v: phase(v) not in (7, 0, 1))
    b.reg("WSTBN", PH, lambda v: phase(v) != 0)
    return b.build()

def gal_panel():
    """Reset, run / step and halt (the front panel). Commits happen at the
    oscillator edge that ends phase 7 when neither STALL nor HOLD. STEP
    (debounced) allows one commit (MICRO) or commits up to the next
    instruction boundary (INSTR); HALT stops the clock until reset."""
    b = GB("PANEL", "reset, run / step, halt", clock="OSC")
    b.inp(*PH).inp("STALL", "STEPBTN", "RUNSW", "MODESW", "NBOUND", "RESBTN", "PWROK", "HALTMODE", "WAITHOLD")
    def commit(v):
        return phase(v) == 7 and not v["STALL"] and not v["HOLD"]
    b.reg("RESET", ["RESBTN", "PWROK"], lambda v: v["RESBTN"] or not v["PWROK"])
    b.reg("STEPS", ["STEPBTN"], lambda v: v["STEPBTN"])
    b.reg("STEPQ", ["RESET", "STEPBTN", "STEPS", "STEPQ", "STALL", "HOLD", "MODESW", "NBOUND"] + PH,
          lambda v: not v["RESET"] and ((v["STEPBTN"] and not v["STEPS"]) or
                                        (v["STEPQ"] and not (commit(v) and (not v["MODESW"] or v["NBOUND"])))))
    b.reg("HALTED", ["RESET", "HALTED", "STALL", "HOLD", "HALTMODE"] + PH,
          lambda v: not v["RESET"] and (v["HALTED"] or (commit(v) and v["HALTMODE"])))
    b.com("HOLD", ["RESET", "HALTED", "WAITHOLD", "RUNSW", "STEPQ"],
          lambda v: not v["RESET"] and (v["HALTED"] or v["WAITHOLD"] or (not v["RUNSW"] and not v["STEPQ"])))
    b.com("RESN", ["RESET"], lambda v: not v["RESET"])
    b.com("RUNLEDN", ["HOLD"], lambda v: v["HOLD"])
    b.com("HALTLEDN", ["HALTED"], lambda v: not v["HALTED"])
    return b.build()

STEPS4 = ["STEP0", "STEP1", "STEP2", "STEP3"]
def nextstate(v):
    """(NUSEQ, NSTEP, NISEQ, OPS, NBOUND) for the next microaddress; None = don't care.
    OPS: the opcode mux -- 0 IR[31:24], 1 R[31:24] (the instruction being fetched), 2 the sequence id."""
    if v["RESET"]:
        return 1, 0, ISEQ["RESET"], 2, 0
    if v["GOTRAP"]:
        return 1, 0, ISEQ["TRAP"], 2, 0
    if v["GOINTR"]:
        return 1, 0, ISEQ["INTR"], 2, 0
    seq = num(v, ["SEQ0", "SEQ1"])
    if seq == SEQ["TRAP"]:
        return None                                     # (EVENTS raises GOTRAP with it)
    if seq == SEQ["NEXT"]:
        step = num(v, STEPS4)
        if v["USEQ"]:
            return 1, (step + 1) & 15, num(v, ["ISEQ0", "ISEQ1"]), 2, 0
        return 0, (step + 1) & 15, None, 1 if step == 0 else 0, 0
    return 0, 0, None, 0, 1                             # END: the next fetch (step 0's word is FETCH for any opcode)

def gal_next():
    """The sequencer: the next microaddress's sequence, step and opcode
    source, from the current one (the microaddress register) and this
    cycle's events."""
    deps = ["USEQ"] + STEPS4 + ["ISEQ0", "ISEQ1", "SEQ0", "SEQ1", "RESET", "GOTRAP", "GOINTR"]
    b = GB("NEXT", "sequencer: the next microaddress").inp(*deps)
    def part(sel):
        def f(v):
            r = nextstate(v)
            return None if r is None else sel(r)
        return f
    def bit(x, i):
        return None if x is None else (x >> i) & 1
    b.com("NUSEQ", deps, part(lambda r: r[0]))
    for i in range(4):
        b.com("NSTEP%d" % i, deps, part(lambda r, i=i: bit(r[1], i)))
    for i in range(2):
        b.com("NISEQ%d" % i, deps, part(lambda r, i=i: bit(r[2], i)))
        b.com("OPS%d" % i, deps, part(lambda r, i=i: bit(r[3], i)))
    b.com("NBOUND", deps, part(lambda r: r[4]))
    return b.build()

CONDS = ["C0", "C1", "C2"]
def gal_cond():
    """The condition for the next microaddress's bit 0, as the microcode
    means it: on the state AFTER this cycle's edge. A word that latches the
    ALU's operands (ALUGO) is tested on the live comparison of A and B; a
    later word on the flags latched then. FFLT / MMUFLT: the trap being
    entered came from a fault in a fetch / from the MMU / data card."""
    b = GB("COND", "the condition; compare flags; fault flags", clock="CCLK", reset="RESN")
    b.inp("CMPEQ", "CMPLT", "CMPLTU", "ALUGOK", *CONDS).inp("CRILL", "CRHW", "FAULT", "FIRSTN", "GOTRAP",
                                                        "GOINTR", "SEQNEXT")
    live = {COND["EQ"]: ("CMPEQ", "EQF"), COND["LT"]: ("CMPLT", "LTF"), COND["LTU"]: ("CMPLTU", "LTUF")}
    def ncond(v):
        if not v["SEQNEXT"]:
            return 0
        c = num(v, CONDS)
        if c in live:
            a, f = live[c]
            return v[a] if v["ALUGOK"] else v[f]
        return {COND["ZERO"]: 0, COND["CRILL"]: v["CRILL"], COND["CRHW"]: v["CRHW"],
                COND["FFAULT"]: v["FFLT"], COND["MMUF"]: v["MMUFLT"]}[c]
    def lit(c):
        return " & ".join(("" if c >> i & 1 else "!") + "C%d" % i for i in range(3))
    terms = []
    for c, (a, f) in live.items():
        terms += ["SEQNEXT & %s & ALUGOK & %s" % (lit(c), a), "SEQNEXT & %s & !ALUGOK & %s" % (lit(c), f)]
    for c, x in ((COND["CRILL"], "CRILL"), (COND["CRHW"], "CRHW"), (COND["FFAULT"], "FFLT"), (COND["MMUF"], "MMUFLT")):
        terms.append("SEQNEXT & %s & %s" % (lit(c), x))
    b.com("NCOND", ["SEQNEXT", "ALUGOK", "CMPEQ", "CMPLT", "CMPLTU", "EQF", "LTF", "LTUF", "CRILL", "CRHW",
                    "FFLT", "MMUFLT"] + CONDS, ncond, eq=" | ".join(terms))
    for a, f in live.values():
        b.reg(f, ["ALUGOK", a, f], lambda v, a=a, f=f: v[a] if v["ALUGOK"] else v[f])
    b.reg("FFLT", ["GOTRAP", "FAULT", "FIRSTN", "FFLT"],
          lambda v: (v["FAULT"] and not v["FIRSTN"]) if v["GOTRAP"] else v["FFLT"])
    b.reg("MMUFLT", ["GOTRAP", "GOINTR", "FAULT", "MMUFLT"],
          lambda v: v["FAULT"] if v["GOTRAP"] else (0 if v["GOINTR"] else v["MMUFLT"]))
    return b.build()

ZG = ["Z10", "Z42", "Z85", "Z119", "Z1512", "Z1916", "Z2320"]   # IR bit groups that are all 0
ZBITS = {"Z10": range(0, 2), "Z42": range(2, 5), "Z85": range(5, 9), "Z119": range(9, 12),
         "Z1512": range(12, 16), "Z1916": range(16, 20), "Z2320": range(20, 24)}
ZCLASS = {c: m for m, c in UC.ZCLASS.items()}                   # class -> must-be-zero mask
def class_groups(c):
    m = ZCLASS[c]
    gs = [g for g, bits in ZBITS.items() if any(m >> b & 1 for b in bits)]
    assert all(all(m >> b & 1 for b in ZBITS[g]) for g in gs), "a mask splits a zero group"
    return gs

def gal_decode():
    """The decode trap (D18), in step 1 of an instruction: an illegal opcode
    or a must-be-zero field that is not, ILLEGAL; a privileged opcode in user
    mode, PRIVILEGE. VA, VB: the must-be-zero check, split for terms."""
    ZC = ["ZC0", "ZC1", "ZC2"]
    b = GB("DECODE", "decode trap: illegal / privileged").inp(*ZG).inp(*ZC).inp("LEGAL", "PRIV", "SUPER", "FIRST1N")
    def viol(v, classes):
        c = num(v, ZC)
        return c in classes and not all(v[g] for g in class_groups(c))
    def lit(c):
        return " & ".join(("" if c >> i & 1 else "!") + "ZC%d" % i for i in range(3))
    A_, B_ = (1, 2, 3, 7), (4, 5, 6)
    b.com("VA", ZG + ZC, lambda v: viol(v, A_),
          eq=" | ".join("%s & !%s" % (lit(c), g) for c in A_ for g in class_groups(c)))
    nz16 = ["Z10", "Z42", "Z85", "Z119", "Z1512"]
    assert set(class_groups(6)) == set(nz16) and set(class_groups(4)) == set(nz16) | {"Z2320"} \
        and set(class_groups(5)) == set(ZG)
    b.com("NZ16", nz16, lambda v: not all(v[g] for g in nz16), eq=" | ".join("!" + g for g in nz16))
    b.com("VB", ["NZ16", "Z1916", "Z2320"] + ZC,
          lambda v: num(v, ZC) in B_ and (v["NZ16"] or (num(v, ZC) in (4, 5) and not v["Z2320"])
                                           or (num(v, ZC) == 5 and not v["Z1916"])),
          eq="%s & NZ16 | %s & NZ16 | %s & NZ16 | %s & !Z2320 | %s & !Z2320 | %s & !Z1916"
             % (lit(4), lit(5), lit(6), lit(4), lit(5), lit(5)))
    ill = lambda v: not v["LEGAL"] or v["VA"] or v["VB"]
    b.com("DECTRAP", ["FIRST1N", "LEGAL", "VA", "VB", "PRIV", "SUPER"],
          lambda v: not v["FIRST1N"] and (ill(v) or (v["PRIV"] and not v["SUPER"])))
    b.com("DCODE0", ["LEGAL", "VA", "VB"], ill)
    b.com("DCODE1", ["LEGAL", "VA", "VB", "PRIV", "SUPER"], lambda v: not ill(v) and v["PRIV"] and not v["SUPER"])
    return b.build()

def gal_events():
    """This cycle's events: an abort (reset, a decode trap, a fault: the
    cycle's effects suppressed), trap entry, interrupt entry (at an END, IE
    and a pending enabled line as the cycle began, not at trap entry's end,
    D20), the commit of an ALUGO, WAIT holding, HALT, and the cause latch's
    load and source (0 RAISE, 1 the fault cause, 2 the decode cause, 3 the
    interrupt level)."""
    M = ["MODE0", "MODE1", "MODE2"]
    deps = ["RESET", "DECTRAP", "FAULT", "SEQ0", "SEQ1"] + M + ["ALUGO", "IE", "ANYPN"]
    b = GB("EVENTS", "abort, trap / interrupt entry, cause select").inp(*deps)
    def ab(v):
        return v["RESET"] or v["DECTRAP"] or v["FAULT"]
    def seq(v):
        return num(v, ["SEQ0", "SEQ1"])
    def gotrap(v):
        return not v["RESET"] and (v["DECTRAP"] or v["FAULT"] or seq(v) == SEQ["TRAP"])
    def gointr(v):
        return (not ab(v) and seq(v) in (SEQ["END"], 3) and v["IE"] and not v["ANYPN"]
                and num(v, M) != MODE["TRAPENTER"])
    def csel(v):
        return 2 if v["DECTRAP"] else 1 if v["FAULT"] else 3 if gointr(v) else 0
    b.com("ABORTN", deps, lambda v: not ab(v))
    b.com("GOTRAP", deps, gotrap)
    b.com("GOINTR", deps, gointr)
    b.com("SEQNEXT", deps, lambda v: not ab(v) and seq(v) == SEQ["NEXT"])
    b.com("ALUGOK", deps, lambda v: v["ALUGO"] and not ab(v))
    b.com("WAITHOLD", deps, lambda v: not ab(v) and num(v, M) == MODE["WAIT"] and v["ANYPN"])
    b.com("HALTMODE", deps, lambda v: not ab(v) and num(v, M) == MODE["HALT"])
    b.com("CSEL0", deps, lambda v: csel(v) & 1)
    b.com("CSEL1", deps, lambda v: csel(v) >> 1)
    b.com("CLOADN", deps, lambda v: not (gotrap(v) or gointr(v)))
    return b.build()

CRA = ["CRA0", "CRA1", "CRA2"]
def gal_status():
    """STATUS (S, IE, PS, PIE: bits 0-3): written by MTCR (a hardware CR,
    CRSEL = STATUS), then trap entry (PS = S, PIE = IE, S = 1, IE = 0) or RTE
    (S = PS, IE = PIE) at the edge; S resets to 1. And TLBW for the MMU /
    data card."""
    M = ["MODE0", "MODE1", "MODE2"]
    b = GB("STATUS", "STATUS register; TLBW", clock="CCLK", reset="RESN")
    b.inp(*M).inp("ABORTN", "CRHWWN", *CRA).inp("LR0", "LR1", "LR2", "LR3")
    # helper outputs (fed back inside): the write, trap entry, RTE
    b.com("WRST", ["CRHWWN"] + CRA, lambda v: not v["CRHWWN"] and num(v, CRA) == HWCR["STATUS"])
    b.com("TE", M + ["ABORTN"], lambda v: v["ABORTN"] and num(v, M) == MODE["TRAPENTER"])
    b.com("RT", M + ["ABORTN"], lambda v: v["ABORTN"] and num(v, M) == MODE["RTE"])
    def nxt(v, k):
        cur = lambda i, n: v["LR%d" % i] if v["WRST"] else v[n]
        if v["TE"] and v["RT"]:
            return None
        if k == 0:
            return 1 if v["TE"] else cur(2, "PS") if v["RT"] else cur(0, "SUPER")
        if k == 1:
            return 0 if v["TE"] else cur(3, "PIE") if v["RT"] else cur(1, "IE")
        if k == 2:
            return cur(0, "SUPER") if v["TE"] else cur(2, "PS")
        return cur(1, "IE") if v["TE"] else cur(3, "PIE")
    H = ["WRST", "TE", "RT"]
    b.reg("SUPER", H + ["LR0", "LR2", "SUPER", "PS"], lambda v: nxt(v, 0), reset_high=True)
    b.reg("IE", H + ["LR1", "LR3", "IE", "PIE"], lambda v: nxt(v, 1))
    b.reg("PS", H + ["LR0", "LR2", "SUPER", "PS"], lambda v: nxt(v, 2))
    b.reg("PIE", H + ["LR1", "LR3", "IE", "PIE"], lambda v: nxt(v, 3))
    b.com("TLBW", M + ["ABORTN"], lambda v: v["ABORTN"] and num(v, M) == MODE["TLBW"])
    return b.build()

def gal_imask():
    """IMASK, held inverted (IMN: 1 = disabled; reset: all disabled): the
    pending logic ORs it with the synchronised -IRQ lines, an inverting
    buffer reads it back."""
    b = GB("IMASK", "IMASK register (inverted)", clock="CCLK", reset="RESN")
    b.inp("CRHWWN", *CRA).inp(*["LR%d" % i for i in range(8)])
    for i in range(8):
        b.reg("IMN%d" % i, ["CRHWWN"] + CRA + ["LR%d" % i, "IMN%d" % i],
              lambda v, i=i: (not v["LR%d" % i]) if (not v["CRHWWN"] and num(v, CRA) == HWCR["IMASK"])
              else v["IMN%d" % i], reset_high=True)
    return b.build()

def gal_regaddr(port):
    """One register-file port's 5-bit address: the IR field the 4:1 mux
    picked (F, with SP = 15) through the r15 bank rule (15 in supervisor
    mode is 31, the other SP), or the microword's constant (RSEL 4), or the
    CR table's address (RSEL 5). The write port sends r0 to the sink, 28."""
    F = ["F0", "F1", "F2", "F3"]
    K = ["K%d" % i for i in range(5)]
    CR = ["CR%d" % i for i in range(5)]
    b = GB("REG" + port, "register address, port %s" % port).inp(*F).inp(*K).inp(*CR).inp("RSEL2", "RSEL0", "SUPER")
    write = port == "W"
    def addr(v):
        if v["RSEL2"]:
            return num(v, CR) if v["RSEL0"] else num(v, K)
        f = num(v, F)
        if write and f == 0:
            return E.R_SINK
        return 31 if (f == 15 and v.get("SUPER")) else f      # (bits 0-3 are the same either way)
    sink = " | !RSEL2 & !F0 & !F1 & !F2 & !F3"
    for i in range(5):
        if i < 4:
            eq = "RSEL2 & RSEL0 & CR%d | RSEL2 & !RSEL0 & K%d | !RSEL2 & F%d" % (i, i, i)
            deps = ["RSEL2", "RSEL0", "CR%d" % i, "K%d" % i] + (F if write else ["F%d" % i])
        else:
            eq = "RSEL2 & RSEL0 & CR4 | RSEL2 & !RSEL0 & K4 | !RSEL2 & F0 & F1 & F2 & F3 & SUPER"
            deps = ["RSEL2", "RSEL0", "CR4", "K4", "SUPER"] + F
        if write and (E.R_SINK >> i) & 1:
            eq += sink
        b.com("O%d" % i, deps, lambda v, i=i: addr(v) >> i & 1, eq=eq)
    return b.build()

def gal_rdec():
    """R's sources on this card besides the ones a 74HCT138 decodes: LUI's
    upper half, the zero fill of each byte (for an immediate, CAUSE and the
    CPU's hardware CRs) and which hardware CR this card drives."""
    IM = ["IMM0", "IMM1", "IMM2"]
    deps = ["IMMN", "CRHWRN", "CAUSEN"] + IM + CRA
    b = GB("RDEC", "R source enables: LUI, zero fill, CPU CRs").inp(*deps)
    def cpucr(v, which=None):
        c = num(v, CRA)
        return not v["CRHWRN"] and (c in (HWCR["STATUS"], HWCR["IPEND"], HWCR["IMASK"]) if which is None else c == which)
    lui = lambda v: not v["IMMN"] and num(v, IM) == IMM["LUI"]
    nonlui = lambda v: not v["IMMN"] and num(v, IM) != IMM["LUI"]
    b.com("RLUIN", deps, lambda v: not lui(v))
    b.com("Z1N", deps, lambda v: not (not v["IMMN"] or not v["CAUSEN"] or cpucr(v)))
    b.com("Z2N", deps, lambda v: not (nonlui(v) or not v["CAUSEN"] or cpucr(v)))
    b.com("Z3N", deps, lambda v: not (nonlui(v) or cpucr(v)))
    b.com("RSTATN", deps, lambda v: not cpucr(v, HWCR["STATUS"]))
    b.com("RIPENDN", deps, lambda v: not cpucr(v, HWCR["IPEND"]))
    b.com("RIMASKN", deps, lambda v: not cpucr(v, HWCR["IMASK"]))
    return b.build()

def gal_abdec():
    """A's and B's sources (the register file only outside the write
    phases), the store data's lane selects, and the sign bits inverted for
    the signed comparison."""
    deps = ["ASRC0", "ASRC1", "BSRC0", "BSRC1", "WPH"]
    b = GB("ABDEC", "A / B source enables; size decode; sign inversion").inp(*deps).inp("SIZE0", "SIZE1", "AB31", "BB31")
    a = lambda v: num(v, ["ASRC0", "ASRC1"])
    bs = lambda v: num(v, ["BSRC0", "BSRC1"])
    b.com("AREGN", deps, lambda v: not (a(v) == ASRC["REG"] and not v["WPH"]))
    b.com("APCN", deps, lambda v: not (a(v) == ASRC["PC"]))
    b.com("BREGN", deps, lambda v: not (bs(v) == BSRC["REG"] and not v["WPH"]))
    b.com("BIMM", deps, lambda v: bs(v) == BSRC["IMM"])
    b.com("BYTE", ["SIZE0", "SIZE1"], lambda v: not v["SIZE0"] and not v["SIZE1"])
    b.com("NOTWORD", ["SIZE1"], lambda v: not v["SIZE1"])
    b.com("NA31", ["AB31"], lambda v: not v["AB31"])
    b.com("NB31", ["BB31"], lambda v: not v["BB31"])
    return b.build()

def imm_value(mode, ir):
    """The immediate unit onto B, as p32uemu's imm_unit()."""
    i16 = ir & 0xFFFF
    s16 = i16 - 0x10000 if i16 & 0x8000 else i16
    return {IMM["SEXT16"]: s16, IMM["ZEXT16"]: i16, IMM["LUI"]: i16 << 16, IMM["BR"]: s16 << 2,
            IMM["J"]: ((ir & 0xFFFFFF) - (0x1000000 if ir & 0x800000 else 0)) << 2, IMM["FOUR"]: 4,
            IMM["ZERO"]: 0, IMM["INC8"]: (ir & 0xFF) - (0x100 if ir & 0x80 else 0)}[mode] & 0xFFFFFFFF

def imm_sources(k):
    """The IR bits B bit k can come from (in any mode)."""
    src = set()
    for mode in IMM.values():
        for bit in range(24):
            if imm_value(mode, 1 << bit) >> k & 1 != imm_value(mode, 0) >> k & 1:
                src.add(bit)
    return sorted(src)

IMM_GROUPS = [range(0, 8), range(8, 16), range(16, 20), range(20, 24), range(24, 28), range(28, 32)]
def gal_imm(n):
    bits = IMM_GROUPS[n]
    IM = ["IMM0", "IMM1", "IMM2"]
    srcs = sorted({s for k in bits for s in imm_sources(k)})
    b = GB("IMM%d" % n, "immediate unit onto B, bits %d-%d" % (bits[0], bits[-1])).inp(*IM).inp(*["IR%d" % s for s in srcs])
    b.inp("BIMM")
    for k in bits:
        deps = IM + ["IR%d" % s for s in imm_sources(k)]
        b.tri("B%d" % k, deps, lambda v, k=k, ss=imm_sources(k): imm_value(num(v, IM), sum(v["IR%d" % s] << s for s in ss)) >> k & 1,
              "BIMM")
    return b.build()

def gal_idx(n):
    """The address adder's index: B shifted left by IR[1:0] (scale 1-8, D59), bits 8n-8n+7."""
    bits = range(8 * n, 8 * n + 8)
    srcs = [i for i in range(8 * n - 3, 8 * n + 8) if i >= 0]
    b = GB("IDX%d" % n, "the index, B << IR[1:0], bits %d-%d" % (bits[0], bits[-1])).inp("IR0", "IR1")
    b.inp(*["BB%d" % i for i in srcs]).inp("IDX")
    for k in bits:
        deps = ["IR0", "IR1"] + ["BB%d" % i for i in range(max(0, k - 3), k + 1)]
        b.tri("O%d" % k, deps, lambda v, k=k: (k - num(v, ["IR0", "IR1"]) >= 0) and v["BB%d" % (k - num(v, ["IR0", "IR1"]))],
              "IDX")
    return b.build()

def gals():
    return {"CLKGEN": gal_clkgen(), "PANEL": gal_panel(), "NEXT": gal_next(), "COND": gal_cond(),
            "DECODE": gal_decode(), "EVENTS": gal_events(), "STATUS": gal_status(), "IMASK": gal_imask(),
            "REGA": gal_regaddr("A"), "REGB": gal_regaddr("B"), "REGW": gal_regaddr("W"), "RDEC": gal_rdec(),
            "ABDEC": gal_abdec(), **{"IMM%d" % n: gal_imm(n) for n in range(6)},
            **{"IDX%d" % n: gal_idx(n) for n in range(4)}}

# ---------------------------------------------------------------- the netlist
def build():
    G = gals()
    c = Card("cpu-card", "P32W CPU CARD")
    role = {}                                           # role -> ref (for the simulation)
    n = [0]
    def U(dev, function, r=None):
        n[0] += 1
        ref = "U%d" % n[0]
        c.part(ref, dev, function)
        if r:
            role[r] = ref
        return ref
    def w(ref, pin, net):
        c.net(net, (ref, pin))
    def gal(name, function, netmap, r=None):
        g = G[name]
        ref = U(gal_device(g), function, r or name)
        dev = c.parts[ref][0]
        from p32net import DEV
        for p, (num_, t) in DEV[dev]["pins"].items():
            if t in ("pwr", "gnd"):
                continue
            if p.startswith("NC") and t == "pas":
                c.unused(ref, p)
                continue
            w(ref, p, netmap.get(p, p))
        return ref
    LR = lambda i: "LR%d" % i
    AB = lambda i: "AB%d" % i
    BB = lambda i: "BB%d" % i
    IR = lambda i: "IR%d" % i

    # ---- clock and control
    osc = U("OSC24M", "24 MHz oscillator (3 MHz microcycle)", "osc")
    w(osc, "EN", "VCC"); w(osc, "OUT", "OSC")
    gal("CLKGEN", "clock: 8 phases, CLK, CCLK, ACLK, WPH, -WSTB",
        {"P0": "PH0", "P1": "PH1", "P2": "PH2", "CLKN": "-CLK", "WPHN": "WPH_N", "WSTBN": "-WSTB"})
    gal("PANEL", "reset, run / step, halt",
        {"P0": "PH0", "P1": "PH1", "P2": "PH2", "RESN": "-RES", "RUNLEDN": "RUNLED_N", "HALTLEDN": "HALTLED_N"})
    inv = U("74HCT14", "debounce RESET / STEP; the ALU's GO toggle", "inv")
    w(inv, "1A", "RESBTN_RAW"); w(inv, "1Y", "RESBTN")
    w(inv, "2A", "STEPBTN_RAW"); w(inv, "2Y", "STEPBTN")
    w(inv, "3A", "GO"); w(inv, "3Y", "GONEXT")
    for k in (4, 5, 6):
        w(inv, "%dA" % k, "GND"); c.unused(inv, "%dY" % k)

    # ---- control store: 7 flash chips, the pipeline register, the microaddress register
    addr = ["NCOND"] + ["NSTEP%d" % i for i in range(4)] + ["NOP%d" % i for i in range(8)] + ["NUSEQ"]
    for k in range(UC.WORD_BYTES):
        f = U("SST39SF010A", "control store, microword bits %d-%d" % (8 * k, 8 * k + 7), "ucode%d" % k)
        for a in range(17):
            w(f, "A%d" % a, addr[a] if a < len(addr) else "GND")
        w(f, "-CE", "GND"); w(f, "-OE", "GND"); w(f, "-WE", "VCC"); c.unused(f, "NC1", "NC30")
        p = U("74HCT574", "pipeline register, microword bits %d-%d" % (8 * k, 8 * k + 7), "pipe%d" % k)
        w(p, "CLK", "CCLK"); w(p, "-OE", "GND")
        for j in range(8):
            w(f, "DQ%d" % j, "UW%d" % (8 * k + j)); w(p, "D%d" % j, "UW%d" % (8 * k + j))
            bn = bitnet(8 * k + j)
            if bn:
                w(p, "Q%d" % j, bn)
            else:
                c.unused(p, "Q%d" % j)
    ua = U("74HCT574", "microaddress register: sequence, step, internal sequence id", "uaddr")
    w(ua, "CLK", "CCLK"); w(ua, "-OE", "GND")
    for j, (d, q) in enumerate([("NUSEQ", "USEQ")] + [("NSTEP%d" % i, "STEP%d" % i) for i in range(4)] +
                               [("NISEQ0", "ISEQ0"), ("NISEQ1", "ISEQ1"), ("GND", None)]):
        w(ua, "D%d" % j, d)
        if q:
            w(ua, "Q%d" % j, q)
        else:
            c.unused(ua, "Q%d" % j)
    gal("NEXT", "sequencer: the next microaddress", {"SEQ0": fnet("SEQ", 0), "SEQ1": fnet("SEQ", 1)})
    for m in range(4):
        x = U("74HCT153", "opcode mux, microaddress bits %d-%d" % (5 + 2 * m, 6 + 2 * m))
        w(x, "S0", "OPS0"); w(x, "S1", "OPS1"); w(x, "-1E", "GND"); w(x, "-2E", "GND")
        for h in (1, 2):
            bit = 2 * m + h - 1
            w(x, "%dI0" % h, IR(24 + bit)); w(x, "%dI1" % h, LR(24 + bit))
            w(x, "%dI2" % h, "NISEQ%d" % bit if bit < 2 else "GND"); w(x, "%dI3" % h, "GND")
            w(x, "%dY" % h, "NOP%d" % bit)
    gal("COND", "the condition; compare and fault flags",
        {"C0": fnet("COND", 0), "C1": fnet("COND", 1), "C2": fnet("COND", 2), "FIRSTN": "FIRST_N", "RESN": "-RES"})
    gal("EVENTS", "abort, trap / interrupt entry, cause select",
        {"SEQ0": fnet("SEQ", 0), "SEQ1": fnet("SEQ", 1), "MODE0": fnet("MODE", 0), "MODE1": fnet("MODE", 1),
         "MODE2": fnet("MODE", 2), "ALUGO": fnet("ALUGO"), "ANYPN": "ANYP_N", "ABORTN": "ABORT_N",
         "CLOADN": "CLOAD_N"})
    fs = U("74HCT138", "step decode: FIRST (fetch), FIRST1 (step 1)", "first")
    w(fs, "A", "STEP0"); w(fs, "B", "STEP1"); w(fs, "C", "STEP2"); w(fs, "G1", "VCC")
    w(fs, "-G2A", "USEQ"); w(fs, "-G2B", "STEP3"); w(fs, "Y0", "FIRST_N"); w(fs, "Y1", "FIRST1_N")
    c.unused(fs, *["Y%d" % i for i in range(2, 8)])

    # ---- instruction unit: IR, the decode tables, the must-be-zero check
    for k in range(4):
        x = U("74HCT377", "IR bits %d-%d" % (8 * k, 8 * k + 7), "ir%d" % k)
        w(x, "CLK", "CCLK"); w(x, "-E", "IR_N")
        for j in range(8):
            w(x, "D%d" % j, LR(8 * k + j)); w(x, "Q%d" % j, IR(8 * k + j))
    dop = U("SST39SF010A", "opcode decode table: legal, privileged, zero class, size, signed", "decop")
    dcr = U("SST39SF010A", "control-register decode table: illegal, hardware, address", "deccr")
    for a in range(17):
        w(dop, "A%d" % a, IR(24 + a) if a < 8 else "GND")
        w(dcr, "A%d" % a, IR(16 + a) if a < 4 else "GND")
    for x in (dop, dcr):
        w(x, "-CE", "GND"); w(x, "-OE", "GND"); w(x, "-WE", "VCC"); c.unused(x, "NC1", "NC30")
    for j, nm in enumerate(["LEGAL", "PRIV", "ZC0", "ZC1", "ZC2", "SIZE0", "SIZE1", "SIGNED"]):
        w(dop, "DQ%d" % j, nm)
    for j, nm in enumerate(["CRILL", "CRHW", "CRSEL0", "CRSEL1", "CRSEL2", "CRA3", "CRA4"]):
        w(dcr, "DQ%d" % j, nm)
    c.unused(dcr, "DQ7")
    groups = list(ZBITS.items()) + [("ZSPARE", [])]
    for k in range(4):
        x = U("74HCT4002", "must-be-zero detectors")
        for h, (gname, bits) in enumerate(groups[2 * k:2 * k + 2], 1):
            ins = [IR(b) for b in bits] + ["GND"] * (4 - len(bits))
            for p, net in zip("ABCD", ins):
                w(x, "%d%s" % (h, p), net)
            if bits:
                w(x, "%dY" % h, gname)
            else:
                c.unused(x, "%dY" % h)
        c.unused(x, "NC6", "NC8")
    CRMAP = {"CRA0": "CRSEL0", "CRA1": "CRSEL1", "CRA2": "CRSEL2"}
    gal("DECODE", "decode trap", {"FIRST1N": "FIRST1_N"})

    # ---- register addresses and the register file (two copies, D58)
    for port, gname in (("A", "REGA"), ("B", "REGB"), ("W", "REGW")):
        sel = {"A": "RSA", "B": "RSB", "W": "RSW"}[port]
        for m in range(2):
            x = U("74HCT153", "register port %s: the IR field (A, B, C, SP), bits %d-%d" % (port, 2 * m, 2 * m + 1))
            w(x, "S0", fnet(sel, 0)); w(x, "S1", fnet(sel, 1)); w(x, "-1E", "GND"); w(x, "-2E", "GND")
            for h in (1, 2):
                bit = 2 * m + h - 1
                w(x, "%dI0" % h, IR(20 + bit)); w(x, "%dI1" % h, IR(16 + bit)); w(x, "%dI2" % h, IR(12 + bit))
                w(x, "%dI3" % h, "VCC"); w(x, "%dY" % h, "F%s%d" % (port, bit))
        nm = {"F%d" % i: "F%s%d" % (port, i) for i in range(4)}
        nm.update({"K%d" % i: fnet("RCONST", i) for i in range(5)})
        nm.update({"CR%d" % i: (["CRSEL0", "CRSEL1", "CRSEL2", "CRA3", "CRA4"][i]) for i in range(5)})
        nm.update({"RSEL2": fnet(sel, 2), "RSEL0": fnet(sel, 0)})
        nm.update({"O%d" % i: "R%s%d" % (port, i) for i in range(5)})
        gal(gname, "register address, port %s" % port, nm)
    wl = U("74HCT574", "the late write: its address and enable, latched at the commit", "wlatch")
    w(wl, "CLK", "CCLK"); w(wl, "-OE", "GND")
    for j in range(8):
        d = "RW%d" % j if j < 5 else "REG_N" if j == 5 else "GND"
        w(wl, "D%d" % j, d)
        if j < 6:
            w(wl, "Q%d" % j, "RWL%d" % j if j < 5 else "WEL_N")
        else:
            c.unused(wl, "Q%d" % j)
    for cp in ("A", "B"):
        for m in range(2):
            x = U("74HCT157", "register copy %s: read / write address" % cp)
            w(x, "S", "WPH"); w(x, "-E", "GND")
            for ch in range(1, 5):
                bit = 4 * m + ch - 1
                if bit < 5:
                    w(x, "%dA" % ch, "R%s%d" % (cp, bit)); w(x, "%dB" % ch, "RWL%d" % bit); w(x, "%dY" % ch, "SA%s%d" % (cp, bit))
                else:
                    w(x, "%dA" % ch, "GND"); w(x, "%dB" % ch, "GND"); c.unused(x, "%dY" % ch)
        bus = AB if cp == "A" else BB
        for k in range(4):
            s = U("CY7C199", "register file copy %s, bits %d-%d" % (cp, 8 * k, 8 * k + 7), "rf%s%d" % (cp, k))
            for a in range(15):
                w(s, "A%d" % a, "SA%s%d" % (cp, a) if a < 5 else "GND")
            w(s, "-CE", "GND"); w(s, "-OE", "WPH"); w(s, "-WE", "RFWE_N")
            d = U("74HCT574", "register copy %s: the write data, bits %d-%d" % (cp, 8 * k, 8 * k + 7))
            w(d, "CLK", "CCLK"); w(d, "-OE", "WPH_N")
            r = U("74HCT541", "register copy %s onto %s, bits %d-%d" % (cp, cp, 8 * k, 8 * k + 7))
            w(r, "-OE1", "AREG_N" if cp == "A" else "BREG_N"); w(r, "-OE2", "GND")
            for j in range(8):
                b = 8 * k + j
                w(s, "IO%d" % j, "RF%s%d" % (cp, b)); w(d, "D%d" % j, LR(b)); w(d, "Q%d" % j, "RF%s%d" % (cp, b))
                w(r, "A%d" % j, "RF%s%d" % (cp, b)); w(r, "Y%d" % j, bus(b))

    # ---- PC (bits 31:2) and onto A
    for k in range(8):
        x = U("74HCT161", "PC bits %d-%d" % (4 * k + 2, 4 * k + 5), "pc%d" % k)
        w(x, "CLK", "CCLK"); w(x, "-CLR", "VCC"); w(x, "-LOAD", "LOADPC_N"); w(x, "ENP", "PCINC")
        w(x, "ENT", "PCINC" if k == 0 else "PCC%d" % k)
        if k < 7:
            w(x, "RCO", "PCC%d" % (k + 1))
        else:
            c.unused(x, "RCO")
        for j, (d, q) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
            b = 4 * k + 2 + j
            if b < 32:
                w(x, d, LR(b)); w(x, q, "PC%d" % b)
            else:
                w(x, d, "GND"); c.unused(x, q)
    for k in range(4):
        x = U("74HCT541", "PC onto A, bits %d-%d" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "APC_N"); w(x, "-OE2", "GND")
        for j in range(8):
            b = 8 * k + j
            w(x, "A%d" % j, "PC%d" % b if b >= 2 else "GND"); w(x, "Y%d" % j, AB(b))

    # ---- the immediate unit (onto B)
    for nimm in range(6):
        nm = {"IMM%d" % i: fnet("IMM", i) for i in range(3)}
        nm.update({"B%d" % k: BB(k) for k in IMM_GROUPS[nimm]})
        gal("IMM%d" % nimm, "immediate unit onto B, bits %d-%d" % (IMM_GROUPS[nimm][0], IMM_GROUPS[nimm][-1]), nm)

    # ---- the address adder (D59): A + offset onto R
    for m in range(4):
        x = U("74HCT257", "address offset from IR, bits %d-%d" % (4 * m, 4 * m + 3))
        w(x, "S", fnet("AOFF", 0)); w(x, "-OE", fnet("AOFF", 1))
        for ch in range(1, 5):
            b = 4 * m + ch - 1
            w(x, "%dA" % ch, IR(b)); w(x, "%dB" % ch, IR(b + 8) if b < 8 else IR(15)); w(x, "%dY" % ch, "OFS%d" % b)
    for m in range(2):
        x = U("74HCT541", "address offset sign, bits %d-%d" % (16 + 8 * m, 23 + 8 * m))
        w(x, "-OE1", fnet("AOFF", 1)); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, IR(15)); w(x, "Y%d" % j, "OFS%d" % (16 + 8 * m + j))
    for nidx in range(4):
        nm = {"IDX": fnet("AOFF", 1)}
        nm.update({"O%d" % k: "OFS%d" % k for k in range(8 * nidx, 8 * nidx + 8)})
        gal("IDX%d" % nidx, "the index, B << IR[1:0], bits %d-%d" % (8 * nidx, 8 * nidx + 7), nm)
    for k in range(8):
        x = U("74F283", "address adder bits %d-%d" % (4 * k, 4 * k + 3))
        w(x, "C0", "GND" if k == 0 else "AGC%d" % k)
        if k < 7:
            w(x, "C4", "AGC%d" % (k + 1))
        else:
            c.unused(x, "C4")
        for j in range(4):
            b = 4 * k + j
            w(x, "A%d" % (j + 1), AB(b)); w(x, "B%d" % (j + 1), "OFS%d" % b); w(x, "S%d" % (j + 1), "AGS%d" % b)
    for k in range(4):
        x = U("74HCT541", "address adder onto R, bits %d-%d" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "RAGU_N"); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, "AGS%d" % (8 * k + j)); w(x, "Y%d" % j, LR(8 * k + j))

    # ---- the comparator (D58): A vs B, a 74HCT85 tree
    def cmp85(fn, a, b_, gt, eq, lt):
        x = U("74HCT85", fn)
        w(x, "IA>B", "GND"); w(x, "IA=B", "VCC"); w(x, "IA<B", "GND")
        for j in range(4):
            w(x, "A%d" % j, a[j]); w(x, "B%d" % j, b_[j])
        for p, net in (("QA>B", gt), ("QA=B", eq), ("QA<B", lt)):
            if net:
                w(x, p, net)
            else:
                c.unused(x, p)
    for k in range(8):
        cmp85("compare bits %d-%d" % (4 * k, 4 * k + 3), [AB(4 * k + j) for j in range(4)],
              [BB(4 * k + j) for j in range(4)], "CGT%d" % k, None, "CLT%d" % k)
    cmp85("compare bits 28-31, signed", [AB(28), AB(29), AB(30), "NA31"], [BB(28), BB(29), BB(30), "NB31"],
          "CGTS7", None, "CLTS7")
    cmp85("compare bits 0-15", ["CGT%d" % k for k in range(4)], ["CLT%d" % k for k in range(4)], "CGTLO", None, "CLTLO")
    cmp85("compare bits 16-31", ["CGT%d" % k for k in range(4, 8)], ["CLT%d" % k for k in range(4, 8)], "CGTHI", None, "CLTHI")
    cmp85("compare bits 16-31, signed", ["CGT4", "CGT5", "CGT6", "CGTS7"], ["CLT4", "CLT5", "CLT6", "CLTS7"],
          "CGTHIS", None, "CLTHIS")
    cmp85("compare: EQ, LTU", ["CGTLO", "CGTHI", "GND", "GND"], ["CLTLO", "CLTHI", "GND", "GND"], None, "CMPEQ", "CMPLTU")
    cmp85("compare: LT (signed)", ["CGTLO", "CGTHIS", "GND", "GND"], ["CLTLO", "CLTHIS", "GND", "GND"], None, None, "CMPLT")

    # ---- MDR (the store data) and its lanes onto R
    for m in range(8):
        x = U("74HCT157", "MDR input: R or B, bits %d-%d" % (4 * m, 4 * m + 3))
        w(x, "S", "MDR_N"); w(x, "-E", "GND")
        for ch in range(1, 5):
            b = 4 * m + ch - 1
            w(x, "%dA" % ch, LR(b)); w(x, "%dB" % ch, BB(b)); w(x, "%dY" % ch, "MDRIN%d" % b)
    for k in range(4):
        x = U("74HCT377", "MDR bits %d-%d" % (8 * k, 8 * k + 7), "mdr%d" % k)
        w(x, "CLK", "CCLK"); w(x, "-E", "MDRLD_N")
        for j in range(8):
            w(x, "D%d" % j, "MDRIN%d" % (8 * k + j)); w(x, "Q%d" % j, "MDR%d" % (8 * k + j))
    x = U("74HCT541", "store data lane 0 onto R")
    w(x, "-OE1", "RMDRS_N"); w(x, "-OE2", "GND")
    for j in range(8):
        w(x, "A%d" % j, "MDR%d" % j); w(x, "Y%d" % j, LR(j))
    for lane, sel in ((1, "BYTE"), (2, "NOTWORD")):
        for m in range(2):
            x = U("74HCT257", "store data lane %d onto R (replicated)" % lane)
            w(x, "S", sel); w(x, "-OE", "RMDRS_N")
            for ch in range(1, 5):
                j = 4 * m + ch - 1
                w(x, "%dA" % ch, "MDR%d" % (8 * lane + j)); w(x, "%dB" % ch, "MDR%d" % j); w(x, "%dY" % ch, LR(8 * lane + j))
    for m in range(4):
        x = U("74HCT253", "store data lane 3 onto R (replicated)")
        w(x, "S0", "SIZE0"); w(x, "S1", "SIZE1"); w(x, "-1OE", "RMDRS_N"); w(x, "-2OE", "RMDRS_N")
        for h in (1, 2):
            j = 2 * m + h - 1
            w(x, "%dI0" % h, "MDR%d" % j); w(x, "%dI1" % h, "MDR%d" % (8 + j)); w(x, "%dI2" % h, "MDR%d" % (24 + j))
            w(x, "%dI3" % h, "GND"); w(x, "%dY" % h, LR(24 + j))

    # ---- the ALU module (D21): operand and function latches, the FPGA, result latches
    for opnd, bus, sels in (("A", AB, ("SELAL_N", "SELAH_N")), ("B", BB, ("SELBL_N", "SELBH_N"))):
        for k in range(4):
            x = U("74LVC574", "ALU operand %s bits %d-%d" % (opnd, 8 * k, 8 * k + 7))
            w(x, "CLK", "ACLK"); w(x, "-OE", sels[k // 2])
            for j in range(8):
                b = 8 * k + j
                w(x, "D%d" % j, bus(b)); w(x, "Q%d" % j, "FD%d" % (b % 16))
    fl = U("74LVC574", "ALU function and GO toggle", "alufn")
    w(fl, "CLK", "ACLK"); w(fl, "-OE", "GND")
    for j in range(8):
        w(fl, "D%d" % j, fnet("ALU", j) if j < 5 else "GONEXT" if j == 5 else "GND")
        if j < 6:
            w(fl, "Q%d" % j, "ALUF%d" % j if j < 5 else "GO")
        else:
            c.unused(fl, "Q%d" % j)
    for k in range(4):
        x = U("74LVC574", "ALU result bits %d-%d onto R" % (8 * k, 8 * k + 7))
        w(x, "CLK", "CKLO" if k < 2 else "CKHI"); w(x, "-OE", "RALU_N")
        for j in range(8):
            b = 8 * k + j
            w(x, "D%d" % j, "FD%d" % (b % 16)); w(x, "Q%d" % j, LR(b))
    lv = U("74LVC244", "5 V -> 3.3 V: the ALU's reset")
    w(lv, "-1G", "GND"); w(lv, "-2G", "GND"); w(lv, "1A1", "-RES"); w(lv, "1Y1", "NANO_RES_N")
    for h, i in [(1, 2), (1, 3), (1, 4)] + [(2, i) for i in range(1, 5)]:
        w(lv, "%dA%d" % (h, i), "GND"); c.unused(lv, "%dY%d" % (h, i))
    nano = U("TANG_NANO_20K", "the ALU: Sipeed Tang Nano 20K (D21)", "nano")
    for i in range(16):
        w(nano, "FD%d" % i, "FD%d" % i)
    for i in range(5):
        w(nano, "ALUF%d" % i, "ALUF%d" % i)
    for p in ("GO", "SELAL_N", "SELAH_N", "SELBL_N", "SELBH_N", "CKLO", "CKHI"):
        w(nano, p, p)
    w(nano, "RES_N", "NANO_RES_N")
    c.unused(nano, "3V3", *["NC%d" % i for i in range(34, 41)])
    reg = U("LD1117V33", "3.3 V for the ALU's latches", "ld1117")
    w(reg, "IN", "VCC"); w(reg, "OUT", "+3V3")
    c.part("C901", "C10U", "3.3 V output"); w("C901", "+", "+3V3"); w("C901", "-", "GND")

    # ---- STATUS, IMASK, interrupts, the cause latch
    gal("STATUS", "STATUS register; TLBW",
        {"MODE0": fnet("MODE", 0), "MODE1": fnet("MODE", 1), "MODE2": fnet("MODE", 2), "ABORTN": "ABORT_N",
         "CRHWWN": "CRHWW_N", "RESN": "-RES", **CRMAP})
    gal("IMASK", "IMASK register (inverted)", {"CRHWWN": "CRHWW_N", "RESN": "-RES", **CRMAP,
                                               **{"IMN%d" % i: "IMN%d" % i for i in range(8)}})
    sy = U("74HCT574", "-IRQ0-7 synchronised (at -CLK rising)", "irqsync")
    w(sy, "CLK", "-CLK"); w(sy, "-OE", "GND")
    for j in range(8):
        w(sy, "D%d" % j, "-IRQ%d" % j); w(sy, "Q%d" % j, "IRQS_N%d" % j)
    ors = [U("74HCT32", "pending = IPEND & IMASK; the register file's -WE") for _ in range(3)]
    gates = [(o, g) for o in ors for g in range(1, 5)]
    for nn in range(8):
        o, g = gates[nn]
        w(o, "%dA" % g, "IRQS_N%d" % nn); w(o, "%dB" % g, "IMN%d" % nn); w(o, "%dY" % g, "PI%d" % (7 - nn))
    o, g = gates[8]
    w(o, "%dA" % g, "-WSTB"); w(o, "%dB" % g, "WEL_N"); w(o, "%dY" % g, "RFWE_N")
    for o, g in gates[9:]:
        w(o, "%dA" % g, "GND"); w(o, "%dB" % g, "GND"); c.unused(o, "%dY" % g)
    pe = U("74HCT148", "interrupt priority: the lowest pending level (its number on -A2..0)", "prio")
    for k in range(8):
        w(pe, "-I%d" % k, "PI%d" % k)
    w(pe, "-EI", "GND"); w(pe, "-GS", "ANYP_N"); c.unused(pe, "-EO")
    for k in range(3):
        w(pe, "-A%d" % k, "IRQN%d" % k)
    for m in range(2):
        x = U("74HCT153", "cause mux: RAISE, fault cause, decode cause, interrupt level, bits %d-%d" % (2 * m, 2 * m + 1))
        w(x, "S0", "CSEL0"); w(x, "S1", "CSEL1"); w(x, "-1E", "GND"); w(x, "-2E", "GND")
        for h in (1, 2):
            b = 2 * m + h - 1
            w(x, "%dI0" % h, fnet("RAISE", b)); w(x, "%dI1" % h, "FC%d" % b)
            w(x, "%dI2" % h, "DCODE%d" % b if b < 2 else "GND"); w(x, "%dI3" % h, "IRQN%d" % b if b < 3 else "GND")
            w(x, "%dY" % h, "CIN%d" % b)
    cl = U("74HCT377", "the cause latch: cause 3:0, IRQ (bit 31)", "cause")
    w(cl, "CLK", "CCLK"); w(cl, "-E", "CLOAD_N")
    for j in range(8):
        w(cl, "D%d" % j, "CIN%d" % j if j < 4 else "GOINTR" if j == 4 else "GND")
        if j < 5:
            w(cl, "Q%d" % j, "CQ%d" % j if j < 4 else "CQIRQ")
        else:
            c.unused(cl, "Q%d" % j)
    for fn, ins, outs in (("CAUSE onto R, bits 0-7", ["CQ0", "CQ1", "CQ2", "CQ3"] + ["GND"] * 4, range(0, 8)),
                          ("CAUSE onto R, bits 24-31", ["GND"] * 7 + ["CQIRQ"], range(24, 32)),
                          ("STATUS onto R, bits 0-7", ["SUPER", "IE", "PS", "PIE"] + ["GND"] * 4, range(0, 8))):
        x = U("74HCT541", fn)
        w(x, "-OE1", "RCAUSE_N" if fn.startswith("CAUSE") else "RSTAT_N"); w(x, "-OE2", "GND")
        for j, (a, b) in enumerate(zip(ins, outs)):
            w(x, "A%d" % j, a); w(x, "Y%d" % j, LR(b))
    for fn, src, en in (("IPEND onto R (inverting the -IRQ lines)", "IRQS_N", "RIPEND_N"),
                        ("IMASK onto R (inverting IMN)", "IMN", "RIMASK_N")):
        x = U("74HCT540", fn)
        w(x, "-OE1", en); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, "%s%d" % (src, j)); w(x, "Y%d" % j, LR(j))

    # ---- R's other sources: decode, zero fill, LUI
    ra = U("74HCT138", "R source decode, RSRC 0-7", "rsrc0")
    w(ra, "A", "RSRC0"); w(ra, "B", "RSRC1"); w(ra, "C", "RSRC2"); w(ra, "G1", "VCC"); w(ra, "-G2A", "RSRC3"); w(ra, "-G2B", "GND")
    for k, net in ((RSRC["ALU"], "RALU_N"), (RSRC["IMM"], "IMM_N"), (RSRC["MDRS"], "RMDRS_N"), (RSRC["CAUSE"], "RCAUSE_N")):
        w(ra, "Y%d" % k, net)
    c.unused(ra, *["Y%d" % k for k in range(8) if k not in (RSRC["ALU"], RSRC["IMM"], RSRC["MDRS"], RSRC["CAUSE"])])
    rb = U("74HCT138", "R source decode, RSRC 8-15", "rsrc1")
    w(rb, "A", "RSRC0"); w(rb, "B", "RSRC1"); w(rb, "C", "RSRC2"); w(rb, "G1", "RSRC3"); w(rb, "-G2A", "GND"); w(rb, "-G2B", "GND")
    w(rb, "Y%d" % (RSRC["CRHW"] - 8), "CRHWR_N"); w(rb, "Y%d" % (RSRC["AGU"] - 8), "RAGU_N")
    c.unused(rb, *["Y%d" % k for k in range(8) if k not in (RSRC["CRHW"] - 8, RSRC["AGU"] - 8)])
    gal("RDEC", "R source enables: LUI, zero fill, CPU CRs",
        {"IMMN": "IMM_N", "CRHWRN": "CRHWR_N", "CAUSEN": "RCAUSE_N", "IMM0": fnet("IMM", 0), "IMM1": fnet("IMM", 1),
         "IMM2": fnet("IMM", 2), "RLUIN": "RLUI_N", "Z1N": "Z1_N", "Z2N": "Z2_N", "Z3N": "Z3_N", "RSTATN": "RSTAT_N",
         "RIPENDN": "RIPEND_N", "RIMASKN": "RIMASK_N", **CRMAP})
    for k, en in enumerate(("IMM_N", "Z1_N", "Z2_N", "Z3_N")):
        x = U("74HCT541", "zero fill onto R, bits %d-%d" % (8 * k, 8 * k + 7))
        w(x, "-OE1", en); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, "GND"); w(x, "Y%d" % j, LR(8 * k + j))
    for k in range(2):
        x = U("74HCT541", "LUI onto R, bits %d-%d" % (16 + 8 * k, 23 + 8 * k))
        w(x, "-OE1", "RLUI_N"); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, IR(8 * k + j)); w(x, "Y%d" % j, LR(16 + 8 * k + j))
    gal("ABDEC", "A / B source enables; size decode; sign inversion",
        {"ASRC0": fnet("ASRC", 0), "ASRC1": fnet("ASRC", 1), "BSRC0": fnet("BSRC", 0), "BSRC1": fnet("BSRC", 1),
         "AREGN": "AREG_N", "APCN": "APC_N", "BREGN": "BREG_N"})

    # ---- R's destinations, gated by the abort; the local link's gated lines
    rd = U("74HCT138", "R destination decode (none while the cycle aborts)", "rdst")
    w(rd, "A", fnet("RDST", 0)); w(rd, "B", fnet("RDST", 1)); w(rd, "C", fnet("RDST", 2))
    w(rd, "G1", "ABORT_N"); w(rd, "-G2A", "GND"); w(rd, "-G2B", "GND")
    for k, net in ((RDST["REG"], "REG_N"), (RDST["PC"], "PC_N"), (RDST["PCMAR"], "PCMAR_N"), (RDST["IR"], "IR_N"),
                   (RDST["MDR"], "MDR_N"), (RDST["CRHW"], "CRHWW_N")):
        w(rd, "Y%d" % k, net)
    c.unused(rd, "Y%d" % RDST["NONE"], "Y%d" % RDST["MAR"])
    ands = [U("74HCT08", "the local link's gated lines; PC load; MDR load") for _ in range(2)]
    agates = [(a, g) for a in ands for g in range(1, 5)]
    for (a, g), (x, y, out) in zip(agates, [(fnet("RDST", 0), "ABORT_N", "RDST0"), (fnet("RDST", 1), "ABORT_N", "RDST1"),
                                            (fnet("RDST", 2), "ABORT_N", "RDST2"), (fnet("PCINC"), "ABORT_N", "PCINC"),
                                            (fnet("MARPC"), "ABORT_N", "MARPC"), (fnet("MARINC"), "ABORT_N", "MARINC"),
                                            ("PC_N", "PCMAR_N", "LOADPC_N"), ("MDRBX_N", "MDR_N", "MDRLD_N")]):
        w(a, "%dA" % g, x); w(a, "%dB" % g, y); w(a, "%dY" % g, out)
    na = U("74HCT00", "MDR from B, gated by the abort")
    w(na, "1A", fnet("MDRB")); w(na, "1B", "ABORT_N"); w(na, "1Y", "MDRBX_N")
    for g in (2, 3, 4):
        w(na, "%dA" % g, "GND"); w(na, "%dB" % g, "GND"); c.unused(na, "%dY" % g)

    # ---- pull-downs on the three buses (nothing drives them in some cycles)
    for bus in ("LR", "AB", "BB"):
        for k in range(4):
            ref = "RN%d" % (len([r for r in c.parts if r.startswith("RN")]) + 1)
            c.part(ref, "RNET9", "%s%d-%d pull-down" % (bus, 8 * k, 8 * k + 7), value="10k x 8 bussed")
            w(ref, "COM", "GND")
            for j in range(8):
                w(ref, "R%d" % (j + 1), "%s%d" % (bus, 8 * k + j))

    # ---- the front panel (front edge, labelled: D48, D55)
    rn = [0]
    def R(value, a, b, fn):
        rn[0] += 1
        ref = "R%d" % rn[0]
        c.part(ref, "R", fn, value=value)
        w(ref, "1", a); w(ref, "2", b)
    for i, (btn, raw) in enumerate((("RESET", "RESBTN_RAW"), ("STEP", "STEPBTN_RAW")), 1):
        c.part("S%d" % i, "SW_PUSH", "%s button" % btn)
        w("S%d" % i, "1", "GND"); w("S%d" % i, "2", raw)
        R("10k", "VCC", raw, "%s pull-up" % btn)
        c.part("C9%02d" % (i + 1), "C100N", "%s debounce" % btn)
        w("C9%02d" % (i + 1), "1", raw); w("C9%02d" % (i + 1), "2", "GND")
    c.part("S3", "SW_DIP2", "RUN / STEP (1), INSTR / MICRO (2)")
    w("S3", "1", "RUNSW"); w("S3", "4", "GND"); w("S3", "2", "MODESW"); w("S3", "3", "GND")
    R("10k", "VCC", "RUNSW", "RUN / STEP pull-up (open = RUN)")
    R("10k", "VCC", "MODESW", "INSTR / MICRO pull-up (open = INSTR)")
    for i, (led, k) in enumerate((("RUN", "RUNLED_N"), ("HALT", "HALTLED_N")), 1):
        c.part("D%d" % i, "LED", "%s LED" % led)
        R("470", "VCC", "%sLED_A" % led, "%s LED resistor" % led)
        w("D%d" % i, "A", "%sLED_A" % led); w("D%d" % i, "K", k)
    c.finish()
    CARDS[c.name] = c
    return c, G, role

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("cpu card: %d errors" % len(errs))
    print("cpu card OK: %d ICs (%d GALs), %d nets, %d parts" % (len(ics), len(G), len(c.nets), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
