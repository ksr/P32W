#!/usr/bin/env python3
"""The P32W video card, revision 2 (any of slots 3-10): docs/video-card-acrtc.md;
D103, D104, D106; D50, D52, D53, D57.

Hitachi's HD63484 ACRTC draws VGA, 640 x 480 at 60 Hz, one bit a dot, into a
frame buffer of two 128K x 8 RAMs on its own memory bus; the CPU sends it
commands. Two PS/2 ports through an ATmega328P. The card is YACC1-D's video
card V3 on P32W's bus:

  * THE KEPT PART: 35 ICs (the chip; the dot counter, the memory control and
    the control register; the address latches, RAMs, buffers and font; the
    shift registers; text method A's eight sockets), with the oscillator, the
    DE-15 and their passives. Transcribed from YACC1-D's v30_netlist.py, of
    which hardware/video-card/yacc1-v3/ holds a copy, pin number for pin
    number (k(), below, takes that file's own "IC6.14"), under that file's net
    names. kept_check() proves the two equal: every part, every pin, every
    net, but for the seam's listed differences; and that nothing of the bus
    side is on a kept net. That part's margins belong to those parts wired
    that way (its timing check, YACC1-D's tests/video/v30_netsim.py): a part
    changed or a gate moved into a GAL is a new circuit to that check.
  * THE BUS SIDE, new: the register page only, the top 256 bytes of the
    device window the jumpers choose (window 5 by default: FFFF_DF00):

      +00  halfword  read: the chip's status      write: its address register
      +04  halfword  the register addressed, or the FIFOs
      +08  byte      read: card status (bit 0 FULL, 1 SRC, 2 BUSY, 3 TEXT,
                     4 the chip's IRQ)            write: a PS/2 command
                     (one wait)
      +0C  byte      read: PS/2 data (clears FULL)
      +F0 / +F1      card ID 3 / revision 2
      +F4  word      interrupt status: bit 0 PS/2, bit 1 the chip

    Anything else, or another size, gets no -ACK: a bus error. There is no
    video memory on the bus.

The chip's cycle (docs/video-card-acrtc.md 4.2): four clocks, three of them
waits, counted by GAL VWT on CLK. RS and R/W are registers that take A2 and
the direction as clock 1 ends and then hold. CS is a flip-flop on -CLK (the
middle of a clock), cleared by RUN (a register: clocks 2-4): low from the
middle of clock 2, to the middle of clock 4 for a write (two clocks: both
edges out of one flip-flop from one clock edge), to the end of clock 4 for a
read. Its twin opens the two data buffers from the middle of clock 2 to the
cycle's end. Neither can have a runt pulse: no decode reaches them.

The chip's 16 data lines HD0-15 are also the card's own data bus: GAL VRD
puts the status, the ID, the interrupt status and the vector there, the
PS/2 data latch its byte; the two 74HCT245s are the one load on D0-D15.
The PS/2 strobes are registers of GAL VWT, never a decode: FULL's clear is
a pulse in the clock after a read of +0C; a write of +08 takes two clocks
(one wait), and the command latch takes the byte as the second begins, the
master holding it a whole clock either side.

Interrupts (D52, D57): device 0 the PS/2 section (FULL, with the firmware's
INTREQ), device 1 the chip (its IRQ); each has its level jumpers and a
74LS145 that pulls that -IRQ line. In the acknowledge cycle a 74F138 gives
the level, GAL VINT matches it against each device's jumpers, GAL VRD puts
the vector, window x 16 + the device, on D7:0.

    python3 card_video.py [--bom]
"""
import os, sys, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32sheets import sheets  # noqa: E402
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from p32gal import minimize, spec                             # noqa: E402
from p32bus import SIGNALS                                    # noqa: E402
from card_cpu import GB, num                                  # noqa: E402

WINDOW, LEVEL = 5, 4                    # the default jumpers (D52): window 5, both devices on level 4
CARD_ID, CARD_REV = 3, 2
CHIP, STAT, DATA, CID, CISR = (0x00, 0x04), 0x08, 0x0C, 0xF0, 0xF4      # the register page (p32io.h)
BA = ["BA%d" % i for i in range(2, 8)]
BBE = ["BBE%d" % k for k in range(4)]
WINJ = ["WIN0", "WIN1", "WIN2"]
LV = lambda d: ["L%d_%d" % (d, i) for i in range(3)]          # device d's level jumpers
ACKL = ["ACK%dN" % k for k in range(8)]                        # the level acknowledged, one low (a 74F138)
ST = ["S0", "S1"]                                              # the chip's cycle: 0 clock 1 (or idle), 1-3 clocks 2-4
B0, B1, HALF, WORD = 0b0001, 0b0010, 0b0011, 0b1111           # the byte enables of a byte on lane 0, on lane 1, ...

# the copy of YACC1-D's netlist that the kept part is checked against
REF_DIR = os.path.join(os.path.dirname(HERE), "hardware", "video-card", "yacc1-v3")
REF_SRC = os.path.expanduser("~/Developer/YACC1-D/hardware/cards/video/kicad/v3.0/v30_netlist.py")

# ---------------------------------------------------------------- the GALs
def reg(v):
    """The register a cycle in the card's page asks for, by A7:A2, the byte
    enables and the direction: "chip", "stat", "cmd", "data", "id", "rev",
    "isr"; or None: no -ACK, a bus error (as p32io.h's vid_read, vid_write
    and card_reg)."""
    o, e, rd, wr = num(v, BA) << 2, num(v, BBE), v["CRD"], v["CWR"]
    if not (rd or wr):
        return None
    if o in CHIP and e == HALF:
        return "chip"
    if o == STAT and e == B0:
        return "stat" if rd else "cmd"
    if rd and o == DATA and e == B0:
        return "data"
    if rd and o == CID and e in (B0, B1):
        return "id" if e == B0 else "rev"
    if rd and o == CISR and e == WORD:
        return "isr"
    return None

def any_of(deps, f, *more):
    """A sum of products: f over deps, minimized, or any of the signals in more."""
    return minimize(spec(deps, f)) + [[(m, True)] for m in more]

def gal_vsel():
    """The card's page (A31:A16 all ones by two 74HCT688s, A15 = 1, A14:A12
    the window jumpers, A11:A8 = F) and the cycle: CRD a read, CWR a write.
    And the revision's bit: HD9 high when +F0 / +F1 are read (the lane's
    buffer puts it on D9 only when +F1 is asked for)."""
    A = ["A%d" % i for i in range(8, 16)]
    b = GB("VSEL", "video card: the page; the revision's bit").inp("HI1N", "HI2N", *A).inp(*WINJ)
    b.inp("MRDN", "MWRN", "BA2", "BA7")
    mine = lambda v: (not v["HI1N"] and not v["HI2N"] and num(v, A[:4]) == 15 and v["A15"]
                      and num(v, ["A12", "A13", "A14"]) == num(v, WINJ))
    deps = ["HI1N", "HI2N"] + A + WINJ
    b.com("CRD", deps + ["MRDN"], lambda v: mine(v) and not v["MRDN"])
    b.com("CWR", deps + ["MWRN"], lambda v: mine(v) and not v["MWRN"])
    b.tri("HD9", [], lambda v: CARD_REV >> 1 & 1, "CRD & BA7 & !BA2")
    return b.build()

