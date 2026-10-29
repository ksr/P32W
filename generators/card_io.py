#!/usr/bin/env python3
"""The P32W I/O card (any of slots 3-10): docs/io-card.md, D23, D43, D45,
D50, D52, D57, D65.

The devices of emulator/p32io.h's I/O card, in hardware, in the 4 KiB
device window its jumpers choose (window 7, FFFF_F000, by default), the
registers in the window's top 256 bytes:

  FF00-07 UART 0   FF08-0F UART 1   FF30-37 UART 2   FF38-3F UART 3
                   (each on a DB9 at the front edge, through a MAX232A)
  FF10 TCOUNT  FF14 TRELOAD  FF18 TCTRL   (the timer: words)
  FF20 input port   FF21 output port
  FF40 the clock's register number   FF41 the clock's register (DS12C887)
  FF50/51 USB 0 (CH376S)   FF52/53 the network mailbox   FF54/55 USB 1
  FFF0 card ID (1)   FFF1 revision (0)   FFF4 interrupt status (a word)

Byte registers answer byte accesses only, the timer and FFF4 words only;
anything else in the window gets no -ACK (a BUSERROR), as p32io.h.

  * The byte devices sit on a byte bus ID0-7; four 74HCT245s put it on the
    byte lane the byte enables select (a register's address = its word's
    address + the lane). Word registers sit on D0-D31 directly.
  * The timer (P32's design, D11): eight 74HCT163s hold the complement of
    TCOUNT and count up (TCOUNT counting down) on CLK, every third clock (1
    MHz; every fourth at the CPU card's 4 MHz, -FAST low: D74); four 74HCT374s hold ~TRELOAD on the load bus LD; 74HCT540s
    invert every path in and out. Expiry is the 163s' carry out (~count all
    ones: count 0). GAL TIMCTL holds the enable, pending and prescaler,
    exactly as p32uemu -C's io_hwedge().
  * Interrupts (D52, D57): each device's level is three jumpers (fitted =
    0); a 74LS145 per device (open-collector outputs) pulls that level's
    -IRQ line while it asks. In the acknowledge cycle a 74HCT138 decodes the
    level (A4:A2), GALs IVA-IVE match it against each device's jumpers, GAL
    IOPRI picks the lowest asking device, and GAL IOVEC puts the vector
    (window x 16 + its interrupt status bit) on D7:0.
  * The network coprocessor's mailbox (D45, docs/net-mailbox.md), P32's
    design: request and reply latches, GALs NSTB and MBOX, a Pico 2 W and
    a WIZ850io.

Strobes: reads and writes act in the bus clock's second half (CLK low), the
address settled since the first; registers on the card change at CLK's
rising edge (the cycle's end) or, for latches, as the strobe starts.

    python3 card_io.py [--bom]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from card_cpu import GB, num                                  # noqa: E402

# ---------------------------------------------------------------- the register map (p32io.h)
UART = (0x00, 0x08, 0x30, 0x38)
TCNT, TREL, TCTL = 0x10, 0x14, 0x18
PIN, POUT, RTCI, RTCD = 0x20, 0x21, 0x40, 0x41
USB0, NETD, NETS, USB1 = 0x50, 0x52, 0x53, 0x54
CID, CREV, CISR = 0xF0, 0xF1, 0xF4

# the interrupt sources, by their interrupt status bit (p32io.h's card_reg):
# (name, the net that asks, asks when low?, the default level (D52))
DEVICES = [("TIMER", "PEND", False, 0), ("UART0", "UINT0", False, 2), ("UART1", "UINT1", False, 3),
           ("UART2", "UINT2", False, 2), ("UART3", "UINT3", False, 3), ("RTC", "RTCIRQN", True, 5),
           ("USB0", "USBINT0N", True, 6), ("USB1", "USBINT1N", True, 6), ("NET", "NETASK", False, 7)]
LV = lambda d: ["L%d_%d" % (d, i) for i in range(3)]          # device d's level jumpers

BA = ["BA%d" % i for i in range(2, 8)]                         # A7:A2, buffered
BBE = ["BBE%d" % k for k in range(4)]

def onehot(v):
    return sum(1 for b in BBE if v.get(b)) == 1
def allbe(v):
    return all(v.get(b) for b in BBE)
def lane(v):
    return [k for k in range(4) if v.get(BBE[k])][0]
def word_off(v):
    return num(v, BA) << 2
LNS = ["LN0", "LN1"]
DEC = BA + LNS + ["BYTE", "WORD", "CACC"]                      # what the decoders see
def byte_at(v, lo, n=1):
    """A byte access in this card's window to offsets lo .. lo + n - 1
    (BYTE: one byte enable, LN1:LN0 its lane)."""
    return bool(v["CACC"] and v["BYTE"]) and lo <= word_off(v) + num(v, LNS) < lo + n
def word_at(v, o):
    return bool(v["CACC"] and v["WORD"]) and word_off(v) == o

# ---------------------------------------------------------------- GALs
def gal_iosel():
    """This card's window: A31:A16 all ones (two 74HCT688s), A15 = 1, A14:A12
    = the window jumpers, A11:A8 = F (the top 256 bytes); CRD / CWR the
    card's read and write cycles. INTA: an interrupt acknowledge cycle."""
    A = ["A%d" % i for i in range(8, 16)]
    b = GB("IOSEL", "I/O card: window decode").inp("HI1N", "HI2N", *A).inp("WIN0", "WIN1", "WIN2", "MRDN", "MWRN", "IACKN")
    mine = lambda v: (not v["HI1N"] and not v["HI2N"] and num(v, A[:4]) == 15 and v["A15"]
                      and num(v, ["A12", "A13", "A14"]) == num(v, ["WIN0", "WIN1", "WIN2"]))
    deps = ["HI1N", "HI2N"] + A + ["WIN0", "WIN1", "WIN2"]
    b.com("CRD", deps + ["MRDN"], lambda v: mine(v) and not v["MRDN"])
    b.com("CWR", deps + ["MWRN"], lambda v: mine(v) and not v["MWRN"])
    b.com("CACC", deps + ["MRDN", "MWRN"], lambda v: mine(v) and not (v["MRDN"] and v["MWRN"]))
    b.com("INTA", ["IACKN"], lambda v: not v["IACKN"])
    return b.build()

