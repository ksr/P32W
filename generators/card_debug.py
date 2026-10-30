#!/usr/bin/env python3
"""The P32W debug card (slot 0): docs/debug-card.md, D50, D64.

A bus tester with two modes, chosen by a switch at its front edge and
never both at once (D64):

  * LISTEN: a logic analyzer on all 164 lines that reach slot 0 -- J1, J2
    (the arbiter's radial fan-in included) and J3, the CPU <-> MMU / data
    card's local link. Each sample goes into 22 latches (74ACT574) and 22
    SRAMs (CY7C199): 32 K samples, taken at CLK's rise (one a bus cycle) or
    at the card's own 24 MHz (a timing view). A trigger RAM (two CY7C199
    addressed by A2-A16 and A17-A31) gives 8 patterns of any mask; the
    capture runs until a trigger, then a set number of samples more.
  * DRIVE: the master. 18 drive registers (74HCT574) reach every line a
    card other than the CPU card reads, in five groups the host enables:
    the data bus, the bus cycle (A, BE, -MRD, -MWR, -IACK), the system lines
    (-RES, -BREQ3-10), the CPU's side of J3 (R, and its control lines);
    -ACK, -WAIT, -LOCK and its own -BREQ by open drain. With the CPU and
    MMU / data cards there it borrows the bus by DMA (-BREQ0, the arbiter's
    first priority) for one cycle at a time (GAL DBSEQ); alone ("ALONE") it
    makes CLK and -CLK itself (GAL DBCLK, from its oscillator or a step at
    a time) and may drive the rest.

The mode switch's LISTEN position holds every driver off in hardware (GAL
DBDRV). A Raspberry Pi Pico 2 is the host link (USB to the Mac): its 8-bit
local bus (a 74LVC245 to the card's 5 V XD0-7, seven address lines,
-XWR / -XRD) reaches the registers below.

Local bus map (XA6:0):
  00-15  write: slice k's drive register      read: slice k (a sample, or the lines live)
  40 CTRL0  GD GBUS GSYS GLR GJ3 ALONE ODACK ODWAIT   (written)
  41 CTRL1  ODLOCK CLKRUN SCLKSEL SETUP TNOW LIVE DMAWR XCLKEN
  42 / 43  the post-trigger count (low, high: 65536 - n)
  44 GO (one DMA cycle)  45 ARM (a capture)  46 STEP (one CLK, alone)  47 CSTEP (the capture address + 1)
  48 / 49  the trigger setup address (A[16:2] or A[31:17])  4A / 4B  write the trigger RAM A / B  4C  the capture address = 0
  40 read: status  41 / 42 read: the capture address

    python3 card_debug.py [--bom]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32sheets import sheets  # noqa: E402
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from card_cpu import GB, num                                  # noqa: E402

GND_ = "GND"
J3CTL = ["RSRC0", "RSRC1", "RSRC2", "RSRC3", "RDST0", "RDST1", "RDST2", "MEM0", "MEM1", "MEM2", "SIZE0", "SIZE1",
         "SIGNED", "SUPER", "TLBW", "CRSEL0", "CRSEL1", "CRSEL2", "-WSTB", "LOCKREQ", "PCINC", "MARPC", "MARINC", "CCLK"]
LISTEN_ONLY = (["PWROK", "-LOCK"] + ["-IRQ%d" % i for i in range(8)] + ["-BREQ", "-BGNT"] +
               ["-BGNT%d" % s for s in range(3, 11)] + ["-ACK", "-WAIT", "FAULT", "FC0", "FC1", "FC2", "FC3", "STALL"])

def pad(lines, n):
    return lines + [GND_] * (n - len(lines))

# the slices: (group, eight lines); a group is driven by the slices' drive registers
SLICES = []
def slices(group, lines):
    lines = pad(lines, (len(lines) + 7) // 8 * 8)
    for k in range(0, len(lines), 8):
        SLICES.append((group, lines[k:k + 8]))
slices("D", ["D%d" % i for i in range(32)])
slices("BUS", ["A%d" % i for i in range(2, 32)] + ["BE%d" % i for i in range(4)] + ["-MRD", "-MWR", "-IACK"])
slices("SYS", ["-RES"] + ["-BREQ%d" % s for s in range(3, 11)] + ["CLK", "-CLK"])
slices("LR", ["LR%d" % i for i in range(32)])
slices("J3", J3CTL)
slices(None, LISTEN_ONLY)
NSLICE = len(SLICES)                    # 22
NOT_DRIVEN = {"CLK", "-CLK", GND_}      # (CLK and -CLK come from GAL DBCLK, not a register)
LINES = [x for g, ls in SLICES for x in ls if x != GND_]
GROUPS = {"D": "GD", "BUS": "GBUS", "SYS": "GSYS", "LR": "GLR", "J3": "GJ3"}

# ---------------------------------------------------------------- GALs
def gal_dbadr():
    """The local bus's decoder enables: the slices (write: drive registers;
    read: samples), XA4 picking 0-15 or 16-21; the control writes (XA3: the
    first or second bank) and reads."""
    b = GB("DBADR", "debug card: local bus decode").inp("XA3", "XA4", "XA5", "XA6", "XWRN", "XRDN")
    sl = lambda v: not v["XA6"] and not v["XA5"]
    w, r = (lambda v: not v["XWRN"]), (lambda v: not v["XRDN"])
    b.com("SWLON", ["XA4", "XA5", "XA6", "XWRN"], lambda v: not (sl(v) and not v["XA4"] and w(v)))
    b.com("SWHIN", ["XA4", "XA5", "XA6", "XWRN"], lambda v: not (sl(v) and v["XA4"] and w(v)))
    b.com("SRLON", ["XA4", "XA5", "XA6", "XRDN"], lambda v: not (sl(v) and not v["XA4"] and r(v)))
    b.com("SRHIN", ["XA4", "XA5", "XA6", "XRDN"], lambda v: not (sl(v) and v["XA4"] and r(v)))
    b.com("CW0N", ["XA3", "XA6", "XWRN"], lambda v: not (v["XA6"] and not v["XA3"] and w(v)))
    b.com("CW1N", ["XA3", "XA6", "XWRN"], lambda v: not (v["XA6"] and v["XA3"] and w(v)))
    b.com("CRN", ["XA6", "XRDN"], lambda v: not (v["XA6"] and r(v)))
    return b.build()

def gal_dbdrv():
    """The drivers' enables, never in LISTEN (DRIVE low): the bus cycle's
    registers while the card has the bus (a DMA cycle) or alone; the data's
    in a DMA write or alone; the system lines and J3's only alone; CLK from
    DBCLK alone."""
    D = ["DRIVE", "GD", "GBUS", "GSYS", "GLR", "GJ3", "ALONE", "BUSEN", "DMAWR"]
    b = GB("DBDRV", "debug card: drive enables").inp(*D)
    dr = lambda v: v["DRIVE"]
    alone = lambda v: v["DRIVE"] and v["ALONE"]
    b.com("BUSOEN", D, lambda v: not (dr(v) and ((v["ALONE"] and v["GBUS"]) or v["BUSEN"])))
    b.com("DOEN", D, lambda v: not (dr(v) and ((v["ALONE"] and v["GD"]) or (v["BUSEN"] and v["DMAWR"]))))
    b.com("SYSOEN", D, lambda v: not (alone(v) and v["GSYS"]))
    b.com("LROEN", D, lambda v: not (alone(v) and v["GLR"]))
    b.com("J3OEN", D, lambda v: not (alone(v) and v["GJ3"]))
    b.com("CLKOE", ["DRIVE", "ALONE"], alone)
    b.com("LISTEN", ["DRIVE"], lambda v: not v["DRIVE"])
    return b.build()

ST = ["REQ", "CYC", "SNAP"]
def gal_dbseq():
    """One DMA cycle, clocked by the bus CLK: ask (-BREQ0), and when granted
    (the bus is the card's from the next clock), drive the cycle for one
    clock, held while -WAIT; then SNAP (the sample of the cycle's end goes
    into the SRAM) and let the bus go. Alone: no asking."""
    D = ["GOQ", "BGNTN", "WAITN", "ALONE", "DRIVE"]
    b = GB("DBSEQ", "debug card: the DMA cycle", clock="CLK").inp(*D)
    idle = lambda v: not (v["REQ"] or v["CYC"] or v["SNAP"])
    b.reg("REQ", D + ST, lambda v: v["DRIVE"] and not v["ALONE"] and
          ((v["GOQ"] and idle(v)) or (v["REQ"] and v["BGNTN"])))
    b.reg("CYC", D + ST, lambda v: v["DRIVE"] and ((v["REQ"] and not v["BGNTN"]) or
                                                   (v["ALONE"] and v["GOQ"] and idle(v)) or (v["CYC"] and not v["WAITN"])))
    b.reg("SNAP", D + ST, lambda v: v["CYC"] and v["WAITN"])
    b.com("BUSEN", ["CYC"], lambda v: v["CYC"])
    b.com("BREQOD", ["REQ", "CYC"], lambda v: v["REQ"] or v["CYC"])
    b.com("GOCLRN", ["SNAP"], lambda v: not v["SNAP"])
    return b.build()

CS = ["RUN", "POST", "DONE"]
def gal_dbcap():
    """The capture, clocked by the sample clock: armed, it runs (every sample
    written, the address counting) until the trigger, then counts the post-
    trigger samples down, and is DONE. In DRIVE only SNAP writes a sample.
    Which of the latches (the lines) or the SRAMs drives the slices' bus."""
    D = ["ARMQ", "TRIG", "PDONE", "DRIVE", "SNAP", "LIVE"]
    b = GB("DBCAP", "debug card: the capture", clock="SCLK").inp(*D)
    act = lambda v: v["RUN"] or v["POST"]
    b.reg("RUN", D + CS, lambda v: not v["DRIVE"] and ((v["ARMQ"] and not act(v)) or (v["RUN"] and not v["TRIG"])))
    b.reg("POST", D + CS, lambda v: not v["DRIVE"] and ((v["RUN"] and v["TRIG"]) or (v["POST"] and not v["PDONE"])))
    b.reg("DONE", D + CS, lambda v: (v["POST"] and v["PDONE"]) or (v["DONE"] and not v["ARMQ"]))
    b.com("SWEN", D + CS, lambda v: (act(v) and not v["DRIVE"]) or (v["SNAP"] and v["DRIVE"]))
    b.com("ACT", CS, act)
    b.com("PCNT", ["POST"], lambda v: v["POST"])
    b.com("PLDN", D + CS, lambda v: not (v["ARMQ"] and not act(v) and not v["DRIVE"]))
    b.com("ARMCLRN", ["RUN"], lambda v: not v["RUN"])
    b.com("LATOEN", D + CS, lambda v: not (v["LIVE"] or act(v) or (v["SNAP"] and v["DRIVE"])))
    b.com("SROEN", D + CS, lambda v: v["LIVE"] or act(v) or (v["SNAP"] and v["DRIVE"]))
    return b.build()

def gal_dbtrig():
    """The trigger: TNOW, or a bus cycle (a strobe low) whose address any
    pattern of the trigger RAMs matches (M7-0: RAM A's bit and RAM B's);
    none while the RAMs are being written (SETUP)."""
    M = ["M%d" % i for i in range(8)]
    D = M + ["MRDN", "MWRN", "IACKN", "TNOW", "SETUP"]
    b = GB("DBTRIG", "debug card: the trigger").inp(*D)
    b.com("ANYM", M, lambda v: any(v[m] for m in M))                     # (helpers, fed back: the terms fit)
    b.com("BCYC", ["MRDN", "MWRN", "IACKN"], lambda v: not (v["MRDN"] and v["MWRN"] and v["IACKN"]))
    b.com("TRIG", ["ANYM", "BCYC", "TNOW", "SETUP"], lambda v: not v["SETUP"] and (v["TNOW"] or (v["BCYC"] and v["ANYM"])))
    b.com("SETUPN", ["SETUP"], lambda v: not v["SETUP"])
    return b.build()

PH = ["P0", "P1", "P2"]
def gal_dbclk():
    """The bus clock when the card is alone (D64): the 24 MHz oscillator
    divided by eight (CLK high in phases 0-3, 3 MHz), running (CLKRUN) or a
    period at a time (STEP: one rising edge, then held low at phase 4).
    With XCLKEN (CTRL1 bit 7, D76) it clocks the machine instead: -XCLKEN
    low tells the CPU card to take XCLK as its oscillator, XCLK the
    oscillator halved (12 MHz: a 1.5 MHz machine) while CLKRUN, or one
    pulse per STEP (one oscillator edge of the CPU card: one of its eight
    phases), low between."""
    b = GB("DBCLK", "debug card: CLK when alone; XCLK", clock="OSC").inp("CLKRUN", "STEPQ", "CLKOE", "XCLKEN")
    run = lambda v: v["CLKRUN"] or v["STEPQ"]
    for i in range(3):
        b.reg("P%d" % i, ["CLKRUN", "STEPQ"] + PH,
              lambda v, i=i: ((num(v, PH) + 1) & 7 if run(v) else num(v, PH)) >> i & 1)
    b.tri("CLKO", PH, lambda v: num(v, PH) < 4, "CLKOE")
    b.tri("CLKON", PH, lambda v: num(v, PH) >= 4, "CLKOE")
    b.com("STEPCLRN", ["CLKRUN", "STEPQ", "XCLKEN", "XCK", "XDN"] + PH,
          lambda v: not (v["STEPQ"] and not v["CLKRUN"] and
                         ((not v["XCLKEN"] and num(v, PH) == 3) or (v["XCLKEN"] and (v["XCK"] or v["XDN"])))))
    # (one pulse per STEP: the host's write holds STEPQ's preset for a few
    # periods, so XDN marks the pulse given until STEPQ has really cleared)
    b.reg("XCK", ["XCLKEN", "CLKRUN", "STEPQ", "XCK", "XDN"],
          lambda v: v["XCLKEN"] and not v["XCK"] and (v["CLKRUN"] or (v["STEPQ"] and not v["XDN"])))
    b.reg("XDN", ["STEPQ", "XCK", "XDN"], lambda v: v["STEPQ"] and (v["XDN"] or v["XCK"]))
    b.com("XCLKENN", ["XCLKEN"], lambda v: not v["XCLKEN"])
    # the timing mode's sample clock: the oscillator halved, 12 MHz (D73: at
    # 24 MHz the capture path did not meet its clock)
    b.reg("T12", ["T12"], lambda v: not v["T12"])
    return b.build()

def gals():
    G = {}
    for f in (gal_dbadr, gal_dbdrv, gal_dbseq, gal_dbcap, gal_dbtrig, gal_dbclk):
        g = f()
        G[g.name] = g
    return G

# ---------------------------------------------------------------- the netlist
def build():
    G = gals()
    c = Card("debug-card", "P32W DEBUG CARD")
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
    XD = ["XD%d" % i for i in range(8)]

    # ---- the host link: a Pico 2 and its local bus
    c.part("A1", "PICO", "host link: Raspberry Pi Pico 2 (USB to the Mac), its USB port at the front edge",
           value="Raspberry Pi Pico 2")
    role["pico"] = "A1"
    for k in (3, 8, 13, 18, 23, 28, 33, 38):
        w("A1", "GND%d" % k, "GND")
    w("A1", "VSYS", "VSYS"); w("A1", "3V3", "+3V3")
    c.part("D1", "D_SCHOTTKY", "the backplane's 5 V into the Pico's VSYS")
    w("D1", "A", "VCC"); w("D1", "K", "VSYS")
    for i in range(8):
        w("A1", "GP%d" % i, "PD%d" % i)
    for i in range(7):
        w("A1", "GP%d" % (8 + i), "XA%d" % i)
    for gp, net in ((15, "XWRN"), (16, "XRDN"), (17, "XDIR"), (18, "CRESN")):
        w("A1", "GP%d" % gp, net)
    c.unused("A1", "RUN", "ADC_VREF", "3V3_EN", "VBUS", *["GP%d" % k for k in (19, 20, 21, 22, 26, 27, 28)])
    lv = U("74LVC245", "the Pico's PD0-7 (3.3 V) <-> the card's XD0-7 (5 V)")
    w(lv, "DIR", "XDIR"); w(lv, "-OE", "GND")
    for i in range(8):
        w(lv, "A%d" % (i + 1), "PD%d" % i); w(lv, "B%d" % (i + 1), XD[i])
    rr = [0]
    def R(value, a, b_, fn):
        rr[0] += 1
        ref = "R%d" % rr[0]
        c.part(ref, "R", fn, value=value)
        w(ref, "1", a); w(ref, "2", b_)
    R("10k", "XWRN", "+3V3", "-XWR held off until the Pico runs")
    R("10k", "XRDN", "+3V3", "-XRD held off until the Pico runs")
    R("10k", "CRESN", "GND", "the card held in reset until the Pico runs (its control registers cleared)")

    # ---- the local bus's decoders
    gal("DBADR", "local bus decode", {})
    dec = {}
    for name, en in (("WLO", "SWLON"), ("WHI", "SWHIN"), ("RLO", "SRLON"), ("RHI", "SRHIN")):
        x = U("74HCT154", "local bus: slices %s (%s)" % ("0-15" if name[1:] == "LO" else "16-21",
                                                         "drive registers" if name[0] == "W" else "read"))
        w(x, "A", "XA0"); w(x, "B", "XA1"); w(x, "C", "XA2"); w(x, "D", "XA3"); w(x, "-G1", en); w(x, "-G2", "GND")
        dec[name] = x
    for k in range(NSLICE):
        lo = k < 16
        w(dec["WLO" if lo else "WHI"], "Y%d" % (k % 16), "WS%dN" % k)
        w(dec["RLO" if lo else "RHI"], "Y%d" % (k % 16), "RS%dN" % k)
    for d in ("WHI", "RHI"):
        c.unused(dec[d], *["Y%d" % i for i in range(NSLICE - 16, 16)])
    banks = {"CW0N": ["CTRL0N", "CTRL1N", "POSTLN", "POSTHN", "GON", "ARMN", "STEPN", "CSTEPN"],
             "CW1N": ["TSALN", "TSAHN", "TWAN", "TWBN", "CAPCLRN", None, None, None],
             "CRN": ["RSTATN", "RCALN", "RCAHN", None, None, None, None, None]}
    for en, outs in banks.items():
        x = U("74HCT138", "local bus: %s" % {"CW0N": "control writes 40-47", "CW1N": "control writes 48-4C",
                                             "CRN": "status reads"}[en])
        w(x, "A", "XA0"); w(x, "B", "XA1"); w(x, "C", "XA2"); w(x, "G1", "VCC"); w(x, "-G2A", en); w(x, "-G2B", "GND")
        for i, o in enumerate(outs):
            if o:
                w(x, "Y%d" % i, o)
            else:
                c.unused(x, "Y%d" % i)

    # ---- the control registers (cleared while the Pico holds CRESN low)
    regs = (("CTRL0N", "control 0: the drive groups, alone, -ACK / -WAIT", ["GD", "GBUS", "GSYS", "GLR", "GJ3", "ALONE", "ODACK", "ODWAIT"]),
            ("CTRL1N", "control 1: -LOCK, the clock, the sample clock, trigger setup, live, DMA write",
             ["ODLOCK", "CLKRUN", "SCLKSEL", "SETUP", "TNOW", "LIVE", "DMAWR", "XCLKEN"]),
            ("POSTLN", "post-trigger count, low", ["PL%d" % i for i in range(8)]),
            ("POSTHN", "post-trigger count, high", ["PL%d" % i for i in range(8, 16)]),
            ("TSALN", "trigger setup address, low", ["TS%d" % i for i in range(8)]),
            ("TSAHN", "trigger setup address, high", ["TS%d" % i for i in range(8, 15)] + [None]))
    for clk, fn, qs in regs:
        x = U("74HCT273", fn)
        w(x, "CLK", clk); w(x, "-MR", "CRESN")
        for i, q in enumerate(qs):
            w(x, "D%d" % i, XD[i])
            if q:
                w(x, "Q%d" % i, q)
            else:
                c.unused(x, "Q%d" % i)

    # ---- the flip-flops the Pico sets: GO, ARM, STEP
    ff = U("74HCT74", "GO (a DMA cycle) and ARM (a capture)")
    w(ff, "-1PRE", "GON"); w(ff, "-1CLR", "GOCLRN"); w(ff, "1D", "GND"); w(ff, "1CLK", "GND"); w(ff, "1Q", "GOQ")
    w(ff, "-2PRE", "ARMN"); w(ff, "-2CLR", "ARMCLRN"); w(ff, "2D", "GND"); w(ff, "2CLK", "GND"); w(ff, "2Q", "ARMQ")
    c.unused(ff, "-1Q", "-2Q")
    ff2 = U("74ACT74", "STEP (one CLK period, alone)")    # (ACT: D73)
    w(ff2, "-1PRE", "STEPN"); w(ff2, "-1CLR", "STEPCLRN"); w(ff2, "1D", "GND"); w(ff2, "1CLK", "GND"); w(ff2, "1Q", "STEPQ")
    w(ff2, "-2PRE", "VCC"); w(ff2, "-2CLR", "VCC"); w(ff2, "2D", "GND"); w(ff2, "2CLK", "GND")
    c.unused(ff2, "-1Q", "2Q", "-2Q")

    # ---- the slices: a latch, an SRAM and a read buffer each; a drive register for the driven ones
    oe = {"D": "DOEN", "BUS": "BUSOEN", "SYS": "SYSOEN", "LR": "LROEN", "J3": "J3OEN"}
    for k, (grp, lines) in enumerate(SLICES):
        sk = ["S%d_%d" % (k, i) for i in range(8)]
        lt = U("74ACT574", "slice %d: sample latch (%s)" % (k, ", ".join(x for x in lines if x != GND_)), "lat%d" % k)
        sr = U("CY7C199", "slice %d: capture SRAM" % k, "sram%d" % k)
        rb = U("74HCT541", "slice %d: onto the local bus" % k)
        w(lt, "CLK", "SCLK"); w(lt, "-OE", "LATOEN")
        w(sr, "-CE", "GND"); w(sr, "-OE", "SROEN"); w(sr, "-WE", "SWE_N")
        for a in range(15):
            w(sr, "A%d" % a, "CA%d" % a)
        w(rb, "-OE1", "RS%dN" % k); w(rb, "-OE2", "GND")
        for i, line in enumerate(lines):
            if line == GND_:
                w(lt, "D%d" % i, "GND")
            else:
                c.net(line, (lt, "D%d" % i))
            w(lt, "Q%d" % i, sk[i]); w(sr, "IO%d" % i, sk[i])
            w(rb, "A%d" % i, sk[i]); w(rb, "Y%d" % i, XD[i])
        if grp:
            dv = U("74HCT574", "slice %d: drive register (%s)" % (k, grp), "drv%d" % k)
            w(dv, "CLK", "WS%dN" % k); w(dv, "-OE", oe[grp])
            for i, line in enumerate(lines):
                w(dv, "D%d" % i, XD[i])
                if line in NOT_DRIVEN:
                    c.unused(dv, "Q%d" % i)
                else:
                    c.net(line, (dv, "Q%d" % i))
    # (a line in no group is listened to only)

    # ---- the capture: the sample clock, the address counter, the write strobe, the post-trigger count
    gal("DBCAP", "the capture", {})
    osc = U("OSC24M", "24 MHz: CLK when alone; halved, the timing samples")
    w(osc, "EN", "VCC"); w(osc, "OUT", "OSC")
    mx = U("74ACT157", "the sample clock (CLK or 12 MHz); the address counter's clock (samples or CSTEP)")
    w(mx, "-E", "GND")
    w(mx, "S", "SCLKSEL")
    # (a 157 has one select: channel 1 the sample clock; the counter's clock below by a gate)
    w(mx, "1A", "CLK"); w(mx, "1B", "T12"); w(mx, "1Y", "SCLK")      # (timing mode: 12 MHz, D73)
    for ch in (2, 3, 4):
        w(mx, "%dA" % ch, "GND"); w(mx, "%dB" % ch, "GND"); c.unused(mx, "%dY" % ch)
    ng = U("74ACT00", "the SRAMs' -WE (the sample clock's low half); the counter's clock")   # (ACT: D73)
    w(ng, "1A", "SCLK"); w(ng, "1B", "SCLK"); w(ng, "1Y", "SCLKN")
    w(ng, "2A", "SWEN"); w(ng, "2B", "SCLKN"); w(ng, "2Y", "SWE_N")
    # the counter's clock: the sample clock while capturing, else CSTEP (its rising edge at the strobe's end)
    w(ng, "3A", "SCLKN"); w(ng, "3B", "ACT"); w(ng, "3Y", "CSA_N")        # NAND(SCLKN, ACT): rises with SCLK
    w(ng, "4A", "CSA_N"); w(ng, "4B", "CSTEPG"); w(ng, "4Y", "CNTCLKN")
    og = U("74HCT32", "CSTEP gated (only when not capturing)")
    w(og, "1A", "CSTEPN"); w(og, "1B", "ACT"); w(og, "1Y", "CSTEPG")          # high unless a CSTEP while idle
    for g in (2, 3, 4):
        w(og, "%dA" % g, "GND"); w(og, "%dB" % g, "GND"); c.unused(og, "%dY" % g)
    inv = U("74ACT04", "the counter clock: the sample clock while capturing, else CSTEP's end")   # (ACT: D73)
    w(inv, "1A", "CNTCLKN"); w(inv, "1Y", "CNTCLK")
    for g in range(2, 7):
        w(inv, "%dA" % g, "GND"); c.unused(inv, "%dY" % g)
    for k in range(4):
        x = U("74F161", "capture address bits %d-%d" % (4 * k, 4 * k + 3), "ca%d" % k)
        w(x, "CLK", "CNTCLK"); w(x, "-CLR", "CAPCLRN"); w(x, "-LOAD", "VCC"); w(x, "ENP", "VCC")
        w(x, "ENT", "VCC" if k == 0 else "CAC%d" % k)
        if k < 3:
            w(x, "RCO", "CAC%d" % (k + 1))
        else:
            c.unused(x, "RCO")
        for i, (d, q) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
            w(x, d, "GND")
            bit = 4 * k + i
            if bit < 15:
                w(x, q, "CA%d" % bit)
            else:
                c.unused(x, q)
    for k in range(4):
        x = U("74F161", "post-trigger count bits %d-%d" % (4 * k, 4 * k + 3), "pc%d" % k)
        w(x, "CLK", "SCLK"); w(x, "-CLR", "VCC"); w(x, "-LOAD", "PLDN"); w(x, "ENP", "PCNT")
        w(x, "ENT", "VCC" if k == 0 else "PCC%d" % k)
        w(x, "RCO", "PCC%d" % (k + 1) if k < 3 else "PDONE")
        for i, (d, q) in enumerate((("A", "QA"), ("B", "QB"), ("C", "QC"), ("D", "QD"))):
            w(x, d, "PL%d" % (4 * k + i)); c.unused(x, q)
    for k, (en, bits) in enumerate((("RCALN", range(0, 8)), ("RCAHN", range(8, 15)))):
        x = U("74HCT541", "the capture address onto the local bus, bits %d-%d" % (bits[0], bits[-1]))
        w(x, "-OE1", en); w(x, "-OE2", "GND")
        for i in range(8):
            if i < len(bits):
                w(x, "A%d" % i, "CA%d" % bits[i])
            else:
                w(x, "A%d" % i, "GND")
            w(x, "Y%d" % i, XD[i])
    st = U("74HCT541", "status onto the local bus")
    w(st, "-OE1", "RSTATN"); w(st, "-OE2", "GND")
    for i, src in enumerate(("DRIVE", "-BGNT", "GOQ", "ACT", "DONE", "ARMQ", "TRIG", "SNAP")):
        c.net(src, (st, "A%d" % i)); w(st, "Y%d" % i, XD[i])

    # ---- the trigger RAMs (A: A16:A2, B: A31:A17), written from the setup address
    gal("DBTRIG", "the trigger", {"MRDN": "-MRD", "MWRN": "-MWR", "IACKN": "-IACK"})
    for m in range(8):
        x = U("74HCT157", "trigger RAM address: the bus or the setup address")
        w(x, "S", "SETUP"); w(x, "-E", "GND")
        for ch in range(1, 5):
            j = 4 * m + ch - 1
            if j < 30:
                ram, bit = ("TRA", j) if j < 15 else ("TRB", j - 15)
                c.net("A%d" % (2 + j), (x, "%dA" % ch))
                w(x, "%dB" % ch, "TS%d" % bit); w(x, "%dY" % ch, "%s%d" % (ram, bit))
            else:
                w(x, "%dA" % ch, "GND"); w(x, "%dB" % ch, "GND"); c.unused(x, "%dY" % ch)
    for ram, we in (("TRA", "TWAN"), ("TRB", "TWBN")):
        x = U("CY7C199", "trigger RAM %s: 8 patterns' bits for A%s" % (ram[-1], "16:2" if ram == "TRA" else "31:17"), ram.lower())
        for a in range(15):
            w(x, "A%d" % a, "%s%d" % (ram, a))
        w(x, "-CE", "GND"); w(x, "-OE", "SETUP"); w(x, "-WE", we)
        bf = U("74HCT541", "trigger RAM %s's write data (setup)" % ram[-1])
        w(bf, "-OE1", "SETUPN"); w(bf, "-OE2", "GND")
        for i in range(8):
            w(x, "IO%d" % i, "%sD%d" % (ram, i)); w(bf, "A%d" % i, XD[i]); w(bf, "Y%d" % i, "%sD%d" % (ram, i))
    for k in range(2):
        x = U("74HCT08", "a pattern matches: RAM A's bit and RAM B's (%d-%d)" % (4 * k, 4 * k + 3))
        for g in range(1, 5):
            i = 4 * k + g - 1
            w(x, "%dA" % g, "TRAD%d" % i); w(x, "%dB" % g, "TRBD%d" % i); w(x, "%dY" % g, "M%d" % i)

    # ---- the DMA cycle, the drive enables, the clock when alone
    gal("DBSEQ", "the DMA cycle", {"BGNTN": "-BGNT", "WAITN": "-WAIT"})
    gal("DBDRV", "the drive enables (none in LISTEN)", {})
    gal("DBCLK", "CLK when alone; XCLK, the machine's clock from the host (D76)",
        {"CLKO": "CLK", "CLKON": "-CLK", "XCK": "XCLK", "XCLKENN": "-XCLKEN"})
    a8 = U("74HCT08", "-ACK, -WAIT, -LOCK: only in DRIVE")
    for g, (x, y) in enumerate((("ODACK", "ACKOD"), ("ODWAIT", "WAITOD"), ("ODLOCK", "LOCKOD")), 1):
        w(a8, "%dA" % g, x); w(a8, "%dB" % g, "DRIVE"); w(a8, "%dY" % g, y)
    w(a8, "4A", "GND"); w(a8, "4B", "GND"); c.unused(a8, "4Y")
    od = U("74HC05", "open drain: -ACK, -WAIT, -LOCK, -BREQ (slot 0's); the mode LEDs")
    for g, (x, y) in enumerate((("ACKOD", "-ACK"), ("WAITOD", "-WAIT"), ("LOCKOD", "-LOCK"), ("BREQOD", "-BREQ"),
                                ("DRIVE", "LEDDRV_K"), ("LISTEN", "LEDLSN_K")), 1):
        w(od, "%dA" % g, x); c.net(y, (od, "%dY" % g))

    # ---- the front edge: the mode switch and its LEDs, labelled
    c.part("S1", "SW_SPDT_RA", "mode switch, front edge: LISTEN / DRIVE (labelled at each end)")
    w("S1", "1", "GND"); w("S1", "2", "DRIVE"); w("S1", "3", "VCC")
    for i, (fn, k) in enumerate((("DRIVE (red): the card may drive the bus", "LEDDRV_K"),
                                 ("LISTEN (green): every driver off", "LEDLSN_K")), 2):
        c.part("D%d" % i, "LED", fn)
        R("470", "LEDA%d" % i, "VCC", "%s LED" % fn.split()[0])
        w("D%d" % i, "A", "LEDA%d" % i); w("D%d" % i, "K", k)
    c.finish()
    # the layout (D80): the mode switch and the Pico (its USB port) at the
    # front edge; the local bus decode, the control registers and the host
    # link beside the Pico; two capture slices a row (sample latch, SRAM,
    # onto the local bus, drive register), by the connector their signals
    # come from: slices 0-8 (D, A) by J1, 9-10 and 18-21 by J2, 11-17 (the
    # local link) by J3; the capture and post-trigger counters with them;
    # the trigger last
    c.front = ["A1", "S1"]
    c.front_rot = {"A1": 270}
    c.front_edge = {"S1": ("crt", 0.0), "A1": ("crt", 0.5)}
    c.led_label = {"D2": "DRIVE", "D3": "LISTEN"}
    c.panel = {"S1": ["MODE", "LISTEN / DRIVE"]}                # (which end is which: to check on the part)
    us = lambda a, b: ["U%d" % i for i in range(a, b + 1)]
    sl = lambda k: us(18 + 4 * k, 21 + 4 * k) if k < 18 else us(90 + 3 * (k - 18), 92 + 3 * (k - 18))
    # a row of two slices: the parts on the bus side (the sample latches, the
    # drive registers) at the left, by the connectors, the capture side (the
    # SRAMs, the drivers onto the local bus) at the right, in columns, so the
    # capture address and the local bus run straight down the card
    two = lambda a, b: ([sl(a)[0]] + sl(a)[3:] + [sl(b)[0]] + sl(b)[3:] +
                        [sl(a)[1], sl(b)[1], sl(a)[2], sl(b)[2]])
    c.place_free, c.place_spread, c.place_right = True, True, 316.0
    c.place_swaps = "row"
    c.place_rows = [
        ["U2", "U3", "U4", "U5", "U6", "U1"],
        us(7, 13),
        ["U14", "U15", "U16", "U17", "U102", "U119", "U134"],
        us(103, 107) + ["R1", "R2", "R3", "D1"],
        two(0, 1), two(2, 3), two(4, 5), two(6, 7),
        [sl(8)[0], sl(8)[3]] + us(108, 111) + [sl(8)[1], sl(8)[2]],
        two(9, 10), two(18, 19), two(20, 21) + ["U137", "U138"],
        two(11, 12), two(13, 14), two(15, 16),
        [sl(17)[0], sl(17)[3]] + us(112, 115) + [sl(17)[1], sl(17)[2]],
        us(120, 127) + ["U132", "U133"],
        ["U128", "U129", "U130", "U131", "U135", "U136"],
        us(116, 118) + ["C139"],
    ]
    # the schematic's sheets (generators/p32sheets.py): a sheet a function
    c.sheets = sheets(c, [
        ("Host link: Pico 2, the local bus decode", [["A1", "D1", "U1", "R1", "R2", "R3"], us(2, 9)]),
        ("Control registers, the mode switch", [us(10, 17), ["S1", "D2", "R4", "D3", "R5"]]),
        ("Capture slices 0-3: D0-D31", [sl(0) + sl(1), sl(2) + sl(3)]),
        ("Capture slices 4-7: A2-A31, BE0-BE1", [sl(4) + sl(5), sl(6) + sl(7)]),
        ("Capture slices 8-11", [sl(8) + sl(9), sl(10) + sl(11)]),
        ("Capture slices 12-15", [sl(12) + sl(13), sl(14) + sl(15)]),
        ("Capture slices 16-21", [sl(16) + sl(17), sl(18) + sl(19), sl(20) + sl(21)]),
        ("Capture: sample clock, address and counters", [us(102, 107), us(108, 115), us(116, 118)]),
        ("The trigger", [["U119"], us(120, 127), us(128, 133)]),
        ("The DMA cycle and the drivers", us(134, 138)),
    ])
    CARDS[c.name] = c
    return c, G, role

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("debug card: %d errors" % len(errs))
    print("debug card OK: %d ICs (%d GALs), %d nets, %d parts; %d lines in %d slices"
          % (len(ics), len(G), len(c.nets), len(c.parts), len(LINES), NSLICE))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