def gal_vdec():
    """Which register (reg(), above), -ACK, and who is on the card's data
    bus HD: CHIP (the chip's cycle: GAL VWT), CMDW (a PS/2 command written),
    PDOEN (the PS/2 data latch onto HD), RDOE (GAL VRD onto HD: a register
    of its own, or the vector), ZON (zeros onto D16-D31: the interrupt
    status is a word), LE0N / LE1N (lanes 0 and 1's buffers: the lanes the
    register is on; both for the chip, while BH; lane 0 for the vector)."""
    D = BA + BBE + ["CRD", "CWR"]
    b = GB("VDEC", "video card: which register, the lanes, -ACK").inp(*D).inp("VHIT", "BH")
    b.com("CHIP", D, lambda v: reg(v) == "chip")
    b.com("CMDW", D, lambda v: reg(v) == "cmd")
    b.com("PDOEN", D, lambda v: reg(v) != "data", inverted=True)
    own = lambda v: reg(v) in ("stat", "id", "isr")
    b.com("RDOE", D + ["VHIT"], lambda v: bool(v["VHIT"]) or own(v), eq=any_of(D, own, "VHIT"))
    b.com("ACK", D + ["VHIT"], lambda v: bool(v["VHIT"]) or reg(v) is not None,
          eq=any_of(D, lambda v: reg(v) is not None, "VHIT"))
    b.com("ZON", D, lambda v: reg(v) != "isr", inverted=True)
    l0 = lambda v: reg(v) in ("stat", "cmd", "data", "id", "isr")
    l1 = lambda v: reg(v) in ("rev", "isr")
    b.com("LE0N", D + ["VHIT", "BH"], lambda v: not (v["BH"] or v["VHIT"] or l0(v)), eq=any_of(D, l0, "BH", "VHIT"),
          inverted=True)
    b.com("LE1N", D + ["BH"], lambda v: not (v["BH"] or l1(v)), eq=any_of(D, l1, "BH"), inverted=True)
    return b.build()

def gal_vwt():
    """The chip's cycle, on CLK (reset by -RES): S counts the clocks (0 the
    first, or idle; 1-3 the waits' end: clocks 2-4), RUN is a register that
    is high in clocks 2-4 (it clears the CS flip-flop and its twin, so it
    must not be a decode), -WAIT through clocks 1-3. RS and RW take A2 and
    the direction as clock 1 ends and hold until the next access's clock 1
    ends. CSD is what the CS flip-flop takes in the middle of a clock: off
    in the middle of clock 4 for a write. The PS/2 strobes, each a register,
    a clock long after the cycle: FCLRN (low: FULL cleared; low from reset
    too) in the clock after a read of +0C. A write of +08 takes two clocks:
    -WAIT through the first, and CMDQ high in the second; its rise, as the
    second begins, clocks the command latch and sets BUSY, with the master
    holding the byte a whole clock before and after. DTACKN, the chip's
    DTACK, is wired and not used (D104)."""
    b = GB("VWT", "video card: the chip's cycle, -WAIT, the PS/2 strobes", clock="BCLK", reset="BRESN")
    b.inp("CHIP", "BA2", "CWR", "CMDW", "PDOEN", "DTACKN", "BRESN")
    s = lambda v: num(v, ST)
    nxt = lambda v: (s(v) + 1) & 3 if v["CHIP"] else 0
    first = lambda v: v["CHIP"] and s(v) == 0
    b.reg("S0", ["CHIP"] + ST, lambda v: nxt(v) & 1)
    b.reg("S1", ["CHIP"] + ST, lambda v: nxt(v) >> 1)
    b.reg("RUN", ["CHIP"] + ST, lambda v: nxt(v) != 0)
    b.reg("RS", ["CHIP", "BA2", "RS"] + ST, lambda v: v["BA2"] if first(v) else v["RS"])
    b.reg("RW", ["CHIP", "CWR", "RW"] + ST, lambda v: (not v["CWR"]) if first(v) else v["RW"], reset_high=True)
    b.com("WAIT", ["CHIP", "CMDW", "CMDQ"] + ST, lambda v: (v["CHIP"] and s(v) != 3) or (v["CMDW"] and not v["CMDQ"]))
    b.com("CSD", ["RW"] + ST, lambda v: v["RW"] or s(v) != 3)
    b.reg("FCLRN", ["PDOEN"], lambda v: v["PDOEN"])
    b.reg("CMDQ", ["CMDW", "CMDQ"], lambda v: v["CMDW"] and not v["CMDQ"])
    return b.build()

def gal_vrd():
    """Onto HD7:0 while RDOE: the vector in the acknowledge cycle (VHIT:
    window x 16 + the device); else the card status (+08: FULL, SRC with
    FULL, BUSY, TEXT, the chip's IRQ), the card ID (+F0: 3), the interrupt
    status (+F4: bit 0 the PS/2 section asks, bit 1 the chip). BA7 and BA2
    tell those three apart."""
    b = GB("VRD", "video card: status, ID, interrupt status, the vector")
    b.inp("RDOE", "VHIT", "BA2", "BA7", "FULL", "SRC", "BUSY", "TEXTN", "IRQN", "PS2ASKN", *WINJ).inp("VDEV")
    def bit(vec, stat, ident, isr):
        return lambda v: vec(v) if v["VHIT"] else stat(v) if not v["BA7"] else ident if not v["BA2"] else isr(v)
    zero = lambda v: 0
    S = ["VHIT", "BA2", "BA7"]
    bits = [(S + ["VDEV", "FULL", "PS2ASKN"], bit(lambda v: v["VDEV"], lambda v: v["FULL"], CARD_ID & 1, lambda v: not v["PS2ASKN"])),
            (S + ["FULL", "SRC", "IRQN"], bit(zero, lambda v: v["FULL"] and v["SRC"], CARD_ID >> 1 & 1, lambda v: not v["IRQN"])),
            (S + ["BUSY"], bit(zero, lambda v: v["BUSY"], CARD_ID >> 2 & 1, zero)),
            (S + ["TEXTN"], bit(zero, lambda v: not v["TEXTN"], CARD_ID >> 3 & 1, zero)),
            (S + ["WIN0", "IRQN"], bit(lambda v: v["WIN0"], lambda v: not v["IRQN"], CARD_ID >> 4 & 1, zero)),
            (S + ["WIN1"], bit(lambda v: v["WIN1"], zero, CARD_ID >> 5 & 1, zero)),
            (S + ["WIN2"], bit(lambda v: v["WIN2"], zero, CARD_ID >> 6 & 1, zero)),
            (S, bit(zero, zero, CARD_ID >> 7 & 1, zero))]
    for i, (deps, f) in enumerate(bits):
        b.tri("HD%d" % i, deps, f, "RDOE")
    return b.build()

def gal_vint():
    """The interrupts. Device 0, the PS/2 section, asks while FULL with the
    firmware's INTREQ (PS2ASKN: to its 74LS145 and GAL VRD); device 1, the
    chip, while its IRQ is low. In the acknowledge cycle (one ACKkN low: a
    74F138 on A4:A2, enabled by -IACK): M0, device 0 asks on the level
    acknowledged (its jumpers); VHIT, either does (the card answers); VDEV,
    the vector's device: 0 if device 0 matches, else 1."""
    b = GB("VINT", "video card: who asks, the acknowledge cycle").inp(*ACKL).inp("INTREQ", "FULL", *LV(0)).inp("IRQN", *LV(1))
    asks = (lambda v: v["INTREQ"] and v["FULL"], lambda v: not v["IRQN"])
    lit = ("INTREQ & FULL", "!IRQN")
    def terms(d):
        return ["%s & %s & !ACK%dN" % (lit[d], " & ".join(("" if k >> i & 1 else "!") + LV(d)[i] for i in range(3)), k)
                for k in range(8)]
    def m(d):
        def f(v):
            if sum(1 for a in ACKL if not v[a]) > 1:
                return None                 # (the 74F138 pulls one line low, or none)
            return bool(asks[d](v)) and not v["ACK%dN" % num(v, LV(d))]
        return f
    D0, D1 = ACKL + ["INTREQ", "FULL"] + LV(0), ["IRQN"] + LV(1)
    def either(v):
        a, c = m(0)(v), m(1)(v)
        return None if a is None else a or c
    b.com("M0", D0, m(0), eq=" | ".join(terms(0)))
    b.com("VHIT", D0 + D1, either, eq=" | ".join(terms(0) + terms(1)))
    b.com("VDEV", ["M0"], lambda v: not v["M0"], eq="!M0")
    b.com("PS2ASKN", ["INTREQ", "FULL"], lambda v: not (v["INTREQ"] and v["FULL"]))
    return b.build()

