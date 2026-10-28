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
    latch onto CD in state 2; STRB (state 1) for the port transceivers.
    PACC: the CPU's access (PSEL) or the DMA engine's (EACC, D75), which
    goes first: a CPU access that comes during one waits (-WAIT) and runs
    after it; PWR: that access's direction."""
    b = GB("DKSEQ", "disk card: wait states, strobes", clock="BCLK", reset="BRESN")
    b.inp("PACC", "PSEL", "EACC", "CRD", "PWR", "IORDYS", "BRESN")
    s = lambda v: num(v, ST)
    def nxt(v):
        if not v["PACC"]:
            return 0
        return {0: 1, 1: 1 if not v["IORDYS"] else 2, 2: 0, 3: 0}[s(v)]
    b.reg("S0", ["PACC", "IORDYS"] + ST, lambda v: nxt(v) & 1)
    b.reg("S1", ["PACC", "IORDYS"] + ST, lambda v: nxt(v) >> 1)
    b.com("WAIT", ["PSEL", "EACC"] + ST, lambda v: v["PSEL"] and (v["EACC"] or s(v) in (0, 1)))
    b.com("DIORN", ["PACC", "PWR"] + ST, lambda v: not (v["PACC"] and not v["PWR"] and s(v) == 1))
    b.com("DIOWN", ["PACC", "PWR"] + ST, lambda v: not (v["PACC"] and v["PWR"] and s(v) == 1))
    b.com("STRB", ST, lambda v: s(v) == 1)
    b.com("RLOEN", ["PSEL", "EACC", "CRD"] + ST, lambda v: not (v["PSEL"] and v["CRD"] and not v["EACC"] and s(v) == 2))
    return b.build()

def gal_dkports(g):
    """Ports 2g and 2g+1: each one's -CS0 / -CS1 (PA3: the control block),
    strobes, and its data transceivers' enable (a write: the whole access,
    the data held through the recovery; a read: the strobe only, so the
    read latch has CD after it). The access and its port (PACC, PS1:PS0)
    are the CPU's or the DMA engine's (GAL DKPSEL, D75). DMAM (multiword
    DMA, which negates the chip selects) is wired for a later program."""
    ports = (2 * g, 2 * g + 1)
    D = ["PACC", "PA3", "PS0", "PS1", "DIORN", "DIOWN", "PWR", "STRB", "DMAM"]
    b = GB("DKPORT%d" % g, "disk card: ports %d and %d" % ports).inp(*D)
    me = lambda v, p: v["PACC"] and num(v, ["PS0", "PS1"]) == p
    for p in ports:
        b.com("CS0N%d" % p, ["PACC", "PA3", "PS0", "PS1"], lambda v, p=p: not (me(v, p) and not v["PA3"]))
        b.com("CS1N%d" % p, ["PACC", "PA3", "PS0", "PS1"], lambda v, p=p: not (me(v, p) and v["PA3"]))
        b.com("DIORN%d" % p, ["PACC", "PS0", "PS1", "DIORN"], lambda v, p=p: not (me(v, p) and not v["DIORN"]))
        b.com("DIOWN%d" % p, ["PACC", "PS0", "PS1", "DIOWN"], lambda v, p=p: not (me(v, p) and not v["DIOWN"]))
        b.com("DOEN%d" % p, ["PACC", "PS0", "PS1", "PWR", "STRB"],
              lambda v, p=p: not (me(v, p) and (v["PWR"] or v["STRB"])))
    return b.build()

