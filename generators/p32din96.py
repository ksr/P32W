#!/usr/bin/env python3
"""p32din96.py -- the DIN 96 shorts tester's KiCad board (D129), placed here; routed by Freerouting
(generators/build_board.sh din96-tester). Run under KiCad's Python. The netlist is card_din96.py's.

  * 4 layers: F.Cu and B.Cu signals, In1 GND, In2 VCC (as P8X's cards; Ken: "2 or 4 layers, not 6").
  * X1, the plug, on the left edge, its row a 5.08 mm in (PLUG_EDGE), pin 1 at the bottom. X2, the
    socket, parallel to it with every pin at its plug pin's height. Between them the six expanders, each lying
    beside the five and a third rows of pins it has: a pin's track runs from the plug to its expander pin and
    on to the socket, level.
  * The Nano at the right, its USB port at the bottom edge; above it, by the right edge (the front: D48),
    the POWER, PASS and FAIL LEDs, the TEST button and the TYPE switch, its eight levers numbered and a table
    of which is which connector (four are spare). Between the socket and the Nano, RN2, the select lines'
    pull-ups (D132).
  * Every text reads upright with the board standing in a backplane, its plug down (D137): KiCad's 270 as
    drawn, but for the board's name line, which runs up the side.
GENERATORS ARE CANON: edit this file or card_din96.py, then rebuild.
"""
import os, sys, pcbnew
from pcbnew import VECTOR2I

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "microcode"))
import p32kicad_pcb as K                                  # noqa: E402  (the cards' geometry and helpers)
from p32net import DEV, FOOTPRINT                         # noqa: E402
import card_din96 as T                                   # noqa: E402

mm, P = K.mm, K.P
BW, BH = 106.0, 112.0
YC = 50.0                  # the connectors' centre, from the top edge
PLUG_EDGE = K.PLUG_EDGE    # the plug's row a, from the board's edge: as on every card (D130, D131)
XU = 22.0                  # the expanders' pin 1
XS = 66.0                  # the socket's column c (its nearest to the expanders)
XN, YN = 84.5, 68.8        # the Nano's pin 1 (D1): its USB end at the bottom edge
ROWP = 2.54

