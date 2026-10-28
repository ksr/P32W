#!/usr/bin/env python3
"""The P32W video card (any of slots 3-10): docs/video-card.md, D50, D52,
D53, D57, D67.

VGA 640 x 480 at 60 Hz from a 25.175 MHz dot clock: 80 x 25 text (8 x 16
cells from a font ROM, on lines 40-439) and a 640 x 480 one-bit overlay,
each switched on and off; two PS/2 ports (keyboard and mouse). As software
sees it (architecture.md 4a), at the window its jumpers choose (window 5 by
default):

  FE00_0000 + n x 64 KiB   the card's RAM: the text at +0 (25 rows x 80
                cells, a cell a halfword: the character, then its
                attribute: bit 0 inverse, 1 underline, 2 bright, 3 blink),
                the overlay at +1000 (480 lines x 80 bytes, bit 7 the
                leftmost dot). Bytes, halfwords and words.
  FFFF_8F00 + n x 4 KiB    the registers, bytes (as p32io.h):
                +00 control (bit 0 TEXT, 1 OVERLAY, 2 XOR, 3 CURSOR)
                +01 status (bit 0 VBLANK, 1 DE)   +02 / +03 cursor row / column
                +08 keyboard data (reading takes it) / command   +09 its status
                    (bit 0 FULL, 1 BUSY)   +0C / +0D the mouse's
                +10 interrupt enables (bit 0 keyboard, 1 mouse)
                +F0 card ID (3)   +F1 revision (0)   +F4 interrupt status
                    (a word: bit 0 keyboard FULL, 1 mouse FULL)

The display (the dot clock's domain, DCLK). A character time is 8 dots
(318 ns): DC0-2 count the dots, HC the characters (0-99: 80 drawn, 2 front
porch, 12 sync, 6 back porch), VC the lines (0-524: 0 the first text line,
which is the frame's line 40; the overlay's line 0 is VC 485). The RAM is
16 bits wide (two CY7C199: the even bytes, the odd bytes), shared in four
2-dot slots of each character time, each slot's address on from a falling
dot-clock edge (GAL VSLOT), its data taken a dot and a half later on a
rising one (GAL VTIM):

  [0.5, 2.5)  the text: the cell at row x 80 + column, char and attribute
              (CL, AL at dot 2); the char and the scan line address the
              font ROM, whose byte FL takes at dot 8 (the next character)
  [2.5, 4.5)  the bus: the low halfword of the word addressed (lanes 0-1)
  [4.5, 6.5)  the bus: the high halfword (lanes 2-3)
  [6.5, 8.5)  the overlay: the halfword with the column's byte (OL0, OL1 at
              dot 8)

So a character's font and overlay bytes are both ready as the next
character time starts; two 74F151s pick its dots (DC selecting), GAL CELL
holds the character's attributes, GAL VOUT combines the layers and
registers each dot with the syncs (74ACT541: the resistors to the DE-15).

The bus: the card answers its memory at once (-ACK) and pulls -WAIT until
the dot clock's side has done the access: REQ (= the access and not
DONEB) crosses into the dot clock through two flip-flops; at the next
character time's dot 2 the access starts (GO), writes in the two bus
slots (-WE a dot each, per byte lane), reads into the read latches; DONE
comes back through one flip-flop clocked on -CLK (GAL VBUS), which ends
-WAIT. One to two wait states at 3 MHz, whatever the bus clock.

The registers answer at once. PS/2: per port a GAL state machine on the bus
clock (receive into a 74HCT595, send from a 74HCT597, a 74HCT163 counting
bits or, while it holds the clock low before sending, lines), the lines
through a 74HCT14 and a 74HCT175 in, a 74HC05 out. One interrupt level for
the card (three jumpers, default 4); the vector is window x 16 + 0 for the
keyboard, + 1 for the mouse.

    python3 card_video.py [--bom]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from card_cpu import GB, num                                  # noqa: E402

WINDOW, LEVEL = 5, 4                    # the default jumpers (D52)
OVERLAY = 0x1000                        # the overlay's offset in the card's RAM (D67)
WINJ = ["WIN0", "WIN1", "WIN2"]
LVL = ["L0", "L1", "L2"]
BA = ["BA%d" % i for i in range(2, 8)]
BBE = ["BBE%d" % k for k in range(4)]
LNS = ["LN0", "LN1"]
DC = ["DC0", "DC1", "DC2"]
HC = ["HC%d" % i for i in range(7)]
VC = ["VC%d" % i for i in range(10)]

# the raster (VC 0 = the frame's line 40, the first text line)
HTOTAL, VTOTAL = 100, 525
HSYNC = (83, 94)                        # HC at the character time's end whose dots are sync (82-93 shown)
TEXT_LINES = 400                        # VC 0-399
VIS_END, VIS_START = 440, 485           # drawn: VC < 440 or VC >= 485 (the frame's lines 0-479)
VSYNC = (450, 451)                      # the frame's lines 490, 491

REGS = {0x00: "ctl", 0x01: "stat", 0x02: "row", 0x03: "col", 0x08: "kdata", 0x09: "kstat",
        0x0C: "mdata", 0x0D: "mstat", 0x10: "ie", 0xF0: "id", 0xF1: "rev"}
ISR = 0xF4

def onehot(v):
    return sum(1 for b in BBE if v.get(b)) == 1
def lane(v):
    return [k for k in range(4) if v.get(BBE[k])][0]
def offset(v):
    return num(v, BA) << 2 | num(v, LNS)
def dc(v):
    return num(v, DC)

# ---------------------------------------------------------------- the bus side
def gal_vhi():
    """A16-A24 against the windows: MHIT (A31:16 = FE00 + the jumpers: the
    card's 64 KiB), RHI (A31:16 = FFFF: the device windows). TOPN: A31-A25
    all ones (a 74HCT688)."""
    A = ["A%d" % i for i in range(16, 25)]
    b = GB("VHI", "video card: memory and register windows").inp("TOPN", *A).inp(*WINJ)
    b.com("MHIT", ["TOPN"] + A + WINJ, lambda v: not v["TOPN"] and not v["A24"]
          and num(v, ["A19", "A20", "A21", "A22", "A23"]) == 0 and num(v, ["A16", "A17", "A18"]) == num(v, WINJ))
    b.com("RHI", ["TOPN"] + A, lambda v: not v["TOPN"] and all(v[a] for a in A))
    return b.build()

def gal_vsel():
    """MACC: an access to the card's RAM; RRD / RWR: to its register page
    (A15 = 1, A14:12 the jumpers, A11:8 = F); RLOEN: the read latches onto
    the card's data bus."""
    A = ["BA%d" % i for i in range(8, 16)]
    b = GB("VSEL", "video card: cycle decode").inp("MHIT", "RHI", *A).inp(*WINJ).inp("MRDN", "MWRN")
    page = lambda v: (v["RHI"] and v["BA15"] and num(v, ["BA12", "BA13", "BA14"]) == num(v, WINJ)
                      and num(v, A[:4]) == 15)
    deps = ["RHI"] + A + WINJ
    b.com("MACC", ["MHIT", "MRDN", "MWRN"], lambda v: v["MHIT"] and not (v["MRDN"] and v["MWRN"]))
    b.com("RRD", deps + ["MRDN"], lambda v: page(v) and not v["MRDN"])
    b.com("RWR", deps + ["MWRN"], lambda v: page(v) and not v["MWRN"])
    b.com("RLOEN", ["MHIT", "MRDN"], lambda v: not (v["MHIT"] and not v["MRDN"]))
    return b.build()

def gal_vbe():
    """The byte enables (BYTE, WORD, LN1:0); -ACK; the lane transceivers
    (each lane the master asks for, when the card answers; the vector on
    lane 0) and their direction."""
    b = GB("VBE", "video card: byte lanes, -ACK").inp(*BBE).inp("MACC", "RHIT", "VHIT", "MWRN")
    b.com("BYTE", BBE, onehot)
    b.com("WORD", BBE, lambda v: all(v[x] for x in BBE))
    b.com("LN0", BBE, lambda v: lane(v) & 1 if onehot(v) else 0)
    b.com("LN1", BBE, lambda v: lane(v) >> 1 if onehot(v) else 0)
    on = lambda v: v["MACC"] or v["RHIT"] or v["VHIT"]
    for k in range(4):
        b.com("LE%dN" % k, ["MACC", "RHIT", "VHIT", BBE[k]], lambda v, k=k: not (on(v) and v[BBE[k]]))
    b.com("LDIR", ["MWRN"], lambda v: not v["MWRN"])
    b.com("ACK", ["MACC", "RHIT", "VHIT"], on)
    return b.build()

def gal_vdec():
    """The register reads: RHIT (a register: -ACK; reads and writes), KRD /
    MRDP (a data register read: it takes the byte), ROWRDN / COLRDN (the
    cursor registers read back), R0EN0 (GAL VRD0's: control, enables, ID,
    interrupt status)."""
    D = BA + LNS + ["BYTE", "WORD", "RRD", "RWR"]
    b = GB("VDEC", "video card: register selects").inp(*D)
    rd = lambda v, o: v["RRD"] and v["BYTE"] and offset(v) == o
    isr = lambda v: v["WORD"] and num(v, BA) << 2 == ISR
    b.com("RHIT", D, lambda v: bool(v["RRD"] or v["RWR"]) and ((v["BYTE"] and offset(v) in REGS) or isr(v)))
    b.com("KRD", D, lambda v: rd(v, 0x08))
    b.com("MRDP", D, lambda v: rd(v, 0x0C))
    b.com("ROWRDN", D, lambda v: not rd(v, 0x02))
    b.com("COLRDN", D, lambda v: not rd(v, 0x03))
    b.com("R0EN0", D, lambda v: bool(v["RRD"]) and ((v["BYTE"] and offset(v) in (0x00, 0x10, 0xF0)) or isr(v)))
    return b.build()

def gal_vwr():
    """The register writes: WCTL / WIE (levels: GAL VCTL takes the byte as
    the cycle ends), KCMDC / MCMDC (a command byte: a level, in the cycle's
    second half, for the port's GAL), and the registers' clocks, each low in
    the second half and rising as the cycle ends (D72: the decode, the
    transceivers and the data have the whole cycle): WROWC / WCOLC (the
    cursor), KCMDK / MCMDK (the send register and its parity bit)."""
    D = BA + LNS + ["BYTE", "RWR", "BCLK"]
    b = GB("VWR", "video card: register writes").inp(*D)
    wr = lambda v, o: v["RWR"] and v["BYTE"] and offset(v) == o
    b.com("WCTL", D, lambda v: wr(v, 0x00))
    b.com("WIE", D, lambda v: wr(v, 0x10))
    for name, o in (("KCMDC", 0x08), ("MCMDC", 0x0C)):
        b.com(name, D, lambda v, o=o: wr(v, o) and not v["BCLK"])
    for name, o in (("WROWC", 0x02), ("WCOLC", 0x03), ("KCMDK", 0x08), ("MCMDK", 0x0C)):
        b.com(name, D, lambda v, o=o: not (wr(v, o) and not v["BCLK"]))
    return b.build()

def gal_vctl():
    """The control and interrupt-enable registers (cleared by -RES), and the
    interrupt: ANYN (to the level's 74LS145), VDEV (the lowest asking
    device: 0 the keyboard), VHIT / VHITN (acknowledged: the level is the
    card's, EQ from a 74HCT85, and a device asks)."""
    XD = ["XD%d" % i for i in range(4)]
    b = GB("VCTL", "video card: control, interrupts", clock="BCLK", reset="BRESN")
    b.inp(*XD).inp("WCTL", "WIE", "KF", "MF", "IACKN", "EQ", "BRESN")
    for i in range(4):
        b.reg("CTL%d" % i, ["WCTL", XD[i], "CTL%d" % i], lambda v, i=i: v["XD%d" % i] if v["WCTL"] else v["CTL%d" % i])
    b.reg("KIE", ["WIE", "XD0", "KIE"], lambda v: v["XD0"] if v["WIE"] else v["KIE"])
    b.reg("MIE", ["WIE", "XD1", "MIE"], lambda v: v["XD1"] if v["WIE"] else v["MIE"])
    ask = lambda v: (v["KF"] and v["KIE"]) or (v["MF"] and v["MIE"])
    b.com("ANYN", ["KF", "MF", "KIE", "MIE"], lambda v: not ask(v))
    b.com("VDEV", ["KF", "KIE"], lambda v: not (v["KF"] and v["KIE"]))
    hit = lambda v: not v["IACKN"] and v["EQ"] and ask(v)
    b.com("VHIT", ["KF", "MF", "KIE", "MIE", "IACKN", "EQ"], hit)
    b.com("VHITN", ["KF", "MF", "KIE", "MIE", "IACKN", "EQ"], lambda v: not hit(v))
    return b.build()

def gal_vrd0():
    """Lane 0, bits 0-3, when R0EN: the control register (+00), the enables
    (+10), the card ID (+F0: 3), the interrupt status (+F4), or in the
    acknowledge cycle (VHIT) the vector's device bit (its window: a
    74HCT244 onto bits 4-6)."""
    D = ["R0EN", "BA2", "BA4", "BA7", "VHIT", "CTL0", "CTL1", "CTL2", "CTL3", "KIE", "MIE", "KF", "MF", "VDEV"]
    b = GB("VRD0", "video card: lane 0 registers, the vector").inp(*D)
    def val(v):
        if v["VHIT"]:
            return v["VDEV"]
        if not v["BA7"]:
            return num(v, ["CTL0", "CTL1", "CTL2", "CTL3"]) if not v["BA4"] else num(v, ["KIE", "MIE"])
        return 3 if not v["BA2"] else num(v, ["KF", "MF"])
    for i in range(4):
        b.tri("XD%d" % i, D[1:], lambda v, i=i: val(v) >> i & 1, "R0EN")
    return b.build()

def gal_vrd1():
    """Lane 1, bits 0-1: the status (+01: VBLANK, DE), the keyboard's and
    the mouse's status (+09, +0D: FULL, BUSY). (+05 has no register: no
    -ACK; the revision +F1 reads the pull-downs' 0.)"""
    D = BA + LNS + ["BYTE", "RRD"]
    S = ["VIS", "OOUT", "KF", "KB", "MF", "MB"]
    b = GB("VRD1", "video card: lane 1 status").inp(*D).inp(*S)
    def val(v):
        w = num(v, ["BA2", "BA3"])
        if w == 0:
            return (0 if v["VIS"] else 1) | v["OOUT"] << 1
        if w == 2:
            return v["KF"] | v["KB"] << 1
        if w == 3:
            return v["MF"] | v["MB"] << 1
        return None
    oe = "RRD & BYTE & LN0 & !LN1 & !BA4 & !BA5 & !BA6 & !BA7"
    for i in range(2):
        b.tri("XD%d" % (8 + i), ["BA2", "BA3"] + S, lambda v, i=i: None if val(v) is None else val(v) >> i & 1, oe)
    return b.build()

def gal_vbus():
    """The bus clock's side of the handshake, clocked on -CLK (mid-cycle):
    DONEB (the dot clock's DONE, one flip-flop), REQ (= -WAIT: the access,
    until DONEB), and TICK (one bus clock per line, from HSR: the PS/2
    ports' timer)."""
    b = GB("VBUS", "video card: the bus handshake", clock="BCLKN").inp("MACC", "DONE", "HSR")
    b.reg("DONEB", ["DONE"], lambda v: v["DONE"])
    b.reg("HS1", ["HSR"], lambda v: v["HSR"])
    b.reg("HS2", ["HS1"], lambda v: v["HS1"])
    b.com("REQ", ["MACC", "DONEB"], lambda v: v["MACC"] and not v["DONEB"])
    b.com("TICK", ["HS1", "HS2"], lambda v: v["HS1"] and not v["HS2"])
    return b.build()

# ---------------------------------------------------------------- the dot clock's side
def gal_vtim():
    """On DCLK's rising edges: the handshake (R1, R2: REQ synchronized; GO:
    the access, from dot 2 to the character time's end, decided at dot 2;
    DONE from dot 6 until REQ goes), the latch clocks (TLK rises at dot 2:
    the text; RLKA / RLKB at dots 4 and 6: the read latches; BCK at dot 8:
    the next character's font, overlay and attributes), the byte lanes'
    -WE (a dot: 3 for lanes 0-1, 5 for lanes 2-3)."""
    b = GB("VTIM", "video card: slots, the handshake", clock="DCLK").inp(*DC).inp("REQ", "MRDN", "MWRN").inp(*BBE)
    b.reg("R1", ["REQ"], lambda v: v["REQ"])
    b.reg("R2", ["R1"], lambda v: v["R1"])
    b.reg("GO", DC + ["R2", "DONE", "GO"], lambda v: (v["R2"] and not v["DONE"]) if dc(v) == 1 else
          (0 if dc(v) == 7 else v["GO"]))
    b.reg("DONE", DC + ["R2", "DONE", "GO"], lambda v: v["R2"] and (v["DONE"] or (v["GO"] and dc(v) == 5)))
    b.reg("TLK", DC, lambda v: dc(v) == 1)
    b.reg("RLKA", DC + ["GO", "MRDN"], lambda v: v["GO"] and not v["MRDN"] and dc(v) == 3)
    b.reg("RLKB", DC + ["GO", "MRDN"], lambda v: v["GO"] and not v["MRDN"] and dc(v) == 5)
    b.reg("BCK", DC, lambda v: dc(v) == 7)
    wr = lambda v, lo, hi: v["GO"] and not v["MWRN"] and ((dc(v) == 2 and v[lo]) or (dc(v) == 4 and v[hi]))
    b.reg("WEEN", DC + ["GO", "MWRN", "BBE0", "BBE2"], lambda v: not wr(v, "BBE0", "BBE2"), reset_high=True)
    b.reg("WEON", DC + ["GO", "MWRN", "BBE1", "BBE3"], lambda v: not wr(v, "BBE1", "BBE3"), reset_high=True)
    return b.build()

def gal_vslot():
    """On DCLK's falling edges (DCLKN rising): whose address the RAM sees
    (the text, the bus, the overlay drivers' enables; HALF: the bus's high
    halfword), and for a write the RAM's -OE off and the write buffers on
    in the bus slots. Off at power-up, until the first edge."""
    b = GB("VSLOT", "video card: the RAM's address", clock="DCLKN").inp(*DC).inp("GO", "MWRN")
    k = dc
    w = lambda v: v["GO"] and not v["MWRN"]
    b.reg("TXENN", DC, lambda v: k(v) not in (0, 1), reset_high=True)
    b.reg("CPENN", DC, lambda v: k(v) not in (2, 3, 4, 5), reset_high=True)
    b.reg("OVENN", DC, lambda v: k(v) not in (6, 7), reset_high=True)
    b.reg("HALF", DC, lambda v: k(v) in (4, 5))
    b.reg("ROEN", DC + ["GO", "MWRN"], lambda v: k(v) in (2, 3, 4, 5) and w(v))
    b.reg("WBAN", DC + ["GO", "MWRN"], lambda v: not (k(v) in (2, 3) and w(v)), reset_high=True)
    b.reg("WBBN", DC + ["GO", "MWRN"], lambda v: not (k(v) in (4, 5) and w(v)), reset_high=True)
    return b.build()

def gal_horz():
    """The characters: ENH (HC counts at dot 5), HCLRN (after 99, and while
    reset), VENA (VC counts), OENA (the overlay's address counts at dot 1,
    after a drawn column), TOUT / OOUT (this column is text / drawn), HSR
    (sync, for the character time now being shown), DRESN (-RES, in step
    with DCLK)."""
    b = GB("HORZ", "video card: the characters", clock="DCLK").inp(*HC).inp(*DC).inp("TL", "VIS", "BRESN")
    hc = lambda v: num(v, HC)
    b.reg("DRESN", ["BRESN"], lambda v: v["BRESN"])
    b.reg("HSR", HC + DC + ["HSR"], lambda v: (HSYNC[0] <= hc(v) <= HSYNC[1]) if dc(v) == 7 else v["HSR"])
    b.com("ENH", DC, lambda v: dc(v) == 4)
    b.com("HCLRN", HC + DC + ["DRESN"], lambda v: v["DRESN"] and not (dc(v) == 4 and hc(v) == HTOTAL - 1))
    b.com("VENA", HC + DC, lambda v: dc(v) == 4 and hc(v) == HTOTAL - 1)
    b.com("OENA", HC + DC + ["VIS"], lambda v: dc(v) == 0 and 1 <= hc(v) <= 80 and v["VIS"])
    b.com("TOUT", HC + ["TL"], lambda v: hc(v) < 80 and v["TL"])
    b.com("OOUT", HC + ["VIS"], lambda v: hc(v) < 80 and v["VIS"])
    return b.build()

def gal_vert():
    """The lines: VCLRN (after 524, and while reset), TL (a text line), VIS
    (drawn), VSR (sync), UL / CL (scan line 15: the underline; 14-15: the
    cursor), OALDN (the overlay's address loads its start, 1000, until
    the frame's first line)."""
    b = GB("VERT", "video card: the lines").inp(*VC).inp("VENA", "DRESN")
    vc = lambda v: num(v, VC)
    def f(g):
        return lambda v: None if vc(v) >= VTOTAL else g(v)
    vis = lambda v: vc(v) < VIS_END or vc(v) >= VIS_START
    b.com("VCLRN", VC + ["VENA", "DRESN"], f(lambda v: v["DRESN"] and not (v["VENA"] and vc(v) == VTOTAL - 1)))
    b.com("TL", VC, f(lambda v: vc(v) < TEXT_LINES))
    b.com("VIS", VC, f(vis))
    b.com("VSR", VC, f(lambda v: vc(v) in VSYNC))
    b.com("UL", VC[:4], lambda v: num(v, VC[:4]) == 15)
    b.com("CL", VC[1:4], lambda v: num(v, VC[1:4]) == 7)
    b.com("OALDN", VC + ["DRESN"], f(lambda v: vis(v) and v["DRESN"]))
    return b.build()

def gal_row5():
    """Row x 5 (row = VC8:4, 0-24): two 74F283s add HC6:4, so the text cell's
    address is row x 80 + column = (row x 5 + HC6:4) x 16 + HC3:0."""
    I = VC[4:9]
    b = GB("ROW5", "video card: row x 5").inp(*I)
    def r5(v):
        row = num(v, I)
        return None if row >= 25 else row * 5
    for i in range(7):
        b.com("R5%d" % i, I, lambda v, i=i: None if r5(v) is None else r5(v) >> i & 1)
    return b.build()

def gal_cell():
    """The character's attributes, on BCK (its dots start), so that its text
    dot is (TD & K) ^ M: K (the glyph shows: text on and in the text area,
    not blinked off, not the underline's line), M (lit regardless: the
    underline's line, inverted by inverse or by the cursor's lines while it
    blinks on; or the background, inverted), BRTT (bright text), E (the
    overlay on, and drawn here), OSEL / OSELN (which overlay latch: the
    byte's address bit 0)."""
    I = ["AT0", "AT1", "AT2", "AT3", "CEQL", "TOUTL", "OOUTL", "OA0", "CTL0", "CTL1", "CTL3", "UL", "CL", "BLINK"]
    b = GB("CELL", "video card: a character's attributes", clock="BCK").inp(*I)
    tt = lambda v: v["CTL0"] and v["TOUTL"]
    und = lambda v: v["AT1"] and v["UL"]
    x = lambda v: bool(v["AT0"]) ^ bool(not v["CEQL"] and v["CTL3"] and v["CL"] and not v["BLINK"])
    T = ["CTL0", "TOUTL", "AT1", "UL"]
    b.reg("K", T + ["AT3", "BLINK"], lambda v: tt(v) and not (v["AT3"] and v["BLINK"]) and not und(v))
    b.reg("M", T + ["AT0", "CEQL", "CTL3", "CL", "BLINK"], lambda v: tt(v) and (bool(und(v)) ^ x(v)))
    b.reg("BRTT", ["CTL0", "TOUTL", "AT2"], lambda v: tt(v) and v["AT2"])
    b.reg("E", ["OOUTL", "CTL1"], lambda v: v["OOUTL"] and v["CTL1"])
    b.reg("OSEL", ["OA0"], lambda v: v["OA0"])
    b.reg("OSELN", ["OA0"], lambda v: not v["OA0"], reset_high=True)
    return b.build()

def gal_vout():
    """Each dot, registered on DCLK: the text dot (TD & K) ^ M and the
    overlay's (OD & E), ORed or XORed (CTL2); VID (lit), VBR (bright text),
    the syncs (active low)."""
    I = ["TD", "OD", "K", "M", "BRTT", "E", "CTL2", "HSR", "VSR"]
    b = GB("VOUT", "video card: the dots", clock="DCLK").inp(*I)
    def pix(v):
        t = bool(v["TD"] and v["K"]) ^ bool(v["M"])
        o = bool(v["OD"] and v["E"])
        return (t ^ o) if v["CTL2"] else (t or o)
    D = ["TD", "OD", "K", "M", "E", "CTL2"]
    b.reg("VID", D, pix)
    b.reg("VBR", D + ["BRTT"], lambda v: pix(v) and v["BRTT"])
    b.reg("HSO", ["HSR"], lambda v: not v["HSR"], reset_high=True)
    b.reg("VSO", ["VSR"], lambda v: not v["VSR"], reset_high=True)
    return b.build()

# ---------------------------------------------------------------- PS/2
def gal_ps2(port):
    """One PS/2 port on the bus clock. CK1: its clock line (synchronized);
    a fall is CK2 & !CK1. Receiving: BC counts the falls; the data bits
    (falls 2-9) shift into the 74HCT595 (SH); the 11th sets FULL, which
    loads the 595's output register and holds the clock low until the CPU
    reads the byte (RDS). Sending (WRS: a command byte into the 74HCT597
    and BUSY): the clock held low for five line ticks, then the data low
    (the start bit) and the clock let go; the device clocks the byte out of
    the 597 (DTDRV: a 0 bit pulls the data low), the parity, the stop bit;
    its 11th fall (the device's acknowledge) ends BUSY."""
    P = "K" if port == 0 else "M"
    b = GB("PS2" + P, "video card: PS/2 %s" % ("keyboard" if port == 0 else "mouse"), clock="BCLK", reset="BRESN")
    BC = ["BC0", "BC1", "BC2", "BC3"]
    b.inp("CK1", *BC).inp("TICK", "Q7", "RDS", "WRS", "BRESN")
    bc = lambda v: num(v, BC)
    fall = lambda v: v["CK2"] and not v["CK1"]
    inh = lambda v: v["BUSY"] and not v["TXA"]                  # holding the clock low before sending
    start = lambda v: inh(v) and v["TICK"] and bc(v) == 4         # the fifth tick: start the frame
    S = ["CK1", "CK2", "BUSY", "TXA"] + BC
    b.reg("CK2", ["CK1"], lambda v: v["CK1"])
    b.reg("SH", S, lambda v: fall(v) and 1 <= bc(v) <= 8 and not inh(v))
    b.reg("FULL", S + ["FULL", "RDS"], lambda v: (v["FULL"] and not v["RDS"]) or
          (fall(v) and bc(v) == 10 and not v["TXA"] and not v["BUSY"]))
    b.reg("BUSY", S + ["WRS"], lambda v: v["WRS"] or (v["BUSY"] and not (v["TXA"] and fall(v) and bc(v) == 10)))
    b.reg("TXA", S + ["WRS", "TICK"], lambda v: not v["WRS"] and
          ((v["TXA"] and not (fall(v) and bc(v) == 10)) or start(v)))
    b.reg("DTDRV", S + ["WRS", "TICK", "Q7"], lambda v: not v["WRS"] and
          (start(v) or (v["TXA"] and not (fall(v) and bc(v) == 10) and
                        (bc(v) == 0 or (1 <= bc(v) <= 9 and not v["Q7"])))))
    b.com("CKDRV", ["FULL", "BUSY", "TXA"], lambda v: v["FULL"] or inh(v))
    b.com("CNT", S + ["TICK"], lambda v: v["TICK"] if inh(v) else fall(v))
    b.com("CLRN", S + ["WRS", "TICK", "FULL"], lambda v: not (v["WRS"] or (fall(v) and bc(v) == 10 and not inh(v)) or
                                                          start(v) or (v["FULL"] and not v["BUSY"])))
    return b.build()

def gals():
    G = {}
    for f in (gal_vhi, gal_vsel, gal_vbe, gal_vdec, gal_vwr, gal_vctl, gal_vrd0, gal_vrd1, gal_vbus,
              gal_vtim, gal_vslot, gal_horz, gal_vert, gal_row5, gal_cell, gal_vout):
        g = f()
        G[g.name] = g
    for p in range(2):
        g = gal_ps2(p)
        G[g.name] = g
    return G

FAST = ("VTIM", "VSLOT", "HORZ", "VERT", "VOUT")       # on the dot clock: the -7 grade

# the DE-15 (VGA) socket and the mini-DIN-6 (PS/2)
VGA = {"R": 1, "G": 2, "B": 3, "HS": 13, "VS": 14}
VGA_GND = (5, 6, 7, 8, 10)
VGA_NC = (4, 9, 11, 12, 15)
PS2PIN = {"DATA": 1, "GND": 3, "VCC": 4, "CLK": 5}

# ---------------------------------------------------------------- the netlist
def build():
    G = gals()
    c = Card("video-card", "P32W VIDEO CARD")
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
    def gal(name, function, netmap=None):
        g = G[name]
        dev = gal_device(g)
        ref = U(dev, function, name, value="ATF22V10C-7PX (%s)" % name if name in FAST else None)
        for p, (num_, t) in DEV[dev]["pins"].items():
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
    def buf541(fn, pairs, en, dev="74HCT541", r=None):
        x = U(dev, fn, r)
        w(x, "-OE1", en); w(x, "-OE2", "GND")
        for j, (src, dst) in enumerate(pairs):
            w(x, "A%d" % j, src); w(x, "Y%d" % j, dst)
        gnd_unused(x, ["A%d" % j for j in range(len(pairs), 8)], ["Y%d" % j for j in range(len(pairs), 8)])
        return x
    def latch(fn, ins, outs, clk, oe="GND", dev="74HCT574", r=None):
        x = U(dev, fn, r)
        w(x, "CLK", clk); w(x, "-OE", oe)
        for i in range(8):
            if i < len(ins):
                w(x, "D%d" % i, ins[i])
                if outs[i]:
                    w(x, "Q%d" % i, outs[i])
                else:
                    c.unused(x, "Q%d" % i)
            else:
                w(x, "D%d" % i, "GND"); c.unused(x, "Q%d" % i)
        return x
    def counter(dev, fn, clk, clr, load, enp, ent, d, q, rco=None):
        x = U(dev, fn)
        w(x, "CLK", clk); w(x, "-CLR", clr); w(x, "-LOAD", load); w(x, "ENP", enp); w(x, "ENT", ent)
        for p, net in zip("ABCD", d):
            w(x, p, net)
        for p, net in zip(("QA", "QB", "QC", "QD"), q):
            if net:
                w(x, p, net)
            else:
                c.unused(x, p)
        if rco:
            w(x, "RCO", rco)
        else:
            c.unused(x, "RCO")
        return x
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

    # ---- the bus interface: one load per line
    buf541("bus buffer: A2-A9", [("A%d" % i, "BA%d" % i) for i in range(2, 10)], "GND")
    buf541("bus buffer: A10-A15, BE0, BE1", [("A%d" % i, "BA%d" % i) for i in range(10, 16)] +
           [("BE0", "BBE0"), ("BE1", "BBE1")], "GND")
    buf541("bus buffer: BE2, BE3, -MRD, -MWR, -IACK, CLK, -CLK, -RES",
           [("BE2", "BBE2"), ("BE3", "BBE3"), ("-MRD", "MRDN"), ("-MWR", "MWRN"), ("-IACK", "IACKN"),
            ("CLK", "BCLK"), ("-CLK", "BCLKN"), ("-RES", "BRESN")], "GND")
    x = U("74HCT688", "A25-A31 all ones")
    w(x, "-G", "GND"); w(x, "-P=Q", "TOPN")
    for i in range(7):
        w(x, "P%d" % i, "A%d" % (25 + i)); w(x, "Q%d" % i, "VCC")
    w(x, "P7", "GND"); w(x, "Q7", "GND")
    gal("VHI", "memory and register windows")
    gal("VSEL", "cycle decode")
    gal("VBE", "byte lanes, -ACK")
    gal("VDEC", "register reads")
    gal("VWR", "register writes")
    od = U("74HC05", "open drain: -ACK, -WAIT, the PS/2 lines")
    for g_, (a, y) in enumerate((("ACK", "-ACK"), ("REQ", "-WAIT"), ("KCKDRV", "KCLK"), ("KDTDRV", "KDAT"),
                                 ("MCKDRV", "MCLK"), ("MDTDRV", "MDAT")), 1):
        w(od, "%dA" % g_, a); w(od, "%dY" % g_, y)
    for k in range(4):
        x = U("74HCT245", "lane %d: D%d-D%d <-> XD%d-XD%d" % (k, 8 * k, 8 * k + 7, 8 * k, 8 * k + 7))
        w(x, "DIR", "LDIR"); w(x, "-OE", "LE%dN" % k)
        for i in range(8):
            w(x, "A%d" % (i + 1), "D%d" % (8 * k + i)); w(x, "B%d" % (i + 1), "XD%d" % (8 * k + i))
    RN("XD0-7 pull-downs (a register's unused bits, an empty PS/2 data register read 0)", "GND",
       ["XD%d" % i for i in range(8)])
    RN("XD8-15 pull-downs", "GND", ["XD%d" % i for i in range(8, 16)])

    # ---- the registers
    gal("VCTL", "control, interrupt enables, the interrupt", {"KF": "KF", "MF": "MF"})
    gal("VRD0", "lane 0 registers, the vector's device")
    gal("VRD1", "lane 1 status", {"KB": "KB", "MB": "MB"})
    x = U("74HCT32", "R0EN: GAL VRD0 drives lane 0 (a register or the vector)")
    w(x, "1A", "R0EN0"); w(x, "1B", "VHIT"); w(x, "1Y", "R0EN")
    gnd_unused(x, ["2A", "2B", "3A", "3B", "4A", "4B"], ["2Y", "3Y", "4Y"])
    x = U("74HCT244", "the vector's window (XD4-6) in the acknowledge cycle")
    w(x, "-1G", "VHITN"); w(x, "-2G", "VCC")
    for i, src in enumerate(WINJ + ["GND"]):
        w(x, "1A%d" % (i + 1), src); w(x, "1Y%d" % (i + 1), "XD%d" % (4 + i))
    gnd_unused(x, ["2A%d" % i for i in range(1, 5)], ["2Y%d" % i for i in range(1, 5)])
    for name, lane_, clk, oe in (("row", 2, "WROWC", "ROWRDN"), ("column", 3, "WCOLC", "COLRDN")):
        r = "CROW" if name == "row" else "CCOL"
        x = U("74HCT273", "the cursor %s (+0%d), cleared by -RES" % (name, lane_), r)
        w(x, "CLK", clk); w(x, "-MR", "BRESN")
        for i in range(8):
            w(x, "D%d" % i, "XD%d" % (8 * lane_ + i)); w(x, "Q%d" % i, "%s%d" % (r, i))
        buf541("the cursor %s read back" % name, [("%s%d" % (r, i), "XD%d" % (8 * lane_ + i)) for i in range(8)], oe)

    # ---- the clock and the counters
    x = U("OSC25M", "the dot clock: 25.175 MHz (VGA 640 x 480 at 60 Hz)", "osc")
    w(x, "EN", "VCC"); w(x, "OUT", "OSC")
    x = U("74ACT04", "the dot clock: DCLKN, DCLK")
    w(x, "1A", "OSC"); w(x, "1Y", "DCLKN"); w(x, "2A", "DCLKN"); w(x, "2Y", "DCLK")
    gnd_unused(x, ["3A", "4A", "5A", "6A"], ["3Y", "4Y", "5Y", "6Y"])
    counter("74F163", "DC: the dot in the character (0-7)", "DCLK", "DRESN", "VCC", "VCC", "VCC",
            ["GND"] * 4, DC + [None])
    counter("74F163", "HC0-3: the character in the line", "DCLK", "HCLRN", "VCC", "ENH", "VCC", ["GND"] * 4, HC[:4], "HRC")
    counter("74F163", "HC4-6", "DCLK", "HCLRN", "VCC", "ENH", "HRC", ["GND"] * 4, HC[4:] + [None])
    counter("74F163", "VC0-3: the line (0 = the frame's line 40)", "DCLK", "VCLRN", "VCC", "VENA", "VCC", ["GND"] * 4,
            VC[:4], "VRC0")
    counter("74F163", "VC4-7", "DCLK", "VCLRN", "VCC", "VENA", "VRC0", ["GND"] * 4, VC[4:8], "VRC1")
    counter("74F163", "VC8-9", "DCLK", "VCLRN", "VCC", "VENA", "VRC1", ["GND"] * 4, VC[8:] + [None, None])
    OA = ["OA%d" % i for i in range(16)]
    for k in range(4):
        d = [("VCC" if 4 * k + i == 12 else "GND") for i in range(4)]
        counter("74F161", "OA%d-%d: the overlay's byte address (from 1000 at the frame's start)" % (4 * k, 4 * k + 3),
                "DCLK", "VCC", "OALDN", "OENA", "VCC" if k == 0 else "ORC%d" % (k - 1), d, OA[4 * k:4 * k + 4],
                "ORC%d" % k if k < 3 else None)
    gal("HORZ", "the characters, sync")
    gal("VERT", "the lines")
    gal("VTIM", "slots, the handshake")
    gal("VSLOT", "the RAM's address")
    gal("VBUS", "the bus handshake (-CLK)")
    fr = U("74HCT393", "frames: BLINK (32 frames: about 1.9 Hz)", "blink")
    w(fr, "1CP", "VSR"); w(fr, "1MR", "GND"); w(fr, "1Q3", "FR3"); w(fr, "2CP", "FR3"); w(fr, "2MR", "GND")
    w(fr, "2Q0", "BLINK")
    c.unused(fr, "1Q0", "1Q1", "1Q2", "2Q1", "2Q2", "2Q3")

    # ---- the RAM and its paths
    TA = ["TA%d" % i for i in range(11)]
    gal("ROW5", "row x 5")
    x = U("74F283", "the text cell's address: TA7:4 = row x 5 + HC6:4 (low half)")
    for i in range(4):
        w(x, "A%d" % (i + 1), "R5%d" % i); w(x, "B%d" % (i + 1), (HC[4:7] + ["GND"])[i]); w(x, "S%d" % (i + 1), TA[4 + i])
    w(x, "C0", "GND"); w(x, "C4", "TAC")
    x = U("74F283", "TA10:8 (high half)")
    for i in range(4):
        w(x, "A%d" % (i + 1), ("R54", "R55", "R56", "GND")[i]); w(x, "B%d" % (i + 1), "GND")
    for i in range(3):
        w(x, "S%d" % (i + 1), TA[8 + i])
    w(x, "C0", "TAC"); c.unused(x, "S4", "C4")
    RA = ["RA%d" % i for i in range(15)]
    buf541("the text address -> RA0-7", [("HC%d" % i, RA[i]) for i in range(4)] + [("TA%d" % i, RA[i]) for i in range(4, 8)],
           "TXENN", "74ACT541")
    buf541("the text address -> RA8-14", [("TA%d" % i, RA[i]) for i in range(8, 11)] + [("GND", RA[i]) for i in range(11, 15)],
           "TXENN", "74ACT541")
    buf541("the overlay address -> RA0-7", [(OA[i + 1], RA[i]) for i in range(8)], "OVENN", "74ACT541")
    buf541("the overlay address -> RA8-14", [(OA[i + 1], RA[i]) for i in range(8, 15)], "OVENN", "74ACT541")
    buf541("the bus address -> RA0-7", [("HALF", "RA0")] + [("BA%d" % (i + 1), RA[i]) for i in range(1, 8)],
           "CPENN", "74ACT541")
    buf541("the bus address -> RA8-14", [("BA%d" % (i + 1), RA[i]) for i in range(8, 15)], "CPENN", "74ACT541")
    RD = ["RD%d" % i for i in range(16)]
    for k, (fn, we) in enumerate((("the even bytes (lanes 0, 2): characters", "WEEN"),
                                  ("the odd bytes (lanes 1, 3): attributes", "WEON"))):
        x = U("CY7C199", "video RAM, %s" % fn, "ram%d" % k)
        w(x, "-CE", "GND"); w(x, "-OE", "ROEN"); w(x, "-WE", we)
        for i in range(15):
            w(x, "A%d" % i, RA[i])
        for i in range(8):
            w(x, "IO%d" % i, RD[8 * k + i])
    for k, (en, half) in enumerate((("WBAN", (0, 1)), ("WBBN", (2, 3)))):
        for j, ln in enumerate(half):
            buf541("write buffer: XD%d-%d (lane %d) -> RD%d-%d" % (8 * ln, 8 * ln + 7, ln, 8 * j, 8 * j + 7),
                   [("XD%d" % (8 * ln + i), RD[8 * j + i]) for i in range(8)], en)
    for k, (ck, half) in enumerate((("RLKA", (0, 1)), ("RLKB", (2, 3)))):
        for j, ln in enumerate(half):
            latch("read latch: RD%d-%d -> XD%d-%d (lane %d)" % (8 * j, 8 * j + 7, 8 * ln, 8 * ln + 7, ln),
                  RD[8 * j:8 * j + 8], ["XD%d" % (8 * ln + i) for i in range(8)], ck, "RLOEN")

    # ---- the text and the overlay
    CH = ["CH%d" % i for i in range(8)]
    latch("CL: the character", RD[:8], CH, "TLK")
    latch("AL: the attribute, the cursor's match, text / drawn here", RD[8:12] + ["CEQN", "TOUT", "OOUT"],
          ["AT0", "AT1", "AT2", "AT3", "CEQL", "TOUTL", "OOUTL"], "TLK")
    x = U("SST39SF010A", "the font: 256 characters x 16 lines (video/font8x16.bin)", "font")
    for i in range(4):
        w(x, "A%d" % i, "VC%d" % i)
    for i in range(8):
        w(x, "A%d" % (4 + i), CH[i])
    for i in range(12, 17):
        w(x, "A%d" % i, "GND")
    w(x, "-CE", "GND"); w(x, "-OE", "GND"); w(x, "-WE", "VCC"); c.unused(x, "NC1", "NC30")
    FD = ["FD%d" % i for i in range(8)]
    for i in range(8):
        w(x, "DQ%d" % i, FD[i])
    FQ = ["FQ%d" % i for i in range(8)]
    latch("FL: the character's dots", FD, FQ, "BCK", dev="74ACT574")
    OB = ["OB%d" % i for i in range(8)]
    latch("OL0: an even overlay byte", RD[:8], OB, "BCK", "OSEL", dev="74ACT574")
    latch("OL1: an odd overlay byte", RD[8:], OB, "BCK", "OSELN", dev="74ACT574")
    for name, src, out in (("the text", FQ, "TD"), ("the overlay", OB, "OD")):
        x = U("74F151", "%s's dot (bit 7 first)" % name)
        w(x, "-E", "GND"); w(x, "S0", "DC0"); w(x, "S1", "DC1"); w(x, "S2", "DC2"); w(x, "Y", out)
        c.unused(x, "-W")
        for i in range(8):
            w(x, "D%d" % i, src[7 - i])
    for k, (cq, p, q) in enumerate((("CEQ1N", [VC[4 + i] for i in range(5)] + ["GND"] * 3, ["CROW%d" % i for i in range(8)]),
                                    ("CEQN", HC + ["GND"], ["CCOL%d" % i for i in range(8)]))):
        x = U("74HCT688", "the cursor: %s" % ("its row (VC8:4)" if k == 0 else "its column (HC)"))
        w(x, "-G", "GND" if k == 0 else "CEQ1N"); w(x, "-P=Q", cq)
        for i in range(8):
            w(x, "P%d" % i, p[i]); w(x, "Q%d" % i, q[i])
    gal("CELL", "a character's attributes")
    gal("VOUT", "the dots")
    x = U("74ACT541", "the VGA drivers")
    w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
    for j, (src, dst) in enumerate((("VID", "VR"), ("VID", "VG"), ("VID", "VB"), ("VBR", "VRB"), ("VBR", "VGB"),
                                    ("VBR", "VBB"), ("HSO", "HSD"), ("VSO", "VSD"))):
        w(x, "A%d" % j, src); w(x, "Y%d" % j, dst)
    c.part("J4", "HD15F_RA", "VGA (DE-15) at the front edge, its opening facing out, labelled VGA")
    for gun in "RGB":
        R("680", "V%s" % gun, "GUN%s" % gun, "%s: 0.5 V into 75 ohms (a dot)" % gun)
        R("1.5k", "V%sB" % gun, "GUN%s" % gun, "%s: with the 680, 0.7 V (a bright dot)" % gun)
        w("J4", str(VGA[gun]), "GUN%s" % gun)
    R("47", "HSD", "HSYNC", "HSYNC series"); w("J4", str(VGA["HS"]), "HSYNC")
    R("47", "VSD", "VSYNC", "VSYNC series"); w("J4", str(VGA["VS"]), "VSYNC")
    for p in VGA_GND:
        w("J4", str(p), "GND")
    w("J4", "SHELL", "GND")
    c.unused("J4", *[str(p) for p in VGA_NC])

    # ---- PS/2
    x = U("74HCT14", "PS/2 inputs (Schmitt)")
    for g_, (a, y) in enumerate((("KCLK", "KCLKS"), ("KDAT", "KDATS"), ("MCLK", "MCLKS"), ("MDAT", "MDATS")), 1):
        w(x, "%dA" % g_, a); w(x, "%dY" % g_, y)
    gnd_unused(x, ["5A", "6A"], ["5Y", "6Y"])
    x = U("74HCT175", "PS/2 lines in step with the bus clock (-Q: the lines' own sense)")
    w(x, "CP", "BCLK"); w(x, "-MR", "VCC")
    for k, (d, q) in enumerate((("KCLKS", "KCK1"), ("KDATS", "KDAT1"), ("MCLKS", "MCK1"), ("MDATS", "MDAT1")), 1):
        w(x, "D%d" % k, d); w(x, "-Q%d" % k, q); c.unused(x, "Q%d" % k)
    x = U("74HC280", "the command byte's parity (odd)")
    for i, p in enumerate("ABCDEFGH"):
        w(x, p, "XD%d" % i)
    w(x, "I", "GND"); w(x, "SEVEN", "PAR"); c.unused(x, "SODD", "NC3")
    x = U("74HCT74", "the parity bits: keyboard, mouse")
    for k, (ck, q) in enumerate((("KCMDK", "KPAR"), ("MCMDK", "MPAR")), 1):
        w(x, "%dD" % k, "PAR"); w(x, "%dCLK" % k, ck); w(x, "-%dPRE" % k, "VCC"); w(x, "-%dCLR" % k, "VCC")
        w(x, "%dQ" % k, q); c.unused(x, "-%dQ" % k)
    nd = U("74HCT00", "a data register onto lane 0 only while FULL (else the pull-downs: 0)")
    w(nd, "1A", "KRD"); w(nd, "1B", "KF"); w(nd, "1Y", "KDOEN")
    w(nd, "2A", "MRDP"); w(nd, "2B", "MF"); w(nd, "2Y", "MDOEN")
    gnd_unused(nd, ["3A", "3B", "4A", "4B"], ["3Y", "4Y"])
    c.part("F1", "PTC", "PS/2 +5 V, both sockets")
    w("F1", "1", "VCC"); w("F1", "2", "PS2V")
    for port, (P, name) in enumerate((("K", "keyboard"), ("M", "mouse"))):
        gal("PS2" + P, "PS/2 %s" % name, {"CK1": P + "CK1", "BC0": P + "BC0", "BC1": P + "BC1", "BC2": P + "BC2",
                                          "BC3": P + "BC3", "Q7": P + "Q7", "RDS": "KRD" if port == 0 else "MRDP",
                                          "WRS": P + "CMDC", "CK2": P + "CK2", "SH": P + "SH", "FULL": P + "F",
                                          "BUSY": P + "B", "TXA": P + "TXA", "DTDRV": P + "DTDRV",
                                          "CKDRV": P + "CKDRV", "CNT": P + "CNT", "CLRN": P + "CLRN"})
        counter("74HCT163", "PS/2 %s: the bits (or, before sending, the line ticks)" % name, "BCLK", P + "CLRN",
                "BRESN", P + "CNT", P + "CNT", ["GND"] * 4, [P + "BC%d" % i for i in range(4)])
        x = U("74HCT595", "PS/2 %s: receive (D0 first: QH = bit 0)" % name, "rx" + P)
        w(x, "SER", P + "DAT1"); w(x, "SRCLK", P + "SH"); w(x, "RCLK", P + "F"); w(x, "-SRCLR", "VCC")
        w(x, "-OE", P + "DOEN"); c.unused(x, "QH'")
        for i, k in enumerate("ABCDEFGH"):
            w(x, "Q" + k, "XD%d" % (7 - i))
        x = U("74HCT597", "PS/2 %s: send (D0 first: H = bit 0; the parity shifts in behind)" % name, "tx" + P)
        for i, k in enumerate("ABCDEFGH"):
            w(x, k, "XD%d" % (7 - i))
        w(x, "RCK", P + "CMDK"); w(x, "-SRLOAD", P + "TXA"); w(x, "SRCK", P + "SH"); w(x, "SER", P + "PAR")
        w(x, "-SRCLR", "VCC"); w(x, "QH'", P + "Q7")
        j = "J%d" % (5 + port)
        c.part(j, "MINIDIN6F_RA", "PS/2 %s (mini-DIN-6) at the front edge, its opening facing out, labelled %s"
               % (name, "KEYBOARD" if port == 0 else "MOUSE"))
        w(j, str(PS2PIN["DATA"]), P + "DAT"); w(j, str(PS2PIN["CLK"]), P + "CLK")
        w(j, str(PS2PIN["GND"]), "GND"); w(j, str(PS2PIN["VCC"]), "PS2V"); w(j, "SHELL", "GND")
        c.unused(j, "2", "6")
        R("4.7k", P + "CLK", "VCC", "PS/2 %s clock pull-up" % name)
        R("4.7k", P + "DAT", "VCC", "PS/2 %s data pull-up" % name)

    # ---- interrupts
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
    """The jumpers as the documents and the kernel assume them (D52): window 5, level 4."""
    j = {"WIN%d" % i: WINDOW >> i & 1 for i in range(3)}
    j.update({"L%d" % i: LEVEL >> i & 1 for i in range(3)})
    return j

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("video card: %d errors" % len(errs))
    print("video card OK: %d ICs (%d GALs), %d nets, %d parts" % (len(ics), len(G), len(c.nets), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