def gal_dkstb():
    """-ACK (the DMA registers' too: DMAK); the lane transceivers' enables
    and direction (the data register on lanes 0-1 onto CD0-15, a byte on
    its lane onto CD0-7, the status word's low byte on lane 0; the vector
    on lane 0), none while the DMA engine has CD (EACC: the access waits)."""
    D = ["PSEL", "WIDE", "SIDR", "SISR", "BYTE", "CRD", "CWR", "INTA", "VHIT", "EACC"] + LNS
    b = GB("DKSTB", "disk card: -ACK, byte lanes").inp(*D).inp("DMAK")
    byte = lambda v: (v["PSEL"] and not v["WIDE"]) or v["SIDR"]
    ln = lambda v: num(v, LNS)
    en = lambda v: not v["EACC"]
    b.com("ACK", ["PSEL", "SIDR", "SISR", "INTA", "VHIT", "DMAK"],
          lambda v: v["PSEL"] or v["SIDR"] or v["SISR"] or (v["INTA"] and v["VHIT"]) or v["DMAK"])
    b.com("LE0N", D, lambda v: not (en(v) and ((byte(v) and ln(v) == 0) or (v["PSEL"] and v["WIDE"]) or v["SISR"]
                                               or (v["INTA"] and v["VHIT"]))))
    b.com("LE1WN", ["PSEL", "WIDE", "EACC"], lambda v: not (en(v) and v["PSEL"] and v["WIDE"]))
    b.com("LE1BN", D, lambda v: not (en(v) and byte(v) and ln(v) == 1))
    b.com("LE2N", D, lambda v: not (en(v) and byte(v) and ln(v) == 2))
    b.com("LE3N", D, lambda v: not (en(v) and byte(v) and ln(v) == 3))
    b.com("LDIR", ["CWR"], lambda v: v["CWR"])
    return b.build()

IRQS = ["INTRQ%d" % p for p in range(NPORT)] + ["DMAIRQ"]       # (the DMA engine: source 4, D75)
def low_irq(v):
    return next((k for k, n in enumerate(IRQS) if v[n]), None)

def gal_dkirq():
    """The interrupts, the ports' and the DMA engine's: ANYN (to the level's
    74LS145), VHIT (asking, and the level acknowledged is the card's: EQ
    from a 74HCT85), IDOE (DKVEC drives the byte bus; not while the DMA
    engine has it: the access waits)."""
    b = GB("DKIRQ", "disk card: interrupts").inp(*IRQS).inp("EQ", "INTA", "SIDR", "SISR", "CRD", "EACC")
    b.com("ANYN", IRQS, lambda v: low_irq(v) is None)
    b.com("VHIT", IRQS + ["EQ", "INTA"], lambda v: bool(v["INTA"] and v["EQ"]) and low_irq(v) is not None)
    b.com("IDOE", IRQS + ["EQ", "INTA", "SIDR", "SISR", "CRD", "EACC"],
          lambda v: not v["EACC"] and ((v["INTA"] and v["EQ"] and low_irq(v) is not None)
                                       or ((v["SIDR"] or v["SISR"]) and v["CRD"])))
    return b.build()

def gal_dkvec():
    """Onto CD0-7: the card ID (FFF0: 2), the revision (FFF1: 1, the DMA
    engine, D75), the status word's low byte (bit p: port p's INTRQ; bit 4:
    the DMA engine's), the vector (window x 16 + the lowest asking source:
    port 0-3, or 4 the DMA engine)."""
    b = GB("DKVEC", "disk card: ID, status, vector").inp("IDOE", "LN0", "INTA", "SISR").inp(*WINJ).inp(*IRQS)
    def val(v):
        if v["INTA"]:
            return num(v, WINJ) << 4 | (low_irq(v) or 0)
        if v["SISR"]:
            return num(v, IRQS)
        return CARD_REV if v["LN0"] else 2
    deps = ["LN0", "INTA", "SISR"] + WINJ + IRQS
    for i in range(8):
        b.tri("CD%d" % i, deps, lambda v, i=i: val(v) >> i & 1, "IDOE")
    return b.build()

# ---------------------------------------------------------------- the DMA engine (D75)
DADR, DCNT, DCTL = 0x40, 0x44, 0x48     # DMAADR, DMACNT, DMACTL: words
CARD_REV = 1
EP = ["PH0", "PH1"]                     # the word's step: drive -> memory (DIR 0): 0, 1 the halfwords
                                        # in, 2 the bus write; memory -> drive (DIR 1): 0 the bus read,
                                        # 1, 2 the halfwords out
def eph(v):
    return num(v, EP)
def portph(v):
    return eph(v) in ((1, 2) if v["DIR"] else (0, 1))
def busph(v):
    return eph(v) == (0 if v["DIR"] else 2)