def build():
    card = T.build()
    parts, nets = card.parts, card.nets
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    board.GetDesignSettings().m_NetSettings.GetDefaultNetclass().SetClearance(mm(0.13))   # (as the cards: two tracks between DIN pads)
    netobj = {}
    for n in nets:
        ni = pcbnew.NETINFO_ITEM(board, n); board.Add(ni); netobj[n] = ni
    fps = {}
    def load(ref):
        dev, fn, val = parts[ref]
        lib, name = FOOTPRINT[DEV[dev]["pkg"]].split(":")
        fp = pcbnew.FootprintLoad(os.path.join(K.P32LIB if lib == "P32W" else K.FP, lib + ".pretty"), name)
        if fp is None:
            raise SystemExit("tester: no footprint %s for %s" % (name, ref))
        fp.SetFPID(pcbnew.LIB_ID(lib, name)); fp.SetReference(ref); fp.SetValue(val)
        if fp.GetAttributes() & pcbnew.FP_SMD:
            raise SystemExit("tester: %s is not through-hole" % ref)
        board.Add(fp); fps[ref] = fp
        return fp
    def pad_of(ref, pin):
        return DEV[parts[ref][0]]["pins"][pin][0]
    def put(ref, pin, x, y, rot):
        """ref turned rot degrees, its pin `pin` at (x, y)"""
        fp = fps.get(ref) or load(ref)
        fp.SetPosition(P(0, 0)); fp.SetOrientationDegrees(rot)
        a = next(p for p in fp.Pads() if p.GetNumber() == pad_of(ref, pin)).GetPosition()
        fp.SetPosition(VECTOR2I(mm(x) - a.x, mm(y) - a.y))
        return fp
    def at(ref, pin):
        q = next(p for p in fps[ref].Pads() if p.GetNumber() == pad_of(ref, pin)).GetPosition()
        return pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)

    # --- the plug: turned 90, its row a PLUG_EDGE from the edge, pin 1 at the bottom ---
    fp = load("X1"); fp.SetPosition(P(0, 0)); fp.SetOrientationDegrees(90)
    x0, y0, x1, y1 = K.pad_bbox(fp)
    a1 = next(p for p in fp.Pads() if p.GetNumber() == pad_of("X1", "A1")).GetPosition()
    fp.SetPosition(VECTOR2I(mm(PLUG_EDGE) - a1.x, mm(YC) - (y0 + y1) // 2))
    xa, y_a1 = at("X1", "A1")
    assert abs(xa - PLUG_EDGE) < 0.01 and x0 < a1.x, "the plug's row a is its nearest to the edge"
    def row_y(n):
        return y_a1 - (n - 1) * ROWP
    assert abs(at("X1", "A32")[1] - row_y(32)) < 0.01 and abs(at("X1", "C1")[0] - (xa + 5.08)) < 0.01, "the plug's turn"
    # --- the socket: turned 180, so that its pin 1 is at the bottom too; column c toward the expanders ---
    put("X2", "C1", XS, row_y(1), 180)
    for pin in ("A1", "B17", "C32"):
        px, py = at("X2", pin)
        assert abs(py - row_y(int(pin[1:]))) < 0.01 and abs(px - (XS + 2.54 * (2 - "ABC".index(pin[0])))) < 0.01, ("the socket's turn", pin)
    # --- the expanders: lying down (turned 90: pins 1-14 the lower row, left to right), each at the middle
    #     height of its sixteen pins; its 100 nF under its VDD and VSS pins ---
    for k in range(T.NCHIP):
        u, c = "U%d" % (k + 1), "C%d" % (k + 1)
        yc = round(row_y(1) - (16 * k + 7.5) / 3.0 * ROWP, 2)
        put(u, "GPB0", XU, yc + 3.81, 90)
        assert abs(at(u, "GPA7")[1] - (yc - 3.81)) < 0.01 and abs(at(u, "SO")[0] - (XU + 13 * 2.54)) < 0.01, ("an expander's turn", u)
        vx, vy = at(u, "VDD")
        put(c, "1", vx - 0.5, vy + 2.96, 0)
    # --- the Nano: its USB port at the bottom edge, its digital pins toward the expanders ---
    put("A1", "D1", XN, YN, 0)
    assert at("A1", "D12")[1] > at("A1", "D1")[1] and at("A1", "VIN")[0] > XN, "the Nano's turn"
    put("C7", "+", 77.6, 70.0, 0)
    # the TYPE levers' pull-ups: standing beside the Nano's A0-A7, a resistor level with each pin
    ax, ay = at("A1", "A0")
    put("RN1", "R1", 103.6, ay, 90)
    assert abs(at("RN1", "R8")[1] - at("A1", "A7")[1]) < 0.01 and abs(at("RN1", "COM")[1] - (ay + 2.54)) < 0.01, "the network's turn"
    # the select lines' pull-ups (D132): between the socket and the Nano, a resistor level with each of D5-D10 on
    # its way to the expanders, the common pin above (its labels above that, under C7, whose own go above it)
    # and two spare below; nearer the Nano than the socket, clear of the socket's "a1"
    put("RN2", "R1", 78.5, at("A1", "D5")[1], 270)
    assert all(abs(at("RN2", "R%d" % (k + 1))[1] - at("A1", T.NANO_CS[k])[1]) < 0.01 for k in range(T.NCHIP)) \
        and at("RN2", "COM")[1] < at("RN2", "R1")[1], "the select pull-ups' turn"
    # --- by the right edge, above the Nano: the LEDs with their resistors, TEST, TYPE ---
    for i, (led, res) in enumerate((("D1", "R1"), ("D2", "R2"), ("D3", "R3"))):
        y = 8.5 + 8.5 * i
        f = put(led, "K", 98.0, y, 0); K.round_led(f)
        put(res, "1", 84.0, y, 0)
    put("SW1", "1", 96.5, 32.0, 0)
    put("SW2", "1", 89.0, 42.0, 0)

    # --- pads to nets; the unused pins as KiCad's schematic names them ---
    for n, ps in nets.items():
        for r, pin in ps:
            num = pad_of(r, pin)
            for pd in fps[r].Pads():
                if pd.GetNumber() == num:
                    pd.SetNet(netobj[n])
    for r, pin in card.nc:
        num = pad_of(r, pin)
        kname = "~{%s}" % pin[1:] if pin.startswith("-") else pin
        ni = pcbnew.NETINFO_ITEM(board, "unconnected-(%s-Pad%s)" % (r, num) if kname == num
                                 else "unconnected-(%s-%s-Pad%s)" % (r, kname, num))
        board.Add(ni)
        for pd in fps[r].Pads():
            if pd.GetNumber() == num:
                pd.SetNet(ni)
    unplaced = [r for r in parts if r not in fps]
    if unplaced:
        raise SystemExit("tester: not placed: %s" % ", ".join(unplaced))

    # --- outline, planes ---
    for xa_, ya_, xb_, yb_ in ((0, 0, BW, 0), (BW, 0, BW, BH), (BW, BH, 0, BH), (0, BH, 0, 0)):
        e = pcbnew.PCB_SHAPE(board); e.SetShape(pcbnew.SHAPE_T_SEGMENT)
        e.SetStart(P(xa_, ya_)); e.SetEnd(P(xb_, yb_)); e.SetLayer(pcbnew.Edge_Cuts); e.SetWidth(mm(0.15)); board.Add(e)
    for layer, n in ((pcbnew.In1_Cu, "GND"), (pcbnew.In2_Cu, "VCC")):
        z = pcbnew.ZONE(board); z.SetLayer(layer); z.SetNet(netobj[n])
        z.SetIsFilled(True); K.thermals(z)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.13))
        o = z.Outline(); o.NewOutline()
        for x, y in ((1, 1), (BW - 1, 1), (BW - 1, BH - 1), (1, BH - 1)):
            o.Append(mm(x), mm(y))
        board.Add(z)
    K.add_mounting_keepouts(board, fps)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    label(board, card, fps)
    outdir = os.environ.get("P32_PCB_OUT") or os.path.join(ROOT, "hardware", T.NAME, "kicad")
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, T.NAME + ".kicad_pcb")
    pcbnew.SaveBoard(out, board)
    return out, len(parts)