def gal_iobe():
    """The byte enables: BYTE (exactly one), WORD (all four), LN1:LN0 (the
    lane: a byte register's address bits 1:0, its chip's A1:A0)."""
    b = GB("IOBE", "I/O card: access size, lane").inp(*BBE)
    b.com("BYTE", BBE, onehot)
    b.com("WORD", BBE, allbe)
    b.com("LN0", BBE, lambda v: lane(v) & 1 if onehot(v) else None)
    b.com("LN1", BBE, lambda v: lane(v) >> 1 if onehot(v) else None)
    return b.build()

def gal_iodec1():
    """The byte devices' chip selects (active low), and BHIT1: one of them."""
    b = GB("IODEC1", "I/O card: byte device selects").inp(*DEC)
    d = DEC
    sels = [("U0N", UART[0], 8), ("U1N", UART[1], 8), ("U2N", UART[2], 8), ("U3N", UART[3], 8),
            ("USB0N", USB0, 2), ("USB1N", USB1, 2), ("RTCDN", RTCD, 1)]
    for name, lo, n in sels:
        b.com(name, d, lambda v, lo=lo, n=n: not byte_at(v, lo, n))
    b.com("NETDS", d, lambda v: byte_at(v, NETD))
    b.com("NETSS", d, lambda v: byte_at(v, NETS))
    b.com("BHIT1", d, lambda v: any(byte_at(v, lo, n) for _, lo, n in sels) or byte_at(v, NETD, 2))
    return b.build()

def gal_iodec2():
    """The other registers' selects: the clock's register number, the ports,
    the ID and revision (bytes); the timer and the interrupt status (words)."""
    b = GB("IODEC2", "I/O card: register selects").inp(*DEC)
    d = DEC
    b.com("SRTCI", d, lambda v: byte_at(v, RTCI))
    b.com("SPIN", d, lambda v: byte_at(v, PIN))
    b.com("SPOUT", d, lambda v: byte_at(v, POUT))
    b.com("SIDR", d, lambda v: byte_at(v, CID, 2))
    b.com("STCNT", d, lambda v: word_at(v, TCNT))
    b.com("STREL", d, lambda v: word_at(v, TREL))
    b.com("STCTL", d, lambda v: word_at(v, TCTL))
    b.com("SISR", d, lambda v: word_at(v, CISR))
    b.com("BHIT2", d, lambda v: byte_at(v, RTCI) or byte_at(v, PIN, 2) or byte_at(v, CID, 2))
    b.com("WHIT", d, lambda v: any(word_at(v, o) for o in (TCNT, TREL, TCTL, CISR)))
    return b.build()

def gal_iostb():
    """The byte devices' strobes, the byte lanes onto the byte bus, -ACK,
    and the byte devices' wait state (D72): a byte access takes two clocks.
    W1, a register on CLK, marks the second (set at the first clock's end,
    cleared at the second's); -WAIT through the first. -RD runs from the
    first clock's middle (the address settled) to the cycle's end, so the
    data stays until the master takes it; -WR from the first clock's middle
    to the second's, a whole clock, with half a clock of hold after it."""
    b = GB("IOSTB", "I/O card: strobes, byte lanes, -ACK, the wait state", clock="BCLK")
    b.inp("BHIT1", "BHIT2", "WHIT", "CRD", "CWR").inp(*BBE).inp("INTA", "VHIT")
    byte = lambda v: v["BHIT1"] or v["BHIT2"]
    acc = lambda v: byte(v) and (v["CRD"] or v["CWR"])
    b.reg("W1", ["BHIT1", "BHIT2", "CRD", "CWR", "W1"], lambda v: acc(v) and not v["W1"])
    b.com("WAIT", ["BHIT1", "BHIT2", "CRD", "CWR", "W1"], lambda v: acc(v) and not v["W1"])
    b.com("IORDN", ["BHIT1", "BHIT2", "CRD", "BCLK", "W1"],
          lambda v: not (byte(v) and v["CRD"] and (not v["BCLK"] or v["W1"])))
    b.com("IOWRN", ["BHIT1", "BHIT2", "CWR", "BCLK", "W1"],
          lambda v: not (byte(v) and v["CWR"] and (not v["BCLK"]) != bool(v["W1"])))
    for k in range(4):
        b.com("LE%dN" % k, ["BHIT1", "BHIT2", BBE[k], "INTA", "VHIT"],
              lambda v, k=k: not ((byte(v) and v[BBE[k]]) or (k == 0 and v["INTA"] and v["VHIT"])))
    b.com("LDIR", ["CWR"], lambda v: v["CWR"])
    b.com("ACK", ["BHIT1", "BHIT2", "WHIT", "INTA", "VHIT"], lambda v: byte(v) or v["WHIT"] or (v["INTA"] and v["VHIT"]))
    return b.build()