def gal_dkdma():
    """The DMA registers' selects: FF40 DMAADR (the physical word address:
    loaded from D31:2, read back), FF44 DMACNT (the words left: loaded, read
    back), FF48 DMACTL (GAL DKDCTL; read back through a 74HCT541); DMAK
    their -ACK; the zeros above a narrower register (ZO1N: D8-15 for the
    status word FFF4 and DMACTL; ZO2N: D16-31 for those and DMACNT)."""
    b = GB("DKDMA", "disk card: DMA register selects").inp(*BA).inp("WORD", "CRD", "CWR", "SISR")
    at = lambda v, o: v["WORD"] and num(v, BA) << 2 == o
    D = BA + ["WORD", "CRD", "CWR"]
    b.com("LDAN", D, lambda v: not (at(v, DADR) and v["CWR"]))
    b.com("SDARN", D, lambda v: not (at(v, DADR) and v["CRD"]))
    b.com("LDCN", D, lambda v: not (at(v, DCNT) and v["CWR"]))
    b.com("SDCRN", D, lambda v: not (at(v, DCNT) and v["CRD"]))
    b.com("SDSW", D, lambda v: at(v, DCTL) and v["CWR"])
    b.com("SDSRN", D, lambda v: not (at(v, DCTL) and v["CRD"]))
    b.com("DMAK", D, lambda v: any(at(v, o) for o in (DADR, DCNT, DCTL)) and (v["CRD"] or v["CWR"]))
    b.com("ZO1N", D + ["SISR"], lambda v: not (v["CRD"] and (v["SISR"] or at(v, DCTL))))
    b.com("ZO2N", D + ["SISR"], lambda v: not (v["CRD"] and (v["SISR"] or at(v, DCTL) or at(v, DCNT))))
    return b.build()

def gal_dkdctl():
    """DMACTL, clocked by CLK, written at the cycle's end (SDSW): D0 RUN (1
    starts; 0 stops after the word in hand), D1 DIR (0 the drive to memory,
    1 memory to the drive), D3:2 the port, D4 IE, D5 MODE (multiword DMA:
    reserved, wired for a later program; 0). A write clears DONE and ERR.
    RUN ends with the count (ALLONES: DONE) or a bus cycle that no card
    answered (BERR: ERR). BUSY: running with words left; DMAIRQ: IE and
    DONE or ERR. Read back: BUSY, DIR, the port, IE, MODE, DONE, ERR."""
    b = GB("DKDCTL", "disk card: DMA control", clock="BCLK", reset="BRESN")
    b.inp("SDSW", "D0", "D1", "D2", "D3", "D4", "D5", "ALLONES", "BERR", "BRESN")
    b.reg("RUN", ["SDSW", "D0", "RUN", "ALLONES", "BERR"],
          lambda v: v["D0"] if v["SDSW"] else (v["RUN"] and not v["ALLONES"] and not v["BERR"]))
    for name, d in (("DIR", "D1"), ("DP0", "D2"), ("DP1", "D3"), ("IE", "D4"), ("MODE", "D5")):
        b.reg(name, ["SDSW", d, name], lambda v, d=d, name=name: v[d] if v["SDSW"] else v[name])
    b.reg("DONE", ["SDSW", "RUN", "ALLONES", "DONE"],
          lambda v: not v["SDSW"] and (v["DONE"] or (v["RUN"] and v["ALLONES"])))
    b.reg("ERR", ["SDSW", "BERR", "ERR"], lambda v: not v["SDSW"] and (v["ERR"] or v["BERR"]))
    b.com("BUSY", ["RUN", "ALLONES"], lambda v: v["RUN"] and not v["ALLONES"])
    b.com("DMAIRQ", ["IE", "DONE", "ERR"], lambda v: v["IE"] and (v["DONE"] or v["ERR"]))
    return b.build()