_GALS = None
def gals():
    global _GALS
    if _GALS is None:
        _GALS = {}
        for f in (gal_vsel, gal_vdec, gal_vwt, gal_vrd, gal_vint):
            g = f()
            _GALS[g.name] = g
    return _GALS

# ---------------------------------------------------------------- the kept part's parts
# YACC1-D's reference -> (P32W device, the part's value there, what it does). The value is the part the timing
# check was run with: kept_check() compares it.
KEPT_ICS = [
    ("IC1", "HD63484", "HD63484P8", "the ACRTC: the raster, the drawing, the frame buffer's cycles"),
    ("OSC1", "OSC25M", None, "the dot clock: 25.175 MHz (VGA 640 x 480 at 60 Hz), in a socket"),
    ("IC6", "74F163", "74F163A", "dot counter: QB = 2CLK, QC follows MCYC (loaded in dots 3 and 7)"),
    ("IC7", "74F74", "74F74", "A: P56, dots 5 and 6 of every cycle; B: VALID, AS came"),
    ("IC16", "74ACT04", "74ACT04", "NOT AS, MCYC, QB, QC, MA19, DISP1"),
    ("IC17", "74ACT02", "74ACT02", "W = NOR(DRAW, MRD): a drawing write; the three guns' gates"),
    ("IC18", "74ACT10", "74ACT10", "the P56 flip-flop's D; the memories' -OE; the blanked dot"),
    ("IC19", "74ACT11", "74ACT11", "a Background cycle; a Window cycle; one spare"),
    ("IC20", "74ACT32", "74ACT32", "the font copy's enable; graphics OR text; a read's buffers wait; not a drawing write"),
    ("IC21", "74ACT08", "74ACT08", "the control register's clock; display on AND a read; the buffers' enable held; the RAMs' -CE"),
    ("IC37", "74F74", "74F74", "A: the MAD buffers' enable; B: the dot retimed to the dot clock"),
    ("IC38", "74F21", "74F21", "the buffers' enable flip-flop's D; the graphics load"),
    ("IC31", "74ACT00", "74ACT00", "the counter's -LOAD; NOT DRAW; the RAM's -WE; the mixed dot"),
    ("IC32", "74ACT86", "74ACT86", "the two syncs' polarity; NOT CHR; NOT MRD"),
    ("IC34", "74ACT273", "74ACT273", "MAD0-7 as dot 7 begins: before HSYNC rises, the attribute byte"),
    ("IC35", "74ACT273", "74ACT273", "the control register: the attribute byte, taken when HSYNC rises"),
    ("IC36", "74ACT32", "74ACT32", "CHR into the address (a character screen: the RAM's top 64K words); the font's -CE"),
    ("IC40", "74F299", "74F299", "the graphics dot delayed 8 dots, into step with the text"),
    ("IC8", "74ACT573", "74ACT573", "address latch VA0-7 (open while AS is low)"),
    ("IC9", "74ACT573", "74ACT573", "address latch VA8-15"),
    ("IC10", "AS6C1008", "AS6C1008-55PCN", "frame buffer, the word's low byte (128K; the 512K part fits)"),
    ("IC11", "AS6C1008", "AS6C1008-55PCN", "frame buffer, the word's high byte"),
    ("IC12", "74ACT245", "74ACT245", "RAM data low byte <-> MAD0-7 (drawing cycles)"),
    ("IC13", "74ACT245", "74ACT245", "RAM data high byte <-> MAD8-15"),
    ("IC14", "27C256", "27C256", "the font, where MA19 is high (video/font-acrtc.bin)"),
    ("IC15", "74HCT244", "74HCT244", "the font's byte onto the high half of the word"),
    ("IC22", "74F299", "74F299", "shift register, dots 8-15 of the word"),
    ("IC23", "74F299", "74F299", "shift register, dots 0-7; Q7 is the video"),
]
TEXT_A = [                              # text method A: sockets, empty on the first build (D104)
    ("IC41", "74F74", "74F74", "A: the character latches' clock; B: WPH, the cycle after a Window cycle"),
    ("IC42", "74F21", "74F21", "dot 3 of a Window cycle; the text shifter's load"),
    ("IC43", "74ACT32", "74ACT32", "a Window cycle or the one after it"),
    ("IC44", "74ACT574", "74ACT574", "the left character's code (the word's low byte)"),
    ("IC45", "74ACT574", "74ACT574", "the right character's code (the high byte)"),
    ("IC46", "74ACT574", "74ACT574", "the raster line RA0-RA4 of the Window cycle"),
    ("IC47", "27C256", "AT27C256R-70", "character generator, 70 ns: line + 16 x code; A12 CUD1, A13 the second font, A14 CUD2"),
    ("IC48", "74F299", "74F299", "the text's dots, 8 a character"),
]
KEPT_REST = [                           # (YACC1-D's reference, device, value, function)
    ("R1", "R", "10k", "the chip's DONE pull-up"),
    ("R18", "R", "1k", "PU: the F parts' unused inputs high"),
    ("R19", "R", "10k", "TDOT pull-down: no text while method A's parts are not fitted"),
    ("R2", "R", "470", "video to R (0.7 V into 75 ohms)"), ("R3", "R", "470", "video to G"), ("R4", "R", "470", "video to B"),
    ("R5", "R", "68", "HSYNC series"), ("R6", "R", "68", "VSYNC series"),
    ("RN1", "RNET9", "10k", "MAD0-7 pull-ups (MAD floats in a display cycle's data half)"),
    ("RN2", "RNET9", "10k", "MAD8-15 pull-ups"),
    ("RN3", "RNET9", "10k", "RAM data low byte pull-ups (undriven at the start of a write cycle)"),
    ("RN4", "RNET9", "10k", "RAM data high byte pull-ups"),
    ("JP4", "JUMPER3", None, "RAM 128K / 512K: the RAMs' pin 30: 1-2 CE2 = VALID (128K), 2-3 A17 (512K)"),
    ("J1", "HD15F_RA", None, "VGA (DE-15) at the front edge, its opening facing out, labelled VGA"),
]
# YACC1-D's parts that are not kept: its bus side and connector, its PS/2 section (P32W's is wired new around the
# same ATmega328P and firmware), its power parts (Card.finish() makes P32W's)
NOT_KEPT = (["X1", "IC2", "IC3", "IC4", "IC5", "IC39"] + ["IC%d" % n for n in range(24, 31)] +
            ["Y1", "J2", "J3", "F1", "J4", "R7", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "C53", "C54", "C55",
             "R16", "R17", "LED1", "RN5", "JP1", "JP2", "JP3", "C1"])