def gal_timdec():
    """The timer's data paths and the status word's (TCTRL, FFF4)."""
    S = ["STCNT", "STREL", "STCTL", "SISR", "CRD", "CWR", "BCLK"]
    b = GB("TIMDEC", "I/O card: timer and status paths").inp(*S)
    b.com("RDCNTN", ["STCNT", "CRD"], lambda v: not (v["STCNT"] and v["CRD"]))      # count -> D (inverted)
    b.com("RDLDN", ["STREL", "CRD"], lambda v: not (v["STREL"] and v["CRD"]))       # LD -> D (inverted): TRELOAD
    b.com("D2LDN", ["STCNT", "STREL", "CWR"], lambda v: not ((v["STCNT"] or v["STREL"]) and v["CWR"]))   # D -> LD
    b.com("ROEN", ["STCNT", "STREL", "CWR"], lambda v: (v["STCNT"] or v["STREL"]) and v["CWR"])       # reload off LD
    # the reload latch takes D at the cycle's end (D72: the decode has the whole cycle)
    b.com("RCLK", ["STREL", "CWR", "BCLK"], lambda v: not (v["STREL"] and v["CWR"] and not v["BCLK"]))
    b.com("WRCNT", ["STCNT", "CWR"], lambda v: v["STCNT"] and v["CWR"])
    b.com("WRCTL", ["STCTL", "CWR"], lambda v: v["STCTL"] and v["CWR"])
    b.com("RDCTL", ["STCTL", "CRD"], lambda v: v["STCTL"] and v["CRD"])
    b.com("STOEN", ["STCTL", "SISR", "CRD"], lambda v: not ((v["STCTL"] or v["SISR"]) and v["CRD"]))
    b.com("ISROEN", ["SISR", "CRD"], lambda v: not (v["SISR"] and v["CRD"]))
    return b.build()

def gal_timctl():
    """The timer's state, clocked by CLK, as p32uemu -C's io_hwedge(): the
    enable (as written), pending (set when the count, enabled, is 0 at an
    edge; a TCTRL write with bit 1 clears it), the prescaler (a tick every
    third clock while enabled; every fourth with -FAST low, the 4 MHz clock:
    1 MHz either way, D74), the counter's load (a TCOUNT write, or the
    reload at expiry). TCTRL reads its two bits on the status bus SB."""
    b = GB("TIMCTL", "I/O card: timer enable, pending, prescaler", clock="BCLK", reset="BRESN")
    b.inp("WRCNT", "WRCTL", "RDCTL", "D0", "D1", "ALLONES", "BRESN", "FASTN")
    PS = ["PS0", "PS1"]
    last = lambda v: 2 if v["FASTN"] else 3                     # the prescaler's last count
    nxt = lambda v: (num(v, PS) + 1) % (last(v) + 1)
    b.reg("EN", ["WRCTL", "D0", "EN"], lambda v: v["D0"] if v["WRCTL"] else v["EN"])
    b.reg("PEND", ["EN", "ALLONES", "PEND", "WRCTL", "D1"],
          lambda v: (v["EN"] and v["ALLONES"]) or (v["PEND"] and not (v["WRCTL"] and v["D1"])))
    b.reg("PS0", ["EN", "FASTN"] + PS, lambda v: v["EN"] and nxt(v) & 1)
    b.reg("PS1", ["EN", "FASTN"] + PS, lambda v: v["EN"] and nxt(v) >> 1)
    b.com("TICK", ["EN", "FASTN"] + PS, lambda v: v["EN"] and num(v, PS) >= last(v))
    b.com("CNTLDN", ["WRCNT", "EN", "ALLONES"], lambda v: not (v["WRCNT"] or (v["EN"] and v["ALLONES"])))
    b.com("MR", ["BRESN"], lambda v: not v["BRESN"])
    b.tri("SB0", ["EN"], lambda v: v["EN"], "RDCTL")
    b.tri("SB1", ["PEND"], lambda v: v["PEND"], "RDCTL")
    return b.build()

def gal_nstb():
    """The mailbox's strobes (FF52 NDATA, FF53 NSTAT / NCTRL), the output
    port's, the clock's AS (FF40 written: a pulse in the second half, the
    address settled -- a pulse from the decode while it settles could fall
    on a wrong address and latch a wrong register number; the chip takes the
    number as AS falls, with CLK's rise, as a UART takes a write)."""
    b = GB("NSTB", "I/O card: mailbox, port and clock strobes").inp("NETDS", "NETSS", "SPIN", "SPOUT", "SRTCI", "CRD", "CWR", "BCLK", "W1")
    # (D72: these are byte devices, two clocks an access; the latches and the
    # clock's AS act in the second (W1), the mailbox's flags see one clock of it)
    b.com("REQCLK", ["NETDS", "CWR", "BCLK", "W1"], lambda v: v["NETDS"] and v["CWR"] and not v["BCLK"] and v["W1"])
    b.com("REPOEN", ["NETDS", "CRD"], lambda v: not (v["NETDS"] and v["CRD"]))
    b.com("STATOEN", ["NETSS", "CRD"], lambda v: not (v["NETSS"] and v["CRD"]))
    b.com("NDWR", ["NETDS", "CWR", "W1"], lambda v: v["NETDS"] and v["CWR"] and v["W1"])
    b.com("NDRD", ["NETDS", "CRD", "W1"], lambda v: v["NETDS"] and v["CRD"] and v["W1"])
    b.com("CTLWR", ["NETSS", "CWR", "W1"], lambda v: v["NETSS"] and v["CWR"] and v["W1"])
    b.com("POCLK", ["SPOUT", "CWR", "BCLK", "W1"], lambda v: v["SPOUT"] and v["CWR"] and not v["BCLK"] and v["W1"])
    b.com("PORDN", ["SPOUT", "CRD"], lambda v: not (v["SPOUT"] and v["CRD"]))
    b.com("PINRDN", ["SPIN", "CRD"], lambda v: not (v["SPIN"] and v["CRD"]))
    b.com("RTCAS", ["SRTCI", "CWR", "BCLK", "W1"], lambda v: v["SRTCI"] and v["CWR"] and not v["BCLK"] and v["W1"])
    return b.build()