def gal_dkeng():
    """The DMA engine's sequence, clocked by CLK. A word: two port accesses
    of the selected port's data register (EACC: DKSEQ runs them, as the
    CPU's; each starts only with DKSEQ idle and no CPU access to the card's
    byte bus asked, CDREQ) and one bus cycle as master (REQ: -BREQ; granted,
    CYC: one clock, held while -WAIT). A word starts only while BUSY; one
    started is finished. INC: the word done (counters advance); BERR: the
    bus cycle had no -ACK (the word is not counted). DRQS and MODE (the
    port's DMARQ, multiword DMA) are wired for a later program."""
    b = GB("DKENG", "disk card: DMA sequence", clock="BCLK", reset="BRESN")
    b.inp("BUSY", "DIR", "S0", "S1", "CDREQ", "BGNTN", "WAITN", "ACKN", "DRQS", "MODE", "BRESN")
    s = lambda v: num(v, ST)
    go = lambda v: v["BUSY"] or eph(v) != 0
    endport = lambda v: v["EACC"] and s(v) == 2
    endbus = lambda v: v["CYC"] and v["WAITN"]
    def nph(v):
        p = eph(v)
        if endport(v):
            return (p + 1) % 3
        if endbus(v):
            return 1 if v["DIR"] and not v["ACKN"] else 0
        return p
    PD = ["DIR", "EACC", "CYC", "WAITN", "ACKN", "S0", "S1"] + EP
    b.reg("PH0", PD, lambda v: nph(v) & 1)
    b.reg("PH1", PD, lambda v: nph(v) >> 1)
    b.reg("EACC", ["EACC", "DIR", "BUSY", "S0", "S1", "CDREQ"] + EP,
          lambda v: (v["EACC"] and s(v) != 2) or
                    (not v["EACC"] and portph(v) and go(v) and s(v) == 0 and not v["CDREQ"]))
    b.reg("REQ", ["DIR", "BUSY", "CYC", "REQ", "BGNTN"] + EP,
          lambda v: busph(v) and go(v) and not v["CYC"] and not (v["REQ"] and not v["BGNTN"]))
    b.reg("CYC", ["REQ", "BGNTN", "CYC", "WAITN"], lambda v: (v["REQ"] and not v["BGNTN"]) or (v["CYC"] and not v["WAITN"]))
    b.com("BREQN", ["REQ", "CYC"], lambda v: not (v["REQ"] or v["CYC"]))
    b.com("INC", ["DIR", "CYC", "WAITN", "ACKN", "EACC", "S0", "S1"] + EP,
          lambda v: (not v["DIR"] and endbus(v) and not v["ACKN"]) or (v["DIR"] and endport(v) and eph(v) == 2))
    b.com("BERR", ["CYC", "WAITN", "ACKN"], lambda v: endbus(v) and v["ACKN"])
    return b.build()

def gal_dkeout():
    """The DMA engine's enables and latch clocks: MSTN (its address, byte
    enables and -MRD / -MWR onto the bus), ERDN / EWRN (its read or write;
    EWRN also puts the word in onto D), CKLO / CKHI (the word in takes CD
    as -DIOR ends, the first halfword low), CKRD (the word out takes D at
    the read's end: CLK gated), OLON / OHIN (the word out's halves onto
    CD), CDREQ (the CPU asks for the card's byte bus) and EWAIT (it waits
    while the engine has it)."""
    D = EP + ["EACC", "CYC", "DIR"]
    b = GB("DKEOUT", "disk card: DMA enables, latch clocks").inp(*D).inp("DIORN", "BCLK", "PSEL", "SIDR", "SISR", "INTA")
    b.com("MSTN", ["CYC"], lambda v: not v["CYC"])
    b.com("ERDN", ["CYC", "DIR"], lambda v: not (v["CYC"] and v["DIR"]))
    b.com("EWRN", ["CYC", "DIR"], lambda v: not (v["CYC"] and not v["DIR"]))
    b.com("CKLO", D + ["DIORN"], lambda v: v["DIORN"] or not (v["EACC"] and not v["DIR"] and eph(v) == 0))
    b.com("CKHI", D + ["DIORN"], lambda v: v["DIORN"] or not (v["EACC"] and not v["DIR"] and eph(v) == 1))
    b.com("CKRD", ["CYC", "DIR", "BCLK"], lambda v: v["BCLK"] or not (v["CYC"] and v["DIR"]))
    b.com("OLON", D, lambda v: not (v["EACC"] and v["DIR"] and eph(v) == 1))
    b.com("OHIN", D, lambda v: not (v["EACC"] and v["DIR"] and eph(v) == 2))
    CQ = ["PSEL", "SIDR", "SISR", "INTA"]
    b.com("CDREQ", CQ, lambda v: any(v[n] for n in CQ))
    b.com("EWAIT", ["EACC"] + CQ, lambda v: v["EACC"] and any(v[n] for n in CQ))
    return b.build()