# The tester stands in a backplane with its plug down, as a card does, and every text reads upright so (Ken,
# 2026-11-18, every board of every system: "all silkscreen text reads correctly with the card held connector
# down"). The plug is on the left edge as drawn, so standing, the board is turned a quarter to the left, and an
# upright text is at KiCad's 270 as drawn. label() places each text on the standing board: X across it from
# the left (the drawing's y), Y down it from the front edge (BW less the drawing's x).
UP = 270

def S(X, Y):
    """a point of the standing board, as drawn"""
    return BW - Y, X

def standing(x0, y0, x1, y1):
    """a box as drawn (left, top, right, bottom), as the standing board has it"""
    return y0, BW - x1, y1, BW - x0

# under the TYPE switch, the board standing: what to do with it; beside that, which lever is which connector
TYPE_LINES = ["TYPE: TURN ON", "THE ONE LEVER", "THAT IS THE", "CONNECTOR'S.", "NONE ON: THE", "TESTER ALONE."]

def lever_table():
    """'1 P32W J1' ... '5-8 SPARE': each lever's number (printed at the lever) and its connector"""
    out = ["%d %s" % (i, T.TYPES[i][0]) for i in range(1, len(T.TYPES))]
    spare = list(range(len(T.TYPES), T.LEVERS + 1))
    if spare:
        out.append(("%d-%d SPARE" % (spare[0], spare[-1])) if len(spare) > 1 else "%d SPARE" % spare[0])
    return out