# the seam: the kept pins whose net differs from YACC1-D's, each with P32W's net and the reason
SEAM = dict(
    [(("IC1", "9"), ("DACK", "DACK is pulled high, not tied to RES: the chip's 16-bit bus (D104)")),
     (("IC1", "10"), ("DTACK#", "DTACK has a pull-up, a test point and a spare input of GAL VWT (D104)")),
     (("OSC1", "1"), ("VCC", "the oscillator's pin 1 (no connection, or an enable) is tied high, as on P32W's other cards"))] +
    [(("IC1", str(25 + i)), ("HD%d" % (8 + i), "the chip's D8-D15: the 16-bit bus (D104)")) for i in range(8)])
# P32W's nets that carry a kept pin and reach the bus side: the whole of the seam
SEAM_NETS = {"RS", "RWL", "CS#", "RESB#", "IRQ#", "DACK", "DTACK#"} | {"HD%d" % i for i in range(16)}

# ---------------------------------------------------------------- the netlist
def build():
    G = gals()
    c = Card("video-card", "P32W VIDEO CARD")
    role, kept, groups = {}, {}, {}
    cur = [None]
    def group(title):
        cur[0] = groups.setdefault(title, [])
    def part(ref, dev, function, value=None):
        c.part(ref, dev, function, value=value)
        cur[0].append(ref)
        return ref
    n = [0]
    def U(dev, function, r=None, value=None):
        n[0] += 1
        ref = part("U%d" % n[0], dev, function, value)
        if r:
            role[r] = ref
        return ref
    def w(ref, pin, net):
        c.net(net, (ref, pin))
    def gal(name, function, netmap=None):
        g = G[name]
        ref = U(gal_device(g), function, name)
        for p, (num_, t) in DEV[c.parts[ref][0]]["pins"].items():
            if t in ("pwr", "gnd"):
                continue
            if p.startswith("NC") and t == "pas":
                c.unused(ref, p)
                continue
            w(ref, p, (netmap or {}).get(p, p))
        return ref
    def gnd_unused(ref, ins, outs):
        for p in ins:
            w(ref, p, "GND")
        c.unused(ref, *outs)
    cnt = {}
    def P(prefix, dev, function, value=None, first=1):
        cnt[prefix] = cnt.get(prefix, first - 1) + 1
        return part("%s%d" % (prefix, cnt[prefix]), dev, function, value)
    def R(value, a, b_, function):
        ref = P("R", "R", function, value)
        w(ref, "1", a); w(ref, "2", b_)
        return ref
    def RN(function, rail, nets):
        ref = P("RN", "RNET9", function, "10k x 8 bussed")
        w(ref, "COM", rail)
        for j, net in enumerate(nets):
            w(ref, "R%d" % (j + 1), net)
        c.unused(ref, *["R%d" % k for k in range(len(nets) + 1, 9)])
        return ref
    # the kept part, as YACC1-D's netlist writes it: k("QA", "IC6.14", "IC31.1") puts those pins, by their
    # numbers, on net QA; knc("IC6", 11, 15) leaves pins open
    pinname = lambda ref: {num_: name for name, (num_, t) in DEV[c.parts[ref][0]]["pins"].items()}
    def k(net, *pins):
        for p in pins:
            ref, pin = p.rsplit(".", 1)
            w(kept[ref], pinname(kept[ref])[pin], net)
    def knc(ref, *pins):
        c.unused(kept[ref], *[pinname(kept[ref])[str(p)] for p in pins])
    table = {ref: (dev, value, fn) for ref, dev, value, fn in KEPT_ICS + TEXT_A + KEPT_REST}
    def K(refs):
        for ref in refs.split():
            dev, value, fn = table[ref]
            if ref.startswith(("IC", "OSC")):
                kept[ref] = U(dev, fn, value=value)
            else:
                prefix = ref.rstrip("0123456789")
                kept[ref] = P(prefix, dev, fn, {"RNET9": "10k x 8 bussed"}.get(dev, value), first={"J": 4, "JP": 11}.get(prefix, 1))
    HD = ["HD%d" % i for i in range(16)]

    # ---- the bus interface: one load a line
    group("Bus interface: buffers, the page, which register")
    b1 = U("74HCT541", "bus buffer: A2-A7, BE0, BE1")
    b2 = U("74HCT541", "bus buffer: BE2, BE3, -MRD, -MWR, -IACK, CLK, -CLK, -RES")
    for x, pairs in ((b1, [("A%d" % i, "BA%d" % i) for i in range(2, 8)] + [("BE0", "BBE0"), ("BE1", "BBE1")]),
                     (b2, [("BE2", "BBE2"), ("BE3", "BBE3"), ("-MRD", "BMRDN"), ("-MWR", "BMWRN"), ("-IACK", "BIACKN"),
                           ("CLK", "BCLK"), ("-CLK", "BCLKN"), ("-RES", "RESB#")])):
        w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
        for j, (src, dst) in enumerate(pairs):
            w(x, "A%d" % j, src); w(x, "Y%d" % j, dst)
    for lo, out in ((16, "HI1N"), (24, "HI2N")):
        x = U("74HCT688", "A%d-A%d all ones" % (lo, lo + 7))
        w(x, "-G", "GND"); w(x, "-P=Q", out)
        for i in range(8):
            w(x, "P%d" % i, "A%d" % (lo + i)); w(x, "Q%d" % i, "VCC")
    gal("VSEL", "the page; the revision's bit", {"MRDN": "BMRDN", "MWRN": "BMWRN"})
    gal("VDEC", "which register, the lanes, -ACK")
    od = U("74HC05", "open drain: -ACK, -WAIT; the ATmega's reset")
    w(od, "1A", "ACK"); w(od, "1Y", "-ACK"); w(od, "2A", "WAIT"); w(od, "2Y", "-WAIT")
    w(od, "3A", "RESB#"); w(od, "3Y", "RESETH"); w(od, "4A", "RESETH"); w(od, "4Y", "MRST")
    gnd_unused(od, ["5A", "6A"], ["5Y", "6Y"])

    # ---- the chip's cycle and the card's data bus
    group("The chip's cycle and the data lanes")
    gal("VWT", "the chip's cycle, -WAIT, the PS/2 strobes", {"BRESN": "RESB#", "RW": "RWL", "DTACKN": "DTACK#"})
    x = U("74ACT74", "on -CLK, cleared by RUN: CS (low from mid-clock 2); BH, the data buffers on (to the cycle's end)", "cs")
    w(x, "1D", "CSD"); w(x, "1CLK", "BCLKN"); w(x, "-1PRE", "VCC"); w(x, "-1CLR", "RUN"); w(x, "-1Q", "CS#")
    w(x, "2D", "VCC"); w(x, "2CLK", "BCLKN"); w(x, "-2PRE", "VCC"); w(x, "-2CLR", "RUN"); w(x, "2Q", "BH")
    c.unused(x, "1Q", "-2Q")
    for lane in range(2):
        x = U("74HCT245", "lane %d: D%d-D%d <-> HD%d-HD%d (the chip's and the card's data)"
              % (lane, 8 * lane, 8 * lane + 7, 8 * lane, 8 * lane + 7))
        w(x, "DIR", "CWR"); w(x, "-OE", "LE%dN" % lane)
        for i in range(8):
            w(x, "A%d" % (i + 1), "D%d" % (8 * lane + i)); w(x, "B%d" % (i + 1), HD[8 * lane + i])
    for lo in (16, 24):
        x = U("74HCT541", "zeros onto D%d-D%d when the interrupt status is read (a word)" % (lo, lo + 7))
        w(x, "-OE1", "ZON"); w(x, "-OE2", "GND")
        for i in range(8):
            w(x, "A%d" % i, "GND"); w(x, "Y%d" % i, "D%d" % (lo + i))
    gal("VRD", "status, ID, interrupt status, the vector", {"IRQN": "IRQ#"})
    RN("HD0-7 pull-downs (nothing drives them between accesses)", "GND", HD[:8])
    RN("HD8-15 pull-downs (the revision and the interrupt status read 0 in every bit not driven)", "GND", HD[8:])

    # ---- the chip (kept), and its host pins (the seam)
    group("The ACRTC and its host pins")
    K("IC1 R1")
    R("10k", "DACK", "VCC", "the chip's DACK high: its 16-bit bus")
    R("4.7k", "IRQ#", "VCC", "the chip's IRQ pull-up (open drain)")
    R("4.7k", "DTACK#", "VCC", "the chip's DTACK pull-up (open drain)")
    part("TP1", "TESTPOINT", "test point DTACK: the chip's DTACK, for the bench (D104)")
    w("TP1", "1", "DTACK#")
    # the rest of the kept part's parts, a sheet a function, before their nets
    group("Dot clock, the counter and the memory control")
    K("OSC1 IC6 IC7 IC16 IC18 IC19 IC20 IC21 IC31 IC36 IC37 IC38 R18")
    group("Frame buffer: the address latches, the RAMs, the font")
    K("IC8 IC9 IC10 IC11 IC12 IC13 IC14 IC15 RN1 RN2 RN3 RN4 JP4")
    group("Dots: the shift registers, the control register, VGA")
    K("IC22 IC23 IC40 IC34 IC35 IC17 IC32 R19 R2 R3 R4 R5 R6 J1")
    group("Text method A: a character screen over the graphics (sockets, empty on the first build)")
    K(" ".join(ref for ref, dev, value, fn in TEXT_A))

    # ---- the chip's host pins: the seam between the kept part and the bus side
    k("RS", "IC1.5"); k("RWL", "IC1.3"); k("CS#", "IC1.4"); k("RESB#", "IC1.6")
    for i in range(16):
        k(HD[i], "IC1.%d" % (17 + i))
    k("DONE#", "IC1.7", "R1.1"); k("VCC", "R1.2"); k("GND", "IC1.64")                    # LPSTB grounded
    k("IRQ#", "IC1.11"); k("DACK", "IC1.9"); k("DTACK#", "IC1.10")
    knc("IC1", 8, 15, 62)                                                                # DREQ, EXSYNC, DISP2

    # ---- the kept part. From here to the PS/2 section each line is YACC1-D's v30_netlist.py's, in its order
    # and under its net names (docs/cards/video-v3.md 10b, 10c have every equation); A0-A16 there are VA0-VA16
    # here, since A2-A31 are the bus's
    PU = "PU"
    k("VCC", "R18.1"); k(PU, "R18.2"); k("VCC", "OSC1.1")
    # the dot clock and the counter: OSC1, IC6
    k("CK", "OSC1.8", "IC6.2", "IC7.3", "IC22.12", "IC23.12", "IC37.3", "IC37.11", "IC40.12", "IC41.3", "IC48.12")
    k(PU, "IC6.1", "IC6.7", "IC6.10")                                         # -CLR, ENP, ENT
    k("GND", "IC6.3", "IC6.4", "IC6.6")                                       # A, B, D load 0
    k("MCYC", "IC1.52", "IC16.3"); k("MCYCN", "IC16.4", "IC6.5")              # C loads NOT MCYC
    k("QA", "IC6.14", "IC31.1", "IC38.9")
    k("2CLK", "IC6.13", "IC1.50", "IC31.2", "IC16.5", "IC38.10")              # QB
    k("QC", "IC6.12", "IC18.1", "IC16.9", "IC38.2", "IC38.12")
    k("LD163#", "IC31.3", "IC6.9", "IC38.4")                                  # NAND(QA, QB): dots 3 and 7
    k("QBN", "IC16.6", "IC18.2"); k("QCN", "IC16.8", "IC7.11")

    # the chip's memory side: the address latches IC8 / IC9, MAD's pull-ups
    MAD = {0: 61, 1: 60, 2: 59, 3: 58, 4: 57, 5: 48, 6: 47, 7: 46, 8: 45, 9: 44, 10: 43, 11: 42, 12: 41, 13: 40, 14: 39, 15: 38}
    RAM_A = {0: 12, 1: 11, 2: 10, 3: 9, 4: 8, 5: 7, 6: 6, 7: 5, 8: 27, 9: 26, 10: 23, 11: 25, 12: 4, 13: 28, 14: 3, 15: 31, 16: 2}
    RAM_D = (13, 14, 15, 17, 18, 19, 20, 21)
    ROM_A = {0: 10, 1: 9, 2: 8, 3: 7, 4: 6, 5: 5, 6: 4, 7: 3, 8: 25, 9: 24, 10: 21, 11: 23, 12: 2, 13: 26, 14: 27}
    ROM_D = (11, 12, 13, 15, 16, 17, 18, 19)
    for i in range(16):
        latch, rn, j = ("IC8", "RN1", i) if i < 8 else ("IC9", "RN2", i - 8)
        k("MAD%d" % i, "IC1.%d" % MAD[i], "%s.%d" % (latch, 2 + j), "%s.%d" % (rn, 2 + j))
        k("VA%d" % i, "%s.%d" % (latch, 19 - j), "IC10.%d" % RAM_A[i], "IC11.%d" % RAM_A[i])
        if i in ROM_A:
            k("VA%d" % i, "IC14.%d" % ROM_A[i])
    k("VCC", "RN1.1", "RN2.1"); k("GND", "IC8.1", "IC9.1")
    k("AS#", "IC1.53", "IC16.1", "IC7.10", "IC18.4"); k("ASN", "IC16.2", "IC8.11", "IC9.11")       # LE = NOT AS; AS presets VALID
    # CHR: in a cycle on a character screen MA16-19 are the raster line, not address: IC36 forces the RAM's A16
    # (and A17, A18 of the 512K part) high and the RAM selected
    k("CHR", "IC1.56", "IC36.2", "IC36.5", "IC36.10", "IC36.13", "IC32.12"); k("VCC", "IC32.13"); k("CHRN", "IC32.11", "IC21.13")
    k("MA16", "IC1.37", "IC36.1", "IC46.2"); k("RA16", "IC36.3", "IC10.2", "IC11.2")
    k("MA17", "IC1.36", "IC36.4", "IC46.3"); k("RA17", "IC36.6", "JP4.3"); k("VALID", "JP4.1"); k("RSEL", "JP4.2", "IC10.30", "IC11.30")
    k("MA18", "IC1.35", "IC36.9", "IC46.4"); k("RA18", "IC36.8", "IC10.1", "IC11.1")     # NC on the 128K part, A18 on the 512K one
    k("MA19", "IC1.34", "IC16.11", "IC21.12", "IC46.5"); k("RAMCE#", "IC21.11", "IC10.22", "IC11.22")   # MA19 AND NOT CHR
    k("MA19N", "IC16.10", "IC36.12"); k("ROMCE#", "IC36.11", "IC14.20", "IC20.9")        # NOT MA19 OR CHR
    # RAM data, the MAD buffers IC12 / IC13, the font IC14 and its copy IC15
    for i in range(8):
        k("DL%d" % i, "IC10.%d" % RAM_D[i], "IC12.%d" % (2 + i), "IC14.%d" % ROM_D[i], "RN3.%d" % (2 + i))
        k("DH%d" % i, "IC11.%d" % RAM_D[i], "IC13.%d" % (2 + i), "RN4.%d" % (2 + i))
        k("MAD%d" % i, "IC12.%d" % (18 - i)); k("MAD%d" % (8 + i), "IC13.%d" % (18 - i))
    k("VCC", "RN3.1", "RN4.1", "IC14.1")                                      # the EPROM's Vpp
    for i, (a, y) in enumerate(((2, 18), (4, 16), (6, 14), (8, 12), (11, 9), (13, 7), (15, 5), (17, 3))):
        k("DL%d" % i, "IC15.%d" % a); k("DH%d" % i, "IC15.%d" % y)
    # memory control: W, -OE, -WE, the buffers' enable, VALID, the load
    k("DRAW#", "IC1.54", "IC17.2", "IC19.4", "IC19.10", "IC31.4", "IC31.5", "IC20.4"); k("DRAWN", "IC31.6", "IC38.1")
    k("MRD", "IC1.55", "IC17.3", "IC12.1", "IC13.1", "IC21.4", "IC32.9", "IC20.5"); k("VCC", "IC32.10"); k("MRDN", "IC32.8", "IC19.9")
    k("W", "IC17.1", "IC31.10", "IC20.2"); k("WN", "IC20.6", "IC18.5")        # a drawing write; not one (DRAW OR MRD)
    k("MOE#", "IC18.6", "IC10.24", "IC11.24", "IC14.22", "IC20.10")           # NAND(VALID, AS, NOT W)
    k("VCC", "IC18.13"); k("P56D", "IC18.12", "IC7.2"); k("P56#", "IC7.5", "IC34.11"); k("P56", "IC7.6", "IC31.9", "IC20.1")
    k("WE#", "IC31.8", "IC10.29", "IC11.29")
    k(PU, "IC7.1", "IC7.4", "IC7.13")
    k("GND", "IC7.12"); k("VALID", "IC7.9", "IC19.5", "IC19.11", "IC18.3")    # B: D = 0 clocked by NOT QC, preset by AS
    k("BUFT", "IC20.3", "IC38.5"); k("BUFD", "IC38.6", "IC37.2"); k("BUFQN", "IC37.6", "IC21.9"); k("VCC", "IC21.10")
    k("BUFEN#", "IC21.8", "IC12.19", "IC13.19"); k(PU, "IC37.1", "IC37.4", "IC37.10", "IC37.13")
    k("F244#", "IC20.8", "IC15.1", "IC15.19")                                 # the font's -CE OR MOE#
    k("GND", "IC19.1", "IC19.2", "IC19.13")                                   # a spare AND
    k("DISP1#", "IC1.63", "IC16.13"); k("DISPON", "IC16.12", "IC21.5"); k("BGON", "IC21.6", "IC19.3")
    k("DISPBG", "IC19.6", "IC38.13"); k("GLOAD", "IC38.8", "IC22.19", "IC23.19")
    k("DISPWN", "IC19.8", "IC41.12", "IC42.5", "IC43.1")

    # the shift registers IC22 (dots 8-15) and IC23 (dots 0-7) and the video
    IO299 = {0: 7, 1: 13, 2: 6, 3: 14, 4: 5, 5: 15, 6: 4, 7: 16}
    for i in range(8):
        k("DL%d" % i, "IC23.%d" % IO299[7 - i]); k("DH%d" % i, "IC22.%d" % IO299[7 - i])       # bit 0 leaves first
    k(PU, "IC22.1", "IC22.2", "IC22.3", "IC22.9", "IC23.1", "IC23.2", "IC23.3", "IC23.9")      # S0, both -OE, -MR
    k("GND", "IC22.11", "IC22.18", "IC23.18"); k("SR8", "IC22.17", "IC23.11")
    k("VDOT", "IC23.17", "IC40.11"); k("GDEL", "IC40.17", "IC20.12", "IC18.9")
    k(PU, "IC40.1", "IC40.2", "IC40.3", "IC40.9", "IC40.7", "IC40.13", "IC40.6", "IC40.14", "IC40.5", "IC40.15", "IC40.4", "IC40.16")
    k("GND", "IC40.19", "IC40.18")                                            # S1 low: shift; the other serial input
    k("TDOT", "IC48.17", "IC20.13", "IC18.10", "R19.1"); k("GND", "R19.2"); k("TEXTON", "IC35.15", "IC48.9"); k("TXOR", "IC35.16", "IC18.11")
    k("GOT", "IC20.11", "IC31.12"); k("NBX", "IC18.8", "IC31.13")
    k("VIDN", "IC31.11", "IC37.12"); k("VIDQ", "IC37.9", "IC17.5", "IC17.8", "IC17.11")
    # the control register: IC34 takes MAD0-7 as dot 7 begins in every cycle, IC35 copies it when HSYNC rises.
    # Bits: 0, 1, 2 = red, green, blue OFF; 3, 4 = HSYNC, VSYNC positive; 5 = the text layer on; 6 = text XOR
    # graphics; 7 = the second font. -RES clears both: white on black, both syncs negative, text off
    k("HSYNC#", "IC1.12", "IC21.1", "IC32.1"); k("VCC", "IC21.2"); k("HSCK", "IC21.3", "IC35.11")
    k("RESB#", "IC34.1", "IC35.1")
    Q273 = ((3, 2), (4, 5), (7, 6), (8, 9), (13, 12), (14, 15), (17, 16), (18, 19))       # (D, Q) bits 0..7
    for i, (d, q) in enumerate(Q273):
        k("MAD%d" % i, "IC34.%d" % d); k("AT%d" % i, "IC34.%d" % q, "IC35.%d" % d)
    k("OFFR", "IC35.2", "IC17.6"); k("OFFG", "IC35.5", "IC17.9"); k("OFFB", "IC35.6", "IC17.12")
    k("POLH", "IC35.9", "IC32.2"); k("POLV", "IC35.12", "IC32.5")
    k("VR", "IC17.4", "R2.1"); k("VG", "IC17.10", "R3.1"); k("VB", "IC17.13", "R4.1")
    k("RED", "R2.2", "J1.1"); k("GREEN", "R3.2", "J1.2"); k("BLUE", "R4.2", "J1.3")
    k("VSYNC#", "IC1.13", "IC32.4")
    k("HSB", "IC32.3", "R5.1"); k("HSOUT", "R5.2", "J1.13"); k("VSB", "IC32.6", "R6.1"); k("VSOUT", "R6.2", "J1.14")
    k("GND", "J1.5", "J1.6", "J1.7", "J1.8", "J1.10", "J1.SH")

    # text method A: IC41-IC48, sockets
    k("QCN", "IC42.1", "IC41.11"); k("2CLK", "IC42.2", "IC42.12"); k("QA", "IC42.4", "IC42.9"); k("D3W", "IC42.6", "IC41.2")
    k(PU, "IC41.1", "IC41.4", "IC41.10", "IC41.13")
    k("WCK", "IC41.5", "IC44.11", "IC45.11", "IC46.11")
    k("WPH", "IC41.9", "IC44.1", "IC43.2"); k("WPHN", "IC41.8", "IC45.1")             # which code drives the generator
    k("WANY", "IC43.3", "IC42.10"); k("QC", "IC42.13"); k("TLOAD", "IC42.8", "IC48.19")   # dot 7 of either
    k("GND", "IC43.4", "IC43.5", "IC43.9", "IC43.10", "IC43.12", "IC43.13")
    for i in range(8):
        k("DL%d" % i, "IC44.%d" % (2 + i)); k("DH%d" % i, "IC45.%d" % (2 + i))
        k("CA%d" % i, "IC44.%d" % (19 - i), "IC45.%d" % (19 - i), "IC47.%d" % ROM_A[4 + i])
        k("CG%d" % i, "IC47.%d" % ROM_D[i], "IC48.%d" % IO299[7 - i])                 # bit 0 is the left-hand dot
    k("RA4", "IC1.33", "IC46.6"); k("GND", "IC46.1", "IC46.7", "IC46.8", "IC46.9")
    for i in range(4):
        k("RL%d" % i, "IC46.%d" % (19 - i), "IC47.%d" % ROM_A[i])
    k("CUD1#", "IC1.1", "IC47.2"); k("CUD2#", "IC1.2", "IC47.27"); k("FONT2", "IC35.19", "IC47.26")
    k("GND", "IC47.20", "IC47.22"); k("VCC", "IC47.1")                           # -CE, -OE on; Vpp
    k(PU, "IC48.1", "IC48.2", "IC48.3"); k("GND", "IC48.11", "IC48.18")
    # pins left open
    for ref, pins in (("IC6", (11, 15)), ("IC7", (8,)), ("IC19", (12,)), ("IC22", (8,)), ("IC23", (8,)), ("IC37", (5, 8)),
                      ("IC38", (3, 11)), ("IC40", (8,)), ("IC41", (6,)), ("IC42", (3, 11)), ("IC43", (6, 8, 11)),
                      ("IC46", (12, 13, 14, 15)), ("IC48", (8,)), ("J1", (4, 9, 11, 12, 15))):
        knc(ref, *pins)

    # ---- PS/2: the ATmega328P with YACC1-D's firmware (embedded/video-ps2), its pins as there. It loads a byte
    # into the data latch (LDDATA's rise: FULL) and reads the command latch (-RDCMD low: BUSY cleared)
    group("PS/2 keyboard and mouse")
    mcu = U("ATMEGA328P", "PS/2 controller: YACC1-D's firmware (keys as ASCII, the mouse's packets)", "mcu")
    MD = ["MD%d" % i for i in range(8)]
    for pin, net in (("-RESET", "MRST"), ("PD0", "FTX"), ("PD1", "FRX"), ("PD2", "KCLK"), ("PD3", "MCLK"), ("PD4", "KDAT"),
                     ("PD5", "MDAT"), ("XTAL1", "XT1"), ("XTAL2", "XT2"), ("PD6", "LDDATA"), ("PD7", "RDCMD"),
                     ("PB0", "MD6"), ("PB1", "MD7"), ("PB2", "FULL"), ("PB3", "BUSY"), ("PB4", "INTREQ"), ("PB5", "SRC")):
        w(mcu, pin, net)
    for i in range(6):
        w(mcu, "PC%d" % i, MD[i])
    c.unused(mcu, "AREF")
    x = U("74HCT574", "PS/2 data latch: the ATmega loads it; +0C reads it", "pdata")
    w(x, "CLK", "LDDATA"); w(x, "-OE", "PDOEN")
    y = U("74HCT574", "PS/2 command latch: +08 writes it; the ATmega reads it", "pcmd")
    w(y, "CLK", "CMDQ"); w(y, "-OE", "RDCMD")
    for i in range(8):
        w(x, "D%d" % i, MD[i]); w(x, "Q%d" % i, HD[i]); w(y, "D%d" % i, HD[i]); w(y, "Q%d" % i, MD[i])
    x = U("74HCT74", "FULL (a byte waits: set by the load, cleared after +0C is read) and BUSY (a command waits)", "flags")
    w(x, "1D", "VCC"); w(x, "1CLK", "LDDATA"); w(x, "-1PRE", "VCC"); w(x, "-1CLR", "FCLRN"); w(x, "1Q", "FULL")
    w(x, "2D", "VCC"); w(x, "2CLK", "CMDQ"); w(x, "-2PRE", "VCC"); w(x, "-2CLR", "RDCMD"); w(x, "2Q", "BUSY")
    c.unused(x, "-1Q", "-2Q")
    part("Y1", "XTAL", "the ATmega's clock: 16 MHz")
    w("Y1", "1", "XT1"); w("Y1", "2", "XT2")
    for i, net in enumerate(("XT1", "XT2")):
        part("C%d" % (902 + i), "C22P", "the crystal's load")
        w("C%d" % (902 + i), "1", net); w("C%d" % (902 + i), "2", "GND")
    part("C901", "C100N", "FTDI DTR to the ATmega's reset: the bootloader's auto-reset")
    w("C901", "1", "FDTR"); w("C901", "2", "MRST")
    R("10k", "MRST", "VCC", "the ATmega's reset pull-up")
    R("4.7k", "RESETH", "VCC", "RESETH pull-up: -RES to the ATmega's reset through two open-drain gates")
    R("10k", "LDDATA", "VCC", "LDDATA pull-up: high until the firmware runs")
    R("10k", "RDCMD", "VCC", "-RDCMD pull-up: the command latch off MD until the firmware runs")
    R("10k", "INTREQ", "GND", "INTREQ pull-down: no request until the firmware runs")
    part("J7", "HDR1X6", "FTDI: 1 GND, 2 CTS, 3 VCC (not connected), 4 TXD, 5 RXD, 6 DTR: the ATmega's bootloader and console")
    w("J7", "1", "GND"); w("J7", "2", "GND"); w("J7", "4", "FTX"); w("J7", "5", "FRX"); w("J7", "6", "FDTR")
    c.unused("J7", "3")
    part("F1", "PTC", "PS/2 +5 V, both sockets")
    w("F1", "1", "VCC"); w("F1", "2", "PS2V")
    for j, P_, name in (("J5", "K", "keyboard"), ("J6", "M", "mouse")):
        part(j, "MINIDIN6F_RA", "PS/2 %s (mini-DIN-6) at the front edge, its opening facing out, labelled %s" % (name, name.upper()))
        w(j, "1", P_ + "DAT"); w(j, "5", P_ + "CLK"); w(j, "3", "GND"); w(j, "4", "PS2V"); w(j, "SHELL", "GND")
        c.unused(j, "2", "6")
        R("4.7k", P_ + "CLK", "VCC", "PS/2 %s clock pull-up" % name)
        R("4.7k", P_ + "DAT", "VCC", "PS/2 %s data pull-up" % name)

    # ---- interrupts: a level's 74LS145 for each device; the acknowledge cycle
    group("Interrupts and the jumpers")
    for d, (name, asks) in enumerate((("the PS/2 section", "PS2ASKN"), ("the chip", "IRQ#"))):
        x = U("74LS145", "%s's interrupt level (jumpers %s): its -IRQ line, open collector" % (name, "/".join(LV(d))))
        w(x, "A", LV(d)[0]); w(x, "B", LV(d)[1]); w(x, "C", LV(d)[2]); w(x, "D", asks)
        for j in range(8):
            w(x, "O%d" % j, "-IRQ%d" % j)
        c.unused(x, "O8", "O9")
    x = U("74F138", "the level being acknowledged (A4:A2 while -IACK is low), one line low")
    w(x, "A", "BA2"); w(x, "B", "BA3"); w(x, "C", "BA4"); w(x, "G1", "VCC"); w(x, "-G2A", "BIACKN"); w(x, "-G2B", "GND")
    for j in range(8):
        w(x, "Y%d" % j, ACKL[j])
    gal("VINT", "who asks, the acknowledge cycle", {"IRQN": "IRQ#"})

    # ---- the jumpers (at an edge of the card, labelled): fitted = 0, open = 1
    jumpers = ([("WIN%d" % i, "window bit %d" % i) for i in range(3)] +
               [(LV(0)[i], "the PS/2 section's level, bit %d" % i) for i in range(3)] +
               [(LV(1)[i], "the chip's level, bit %d" % i) for i in range(3)] +
               [("TEXTN", "TEXT: fitted with text method A's parts (card status bit 3 = 1)")])
    for i, (net, fn) in enumerate(jumpers, 1):
        part("JP%d" % i, "JUMPER2", "jumper %s: %s" % (net, fn))
        w("JP%d" % i, "1", net); w("JP%d" % i, "2", "GND")
    RN("jumper pull-ups: the window, the PS/2 section's level, the chip's level bits 0 and 1", "VCC", [net for net, fn in jumpers[:8]])
    RN("jumper pull-ups: the chip's level bit 2, TEXT", "VCC", [net for net, fn in jumpers[8:]])

    # the chip's second VCC pin and the ATmega's AVCC: a capacitor each of their own (as YACC1-D's C50, C52)
    cur[0] = []
    for ref, what, at in (("C904", "the ACRTC's second VCC pin", kept["IC1"]), ("C905", "the ATmega's AVCC", mcu)):
        part(ref, "C100N", "bypass: %s (%s)" % (what, at))
        w(ref, "1", "VCC"); w(ref, "2", "GND")

    # nothing local may bear a backplane signal's name: Card.finish() would join it to the connector
    bus = ({"A%d" % i for i in range(2, 32)} | {"D%d" % i for i in range(32)} | {"BE%d" % i for i in range(4)} |
           {"-IRQ%d" % i for i in range(8)} | {"-MRD", "-MWR", "-IACK", "CLK", "-CLK", "-RES", "-ACK", "-WAIT"})
    stray = sorted(net for net in c.nets if net in SIGNALS and net not in bus)
    assert not stray, "local nets named as bus signals: %s" % stray
    c.finish()

    # the layout (step 5 of docs/video-card-acrtc.md's plan gives the rows): the outside connectors at the
    # front edge, facing out (D48); the jumpers and the FTDI header at an edge
    c.front = [kept["J1"], "J5", "J6"]
    c.front_rot = {kept["J1"]: 90}
    c.front_edge = {kept["J1"]: ("pad1", 6.83), "J5": ("crt", 0.0), "J6": ("crt", 0.0)}
    c.edge_row = ["JP%d" % i for i in range(1, len(jumpers) + 1)]
    c.kept = dict(kept)                                 # YACC1-D's reference -> ours
    c.text_a = [kept[r] for r, dev, value, fn in TEXT_A]
    c.sheets = sheets(c, [(title, refs) for title, refs in groups.items()])
    CARDS[c.name] = c
    return c, G, role