def gal_dkpsel():
    """The port access's address: the CPU's (PSEL: BA5:4 the port, BA3 the
    control block, BA2 / LN1:0 DA2:0, CWR the direction) or, while the DMA
    engine's runs (EACC), its port's data register (DMACTL's port, DIR).
    DMAM: multiword DMA (MODE), wired for a later program."""
    b = GB("DKPSEL", "disk card: the port access's address").inp("PSEL", "EACC", "BA2", "BA3", "BA4", "BA5")
    b.inp("LN0", "LN1", "DP0", "DP1", "CWR", "DIR", "MODE")
    b.com("PACC", ["PSEL", "EACC"], lambda v: v["PSEL"] or v["EACC"])
    for out, src in (("PA2", "BA2"), ("PA3", "BA3"), ("PL0", "LN0"), ("PL1", "LN1")):
        b.com(out, ["EACC", src], lambda v, src=src: v[src] and not v["EACC"])
    for out, cpu, eng in (("PS0", "BA4", "DP0"), ("PS1", "BA5", "DP1"), ("PWR", "CWR", "DIR")):
        b.com(out, ["EACC", cpu, eng], lambda v, cpu=cpu, eng=eng: v[eng] if v["EACC"] else v[cpu])
    b.com("DMAM", ["EACC", "MODE"], lambda v: v["EACC"] and v["MODE"])
    return b.build()

def gal_dkmwd():
    """Multiword DMA, wired for a later program (D75): each port's DMARQ in,
    -DMACK out (through a 74HCT541), the selected port's DMARQ (DRQS) to
    the sequencer. As programmed now (PIO only): -DMACK never asserted."""
    DRQ = ["DRQ%d" % p for p in range(NPORT)]
    b = GB("DKMWD", "disk card: multiword DMA (reserved)").inp(*DRQ).inp("DP0", "DP1", "MODE", "EACC", "BUSY", "STRB")
    for p in range(NPORT):
        b.com("DACKN%d" % p, ["MODE"], lambda v: True)
    b.com("DRQS", DRQ + ["DP0", "DP1"], lambda v: v["DRQ%d" % num(v, ["DP0", "DP1"])])
    return b.build()

