#!/usr/bin/env python3
"""The P32W disk card (any of slots 3-10): docs/disk-card.md, D26, D50, D52,
D57, D66.

Four ATA ports, PIO, 16 bits wide (D26), in the 4 KiB device window its
jumpers choose (window 6, FFFF_E000, by default), registers in the window's
top 256 bytes (D52):

  FF00 + 16 p   port p's registers: +0-7 the task file (CS0: +0 data, a
                halfword or a byte; +7 status / command), +8-15 the control
                block (CS1: +14 alternate status / device control).
                Ports 0 and 1: the CompactFlash adapters (right-angle 40-pin
                headers at the front edge, each taking a CF-to-IDE adapter
                with its slot facing out); 2 and 3: IDE channels A and B
                (boxed 40-pin headers), master and slave each.
  FFF0 card ID (2)   FFF1 revision (0)   FFF4 interrupt status (bit p: port
                p's INTRQ)

The data register answers halfwords and bytes, every other register bytes,
FFF4 words; anything else in the window gets no -ACK (a bus error), as
p32io.h. A port with nothing on it reads 00 (pull-downs on its data lines).

  * Two wait states on every port access: the card pulls -WAIT for two
    clocks (D50). The address and chip selects are out from the first
    clock, -DIOR / -DIOW for the whole second (333 ns at 3 MHz, ATA PIO
    mode 0 wants 165), the third is the recovery: the write data held, the
    read data from a latch that took it as -DIOR ended. IORDY low stretches
    the strobe. GAL DKSEQ, clocked by CLK, is the sequence.
  * One 16-bit card bus CD0-15: five 74HCT245s put it on the bus lanes (the
    data register on lanes 0-1, a byte register on its lane, onto CD0-7);
    two 74HCT245s per port between CD and the port's DD0-15, enabled only
    for that port; a 74HCT541 per port drives its DA2-0, -CS0, -CS1,
    -DIOR, -DIOW and -RESET.
  * Interrupts: one level for the card, three jumpers (default 1, D52); a
    74LS145 pulls that -IRQ line while any port's INTRQ is up; in the
    acknowledge cycle a 74HCT85 compares the level, GAL DKIRQ picks the
    lowest asking port, GAL DKVEC drives the vector (window x 16 + port).
  * An activity LED per port (the drives' -DASP) at the front edge.

    python3 card_disk.py [--bom]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from card_cpu import GB, num                                  # noqa: E402

CID, CREV, CISR = 0xF0, 0xF1, 0xF4
NPORT = 4
BA = ["BA%d" % i for i in range(2, 8)]
BBE = ["BBE%d" % k for k in range(4)]
LNS = ["LN0", "LN1"]
WINJ = ["WIN0", "WIN1", "WIN2"]
LVL = ["L0", "L1", "L2"]
ST = ["S0", "S1"]                       # the access sequence: 0 address, 1 strobe, 2 recovery
WINDOW, LEVEL = 6, 1                    # the default jumpers (D52)

def onehot(v):
    return sum(1 for b in BBE if v.get(b)) == 1
def lane(v):
    return [k for k in range(4) if v.get(BBE[k])][0]

# ---------------------------------------------------------------- GALs
def gal_dksel():
    """This card's window (A31:A16 all ones by two 74HCT688s, A15 = 1,
    A14:A12 the jumpers, A11:A8 = F) and the cycle: CRD, CWR, CACC; INTA, an
    acknowledge cycle."""
    A = ["A%d" % i for i in range(8, 16)]
    b = GB("DKSEL", "disk card: window decode").inp("HI1N", "HI2N", *A).inp(*WINJ).inp("MRDN", "MWRN", "IACKN")
    mine = lambda v: (not v["HI1N"] and not v["HI2N"] and num(v, A[:4]) == 15 and v["A15"]
                      and num(v, ["A12", "A13", "A14"]) == num(v, WINJ))
    deps = ["HI1N", "HI2N"] + A + WINJ
    b.com("CRD", deps + ["MRDN"], lambda v: mine(v) and not v["MRDN"])
    b.com("CWR", deps + ["MWRN"], lambda v: mine(v) and not v["MWRN"])
    b.com("CACC", deps + ["MRDN", "MWRN"], lambda v: mine(v) and not (v["MRDN"] and v["MWRN"]))
    b.com("INTA", ["IACKN"], lambda v: not v["IACKN"])
    return b.build()

def gal_dkbe():
    """The byte enables: BYTE (exactly one) and its lane LN1:LN0, HALF (lanes
    0-1: the data register), WORD."""
    b = GB("DKBE", "disk card: access size, lane").inp(*BBE)
    b.com("BYTE", BBE, onehot)
    b.com("HALF", BBE, lambda v: v["BBE0"] and v["BBE1"] and not v["BBE2"] and not v["BBE3"])
    b.com("WORD", BBE, lambda v: all(v[x] for x in BBE))
    b.com("LN0", BBE, lambda v: lane(v) & 1 if onehot(v) else 0)
    b.com("LN1", BBE, lambda v: lane(v) >> 1 if onehot(v) else 0)
    return b.build()

def offset(v):
    return num(v, BA) << 2 | num(v, LNS)

def gal_dkdec():
    """PSEL: a port register (a byte at any of its 16 offsets, or a halfword
    at +0, the data register: WIDE); SIDR the ID / revision bytes; SISR the
    interrupt status word."""
    D = BA + LNS + ["BYTE", "HALF", "WORD", "CACC"]
    b = GB("DKDEC", "disk card: register selects").inp(*D)
    port = lambda v: num(v, BA) >> 2 < NPORT and not v["BA6"] and not v["BA7"]
    wide = lambda v: v["CACC"] and v["HALF"] and port(v) and num(v, BA) & 3 == 0
    byte = lambda v: v["CACC"] and v["BYTE"] and port(v)
    b.com("PSEL", D, lambda v: wide(v) or byte(v))
    b.com("WIDE", D, wide)
    b.com("SIDR", D, lambda v: bool(v["CACC"] and v["BYTE"]) and offset(v) in (CID, CREV))
    b.com("SISR", D, lambda v: bool(v["CACC"] and v["WORD"]) and num(v, BA) << 2 == CISR)
    return b.build()

def gal_dkseq():
    """The port access's sequence, clocked by CLK (reset by -RES): S = 0 the
    first clock (address and chip select out, -WAIT), 1 the strobe (held
    while the port's IORDY is low), 2 the recovery (the strobe off, -WAIT
    released: the cycle ends at its end). -DIOR / -DIOW in state 1; the read
    latch onto CD in state 2; STRB (state 1) for the port transceivers."""
    b = GB("DKSEQ", "disk card: wait states, strobes", clock="BCLK", reset="BRESN")
    b.inp("PSEL", "CRD", "CWR", "IORDYS", "BRESN")
    s = lambda v: num(v, ST)
    def nxt(v):
        if not v["PSEL"]:
            return 0
        return {0: 1, 1: 1 if not v["IORDYS"] else 2, 2: 0, 3: 0}[s(v)]
    b.reg("S0", ["PSEL", "IORDYS"] + ST, lambda v: nxt(v) & 1)
    b.reg("S1", ["PSEL", "IORDYS"] + ST, lambda v: nxt(v) >> 1)
    b.com("WAIT", ["PSEL"] + ST, lambda v: v["PSEL"] and s(v) in (0, 1))
    b.com("DIORN", ["PSEL", "CRD"] + ST, lambda v: not (v["PSEL"] and v["CRD"] and s(v) == 1))
    b.com("DIOWN", ["PSEL", "CWR"] + ST, lambda v: not (v["PSEL"] and v["CWR"] and s(v) == 1))
    b.com("STRB", ST, lambda v: s(v) == 1)
    b.com("RLOEN", ["PSEL", "CRD"] + ST, lambda v: not (v["PSEL"] and v["CRD"] and s(v) == 2))
    return b.build()

def gal_dkports(g):
    """Ports 2g and 2g+1: each one's -CS0 / -CS1 (BA3: the control block),
    strobes, and its data transceivers' enable (a write: the whole access,
    the data held through the recovery; a read: the strobe only, so the
    read latch has CD after it)."""
    ports = (2 * g, 2 * g + 1)
    D = ["PSEL", "BA3", "BA4", "BA5", "DIORN", "DIOWN", "CWR", "STRB"]
    b = GB("DKPORT%d" % g, "disk card: ports %d and %d" % ports).inp(*D)
    me = lambda v, p: v["PSEL"] and num(v, ["BA4", "BA5"]) == p
    for p in ports:
        b.com("CS0N%d" % p, ["PSEL", "BA3", "BA4", "BA5"], lambda v, p=p: not (me(v, p) and not v["BA3"]))
        b.com("CS1N%d" % p, ["PSEL", "BA3", "BA4", "BA5"], lambda v, p=p: not (me(v, p) and v["BA3"]))
        b.com("DIORN%d" % p, ["PSEL", "BA4", "BA5", "DIORN"], lambda v, p=p: not (me(v, p) and not v["DIORN"]))
        b.com("DIOWN%d" % p, ["PSEL", "BA4", "BA5", "DIOWN"], lambda v, p=p: not (me(v, p) and not v["DIOWN"]))
        b.com("DOEN%d" % p, ["PSEL", "BA4", "BA5", "CWR", "STRB"],
              lambda v, p=p: not (me(v, p) and (v["CWR"] or v["STRB"])))
    return b.build()

def gal_dkstb():
    """-ACK; the lane transceivers' enables and direction (the data
    register on lanes 0-1 onto CD0-15, a byte on its lane onto CD0-7, the
    status word's low byte on lane 0; the vector on lane 0); ZOEN: zeros on
    lanes 1-3 for the status word."""
    D = ["PSEL", "WIDE", "SIDR", "SISR", "BYTE", "CRD", "CWR", "INTA", "VHIT"] + LNS
    b = GB("DKSTB", "disk card: -ACK, byte lanes").inp(*D)
    byte = lambda v: (v["PSEL"] and not v["WIDE"]) or v["SIDR"]
    ln = lambda v: num(v, LNS)
    b.com("ACK", ["PSEL", "SIDR", "SISR", "INTA", "VHIT"],
          lambda v: v["PSEL"] or v["SIDR"] or v["SISR"] or (v["INTA"] and v["VHIT"]))
    b.com("LE0N", D, lambda v: not ((byte(v) and ln(v) == 0) or (v["PSEL"] and v["WIDE"]) or v["SISR"]
                                    or (v["INTA"] and v["VHIT"])))
    b.com("LE1WN", ["PSEL", "WIDE"], lambda v: not (v["PSEL"] and v["WIDE"]))
    b.com("LE1BN", D, lambda v: not (byte(v) and ln(v) == 1))
    b.com("LE2N", D, lambda v: not (byte(v) and ln(v) == 2))
    b.com("LE3N", D, lambda v: not (byte(v) and ln(v) == 3))
    b.com("LDIR", ["CWR"], lambda v: v["CWR"])
    b.com("ZOEN", ["SISR", "CRD"], lambda v: not (v["SISR"] and v["CRD"]))
    return b.build()

def gal_dkirq():
    """The ports' interrupts: ANYN (to the level's 74LS145), the lowest
    asking port (VEC1:0), VHIT (asking, and the level acknowledged is the
    card's: EQ from a 74HCT85), IDOE (DKVEC drives the byte bus)."""
    IRQ = ["INTRQ%d" % p for p in range(NPORT)]
    b = GB("DKIRQ", "disk card: interrupts, the vector's port").inp(*IRQ).inp("EQ", "INTA", "SIDR", "SISR", "CRD")
    low = lambda v: next((p for p in range(NPORT) if v["INTRQ%d" % p]), None)
    b.com("ANYN", IRQ, lambda v: low(v) is None)
    b.com("VEC0", IRQ, lambda v: None if low(v) is None else low(v) & 1)
    b.com("VEC1", IRQ, lambda v: None if low(v) is None else low(v) >> 1)
    b.com("VHIT", IRQ + ["EQ", "INTA"], lambda v: bool(v["INTA"] and v["EQ"]) and low(v) is not None)
    b.com("IDOE", IRQ + ["EQ", "INTA", "SIDR", "SISR", "CRD"],
          lambda v: (v["INTA"] and v["EQ"] and low(v) is not None) or ((v["SIDR"] or v["SISR"]) and v["CRD"]))
    return b.build()

def gal_dkvec():
    """Onto CD0-7: the card ID (FFF0: 2), the revision (FFF1: 0), the status
    word's low byte (bit p: port p's INTRQ), the vector (window x 16 + the
    port)."""
    IRQ = ["INTRQ%d" % p for p in range(NPORT)]
    b = GB("DKVEC", "disk card: ID, status, vector").inp("IDOE", "LN0", "INTA", "SISR", "VEC0", "VEC1").inp(*WINJ).inp(*IRQ)
    def val(v):
        if v["INTA"]:
            return num(v, WINJ) << 4 | num(v, ["VEC0", "VEC1"])
        if v["SISR"]:
            return num(v, IRQ)
        return 0 if v["LN0"] else 2
    deps = ["LN0", "INTA", "SISR", "VEC0", "VEC1"] + WINJ + IRQ
    for i in range(8):
        b.tri("CD%d" % i, deps, lambda v, i=i: val(v) >> i & 1, "IDOE")
    return b.build()

def gals():
    G = {}
    for f in (gal_dksel, gal_dkbe, gal_dkdec, gal_dkseq, gal_dkstb, gal_dkirq, gal_dkvec):
        g = f()
        G[g.name] = g
    for k in range(2):
        g = gal_dkports(k)
        G[g.name] = g
    return G

# ---------------------------------------------------------------- the ATA port header (40 pins)
ATA = {"-RESET": 1, "DD7": 3, "DD8": 4, "DD6": 5, "DD9": 6, "DD5": 7, "DD10": 8, "DD4": 9, "DD11": 10,
       "DD3": 11, "DD12": 12, "DD2": 13, "DD13": 14, "DD1": 15, "DD14": 16, "DD0": 17, "DD15": 18,
       "DMARQ": 21, "-DIOW": 23, "-DIOR": 25, "IORDY": 27, "CSEL": 28, "-DMACK": 29, "INTRQ": 31,
       "-IOCS16": 32, "DA1": 33, "-PDIAG": 34, "DA0": 35, "DA2": 36, "-CS0": 37, "-CS1": 38, "-DASP": 39}
ATA_GND = (2, 19, 22, 24, 26, 30, 40)

# ---------------------------------------------------------------- the netlist
def build():
    G = gals()
    c = Card("disk-card", "P32W DISK CARD")
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
    def gnd_unused(ref, ins, outs):
        for p in ins:
            w(ref, p, "GND")
        c.unused(ref, *outs)

    # ---- the bus interface: one load per line
    b1 = U("74HCT541", "bus buffer: A2-A7, BE0, BE1")
    b2 = U("74HCT541", "bus buffer: BE2, BE3, -MRD, -MWR, -IACK, CLK, -RES")
    for x in (b1, b2):
        w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
    for j, (src, dst) in enumerate([("A%d" % i, "BA%d" % i) for i in range(2, 8)] + [("BE0", "BBE0"), ("BE1", "BBE1")]):
        w(b1, "A%d" % j, src); w(b1, "Y%d" % j, dst)
    for j, (src, dst) in enumerate([("BE2", "BBE2"), ("BE3", "BBE3"), ("-MRD", "MRDN"), ("-MWR", "MWRN"),
                                    ("-IACK", "IACKN"), ("CLK", "BCLK"), ("-RES", "BRESN")]):
        w(b2, "A%d" % j, src); w(b2, "Y%d" % j, dst)
    gnd_unused(b2, ["A7"], ["Y7"])
    for lo, out in ((16, "HI1N"), (24, "HI2N")):
        x = U("74HCT688", "A%d-A%d all ones" % (lo, lo + 7))
        w(x, "-G", "GND"); w(x, "-P=Q", out)
        for i in range(8):
            w(x, "P%d" % i, "A%d" % (lo + i)); w(x, "Q%d" % i, "VCC")
    gal("DKSEL", "window decode", {})
    gal("DKBE", "access size, lane", {})
    gal("DKDEC", "register selects", {})
    gal("DKSEQ", "wait states, strobes", {})
    gal("DKSTB", "-ACK, byte lanes", {})
    od = U("74HC05", "open drain: -ACK, -WAIT")
    w(od, "1A", "ACK"); w(od, "1Y", "-ACK"); w(od, "2A", "WAIT"); w(od, "2Y", "-WAIT")
    gnd_unused(od, ["%dA" % g for g in range(3, 7)], ["%dY" % g for g in range(3, 7)])

    # ---- the lanes onto the card bus CD0-15
    for fn, lanes_, cd, en in (("lane 0 <-> CD0-7", 0, 0, "LE0N"), ("lane 1 <-> CD8-15 (the data register's high byte)", 1, 8, "LE1WN"),
                               ("lane 1 <-> CD0-7 (a byte)", 1, 0, "LE1BN"), ("lane 2 <-> CD0-7", 2, 0, "LE2N"),
                               ("lane 3 <-> CD0-7", 3, 0, "LE3N")):
        x = U("74HCT245", fn)
        w(x, "DIR", "LDIR"); w(x, "-OE", en)
        for i in range(8):
            w(x, "A%d" % (i + 1), "D%d" % (8 * lanes_ + i)); w(x, "B%d" % (i + 1), "CD%d" % (cd + i))
    for k in range(3):
        x = U("74HCT541", "zeros on D%d-D%d (the status word)" % (8 + 8 * k, 15 + 8 * k))
        w(x, "-OE1", "ZOEN"); w(x, "-OE2", "GND")
        for i in range(8):
            w(x, "A%d" % i, "GND"); w(x, "Y%d" % i, "D%d" % (8 + 8 * k + i))
    for k in range(2):
        x = U("74HCT574", "read latch CD%d-CD%d (taken as -DIOR ends)" % (8 * k, 8 * k + 7), "rlatch%d" % k)
        w(x, "CLK", "DIORN"); w(x, "-OE", "RLOEN")
        for i in range(8):
            w(x, "D%d" % i, "CD%d" % (8 * k + i)); w(x, "Q%d" % i, "CD%d" % (8 * k + i))

    # ---- the ports
    gal("DKPORT0", "ports 0 and 1: selects, strobes, transceivers", {})
    gal("DKPORT1", "ports 2 and 3: selects, strobes, transceivers", {})
    mux = U("74HCT153", "IORDY of the port addressed")
    w(mux, "S0", "BA4"); w(mux, "S1", "BA5"); w(mux, "-1E", "GND"); w(mux, "-2E", "VCC")
    for p in range(NPORT):
        w(mux, "1I%d" % p, "IORDY%d" % p)
        w(mux, "2I%d" % p, "GND")
    w(mux, "1Y", "IORDYS"); c.unused(mux, "2Y")
    names = ("CF 0 (the boot card)", "CF 1", "IDE channel A", "IDE channel B")
    rn = [0]
    def RN(fn, rail, nets):
        rn[0] += 1
        ref = "RN%d" % rn[0]
        c.part(ref, "RNET9", fn, value="10k x 8 bussed")
        w(ref, "COM", rail)
        for j, net in enumerate(nets):
            w(ref, "R%d" % (j + 1), net)
        c.unused(ref, *["R%d" % k for k in range(len(nets) + 1, 9)])
    rr = [0]
    def R(value, a, b_, fn):
        rr[0] += 1
        ref = "R%d" % rr[0]
        c.part(ref, "R", fn, value=value)
        w(ref, "1", a); w(ref, "2", b_)
    for p in range(NPORT):
        hdr = "J%d" % (4 + p)
        if p < 2:
            c.part(hdr, "HDR2X20_RA", "port %d, %s: a right-angle 40-pin header at the front edge; a CF-to-IDE "
                   "adapter plugs on, its slot facing out" % (p, names[p]))
        else:
            c.part(hdr, "IDC40", "port %d, %s: a 40-pin IDE cable (master and slave)" % (p, names[p]))
        role["port%d" % p] = hdr
        pn = lambda s, p=p: "P%d_%s" % (p, s.lstrip("-"))
        for k in range(2):
            x = U("74HCT245", "port %d: CD%d-%d <-> DD%d-%d" % (p, 8 * k, 8 * k + 7, 8 * k, 8 * k + 7))
            w(x, "DIR", "LDIR"); w(x, "-OE", "DOEN%d" % p)
            for i in range(8):
                w(x, "A%d" % (i + 1), "CD%d" % (8 * k + i)); w(x, "B%d" % (i + 1), pn("DD%d" % (8 * k + i)))
        x = U("74HCT541", "port %d: DA2-0, -CS0, -CS1, -DIOR, -DIOW, -RESET" % p)
        w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
        for j, (src, dst) in enumerate((("LN0", "DA0"), ("LN1", "DA1"), ("BA2", "DA2"), ("CS0N%d" % p, "-CS0"),
                                        ("CS1N%d" % p, "-CS1"), ("DIORN%d" % p, "-DIOR"), ("DIOWN%d" % p, "-DIOW"),
                                        ("BRESN", "-RESET"))):
            w(x, "A%d" % j, src); w(x, "Y%d" % j, pn(dst))
        for sig, pin in ATA.items():
            if sig in ("-IOCS16", "-PDIAG"):
                c.unused(hdr, str(pin))
            elif sig == "CSEL":
                w(hdr, str(pin), "GND")             # (cable select: the master unless the drive is jumpered)
            elif sig == "IORDY":
                w(hdr, str(pin), "IORDY%d" % p)
            elif sig == "INTRQ":
                w(hdr, str(pin), "INTRQ%d" % p)
            else:
                w(hdr, str(pin), pn(sig))
        for pin in ATA_GND:
            w(hdr, str(pin), "GND")
        c.unused(hdr, "20")                         # (the key)
        RN("port %d: DD0-7 pull-downs (an empty port reads 00; DD7 as ATA asks)" % p, "GND", [pn("DD%d" % i) for i in range(8)])
        RN("port %d: DD8-15 pull-downs" % p, "GND", [pn("DD%d" % i) for i in range(8, 16)])
        R("1k", "IORDY%d" % p, "VCC", "port %d: IORDY pull-up (ATA: 1k)" % p)
        R("10k", "INTRQ%d" % p, "GND", "port %d: INTRQ pull-down (no drive: no interrupt)" % p)
        R("5.6k", pn("DMARQ"), "GND", "port %d: DMARQ pull-down (no DMA yet)" % p)
        R("10k", pn("DMACK"), "VCC", "port %d: -DMACK held off" % p)
        c.part("D%d" % (1 + p), "LED", "port %d activity (the drive's -DASP), front edge, labelled DISK %d" % (p, p))
        R("470", "LEDA%d" % p, "VCC", "port %d activity LED" % p)
        w("D%d" % (1 + p), "A", "LEDA%d" % p); w("D%d" % (1 + p), "K", pn("DASP"))

    # ---- interrupts
    gal("DKIRQ", "interrupts, the vector's port", {})
    gal("DKVEC", "ID, status, vector", {})
    lv = U("74LS145", "the card's interrupt level (jumpers L2-0): its -IRQ line, open collector")
    w(lv, "A", "L0"); w(lv, "B", "L1"); w(lv, "C", "L2"); w(lv, "D", "ANYN")
    for k in range(8):
        w(lv, "O%d" % k, "-IRQ%d" % k)
    c.unused(lv, "O8", "O9")
    cmp_ = U("74HCT85", "the level acknowledged (A4:A2) against the jumpers")
    for i in range(3):
        w(cmp_, "A%d" % i, "L%d" % i); w(cmp_, "B%d" % i, "BA%d" % (2 + i))
    w(cmp_, "A3", "GND"); w(cmp_, "B3", "GND")
    w(cmp_, "IA>B", "GND"); w(cmp_, "IA=B", "VCC"); w(cmp_, "IA<B", "GND")
    w(cmp_, "QA=B", "EQ"); c.unused(cmp_, "QA>B", "QA<B")

    # ---- the jumpers (at the card's edge, labelled): fitted = 0, open = 1
    jumpers = [("WIN%d" % i, "window bit %d" % i) for i in range(3)] + [("L%d" % i, "interrupt level bit %d" % i) for i in range(3)]
    for i, (net, fn) in enumerate(jumpers, 1):
        c.part("JP%d" % i, "JUMPER2", "jumper %s: %s" % (net, fn))
        w("JP%d" % i, "1", net); w("JP%d" % i, "2", "GND")
    RN("jumper pull-ups", "VCC", [net for net, fn in jumpers])
    c.finish()
    CARDS[c.name] = c
    return c, G, role

def defaults():
    """The jumpers as the documents and the kernel assume them (D52): window 6, level 1."""
    j = {"WIN%d" % i: WINDOW >> i & 1 for i in range(3)}
    j.update({"L%d" % i: LEVEL >> i & 1 for i in range(3)})
    return j

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("disk card: %d errors" % len(errs))
    print("disk card OK: %d ICs (%d GALs), %d nets, %d parts" % (len(ics), len(G), len(c.nets), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