# ---------------------------------------------------------------- the kept part against YACC1-D's netlist
def reference():
    """YACC1-D's v30_netlist.py, the copy in hardware/video-card/yacc1-v3 (plain Python data: its parts, their
    pins, its nets, the pins it leaves open)."""
    s = importlib.util.spec_from_file_location("yacc1_v30_netlist", os.path.join(REF_DIR, "v30_netlist.py"))
    m = importlib.util.module_from_spec(s)
    s.loader.exec_module(m)
    return m

def kept_check(c):
    """The kept part against YACC1-D's netlist. Returns (faults, a line that says what was compared).

      * every part of YACC1-D's is kept here or named in NOT_KEPT; a kept part is the same part (its value);
      * every pin of a kept part is on the same net as there: two kept pins share a net here if and only if
        they do there, VCC is VCC, GND is GND, a pin left open there is left open here. SEAM lists the pins
        that differ, each with its reason, and each is on the net SEAM says;
      * nothing but kept parts is on a net of the kept part, except the seam's nets (SEAM_NETS), VCC and GND:
        the bus side loads and drives the kept part only where the design note says."""
    V = reference()
    faults = []
    vnet = {rp: net for net, pins in V.NETS.items() for rp in pins}
    vnet.update({(ref, p): None for ref, pins in V.NO_CONNECT.items() for p in pins})
    pnet = {rp: net for net, pins in c.nets.items() for rp in pins}
    caps = set(V.DECOUPLE)
    for ref in V.PARTS:
        if ref not in c.kept and ref not in NOT_KEPT and ref not in caps:
            faults.append("YACC1-D's %s is neither kept nor named as not kept" % ref)
    values = {ref: value for ref, dev, value, fn in KEPT_ICS + TEXT_A + KEPT_REST}
    there, here, npins = {}, {}, 0                      # their net -> ours, and back
    mine = set()
    for vref, ref in sorted(c.kept.items()):
        if vref not in V.PARTS:
            faults.append("%s is not a part of YACC1-D's netlist" % vref)
            continue
        want, got = V.PARTS[vref][0], c.parts[ref][2]
        if values[vref] is not None and not (want.startswith(values[vref]) and got.startswith(values[vref])):
            faults.append("%s (%s): YACC1-D's part is a %s, ours a %s" % (vref, ref, want, got))
        name = {num_: n for n, (num_, t) in DEV[c.parts[ref][0]]["pins"].items()}
        if sorted(name) != sorted(V.PINS[vref]):
            faults.append("%s (%s): its pins are not YACC1-D's part's" % (vref, ref))
            continue
        for pin in V.PINS[vref]:
            rp = (ref, name[pin])
            mine.add(rp)
            a = vnet[(vref, pin)]
            b = None if rp in c.nc else pnet.get(rp, "?")
            npins += 1
            if (vref, pin) in SEAM:
                if b != SEAM[(vref, pin)][0]:
                    faults.append("%s.%s (%s): the seam says net %s, it is on %s" % (vref, pin, ref, SEAM[(vref, pin)][0], b))
                continue
            if (a is None) != (b is None) or (a in ("VCC", "GND") or b in ("VCC", "GND")) and a != b:
                faults.append("%s.%s (%s %s): on %s there, %s here" % (vref, pin, ref, name[pin], a, b))
            elif a is not None:
                if there.setdefault(a, b) != b:
                    faults.append("%s.%s (%s): YACC1-D's net %s is two nets here, %s and %s" % (vref, pin, ref, a, there[a], b))
                if here.setdefault(b, a) != a:
                    faults.append("%s.%s (%s): net %s joins YACC1-D's %s and %s" % (vref, pin, ref, b, here[b], a))
    for net, pins in c.nets.items():
        if net in ("VCC", "GND") or net in SEAM_NETS or not any(rp in mine for rp in pins):
            continue
        for rp in pins:
            if rp not in mine:
                faults.append("net %s of the kept part also has %s.%s, which is not kept" % (net, rp[0], rp[1]))
    for net in SEAM_NETS:
        if not any(rp in mine for rp in c.nets.get(net, ())):
            faults.append("the seam's net %s has no kept pin" % net)
    nic = sum(1 for r in c.kept if r.startswith("IC"))
    line = ("the kept part: %d ICs, %d parts, %d pins, %d nets as YACC1-D's v30_netlist.py; %d pins differ at the seam, as listed"
            % (nic, len(c.kept), npins, len(there), len(SEAM)))
    if os.path.exists(REF_SRC) and open(REF_SRC, "rb").read() != open(os.path.join(REF_DIR, "v30_netlist.py"), "rb").read():
        line += "; NOTE: YACC1-D's file has changed since the copy was taken"
    return faults, line

def defaults():
    """The jumpers as the documents and the kernel assume them (D52): window 5, both devices on level 4,
    text method A's parts not fitted. {net: 0 fitted / 1 open}."""
    j = {"WIN%d" % i: WINDOW >> i & 1 for i in range(3)}
    for d in range(2):
        j.update({LV(d)[i]: LEVEL >> i & 1 for i in range(3)})
    j["TEXTN"] = 1
    return j

def main():
    c, G, role = build()
    errs = check(c)
    faults, line = kept_check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs or faults:
        print("\n".join((errs + faults)[:60]))
        sys.exit("video card: %d errors" % len(errs + faults))
    print("video card OK: %d ICs (%d GALs; %d of the ICs text method A's sockets), %d nets, %d parts; %s"
          % (len(ics), len(G), len(c.text_a), len(c.nets), len(c.parts), line))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