def gals():
    G = {}
    for f in (gal_dksel, gal_dkbe, gal_dkdec, gal_dkseq, gal_dkstb, gal_dkirq, gal_dkvec,
              gal_dkdma, gal_dkdctl, gal_dkeng, gal_dkeout, gal_dkpsel, gal_dkmwd):
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
    od = U("74HC05", "open drain: -ACK, -WAIT (the ports', the DMA engine's)")
    w(od, "1A", "ACK"); w(od, "1Y", "-ACK"); w(od, "2A", "WAIT"); w(od, "2Y", "-WAIT")
    w(od, "3A", "EWAIT"); w(od, "3Y", "-WAIT")
    gnd_unused(od, ["%dA" % g for g in range(4, 7)], ["%dY" % g for g in range(4, 7)])

    # ---- the lanes onto the card bus CD0-15
    for fn, lanes_, cd, en in (("lane 0 <-> CD0-7", 0, 0, "LE0N"), ("lane 1 <-> CD8-15 (the data register's high byte)", 1, 8, "LE1WN"),
                               ("lane 1 <-> CD0-7 (a byte)", 1, 0, "LE1BN"), ("lane 2 <-> CD0-7", 2, 0, "LE2N"),
                               ("lane 3 <-> CD0-7", 3, 0, "LE3N")):
        x = U("74HCT245", fn)
        w(x, "DIR", "LDIR"); w(x, "-OE", en)
        for i in range(8):
            w(x, "A%d" % (i + 1), "D%d" % (8 * lanes_ + i)); w(x, "B%d" % (i + 1), "CD%d" % (cd + i))
    for k in range(3):
        x = U("74HCT541", "zeros on D%d-D%d (the status word, DMACTL%s)" % (8 + 8 * k, 15 + 8 * k, ", DMACNT" if k else ""))
        w(x, "-OE1", "ZO1N" if k == 0 else "ZO2N"); w(x, "-OE2", "GND")
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
    mux = U("74HCT153", "IORDY of the port addressed (the CPU's or the DMA engine's)")
    w(mux, "S0", "PS0"); w(mux, "S1", "PS1"); w(mux, "-1E", "GND"); w(mux, "-2E", "VCC")
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
            w(x, "DIR", "PWR"); w(x, "-OE", "DOEN%d" % p)
            for i in range(8):
                w(x, "A%d" % (i + 1), "CD%d" % (8 * k + i)); w(x, "B%d" % (i + 1), pn("DD%d" % (8 * k + i)))
        x = U("74HCT541", "port %d: DA2-0, -CS0, -CS1, -DIOR, -DIOW, -RESET" % p)
        w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
        for j, (src, dst) in enumerate((("PL0", "DA0"), ("PL1", "DA1"), ("PA2", "DA2"), ("CS0N%d" % p, "-CS0"),
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
        R("5.6k", pn("DMARQ"), "GND", "port %d: DMARQ pull-down (ATA: the host's)" % p)
        c.part("D%d" % (1 + p), "LED", "port %d activity (the drive's -DASP), front edge, labelled DISK %d" % (p, p))
        R("470", "LEDA%d" % p, "VCC", "port %d activity LED" % p)
        w("D%d" % (1 + p), "A", "LEDA%d" % p); w("D%d" % (1 + p), "K", pn("DASP"))

    # ---- interrupts
    gal("DKIRQ", "interrupts", {})
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

    # ---- the DMA engine (D75)
    gal("DKDMA", "DMA register selects", {})
    gal("DKDCTL", "DMA control (DMACTL)", {})
    gal("DKENG", "DMA sequence", {"BREQN": "-BREQ", "BGNTN": "-BGNT", "WAITN": "-WAIT", "ACKN": "-ACK"})
    gal("DKEOUT", "DMA enables, latch clocks", {})
    gal("DKPSEL", "the port access's address (the CPU's or the DMA engine's)", {})
    gal("DKMWD", "multiword DMA (wired, reserved)", {"DRQ%d" % p: "P%d_DMARQ" % p for p in range(NPORT)})
    EA = lambda i: "EA%d" % i
    # the address: 30 bits counting words (74F: the carry chain settles within a clock)
    for k in range(8):
        x = U("74F163", "DMA address A%d-A%d" % (2 + 4 * k, min(5 + 4 * k, 31)), "dadr%d" % k)
        w(x, "CLK", "BCLK"); w(x, "-CLR", "VCC"); w(x, "ENP", "INC"); w(x, "-LOAD", "LDAN")
        w(x, "ENT", "VCC" if k == 0 else "ARCO%d" % (k - 1))
        if k < 7:
            w(x, "RCO", "ARCO%d" % k)
        else:
            c.unused(x, "RCO")
        for i, (d, q) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
            b_ = 2 + 4 * k + i
            if b_ < 32:
                w(x, d, "D%d" % b_); w(x, q, EA(b_))
            else:
                w(x, d, "GND"); c.unused(x, q)
    for k in range(4):                                  # onto the bus as master
        x = U("74HCT541", "DMA address onto A%d-A%d" % (2 + 8 * k, min(9 + 8 * k, 31)))
        w(x, "-OE1", "MSTN"); w(x, "-OE2", "GND")
        for i in range(8):
            b_ = 2 + 8 * k + i
            if b_ < 32:
                w(x, "A%d" % i, EA(b_)); w(x, "Y%d" % i, "A%d" % b_)
            else:
                w(x, "A%d" % i, "GND"); c.unused(x, "Y%d" % i)
    x = U("74HCT541", "DMA cycle onto BE0-3 (a word), -MRD, -MWR")
    w(x, "-OE1", "MSTN"); w(x, "-OE2", "GND")
    for i, (src, dst) in enumerate((("VCC", "BE0"), ("VCC", "BE1"), ("VCC", "BE2"), ("VCC", "BE3"),
                                    ("ERDN", "-MRD"), ("EWRN", "-MWR"))):
        w(x, "A%d" % i, src); w(x, "Y%d" % i, dst)
    gnd_unused(x, ["A6", "A7"], ["Y6", "Y7"])
    for k in range(4):                                  # DMAADR read back
        x = U("74HCT541", "DMAADR read back onto D%d-D%d" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "SDARN"); w(x, "-OE2", "GND")
        for i in range(8):
            b_ = 8 * k + i
            w(x, "A%d" % i, EA(b_) if b_ >= 2 else "GND"); w(x, "Y%d" % i, "D%d" % b_)
    # the count: 16 bits, its complement counting up (all ones: done)
    for k in range(2):
        x = U("74HCT540", "DMACNT load: D%d-D%d inverted" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
        for i in range(8):
            w(x, "A%d" % i, "D%d" % (8 * k + i)); w(x, "Y%d" % i, "WL%d" % (8 * k + i))
    for k in range(4):
        x = U("74F163", "DMA count (complement) bits %d-%d" % (4 * k, 4 * k + 3), "dcnt%d" % k)
        w(x, "CLK", "BCLK"); w(x, "-CLR", "VCC"); w(x, "ENP", "INC"); w(x, "-LOAD", "LDCN")
        w(x, "ENT", "VCC" if k == 0 else "WRCO%d" % (k - 1))
        w(x, "RCO", "ALLONES" if k == 3 else "WRCO%d" % k)
        for i, (d, q) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
            w(x, d, "WL%d" % (4 * k + i)); w(x, q, "WC%d" % (4 * k + i))
    for k in range(2):
        x = U("74HCT540", "DMACNT read back onto D%d-D%d (inverted: the words left)" % (8 * k, 8 * k + 7))
        w(x, "-OE1", "SDCRN"); w(x, "-OE2", "GND")
        for i in range(8):
            w(x, "A%d" % i, "WC%d" % (8 * k + i)); w(x, "Y%d" % i, "D%d" % (8 * k + i))
    x = U("74HCT541", "DMACTL read back onto D0-7")
    w(x, "-OE1", "SDSRN"); w(x, "-OE2", "GND")
    for i, src in enumerate(("BUSY", "DIR", "DP0", "DP1", "IE", "MODE", "DONE", "ERR")):
        w(x, "A%d" % i, src); w(x, "Y%d" % i, "D%d" % i)
    # the word buffers: in (drive -> memory) and out (memory -> drive)
    for half, ck in ((0, "CKLO"), (1, "CKHI")):
        for k in range(2):
            x = U("74HCT574", "DMA word in: CD%d-CD%d -> D%d-D%d" % (8 * k, 8 * k + 7, 16 * half + 8 * k, 16 * half + 8 * k + 7),
                  "win%d" % (2 * half + k))
            w(x, "CLK", ck); w(x, "-OE", "EWRN")
            for i in range(8):
                w(x, "D%d" % i, "CD%d" % (8 * k + i)); w(x, "Q%d" % i, "D%d" % (16 * half + 8 * k + i))
    for j in range(4):
        x = U("74HCT574", "DMA word out: D%d-D%d -> CD%d-CD%d" % (8 * j, 8 * j + 7, 8 * (j % 2), 8 * (j % 2) + 7), "wout%d" % j)
        w(x, "CLK", "CKRD"); w(x, "-OE", "OLON" if j < 2 else "OHIN")
        for i in range(8):
            w(x, "D%d" % i, "D%d" % (8 * j + i)); w(x, "Q%d" % i, "CD%d" % (8 * (j % 2) + i))
    x = U("74HCT541", "the ports' -DMACK (multiword DMA, reserved)")
    w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
    for p in range(NPORT):
        w(x, "A%d" % p, "DACKN%d" % p); w(x, "Y%d" % p, "P%d_DMACK" % p)
    gnd_unused(x, ["A%d" % i for i in range(4, 8)], ["Y%d" % i for i in range(4, 8)])
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
