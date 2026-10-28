#!/usr/bin/env python3
"""The P32W MMU / data card (slot 2): docs/mmu-card.md, D12, D17, D50, D57, D59.

The CPU card's partner on the local link (J3) and its way onto the system
bus (J1, J2): MAR and the shadow PC, the split TLB and the fault checks,
the load steering and the store's byte enables, the bus master interface,
the interrupt acknowledge cycle, the bus lock and the arbiter for DMA
masters in slots 3-10. Designed from p32uemu's model of this card
(translate(), mem_cycle(), the edge), which the gate-level simulation
checks it against (generators/sim_sys.py).

It has no clock of its own: CCLK (the commit), CLK (the bus) and -WSTB
(the CPU's write strobe, phase 1) come from the CPU card.

    python3 card_mmu.py [--bom]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "microcode"))
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from card_cpu import GB, num                                  # noqa: E402
import p32ucode as UC                                         # noqa: E402
from p32isa import CAUSES                                     # noqa: E402

MEM, RSRC, RDST, HWCR = UC.MEM, UC.RSRC, UC.RDST, UC.HWCR
MM = ["MEM0", "MEM1", "MEM2"]
SZ = ["SIZE0", "SIZE1"]
RD = ["RDST0", "RDST1", "RDST2"]
READS = (MEM["FETCH"], MEM["LOAD"], MEM["LOADW"])
WRITES = (MEM["STORE"], MEM["STOREW"])
BUSKINDS = READS + WRITES + (MEM["IACK"],)

def access_size(v):
    """Bytes in this access (p32uemu's mem_cycle: a LOAD / STORE its SIZE, the rest words)."""
    if num(v, MM) in (MEM["LOAD"], MEM["STORE"]):
        return {0: 1, 1: 2}.get(num(v, SZ), 4)
    return 4

def pre_fault(v):
    """(cause or 0, TLBHI to load from the fault) before the bus, as translate():
    alignment, then (user mode) the upper half, the TLB miss, protection."""
    k = num(v, MM)
    if k in (MEM["NONE"], MEM["IACK"]):
        return 0, 0
    if num(v, ["MAR0", "MAR1"]) & (access_size(v) - 1):
        return CAUSES["MISALIGNED"], 0
    if k == MEM["CHECKW"] or v["SUPER"]:
        return 0, 0
    if v["MAR31"]:
        return CAUSES["PRIVILEGE"], 0
    fetch, store = k == MEM["FETCH"], k in WRITES
    if not (v["MATCH"] and v["V"]):
        return (CAUSES["TLBMISS_X"] if fetch else CAUSES["TLBMISS_W"] if store else CAUSES["TLBMISS_R"]), 1
    if (store and not v["W"]) or (fetch and not v["X"]):
        return CAUSES["PROTECT"], 1
    return 0, 0

# ---------------------------------------------------------------- GALs
def gal_mmuctl():
    """MAR's low bits and its load / source, the shadow PC's load, the TLB
    write pending (TLBW is written late: in the next cycle, at -WSTB, which
    is always an untranslated supervisor fetch), the CRHW write."""
    b = GB("MMUCTL", "MAR control, TLB write pending", clock="CCLK", reset="RESN")
    b.inp(*RD).inp("MARPC", "LR0", "LR1", "TLBW")
    rd = lambda v: num(v, RD)
    for i in (0, 1):
        b.reg("MAR%d" % i, RD + ["MARPC", "LR%d" % i, "MAR%d" % i],
              lambda v, i=i: v["LR%d" % i] if rd(v) == RDST["MAR"]
              else 0 if (rd(v) == RDST["PCMAR"] or v["MARPC"]) else v["MAR%d" % i])
    b.reg("TLBWP", ["TLBW"], lambda v: v["TLBW"])
    b.com("TLBWPN", ["TLBWP"], lambda v: not v["TLBWP"])
    b.com("MARLDN", RD + ["MARPC"], lambda v: not (rd(v) in (RDST["MAR"], RDST["PCMAR"]) or v["MARPC"]))
    b.com("MARSEL", ["MARPC"], lambda v: v["MARPC"])
    b.com("SPCLDN", RD, lambda v: not (rd(v) in (RDST["PC"], RDST["PCMAR"])))
    b.com("CRHWWN", RD, lambda v: rd(v) != RDST["CRHW"])
    return b.build()

def access(v):
    """The access as FAULTA decodes it: misaligned, privilege (user, upper
    half), translated (the TLB decides), fetch, store, a bus kind."""
    k = num(v, MM)
    faulting = k not in (MEM["NONE"], MEM["IACK"])
    mis = faulting and bool(num(v, ["MAR0", "MAR1"]) & (access_size(v) - 1))
    user = faulting and k != MEM["CHECKW"] and not v["SUPER"] and not mis
    return dict(MIS=mis, PRV=user and bool(v["MAR31"]), XL=user and not v["MAR31"],
                ISFETCH=k == MEM["FETCH"], ISSTORE=k in WRITES, BUSK=k in BUSKINDS)

def outcome(v):
    """(cause, TLBF) from FAULTA's outputs and the TLB's entry."""
    if v["MIS"]:
        return CAUSES["MISALIGNED"], 0
    if v["PRV"]:
        return CAUSES["PRIVILEGE"], 0
    if v["XL"] and not (v["MATCH"] and v["V"]):
        return (CAUSES["TLBMISS_X"] if v["ISFETCH"] else CAUSES["TLBMISS_W"] if v["ISSTORE"] else CAUSES["TLBMISS_R"]), 1
    if v["XL"] and ((v["ISSTORE"] and not v["W"]) or (v["ISFETCH"] and not v["X"])):
        return CAUSES["PROTECT"], 1
    return 0, 0

def gal_faulta():
    deps = MM + SZ + ["MAR0", "MAR1", "SUPER", "MAR31"]
    b = GB("FAULTA", "the access: misaligned, privilege, translated").inp(*deps)
    for o in ("MIS", "PRV", "XL", "ISFETCH", "ISSTORE", "BUSK"):
        b.com(o, deps, lambda v, o=o: access(v)[o])
    return b.build()

def gal_faultb():
    """The faults found before the bus (translate()), whether TLBHI takes
    the fault, and whether the bus cycle runs."""
    deps = ["MIS", "PRV", "XL", "ISFETCH", "ISSTORE", "MATCH", "V", "W", "X"]
    b = GB("FAULTB", "faults before the bus; the bus cycle").inp(*deps).inp("BUSK", "RESN")
    for i in range(4):
        b.com("C%d" % i, deps, lambda v, i=i: outcome(v)[0] >> i & 1)
    b.com("PRE", deps, lambda v: outcome(v)[0] != 0)
    b.com("TLBF", deps, lambda v: outcome(v)[1])
    b.com("BUSCYC", deps + ["BUSK", "RESN"], lambda v: v["RESN"] and v["BUSK"] and outcome(v)[0] == 0)
    return b.build()

def check_faults():
    """FAULTA then FAULTB is exactly translate()'s order (pre_fault), for every input."""
    import itertools
    names = MM + SZ + ["MAR0", "MAR1", "SUPER", "MAR31", "MATCH", "V", "W", "X"]
    for bits in itertools.product((0, 1), repeat=len(names)):
        v = dict(zip(names, bits))
        u = dict(v, **access(v))
        if outcome(u) != pre_fault(v):
            raise SystemExit("FAULTA + FAULTB differ from translate() at %s" % v)

def gal_fault2():
    """FAULT and its cause to the CPU (a bus cycle with no -ACK is a bus
    error, except the acknowledge cycle: no answer there is vector FF);
    BEF selects BADADDR's source (the physical address for a bus error);
    STALL: the cycle waits (-WAIT) or a DMA master has the bus."""
    b = GB("FAULT2", "FAULT, its cause, STALL").inp("PRE", "C0", "C1", "C2", "C3", "BUSCYC", "ACKN", *MM)
    b.inp("WAITN", "DMAOWN")
    iack = lambda v: num(v, MM) == MEM["IACK"]
    berr = lambda v: not v["PRE"] and v["BUSCYC"] and not iack(v) and v["ACKN"]
    deps = ["PRE", "BUSCYC", "ACKN"] + MM
    b.com("FAULT", deps, lambda v: v["PRE"] or berr(v))
    b.com("BEF", deps, berr)
    for i in range(4):
        b.com("FC%d" % i, deps + ["C%d" % i],
              lambda v, i=i: v["C%d" % i] if v["PRE"] else (CAUSES["BUSERROR"] >> i & 1 if berr(v) else 0))
    b.com("STALL", ["BUSCYC", "WAITN", "DMAOWN"], lambda v: v["BUSCYC"] and (not v["WAITN"] or v["DMAOWN"]))
    return b.build()

def byte_enables(v):
    lane, size = num(v, ["MAR0", "MAR1"]), access_size(v)
    if num(v, MM) == MEM["IACK"]:
        return 1
    return ((1 << size) - 1) << lane & 15

def gal_strobe():
    """The bus cycle's strobes and byte enables (off the bus while a DMA
    master has it), and the store data's drivers."""
    deps = ["BUSCYC"] + MM + SZ + ["MAR0", "MAR1"]
    b = GB("STROBE", "bus strobes, byte enables").inp(*deps).inp("DMAOWN")
    k = lambda v: num(v, MM)
    b.tri("MRDN", deps, lambda v: not (v["BUSCYC"] and k(v) in READS), "!DMAOWN")
    b.tri("MWRN", deps, lambda v: not (v["BUSCYC"] and k(v) in WRITES), "!DMAOWN")
    b.tri("IACKN", deps, lambda v: not (v["BUSCYC"] and k(v) == MEM["IACK"]), "!DMAOWN")
    for i in range(4):
        b.tri("BE%d" % i, deps, lambda v, i=i: v["BUSCYC"] and byte_enables(v) >> i & 1, "!DMAOWN")
    b.com("DOUTN", deps + ["DMAOWN"], lambda v: not (v["BUSCYC"] and k(v) in WRITES and not v["DMAOWN"]))
    return b.build()

def gal_steer():
    """Load steering onto R (p32uemu's load_extract): byte 0 a lane of D,
    byte 1 the next lane, the vector (IACK) or the fill, bytes 2-3 D or the
    fill (sign or zero); the IACK cycle's R is MAR with the vector in byte
    1, so MAR's drivers for bytes 0, 2, 3 come on with it."""
    deps = MM + SZ + ["MAR0", "MAR1"]
    b = GB("STEER", "load steering").inp(*deps).inp("SIGNED", "LR7", "LR15", "RMEMN", "RMARN")
    k = lambda v: num(v, MM)
    reading = lambda v: not v["RMEMN"]
    iack = lambda v: k(v) == MEM["IACK"]
    size = lambda v: access_size(v) if k(v) == MEM["LOAD"] else 4
    def b0sel(v):
        s = size(v)
        return num(v, ["MAR0", "MAR1"]) if s == 1 else (2 * v["MAR1"] if s == 2 else 0)
    def b1sel(v):
        if iack(v):
            return 2
        s = size(v)
        return 3 if s == 1 else (1 if s == 2 and v["MAR1"] else 0)
    b.com("B0S0", deps, lambda v: b0sel(v) & 1)
    b.com("B0S1", deps, lambda v: b0sel(v) >> 1)
    b.com("B1S0", deps, lambda v: b1sel(v) & 1)
    b.com("B1S1", deps, lambda v: b1sel(v) >> 1)
    b.com("HISEL", deps, lambda v: size(v) != 4)
    b.com("FILL", deps + ["SIGNED", "LR7", "LR15"],
          lambda v: k(v) == MEM["LOAD"] and v["SIGNED"] and (v["LR7"] if size(v) == 1 else v["LR15"] if size(v) == 2 else 0))
    b.com("B0ENN", MM + ["RMEMN"], lambda v: not (reading(v) and not iack(v)))
    b.com("B23ENN", MM + ["RMEMN"], lambda v: not (reading(v) and not iack(v)))
    b.com("B1ENN", ["RMEMN"], lambda v: not reading(v))
    b.com("MAR023N", MM + ["RMEMN", "RMARN"], lambda v: not (not v["RMARN"] or (reading(v) and iack(v))))
    return b.build()

def gal_lock():
    """The bus lock (D54): from a locked LOADW's commit to its STOREW's (or a
    fault); and the TLB tag's match (all four comparators equal)."""
    b = GB("LOCKG", "bus lock; TLB match", clock="CCLK", reset="RESN")
    b.inp(*MM).inp("LOCKREQ", "FAULT", "EQ0N", "EQ1N", "EQ2N", "EQ3N")
    b.reg("LOCKED", MM + ["LOCKREQ", "FAULT", "LOCKED"],
          lambda v: 0 if v["FAULT"] else 1 if (num(v, MM) == MEM["LOADW"] and v["LOCKREQ"])
          else 0 if num(v, MM) == MEM["STOREW"] else v["LOCKED"])
    b.com("MATCH", ["EQ0N", "EQ1N", "EQ2N", "EQ3N"], lambda v: not (v["EQ0N"] or v["EQ1N"] or v["EQ2N"] or v["EQ3N"]))
    return b.build()

SLOTS = [0] + list(range(3, 11))            # the requesters: the debug slot (D64) and slots 3-10
def priority(s):
    """Higher wins: slot 0 (the debug card) first, then the higher slot."""
    return 100 if s == 0 else s

def gal_arb():
    """The arbiter (D50, D64): at a CLK edge with the bus free and not
    locked, the requesting slot of the highest priority (slot 0, then the
    highest) gets -BGNT; it keeps it while it asks."""
    REQ = ["BREQ%dN" % s for s in SLOTS]
    GN = ["BGNT%dN" % s for s in SLOTS]
    b = GB("ARB", "the bus arbiter (slot 0, slots 3-10)", clock="CLK", reset="RESN").inp(*REQ).inp("LOCKN")
    def nxt(v, s):
        if any(not v[g] for g in GN):                   # someone has it: kept while asked for
            return not v["BGNT%dN" % s] and not v["BREQ%dN" % s]
        if not v["LOCKN"]:
            return 0
        asking = [t for t in SLOTS if not v["BREQ%dN" % t]]
        return bool(asking) and max(asking, key=priority) == s
    for s in SLOTS:
        hi = ["BREQ%dN" % t for t in SLOTS if priority(t) > priority(s)]
        eq = "!BGNT%dN & !BREQ%dN | %s & LOCKN & !BREQ%dN%s" % (
            s, s, " & ".join(GN), s, "".join(" & " + h for h in hi))
        # (reset_high: the equation is the register's, i.e. "granted"; the pin, -BGNT, its inverse)
        b.reg("BGNT%dN" % s, GN + REQ + ["LOCKN"], lambda v, s=s: not nxt(v, s), reset_high=True, eq=eq)
    b.com("DMAOWN", GN, lambda v: any(not v[g] for g in GN))
    return b.build()

def gals():
    check_faults()
    return {"MMUCTL": gal_mmuctl(), "FAULTA": gal_faulta(), "FAULTB": gal_faultb(), "FAULT2": gal_fault2(), "STROBE": gal_strobe(),
            "STEER": gal_steer(), "LOCKG": gal_lock(), "ARB": gal_arb()}

# ---------------------------------------------------------------- the netlist
# TLB entry bits in its seven SRAMs: 0-19 VPN (TLBHI 31:12), 20-27 ASID, 28-47 PFN (TLBLO 31:12), 48 V, 49 W, 50 X
E_VPN, E_ASID, E_PFN, E_V, E_W, E_X = 0, 20, 28, 48, 49, 50

def build():
    G = gals()
    c = Card("mmu-card", "P32W MMU / DATA CARD")
    role = {}
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
    def gal(name, function, netmap):
        g = G[name]
        ref = U(gal_device(g), function, name)
        for p, (num_, t) in DEV[c.parts[ref][0]]["pins"].items():
            if t in ("pwr", "gnd"):
                continue
            if p.startswith("NC") and t == "pas":
                c.unused(ref, p)
                continue
            w(ref, p, netmap.get(p, p))
        return ref
    LR = lambda i: "LR%d" % i
    MAR = lambda i: "MAR%d" % i
    D = lambda i: "D%d" % i
    E = lambda i: "E%d" % i

    # ---- MAR (bits 31:2 count by 4; 1:0 in MMUCTL) and the shadow PC
    gal("MMUCTL", "MAR control, TLB write pending", {"RESN": "-RES", "CRHWWN": "CRHWW_N"})
    for m in range(8):
        x = U("74HCT157", "MAR input: R or the shadow PC, bits %d-%d" % (4 * m + 2, 4 * m + 5))
        w(x, "S", "MARSEL"); w(x, "-E", "GND")
        for ch in range(1, 5):
            bit = 4 * m + 2 + ch - 1
            if bit < 32:
                w(x, "%dA" % ch, LR(bit)); w(x, "%dB" % ch, "SPC%d" % bit); w(x, "%dY" % ch, "MARIN%d" % bit)
            else:
                w(x, "%dA" % ch, "GND"); w(x, "%dB" % ch, "GND"); c.unused(x, "%dY" % ch)
    for name, din, ld, inc, out in (("MAR", "MARIN", "MARLDN", "MARINC", "MAR"), ("shadow PC", "LR", "SPCLDN", "PCINC", "SPC")):
        for k in range(8):
            x = U("74F161" if name == "MAR" else "74HCT161", "%s bits %d-%d" % (name, 4 * k + 2, 4 * k + 5), "%s%d" % (out.lower(), k))
            w(x, "CLK", "CCLK"); w(x, "-CLR", "VCC"); w(x, "-LOAD", ld); w(x, "ENP", inc)
            w(x, "ENT", "VCC" if k == 0 else "%sC%d" % (out, k))   # (the synchronous cascade: D71)
            if k < 7:
                w(x, "RCO", "%sC%d" % (out, k + 1))
            else:
                c.unused(x, "RCO")
            for j, (dp, qp) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
                bit = 4 * k + 2 + j
                if bit < 32:
                    w(x, dp, "%s%d" % (din, bit)); w(x, qp, "%s%d" % (out, bit))
                else:
                    w(x, dp, "GND"); c.unused(x, qp)

    # ---- the TLB: index, seven SRAMs, the late write, the comparators
    for m in range(2):
        x = U("74ACT157", "TLB index: lookup or TLBW")
        w(x, "S", "TLBWP"); w(x, "-E", "GND")
        for ch in range(1, 5):
            j = 4 * m + ch - 1
            if j < 6:
                w(x, "%dA" % ch, MAR(12 + j)); w(x, "%dB" % ch, "TLBHI%d" % (12 + j)); w(x, "%dY" % ch, "TA%d" % j)
            elif j == 6:
                w(x, "%dA" % ch, "ISFETCH"); w(x, "%dB" % ch, "TLBHI8"); w(x, "%dY" % ch, "TA6")
            else:
                w(x, "%dA" % ch, "GND"); w(x, "%dB" % ch, "GND"); c.unused(x, "%dY" % ch)
    src = {}
    for j in range(20):
        src[E_VPN + j] = "TLBHI%d" % (12 + j)
        src[E_PFN + j] = "TLBLO%d" % (12 + j)
    for j in range(8):
        src[E_ASID + j] = "TLBHI%d" % j
    src[E_V], src[E_W], src[E_X] = "TLBLO0", "TLBLO1", "TLBLO2"
    for k in range(7):
        s = U("CY7C199", "TLB entries, bits %d-%d" % (8 * k, 8 * k + 7), "tlb%d" % k)
        for a in range(15):
            w(s, "A%d" % a, "TA%d" % a if a < 7 else "GND")
        w(s, "-CE", "GND"); w(s, "-OE", "TLBWP"); w(s, "-WE", "TLBWE_N")
        bf = U("74ACT541", "TLB write data, bits %d-%d" % (8 * k, 8 * k + 7))
        w(bf, "-OE1", "TLBWPN"); w(bf, "-OE2", "GND")
        for j in range(8):
            e = 8 * k + j
            w(s, "IO%d" % j, E(e)); w(bf, "A%d" % j, src.get(e, "GND")); w(bf, "Y%d" % j, E(e))
    for k in range(4):
        x = U("74F521", "TLB tag compare (VPN, ASID), bits %d-%d" % (8 * k, 8 * k + 7))
        w(x, "-G", "GND")
        for j in range(8):
            t = 8 * k + j
            p = MAR(12 + t) if t < 20 else "ASID%d" % (t - 20) if t < 28 else "GND"
            w(x, "P%d" % j, p); w(x, "Q%d" % j, E(t) if t < 28 else "GND")
        w(x, "-P=Q", "EQ%dN" % k)
    gal("LOCKG", "bus lock; TLB match", {"RESN": "-RES"})
    gal("FAULTA", "the access: misaligned, privilege, translated", {})
    gal("FAULTB", "faults before the bus; the bus cycle", {"V": E(E_V), "W": E(E_W), "X": E(E_X), "RESN": "-RES"})
    gal("FAULT2", "FAULT, its cause, STALL", {"ACKN": "-ACK", "WAITN": "-WAIT"})

    # ---- the physical address and the bus
    for m in range(5):
        x = U("74HCT157", "physical address: the TLB's PFN or MAR (supervisor), bits %d-%d" % (12 + 4 * m, 15 + 4 * m))
        w(x, "S", "SUPER"); w(x, "-E", "GND")
        for ch in range(1, 5):
            j = 4 * m + ch - 1
            w(x, "%dA" % ch, E(E_PFN + j)); w(x, "%dB" % ch, MAR(12 + j)); w(x, "%dY" % ch, "PA%d" % (12 + j))
    x = U("74HCT157", "A4:A2: the level in the acknowledge cycle")
    w(x, "S", "IACKSEL"); w(x, "-E", "GND")
    for ch in range(1, 5):
        if ch <= 3:
            w(x, "%dA" % ch, MAR(1 + ch)); w(x, "%dB" % ch, MAR(ch - 1)); w(x, "%dY" % ch, "ADR%d" % (1 + ch))
        else:
            w(x, "%dA" % ch, "GND"); w(x, "%dB" % ch, "GND"); c.unused(x, "%dY" % ch)
    abits = list(range(2, 32))
    for k in range(4):
        x = U("74ACT541", "address onto the bus, A%d-A%d" % (abits[8 * k], abits[min(8 * k + 7, 29)]))
        w(x, "-OE1", "DMAOWN"); w(x, "-OE2", "GND")
        for j in range(8):
            if 8 * k + j < 30:
                bit = abits[8 * k + j]
                srcn = "ADR%d" % bit if bit <= 4 else MAR(bit) if bit < 12 else "PA%d" % bit
                w(x, "A%d" % j, srcn); w(x, "Y%d" % j, "A%d" % bit)
            else:
                w(x, "A%d" % j, "GND"); c.unused(x, "Y%d" % j)
    gal("STROBE", "bus strobes, byte enables", {"MRDN": "-MRD", "MWRN": "-MWR", "IACKN": "-IACK", "DOUTN": "DOUT_N"})
    for k in range(4):
        x = U("74HCT541", "store data onto the bus, D%d-D%d" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "DOUT_N"); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, LR(8 * k + j)); w(x, "Y%d" % j, D(8 * k + j))
    gal("ARB", "the bus arbiter (slot 0, slots 3-10)",
        {**{"BREQ%dN" % s: "-BREQ%d" % s for s in SLOTS}, **{"BGNT%dN" % s: "-BGNT%d" % s for s in SLOTS},
         "LOCKN": "-LOCK", "RESN": "-RES"})
    oc = U("74HC05", "-LOCK (open drain)")
    w(oc, "1A", "LOCKED"); w(oc, "1Y", "-LOCK")
    for g in range(2, 7):
        w(oc, "%dA" % g, "GND"); c.unused(oc, "%dY" % g)

    # ---- the hardware control registers: TLBHI, TLBLO, ASID; BADADDR
    hibits = list(range(0, 9)) + list(range(12, 32))
    for m in range(8):
        x = U("74HCT157", "TLBHI input: R (MTCR) or the fault")
        w(x, "S", "TLBF"); w(x, "-E", "GND")
        for ch in range(1, 5):
            j = 4 * m + ch - 1
            if j < len(hibits):
                bit = hibits[j]
                fsrc = "ASID%d" % bit if bit < 8 else "ISFETCH" if bit == 8 else MAR(bit)
                w(x, "%dA" % ch, LR(bit)); w(x, "%dB" % ch, fsrc); w(x, "%dY" % ch, "TLBHIIN%d" % bit)
            else:
                w(x, "%dA" % ch, "GND"); w(x, "%dB" % ch, "GND"); c.unused(x, "%dY" % ch)
    def reg377(fn, bits, din, q, en, r=None, dev="74HCT377"):
        for k in range(0, len(bits), 8):
            x = U(dev, fn % (bits[k], bits[min(k + 7, len(bits) - 1)]), (r + str(k // 8)) if r else None)
            w(x, "CLK", "CCLK"); w(x, "-E", en)
            for j in range(8):
                if k + j < len(bits):
                    bit = bits[k + j]
                    w(x, "D%d" % j, din(bit)); w(x, "Q%d" % j, q(bit))
                else:
                    w(x, "D%d" % j, "GND"); c.unused(x, "Q%d" % j)
    reg377("TLBHI bits %d-%d", hibits, lambda b_: "TLBHIIN%d" % b_, lambda b_: "TLBHI%d" % b_, "TLBHILD_N", "tlbhi", dev="74ABT377")
    lobits = [0, 1, 2] + list(range(12, 32))
    reg377("TLBLO bits %d-%d", lobits, LR, lambda b_: "TLBLO%d" % b_, "TLBLOW_N", "tlblo", dev="74ABT377")
    reg377("ASID bits %d-%d", list(range(8)), LR, lambda b_: "ASID%d" % b_, "ASIDW_N", "asid")
    for m in range(5):
        x = U("74HCT157", "BADADDR input: MAR, or the physical address for a bus error")
        w(x, "S", "BEF"); w(x, "-E", "GND")
        for ch in range(1, 5):
            j = 12 + 4 * m + ch - 1
            w(x, "%dA" % ch, MAR(j)); w(x, "%dB" % ch, "PA%d" % j); w(x, "%dY" % ch, "BADIN%d" % j)
    reg377("BADADDR bits %d-%d", list(range(32)), lambda b_: MAR(b_) if b_ < 12 else "BADIN%d" % b_,
           lambda b_: "BAD%d" % b_, "FAULT_N", "bad")
    cw = U("74HCT138", "hardware CR writes (RDST = CRHW): ASID, TLBHI, TLBLO")
    w(cw, "A", "CRSEL0"); w(cw, "B", "CRSEL1"); w(cw, "C", "CRSEL2"); w(cw, "G1", "VCC")
    w(cw, "-G2A", "CRHWW_N"); w(cw, "-G2B", "GND")
    for k, net in ((HWCR["ASID"], "ASIDW_N"), (HWCR["TLBHI"], "TLBHIW_N"), (HWCR["TLBLO"], "TLBLOW_N")):
        w(cw, "Y%d" % k, net)
    c.unused(cw, *["Y%d" % k for k in range(8) if k not in (HWCR["ASID"], HWCR["TLBHI"], HWCR["TLBLO"])])

    # ---- R's sources on this card
    r0 = U("74HCT138", "R source decode, RSRC 0-7")
    w(r0, "A", "RSRC0"); w(r0, "B", "RSRC1"); w(r0, "C", "RSRC2"); w(r0, "G1", "VCC"); w(r0, "-G2A", "RSRC3"); w(r0, "-G2B", "GND")
    for k, net in ((RSRC["MEM"], "RMEM_N"), (RSRC["MAR"], "RMAR_N"), (RSRC["BADADDR"], "RBAD_N")):
        w(r0, "Y%d" % k, net)
    c.unused(r0, *["Y%d" % k for k in range(8) if k not in (RSRC["MEM"], RSRC["MAR"], RSRC["BADADDR"])])
    r1 = U("74HCT138", "R source decode: CRHW, which register")
    w(r1, "A", "CRSEL0"); w(r1, "B", "CRSEL1"); w(r1, "C", "CRSEL2")
    w(r1, "G1", "RSRC3"); w(r1, "-G2A", "RSRC0"); w(r1, "-G2B", "RSRC1")   # RSRC = 8 (CRHW): RSRC3 and not 0, 1 ...
    for k, net in ((HWCR["ASID"], "RASID_N"), (HWCR["TLBHI"], "RTLBHI_N"), (HWCR["TLBLO"], "RTLBLO_N")):
        w(r1, "Y%d" % k, net)
    c.unused(r1, *["Y%d" % k for k in range(8) if k not in (HWCR["ASID"], HWCR["TLBHI"], HWCR["TLBLO"])])
    # (RSRC 9, the CPU card's address adder, has RSRC0 set: the decoder is off for it)
    for k in range(4):
        x = U("74HCT541", "MAR onto R, bits %d-%d" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "RMAR_N" if k == 1 else "MAR023_N"); w(x, "-OE2", "GND")
        for j in range(8):
            w(x, "A%d" % j, MAR(8 * k + j)); w(x, "Y%d" % j, LR(8 * k + j))
    for fn, q, en, have in (("BADADDR", "BAD", "RBAD_N", range(32)), ("ASID", "ASID", "RASID_N", range(8)),
                            ("TLBHI", "TLBHI", "RTLBHI_N", hibits), ("TLBLO", "TLBLO", "RTLBLO_N", lobits)):
        for k in range(4):
            x = U("74HCT541", "%s onto R, bits %d-%d" % (fn, 8 * k, 8 * k + 7))
            w(x, "-OE1", en); w(x, "-OE2", "GND")
            for j in range(8):
                bit = 8 * k + j
                w(x, "A%d" % j, "%s%d" % (q, bit) if bit in have else "GND"); w(x, "Y%d" % j, LR(bit))
    gal("STEER", "load steering", {"RMEMN": "RMEM_N", "RMARN": "RMAR_N", "B0ENN": "B0EN_N", "B23ENN": "B23EN_N",
                                   "B1ENN": "B1EN_N", "MAR023N": "MAR023_N"})
    for m in range(4):
        x = U("74HCT253", "load steering, byte 0: the addressed lane")
        w(x, "S0", "B0S0"); w(x, "S1", "B0S1"); w(x, "-1OE", "B0EN_N"); w(x, "-2OE", "B0EN_N")
        for h in (1, 2):
            j = 2 * m + h - 1
            for lane in range(4):
                w(x, "%dI%d" % (h, lane), D(8 * lane + j))
            w(x, "%dY" % h, LR(j))
    for m in range(4):
        x = U("74HCT253", "load steering, byte 1: lane 1, lane 3, the vector, the fill")
        w(x, "S0", "B1S0"); w(x, "S1", "B1S1"); w(x, "-1OE", "B1EN_N"); w(x, "-2OE", "B1EN_N")
        for h in (1, 2):
            j = 2 * m + h - 1
            w(x, "%dI0" % h, D(8 + j)); w(x, "%dI1" % h, D(24 + j)); w(x, "%dI2" % h, D(j)); w(x, "%dI3" % h, "FILL")
            w(x, "%dY" % h, LR(8 + j))
    for m in range(4):
        x = U("74HCT257", "load steering, bytes 2-3: the data or the fill")
        w(x, "S", "HISEL"); w(x, "-OE", "B23EN_N")
        for ch in range(1, 5):
            j = 16 + 4 * m + ch - 1
            w(x, "%dA" % ch, D(j)); w(x, "%dB" % ch, "FILL"); w(x, "%dY" % ch, LR(j))

    # ---- glue
    ag = U("74ACT08", "IACK select; TLBHI load")
    w(ag, "1A", "MEM0"); w(ag, "1B", "MEM1"); w(ag, "1Y", "MEM01")
    w(ag, "2A", "MEM01"); w(ag, "2B", "MEM2"); w(ag, "2Y", "IACKSEL")
    w(ag, "3A", "TLBHIW_N"); w(ag, "3B", "TLBF_N"); w(ag, "3Y", "TLBHILD_N")
    w(ag, "4A", "GND"); w(ag, "4B", "GND"); c.unused(ag, "4Y")
    ng = U("74HCT00", "inverters: TLBF, FAULT")
    w(ng, "1A", "TLBF"); w(ng, "1B", "TLBF"); w(ng, "1Y", "TLBF_N")
    w(ng, "2A", "FAULT"); w(ng, "2B", "FAULT"); w(ng, "2Y", "FAULT_N")
    for g in (3, 4):
        w(ng, "%dA" % g, "GND"); w(ng, "%dB" % g, "GND"); c.unused(ng, "%dY" % g)
    og = U("74HCT32", "the TLB's -WE")
    w(og, "1A", "-WSTB"); w(og, "1B", "TLBWPN"); w(og, "1Y", "TLBWE_N")
    for g in (2, 3, 4):
        w(og, "%dA" % g, "GND"); w(og, "%dB" % g, "GND"); c.unused(og, "%dY" % g)
    for i, (fn, nets) in enumerate((("-BREQ3-10 pull-ups (empty slots)", ["-BREQ%d" % s for s in range(3, 11)]),
                                    ("D0-7 pull-ups (no vector: FF)", [D(j) for j in range(8)])), 1):
        c.part("RN%d" % i, "RNET9", fn, value="10k x 8 bussed")
        w("RN%d" % i, "COM", "VCC")
        for j, net in enumerate(nets):
            w("RN%d" % i, "R%d" % (j + 1), net)
    c.part("R1", "R", "-BREQ0 pull-up (no debug card in slot 0)", value="10k")
    w("R1", "1", "-BREQ0"); w("R1", "2", "VCC")
    c.finish()
    CARDS[c.name] = c
    return c, G, role

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("mmu card: %d errors" % len(errs))
    print("mmu card OK: %d ICs (%d GALs), %d nets, %d parts" % (len(ics), len(G), len(c.nets), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