def gal_mbox():
    """The mailbox's flags (P32's design), clocked by CLK, reset by -RES.
    The Pico's -PRD and PWR are asynchronous: each passes two registers,
    and its leading edge (the first on, the second still off) acts, once.
    TXE resets high (both latches empty). RESET (NCTRL bit 7) is RST, one
    clock long. NETASK: ATTN with IEN (interrupt status bit 8)."""
    b = GB("MBOX", "I/O card: mailbox flags, RESET pulse", clock="BCLK", reset="BRESN")
    b.inp("BRESN", "PRDN", "PWR", "ATTN", "ID3", "ID7", "NDWR", "NDRD", "CTLWR")
    b.reg("PRD1", ["PRDN"], lambda v: not v["PRDN"])
    b.reg("PRD2", ["PRD1"], lambda v: v["PRD1"])
    b.reg("PWR1", ["PWR"], lambda v: v["PWR"])
    b.reg("PWR2", ["PWR1"], lambda v: v["PWR1"])
    def txe(v):
        if v["RST"]: return 1
        if v["NDWR"]: return 0
        if v["PRD1"] and not v["PRD2"]: return 1
        return v["TXE"]
    b.reg("TXE", ["RST", "NDWR", "PRD1", "PRD2", "TXE"], txe, reset_high=True)
    def rxf(v):
        if v["RST"]: return 0
        if v["PWR1"] and not v["PWR2"]: return 1
        if v["NDRD"]: return 0
        return v["RXF"]
    b.reg("RXF", ["RST", "PWR1", "PWR2", "NDRD", "RXF"], rxf)
    b.reg("IEN", ["CTLWR", "ID3", "IEN"], lambda v: v["ID3"] if v["CTLWR"] else v["IEN"])
    b.reg("RST", ["CTLWR", "ID7"], lambda v: v["CTLWR"] and v["ID7"])
    b.com("NETASK", ["ATTN", "IEN"], lambda v: v["ATTN"] and v["IEN"])
    b.com("NRUN", ["RST", "BRESN"], lambda v: v["RST"] or not v["BRESN"])
    return b.build()

def ask(v, d):
    name, net, low, _ = DEVICES[d]
    return (not v[net]) if low else bool(v[net])

NGRP = 5                                                       # the acknowledge GALs: two devices each
ACKL = ["ACK%dN" % k for k in range(8)]                        # the level acknowledged, one-hot (a 74HCT138)

def gal_iv(g):
    """The acknowledge cycle for devices 2g and 2g+1: each one's match --
    it asks, and its level jumpers equal the level being acknowledged
    (ACKkN low: a 74HCT138 decodes A4:A2 in an acknowledge cycle)."""
    devs = [d for d in (2 * g, 2 * g + 1) if d < len(DEVICES)]
    b = GB("IV" + "ABCDE"[g], "I/O card: acknowledge, devices %s" % "-".join(map(str, devs))).inp(*ACKL)
    for d in devs:
        name, net, low, _ = DEVICES[d]
        b.inp(net, *LV(d))
        deps = ACKL + [net] + LV(d)
        lit = ("!" if low else "") + net
        terms = []
        for k in range(8):
            bits = " & ".join(("" if k >> i & 1 else "!") + LV(d)[i] for i in range(3))
            terms.append("%s & %s & !ACK%dN" % (lit, bits, k))
        b.com("M%d" % d, deps, lambda v, d=d: ask(v, d) and not v["ACK%dN" % num(v, LV(d))],
              eq=" | ".join(terms))
    return b.build()

def gal_iopri():
    """The lowest asking device on the level acknowledged: its interrupt
    status bit (VEC3:0), and VHIT (one is asking). IDOE: GAL IOVEC drives
    the byte bus (a GAL output enable is one product term)."""
    M = ["M%d" % d for d in range(len(DEVICES))]
    b = GB("IOPRI", "I/O card: the vector's device").inp(*M).inp("INTA", "SIDR", "SRTCI", "CRD")
    def dev(v):
        for d in range(len(DEVICES)):
            if v["M%d" % d]:
                return d
        return None
    for i in range(4):
        b.com("VEC%d" % i, M, lambda v, i=i: None if dev(v) is None else dev(v) >> i & 1)
    b.com("VHIT", M, lambda v: dev(v) is not None)
    b.com("IDOE", M + ["INTA", "SIDR", "SRTCI", "CRD"],
          lambda v: (v["INTA"] and dev(v) is not None) or ((v["SIDR"] or v["SRTCI"]) and v["CRD"]))
    return b.build()

def gal_iovec():
    """Drives the byte bus for the card ID (FFF0: 1), the revision (FFF1:
    0), the clock's register number read (FF40: FF, as p32io.h) and the
    acknowledge cycle's vector: window x 16 + the device."""
    W = ["WIN0", "WIN1", "WIN2"]
    V = ["VEC0", "VEC1", "VEC2", "VEC3"]
    b = GB("IOVEC", "I/O card: ID, revision, vector").inp("IDOE", "LN0", "SRTCI", "INTA", *V).inp(*W)
    def val(v):
        if v["INTA"]:
            return num(v, W) << 4 | num(v, V)
        if v["SRTCI"]:
            return 0xFF
        return 0 if v["LN0"] else 1
    deps = ["LN0", "SRTCI", "INTA"] + V + W
    for i in range(8):
        b.tri("ID%d" % i, deps, lambda v, i=i: val(v) >> i & 1, "IDOE")
    return b.build()

def gals():
    G = {}
    for f in (gal_iosel, gal_iobe, gal_iodec1, gal_iodec2, gal_iostb, gal_timdec, gal_timctl, gal_nstb, gal_mbox, gal_iopri, gal_iovec):
        g = f()
        G[g.name] = g
    for k in range(NGRP):
        g = gal_iv(k)
        G[g.name] = g
    return G