def label(board, card, fps):
    """Every part's reference and what it is, outside its outline (D48, D90), every text upright with the board
    standing on its plug (D137): what each LED shows, what the button and the switch do, the levers' numbers and
    which connector each is; which connector is which, and its pin a1; the board's name, revision and
    cottageworker.com, running up the side (the rule's one exception). The TEST button and the TYPE switch go
    by their function alone (Ken, 2026-11-18; pcb-check.ini's exempt_labels)."""
    L, R, C_ = pcbnew.GR_TEXT_H_ALIGN_LEFT, pcbnew.GR_TEXT_H_ALIGN_RIGHT, pcbnew.GR_TEXT_H_ALIGN_CENTER
    def put(t, X, Y, size, just, thick, angle):
        t.SetLayer(pcbnew.F_SilkS); t.SetTextThickness(mm(thick)); t.SetTextSize(VECTOR2I(mm(size), mm(size)))
        t.SetHorizJustify(just); t.SetPosition(P(*S(X, Y))); t.SetTextAngleDegrees(angle)
        t.SetKeepUpright(False)                       # (KiCad's "keep upright" would draw a 270 as a 90)
    def txt(field, X, Y, size=0.8, just=C_):          # a part's own field
        field.SetVisible(True); put(field, X, Y, size, just, 0.15, UP)
    def silk(s, X, Y, size=0.8, just=C_, thick=0.15, angle=UP):
        t = pcbnew.PCB_TEXT(board); t.SetText(s); put(t, X, Y, size, just, thick, angle); board.Add(t)
    def box(ref):                                     # its courtyard, on the standing board
        b = fps[ref].GetCourtyard(pcbnew.F_CrtYd).BBox()
        return standing(*[pcbnew.ToMM(v) for v in (b.GetLeft(), b.GetTop(), b.GetRight(), b.GetBottom())])
    def pad(ref, pin):                                # a pin, on the standing board
        q = next(p for p in fps[ref].Pads() if p.GetNumber() == DEV[card.parts[ref][0]]["pins"][pin][0]).GetPosition()
        return pcbnew.ToMM(q.y), BW - pcbnew.ToMM(q.x)
    def conn_y(ref):                                  # a connector's line, over its courtyard (X1's clear of a via
        return box(ref)[1] - (1.8 if ref == "X1" else 1.5)   # under it; X2's of RN2 over it)
    for ref, fp in fps.items():
        dev = card.parts[ref][0]
        val = K.short_value(dev, fp.GetValue())
        bl, bt, br, bb = box(ref); cx, cy = (bl + br) / 2, (bt + bb) / 2
        fp.Value().SetVisible(False)
        if ref in ("X1", "X2"):                       # a line above it from its left end: which it is, what it does
            txt(fp.Reference(), 6.0, conn_y(ref), 0.9)  # (and "a1" over pin a1, below)
            silk("%s  %s" % (val, "PLUG: INTO A BACKPLANE'S SOCKET" if ref == "X1" else "SOCKET: ONTO A CARD'S PLUG"),
                 9.5, conn_y(ref), 0.9, just=L)
        elif dev == "MCP23S17":                       # two lines above it, toward the socket: which it is, what it is
            txt(fp.Reference(), cx, bt - 3.9)
            txt(fp.Value(), cx, bt - 2.1)             # (its value whole, as the schematic has it: KiCad compares the two)
        elif dev == "C100N":                          # under it, between two expanders: which it is, its value
            txt(fp.Reference(), cx, bb + 1.3); silk(val, cx, bb + 2.75)
        elif dev == "C10U":                           # to its left: under it are RN2's, under the Nano's
            txt(fp.Reference(), bl - 0.5, cy - 0.8, 0.9, just=R); silk(val, bl - 0.5, cy + 0.75, just=R)
        elif dev == "RNET9":                          # to the left of its first pin
            txt(fp.Reference(), bl - 6.9, cy); silk(val, bl - 2.6, cy)
        elif dev == "NANO":                           # under it, over RN2
            txt(fp.Reference(), cx - 6.4, bb + 1.35, just=L); silk(val.upper(), cx - 3.2, bb + 1.35, just=L)
        elif dev == "LED":                            # what it shows over it, toward the front edge; its reference to
            silk(card.parts[ref][1], cx, bt - 1.45, 0.9)  # its left; K by its cathode
            txt(fp.Reference(), bl - 0.5, cy, just=R)
            kx, ky = pad(ref, "K")
            silk("K", kx, ky + (1.9 if ky > cy else -1.9))
        elif dev == "R":                              # to its left, which it is over its value
            txt(fp.Reference(), bl - 0.4, cy - 0.8, just=R); txt(fp.Value(), bl - 0.4, cy + 0.8, just=R)
        elif ref == "SW1":                            # TEST under it
            fp.Reference().SetVisible(False)
            silk("TEST", cx, bb + 1.3, 1.0, thick=0.2)
        elif ref == "SW2":                            # TYPE over it and each lever's number at the lever; under it,
            fp.Reference().SetVisible(False)          # what to do, and beside that which lever is which connector
            for i in range(1, T.LEVERS + 1):
                (px, py), (qx, qy) = pad(ref, str(i)), pad(ref, str(2 * T.LEVERS + 1 - i))
                assert abs(px - qx) < 0.01, "the TYPE switch must stand with its levers up and down"
                silk(str(i), px, bt - 1.0)
            silk("TYPE", cx, bt - 3.1, 0.9)
            for i, line in enumerate(TYPE_LINES):
                silk(line, bl + 0.2, bb + 1.6 + 1.45 * i, just=L)
            for i, line in enumerate(lever_table()):
                silk(line, bl + 12.2, bb + 1.6 + 1.45 * i, just=L)
        else:
            raise SystemExit("tester: no label for %s" % ref)
    for g in fps["A1"].GraphicalItems():              # the Nano's own "USB", at its USB end
        if isinstance(g, pcbnew.PCB_TEXT) and g.GetLayer() == pcbnew.F_SilkS:
            g.SetTextAngleDegrees(UP); g.SetKeepUpright(False)
    for ref in ("X1", "X2"):                          # pin a1: over it, on the line of the connector's name
        ax, ay = pad(ref, "A1")
        silk("a1", ax, conn_y(ref))
    # what it may be plugged onto: over the plug's line
    silk("ONLY ON A BOARD WITH NOTHING BUT ITS CONNECTORS FITTED, AND NO POWER ON IT", 5.1, conn_y("X1") - 1.85, 0.9,
         just=L)
    # the name line, as drawn along the bottom edge: standing, it runs up the right-hand side
    silk("%s   REV %s   cottageworker.com" % (card.title, getattr(card, "rev", "1")), BH - 5.0, BW - 41.5, 1.5,
         thick=0.2, angle=0)

if __name__ == "__main__":
    out, n = build()
    print("wrote %s (%.0f x %.0f mm, 4 layers, %d parts)" % (out, BW, BH, n))