# ---------------------------------------------------------------- the netlist
def build():
    G = gals()
    c = Card("io-card", "P32W I/O CARD")
    role = {}
    n = [0]
    def U(dev, function, r=None, value=None):
        n[0] += 1
        ref = "U%d" % n[0]
        c.part(ref, dev, function, value=value)
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
    for k, (lo, out) in enumerate(((16, "HI1N"), (24, "HI2N"))):
        x = U("74HCT688", "A%d-A%d all ones" % (lo, lo + 7))
        w(x, "-G", "GND"); w(x, "-P=Q", out)
        for i in range(8):
            w(x, "P%d" % i, "A%d" % (lo + i)); w(x, "Q%d" % i, "VCC")
    gal("IOSEL", "window decode", {})
    gal("IOBE", "access size, lane", {})
    gal("IODEC1", "byte device selects", {})
    gal("IODEC2", "register selects", {})
    gal("IOSTB", "strobes, byte lanes, -ACK", {})
    for k in range(4):
        x = U("74HCT245", "byte lane %d (D%d-D%d) <-> the byte bus" % (k, 8 * k, 8 * k + 7))
        w(x, "DIR", "LDIR"); w(x, "-OE", "LE%dN" % k)
        for i in range(8):
            w(x, "A%d" % (i + 1), "D%d" % (8 * k + i)); w(x, "B%d" % (i + 1), "ID%d" % i)

    # ---- the UARTs
    osc = U("OSC1M8432", "the UARTs' clock")
    w(osc, "EN", "VCC"); w(osc, "OUT", "UCLK")
    # each UART's port: a MAX232A (TXD and RTS out, RXD and CTS in, at RS-232
    # levels) and a DB9 plug at the front edge, wired as a PC's (DTE)
    for k in range(4):
        x = U("TL16C550", "UART %d (FF%02X)%s" % (k, UART[k], ", the console" if k == 0 else ""), "uart%d" % k)
        for i in range(8):
            w(x, "D%d" % i, "ID%d" % i)
        w(x, "A0", "LN0"); w(x, "A1", "LN1"); w(x, "A2", "BA2")
        w(x, "CS0", "VCC"); w(x, "CS1", "VCC"); w(x, "-CS2", "U%dN" % k); w(x, "-RI", "VCC")
        for p in ("-ADS", "RD2", "WR2", "-DSR", "-DCD"):
            w(x, p, "GND")
        w(x, "-RD1", "IORDN"); w(x, "-WR1", "IOWRN"); w(x, "MR", "MR")
        w(x, "XIN", "UCLK"); w(x, "-BAUDOUT", "BAUD%d" % k); w(x, "RCLK", "BAUD%d" % k)
        w(x, "INTRPT", "UINT%d" % k)
        w(x, "SIN", "SIN%d" % k); w(x, "SOUT", "SOUT%d" % k); w(x, "-CTS", "CTS%dN" % k); w(x, "-RTS", "RTS%dN" % k)
        c.unused(x, "XOUT", "DDIS", "-TXRDY", "-RXRDY", "-OUT1", "-OUT2", "-DTR")
        m = U("MAX232A", "UART %d: RS-232 levels" % k)
        w(m, "T1IN", "SOUT%d" % k); w(m, "T1OUT", "TXD%d" % k)
        w(m, "T2IN", "RTS%dN" % k); w(m, "T2OUT", "RTS%d" % k)
        w(m, "R1IN", "RXD%d" % k); w(m, "R1OUT", "SIN%d" % k)
        w(m, "R2IN", "CTS%d" % k); w(m, "R2OUT", "CTS%dN" % k)
        for cn, (pa, pb) in enumerate((("C1+", "C1-"), ("C2+", "C2-"), ("V+", "VCC"), ("V-", "GND")), 1):
            ref = "C%d" % (910 + 4 * k + cn)
            c.part(ref, "C100N", "UART %d: the MAX232A's charge pump" % k)
            w(ref, "1", "MP%d_%s" % (k, pa) if pa[0] in "CV" else pa)
            w(ref, "2", pb if pb in ("VCC", "GND") else "MP%d_%s" % (k, pb))
        for pin in ("C1+", "C1-", "C2+", "C2-", "V+", "V-"):
            w(m, pin, "MP%d_%s" % (k, pin))
        j = "J%d" % (4 + k)
        c.part(j, "DB9M_RA", "UART %d (tty%d): DB9 plug at the front edge, as a PC's COM port" % (k, k))
        w(j, "2", "RXD%d" % k); w(j, "3", "TXD%d" % k); w(j, "5", "GND"); w(j, "7", "RTS%d" % k)
        w(j, "8", "CTS%d" % k); w(j, "SHELL", "GND")
        c.unused(j, "1", "4", "6", "9")

    # ---- the timer
    gal("TIMDEC", "timer and status paths", {})
    gal("TIMCTL", "timer enable, pending, prescaler", {"FASTN": "-FAST"})
    for k in range(8):
        # (74F: the carry chain to ALLONES, 8 stages, must settle within a
        # clock of a load: 8 x 40 ns in HCT does not, D72)
        x = U("74F163", "timer: ~TCOUNT bits %d-%d" % (4 * k, 4 * k + 3), "cnt%d" % k)
        w(x, "CLK", "BCLK"); w(x, "-CLR", "VCC"); w(x, "ENP", "TICK"); w(x, "-LOAD", "CNTLDN")
        w(x, "ENT", "VCC" if k == 0 else "RCO%d" % (k - 1))
        w(x, "RCO", "ALLONES" if k == 7 else "RCO%d" % k)
        for i, (d, q) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
            w(x, d, "LD%d" % (4 * k + i)); w(x, q, "CQ%d" % (4 * k + i))
    for j in range(4):
        r = U("74HCT374", "timer: ~TRELOAD bits %d-%d (on the load bus)" % (8 * j, 8 * j + 7), "rel%d" % j)
        win = U("74HCT540", "timer: D -> load bus, inverted (TCOUNT, TRELOAD writes), bits %d-%d" % (8 * j, 8 * j + 7))
        rld = U("74HCT540", "timer: load bus -> D, inverted (TRELOAD reads), bits %d-%d" % (8 * j, 8 * j + 7))
        rcn = U("74HCT540", "timer: count -> D, inverted (TCOUNT reads), bits %d-%d" % (8 * j, 8 * j + 7))
        w(r, "CLK", "RCLK"); w(r, "-OE", "ROEN")
        w(win, "-OE1", "D2LDN"); w(rld, "-OE1", "RDLDN"); w(rcn, "-OE1", "RDCNTN")
        for x in (win, rld, rcn):
            w(x, "-OE2", "GND")
        for i in range(8):
            bit = 8 * j + i
            w(r, "D%d" % i, "LD%d" % bit); w(r, "Q%d" % i, "LD%d" % bit)
            w(win, "A%d" % i, "D%d" % bit); w(win, "Y%d" % i, "LD%d" % bit)
            w(rld, "A%d" % i, "LD%d" % bit); w(rld, "Y%d" % i, "D%d" % bit)
            w(rcn, "A%d" % i, "CQ%d" % bit); w(rcn, "Y%d" % i, "D%d" % bit)

    # ---- the status word (TCTRL, FFF4) on the status bus SB, pulled down
    isr = U("74HCT541", "interrupt status bits 0-7 -> the status bus")
    w(isr, "-OE1", "ISROEN"); w(isr, "-OE2", "GND")
    asks = ["PEND", "UINT0", "UINT1", "UINT2", "UINT3", "RTCASK", "USB0ASK", "USB1ASK"]
    for i, a in enumerate(asks):
        w(isr, "A%d" % i, a); w(isr, "Y%d" % i, "SB%d" % i)
    b8 = U("74HCT125", "interrupt status bit 8 (the network) -> the status bus")
    w(b8, "1A", "NETASK"); w(b8, "1Y", "SB8"); w(b8, "-1OE", "ISROEN")
    for g in range(2, 5):
        w(b8, "%dA" % g, "GND"); w(b8, "-%dOE" % g, "VCC"); c.unused(b8, "%dY" % g)
    c.part("RN1", "RNET9", "status bus pull-downs SB0-SB7", value="10k x 8 bussed")
    w("RN1", "COM", "GND")
    for i in range(8):
        w("RN1", "R%d" % (i + 1), "SB%d" % i)
    c.part("R9", "R", "status bus pull-down SB8", value="10k")
    w("R9", "1", "SB8"); w("R9", "2", "GND")
    for j in range(4):
        x = U("74HCT541", "status bus -> D%d-D%d (TCTRL, FFF4 reads)" % (8 * j, 8 * j + 7))
        w(x, "-OE1", "STOEN"); w(x, "-OE2", "GND")
        for i in range(8):
            bit = 8 * j + i
            w(x, "A%d" % i, "SB%d" % bit if bit <= 8 else "GND"); w(x, "Y%d" % i, "D%d" % bit)

    # ---- the ports (FF20 in, FF21 out), on IDC headers at the edge
    pin_ = U("74HCT541", "input port (FF20) -> the byte bus")
    pout = U("74HCT574", "output port (FF21)", "pout")
    prb = U("74HCT541", "output port read back -> the byte bus")
    w(pin_, "-OE1", "PINRDN"); w(pin_, "-OE2", "GND"); w(prb, "-OE1", "PORDN"); w(prb, "-OE2", "GND")
    w(pout, "CLK", "POCLK"); w(pout, "-OE", "GND")
    c.part("J8", "IDC10", "input port: 1-8 bits 0-7, 9 +5 V, 10 GND")
    c.part("J9", "IDC10", "output port: 1-8 bits 0-7, 9 +5 V, 10 GND")
    c.part("RN2", "RNET9", "input port pull-ups (open inputs read 1)", value="10k x 8 bussed")
    w("RN2", "COM", "VCC")
    for i in range(8):
        w(pin_, "A%d" % i, "PI%d" % i); w(pin_, "Y%d" % i, "ID%d" % i); w("J8", str(i + 1), "PI%d" % i)
        w("RN2", "R%d" % (i + 1), "PI%d" % i)
        w(pout, "D%d" % i, "ID%d" % i); w(pout, "Q%d" % i, "PO%d" % i); w("J9", str(i + 1), "PO%d" % i)
        w(prb, "A%d" % i, "PO%d" % i); w(prb, "Y%d" % i, "ID%d" % i)
    for h in ("J8", "J9"):
        w(h, "9", "VCC"); w(h, "10", "GND")

    # ---- the real-time clock (D23)
    rtc = U("DS12C887", "real-time clock (FF40 register number, FF41 register)", "rtc")
    for i in range(8):
        w(rtc, "AD%d" % i, "ID%d" % i)
    w(rtc, "MOT", "GND"); w(rtc, "AS", "RTCAS"); w(rtc, "-CS", "RTCDN"); w(rtc, "-RD", "IORDN")
    w(rtc, "-WR", "IOWRN"); w(rtc, "-RESET", "BRESN"); w(rtc, "-IRQ", "RTCIRQN"); w(rtc, "-RCLR", "VCC")
    c.unused(rtc, "SQW", "NC2", "NC3", "NC16", "NC20", "NC22")
    c.part("R10", "R", "the clock's -IRQ pull-up (open drain)", value="10k")
    w("R10", "1", "RTCIRQN"); w("R10", "2", "VCC")

    # ---- USB: two CH376S modules (D43), their 16-pin headers: D0-D7 the odd
    # pins 1-15, GND 2 and 4, +5 V 6, INT# 8, A0 10, CS# 12, RD# 14, WR# 16
    for k in range(2):
        s = "J%d" % (10 + k)
        c.part(s, "SOCK2X8", "USB %d (FF%02X): a CH376S module, its USB-A socket at the front edge" % (k, (USB0, USB1)[k]))
        role["usb%d" % k] = s
        for i in range(8):
            w(s, str(1 + 2 * i), "ID%d" % i)
        w(s, "2", "GND"); w(s, "4", "GND"); w(s, "6", "VCC")
        w(s, "8", "USBINT%dN" % k); w(s, "10", "LN0"); w(s, "12", "USB%dN" % k); w(s, "14", "IORDN"); w(s, "16", "IOWRN")
        r = "R%d" % (11 + k)
        c.part(r, "R", "USB %d: INT# pull-up" % k, value="4.7k")
        w(r, "1", "USBINT%dN" % k); w(r, "2", "VCC")

    # ---- the network coprocessor (D45, docs/net-mailbox.md)
    gal("NSTB", "mailbox, port and clock strobes", {})
    gal("MBOX", "mailbox flags, RESET pulse", {"PRDN": "-PRD"})
    c.part("A1", "PICO", "network coprocessor: Pico 2 W (RP2350, Wi-Fi), its USB port at the front edge",
           value="Raspberry Pi Pico 2 W")
    role["pico"] = "A1"
    for k in (3, 8, 13, 18, 23, 28, 33, 38):
        w("A1", "GND%d" % k, "GND")
    w("A1", "VSYS", "VSYS"); w("A1", "3V3", "+3V3P"); w("A1", "RUN", "RUN")
    c.unused("A1", "ADC_VREF", "3V3_EN", "VBUS", *["GP%d" % k for k in (14, 15, 22, 26, 27, 28)])
    for i in range(8):
        w("A1", "GP%d" % i, "PD%d" % i)
    for gp, net in ((8, "-PRD"), (9, "PWR"), (10, "PTXE"), (11, "PRXF"), (12, "READY"), (13, "ATTN"),
                    (16, "WMISO"), (17, "WCS"), (18, "WSCK"), (19, "WMOSI"), (20, "WRST"), (21, "WINT")):
        w("A1", "GP%d" % gp, net)
    c.part("D1", "D_SCHOTTKY", "the backplane's 5 V into the Pico's VSYS")
    w("D1", "A", "VCC"); w("D1", "K", "VSYS")
    c.part("A2", "WIZ850IO", "Ethernet: WIZnet WIZ850io (W5500, its RJ45 at the front edge) on the Pico's SPI0")
    for p in ("GND1", "GND2", "GND7"):
        w("A2", p, "GND")
    w("A2", "3V3A", "+3V3"); w("A2", "3V3B", "+3V3")
    for p, net in (("MOSI", "WMOSI"), ("SCLK", "WSCK"), ("SCNn", "WCS"), ("INTn", "WINT"), ("RSTn", "WRST"), ("MISO", "WMISO")):
        w("A2", p, net)
    c.unused("A2", "NC10")
    for r, net, to, what in (("R13", "-PRD", "+3V3P", "request buffer off"), ("R14", "PWR", "GND", "no reply strobe"),
                             ("R15", "READY", "GND", "not ready"), ("R16", "ATTN", "GND", "no events"),
                             ("R17", "WRST", "+3V3", "the W5500 running"), ("R18", "WCS", "+3V3", "the W5500 deselected")):
        c.part(r, "R", "until the firmware runs: %s" % what, value="10k")
        w(r, "1", net); w(r, "2", to)
    c.part("R19", "R", "RUN pull-up (with C: the RESET pulse stretched to about 15 us)", value="10k")
    w("R19", "1", "RUN"); w("R19", "2", "+3V3P")
    reg = U("LD1117V33", "3.3 V: the WIZ850io and the 3.3 V buffers")
    w(reg, "IN", "VCC"); w(reg, "OUT", "+3V3")
    req = U("74HCT574", "mailbox request latch: P32W writes NDATA (FF52)", "req")
    reqb = U("74LVC244", "request latch -> the Pico's PD0-7 at 3.3 V (its -PRD)")
    rep = U("74HCT574", "mailbox reply latch: the Pico writes (PWR), P32W reads NDATA", "rep")
    stat = U("74HCT244", "NSTAT (FF53) -> the byte bus")
    w(req, "CLK", "REQCLK"); w(req, "-OE", "GND")
    w(reqb, "-1G", "-PRD"); w(reqb, "-2G", "-PRD")
    w(rep, "CLK", "PWR"); w(rep, "-OE", "REPOEN")
    w(stat, "-1G", "STATOEN"); w(stat, "-2G", "STATOEN")
    def ch244(b):
        return ("1A%d" % (b + 1), "1Y%d" % (b + 1)) if b < 4 else ("2A%d" % (b - 3), "2Y%d" % (b - 3))
    for i in range(8):
        a, y = ch244(i)
        w(req, "D%d" % i, "ID%d" % i); w(rep, "Q%d" % i, "ID%d" % i)
        w(req, "Q%d" % i, "RQ%d" % i); w(reqb, a, "RQ%d" % i)
        w(reqb, y, "PD%d" % i); w(rep, "D%d" % i, "PD%d" % i)
    for i, src in enumerate(("RXF", "TXE", "ATTN", "IEN", "GND", "GND", "GND", "READY")):
        a, y = ch244(i)
        w(stat, a, src); w(stat, y, "ID%d" % i)
    ls = U("CD74HC4050", "TXE, RXF -> the Pico at 3.3 V")
    w(ls, "1A", "TXE"); w(ls, "1Y", "PTXE"); w(ls, "2A", "RXF"); w(ls, "2Y", "PRXF")
    gnd_unused(ls, ["%dA" % g for g in range(3, 7)], ["%dY" % g for g in range(3, 7)] + ["NC13", "NC16"])
    c.part("C901", "C10U", "LD1117V33 input"); c.part("C902", "C10U", "LD1117V33 output (10 uF minimum)")
    w("C901", "+", "VCC"); w("C902", "+", "+3V3"); w("C901", "-", "GND"); w("C902", "-", "GND")
    c.part("C903", "C2N2", "RUN: the RESET pulse stretched (with R19)")
    w("C903", "1", "RUN"); w("C903", "2", "GND")

    # ---- interrupts: the levels (a 74LS145 per device), the acknowledge cycle
    i1 = U("74HCT14", "inverters: the active-high requests, for the 74LS145s")
    for g, a in enumerate(["PEND", "UINT0", "UINT1", "UINT2", "UINT3", "NETASK"], 1):
        w(i1, "%dA" % g, a); w(i1, "%dY" % g, a + "N")
    i2 = U("74HCT14", "inverters: the active-low requests, for the interrupt status")
    for g, (a, y) in enumerate((("RTCIRQN", "RTCASK"), ("USBINT0N", "USB0ASK"), ("USBINT1N", "USB1ASK")), 1):
        w(i2, "%dA" % g, a); w(i2, "%dY" % g, y)
    gnd_unused(i2, ["%dA" % g for g in range(4, 7)], ["%dY" % g for g in range(4, 7)])
    lowask = {"PEND": "PENDN", "UINT0": "UINT0N", "UINT1": "UINT1N", "UINT2": "UINT2N", "UINT3": "UINT3N",
              "NETASK": "NETASKN", "RTCIRQN": "RTCIRQN", "USBINT0N": "USBINT0N", "USBINT1N": "USBINT1N"}
    for d, (name, net, low, lev) in enumerate(DEVICES):
        x = U("74LS145", "%s's interrupt level (jumpers %s): its -IRQ line, open collector" % (name, "/".join(LV(d))))
        w(x, "A", LV(d)[0]); w(x, "B", LV(d)[1]); w(x, "C", LV(d)[2]); w(x, "D", lowask[net])
        for k in range(8):
            w(x, "O%d" % k, "-IRQ%d" % k)
        c.unused(x, "O8", "O9")
    ad = U("74F138", "the level being acknowledged (A4:A2 in an acknowledge cycle), one-hot")
    w(ad, "A", "BA2"); w(ad, "B", "BA3"); w(ad, "C", "BA4"); w(ad, "G1", "INTA"); w(ad, "-G2A", "GND"); w(ad, "-G2B", "GND")
    for k in range(8):
        w(ad, "Y%d" % k, "ACK%dN" % k)
    for k in range(NGRP):
        gal("IV" + "ABCDE"[k], "acknowledge: devices %d-%d" % (2 * k, min(2 * k + 1, len(DEVICES) - 1)), {})
    gal("IOPRI", "the vector's device", {})
    gal("IOVEC", "ID, revision, vector", {})
    od = U("74HC05", "open drain: -ACK, -WAIT; the Pico's RUN")
    w(od, "1A", "ACK"); w(od, "1Y", "-ACK"); w(od, "2A", "NRUN"); w(od, "2Y", "RUN")
    w(od, "3A", "WAIT"); w(od, "3Y", "-WAIT")                  # (the byte devices' wait state, D72)
    gnd_unused(od, ["%dA" % g for g in range(4, 7)], ["%dY" % g for g in range(4, 7)])

    # ---- the jumpers (at the card's edge, labelled): fitted = 0, open = 1
    jumpers = [("WIN%d" % i, "window bit %d" % i) for i in range(3)]
    for d, (name, net, low, lev) in enumerate(DEVICES):
        jumpers += [(LV(d)[i], "%s's level, bit %d" % (name, i)) for i in range(3)]
    for i, (net, fn) in enumerate(jumpers, 1):
        c.part("JP%d" % i, "JUMPER2", "jumper %s: %s" % (net, fn))
        w("JP%d" % i, "1", net); w("JP%d" % i, "2", "GND")
        rn = "RN%d" % (3 + (i - 1) // 8)
        if rn not in c.parts:
            c.part(rn, "RNET9", "jumper pull-ups", value="10k x 8 bussed")
            w(rn, "COM", "VCC")
        w(rn, "R%d" % ((i - 1) % 8 + 1), net)
    last = "RN%d" % (3 + (len(jumpers) - 1) // 8)
    c.unused(last, *["R%d" % k for k in range((len(jumpers) - 1) % 8 + 2, 9)])

    # ---- the layout (generators/p32kicad_pcb.py; the board rules, D48): the
    # cable connectors and modules down the front edge, facing out -- the DB9s
    # (pin 1 7.70 mm from the edge, the shell through the panel), the USB
    # modules, the WIZ850io's RJ45, the Pico 2 W's USB port (Ken, 2026-10-28;
    # no copper under its antenna end, inward), the port headers; the jumpers
    # along the bottom edge
    c.front = ["J4", "J5", "J6", "J7", "J10", "J11", "A2", "A1", "J8", "J9"]
    c.front_rot = {"J4": 90, "J5": 90, "J6": 90, "J7": 90, "A1": 270, "A2": 0}
    c.front_edge = {"J4": ("pad1", 7.70), "J5": ("pad1", 7.70), "J6": ("pad1", 7.70), "J7": ("pad1", 7.70),
                    "A1": ("crt", 0.5), "A2": ("crt", 0.0)}
    c.rf_keepout = {"A1": (8.0, "-x")}
    c.edge_row = ["JP%d" % i for i in range(1, len(jumpers) + 1)]
    c.finish()
    CARDS[c.name] = c
    return c, G, role

def defaults():
    """The jumpers as the documents and the kernel assume them (D52):
    window 7, the levels of DEVICES. {net: 0 fitted / 1 open}."""
    j = {"WIN%d" % i: 1 for i in range(3)}
    for d, (name, net, low, lev) in enumerate(DEVICES):
        for i in range(3):
            j[LV(d)[i]] = lev >> i & 1
    return j

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("I/O card: %d errors" % len(errs))
    print("I/O card OK: %d ICs (%d GALs), %d nets, %d parts" % (len(ics), len(G), len(c.nets), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
