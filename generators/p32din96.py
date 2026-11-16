#!/usr/bin/env python3
"""p32din96.py -- the DIN 96 shorts tester's KiCad board (D129), placed here; routed by Freerouting
(generators/build_board.sh din96-tester). Run under KiCad's Python. The netlist is card_din96.py's.

  * 4 layers: F.Cu and B.Cu signals, In1 GND, In2 VCC (as P8X's cards; Ken: "2 or 4 layers, not 6").
  * X1, the plug, on the left edge, placed as a P32W card's connector is (pin 1 at the bottom). X2, the
    socket, parallel to it with every pin at its plug pin's height. Between them the six expanders, each lying
    beside the five and a third rows of pins it has: a pin's track runs from the plug to its expander pin and
    on to the socket, level.
  * The Nano at the right, its USB port at the bottom edge; above it, by the right edge (the front: D48),
    the POWER, PASS and FAIL LEDs, the TEST button and the TYPE switch, a connector's name at each of its
    eight levers (four are spare).
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

    # --- the plug, as a card's connector: turned 90, its pads 4 mm in from the edge, pin 1 at the bottom ---
    fp = load("X1"); fp.SetPosition(P(0, 0)); fp.SetOrientationDegrees(90)
    x0, y0, x1, y1 = K.pad_bbox(fp)
    fp.SetPosition(VECTOR2I(mm(4) - x0, mm(YC) - (y0 + y1) // 2))
    xa, y_a1 = at("X1", "A1")
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

# beside the TYPE switch: what to do with it. (Each lever's connector is printed at the lever.)
TYPE_LINES = ["TYPE: TURN ON", "THE ONE LEVER", "THAT IS THE", "CONNECTOR'S.", "NONE ON: THE", "TESTER ALONE."]

def label(board, card, fps):
    """Every part's reference and what it is, outside its outline (D48, D90); what each LED shows, what the
    button and the switches do, the table of connector types; which connector is which; the board's name,
    revision and cottageworker.com."""
    L, R, C_ = pcbnew.GR_TEXT_H_ALIGN_LEFT, pcbnew.GR_TEXT_H_ALIGN_RIGHT, pcbnew.GR_TEXT_H_ALIGN_CENTER
    def txt(field, x, y, size=0.8, angle=0, just=C_):
        field.SetVisible(True); field.SetLayer(pcbnew.F_SilkS)
        field.SetTextSize(VECTOR2I(mm(size), mm(size))); field.SetTextThickness(mm(0.15))
        field.SetHorizJustify(just); field.SetPosition(P(x, y)); field.SetTextAngleDegrees(angle)
    def silk(s, x, y, size=0.8, just=C_, angle=0, thick=0.15):
        t = pcbnew.PCB_TEXT(board); t.SetText(s); t.SetPosition(P(x, y)); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextThickness(mm(thick)); t.SetTextSize(VECTOR2I(mm(size), mm(size)))
        t.SetHorizJustify(just); t.SetTextAngleDegrees(angle); board.Add(t)
    def box(ref):
        b = fps[ref].GetCourtyard(pcbnew.F_CrtYd).BBox()
        return [pcbnew.ToMM(v) for v in (b.GetLeft(), b.GetTop(), b.GetRight(), b.GetBottom())]
    def pads(ref):
        x0, y0, x1, y1 = K.pad_bbox(fps[ref])
        return [pcbnew.ToMM(v) for v in (x0, y0, x1, y1)]
    def pad(ref, pin):
        q = next(p for p in fps[ref].Pads() if p.GetNumber() == DEV[card.parts[ref][0]]["pins"][pin][0]).GetPosition()
        return pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)
    for ref, fp in fps.items():
        dev = card.parts[ref][0]
        val = K.short_value(dev, fp.GetValue())
        x0, y0, x1, y1 = pads(ref); cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        bl, bt, br, bb = box(ref)
        fp.Value().SetVisible(False)
        if ref == "X1":                               # along its inner side
            txt(fp.Reference(), br + 1.3, cy - 22.0, 0.9, 90)
            silk("%s  PLUG: INTO A BACKPLANE'S SOCKET" % val, br + 1.3, cy + 2.0, 0.9, angle=90)
        elif ref == "X2":
            txt(fp.Reference(), br + 1.3, 6.0, 0.9, 90)
            silk("%s  SOCKET: ONTO A CARD'S PLUG" % val, br + 1.3, 24.0, 0.9, angle=90)
        elif dev == "MCP23S17":                       # a line above its upper row of pins: its body lies between the rows
            txt(fp.Reference(), x0 - 0.8, y0 - 2.3, just=L)
            txt(fp.Value(), x1 + 0.8, y0 - 2.3, just=R)     # (its value whole, as the schematic has it: KiCad compares the two)
        elif dev == "C100N":                          # to its left, on its own line between two expanders
            txt(fp.Reference(), bl - 4.4, cy, just=R)
            silk(val, bl - 0.6, cy, just=R)
        elif dev == "C10U":
            txt(fp.Reference(), cx, bb + 0.9, 0.9); silk(val, cx, bb + 2.1)
        elif dev == "RNET9":                          # above its top pin, reading up the board's edge
            txt(fp.Reference(), cx, bt - 6.9, angle=90); silk(val, cx, bt - 2.6, angle=90)
        elif dev == "NANO":
            txt(fp.Reference(), bl, bt - 0.75, just=L); silk(val.upper(), br, bt - 0.75, just=R)
        elif dev == "LED":                            # its reference over it, what it shows under it; K by its cathode
            txt(fp.Reference(), cx, cy - 3.0)
            kx, ky = pad(ref, "K")
            silk("K", kx + (-1.9 if kx < cx else 1.9), ky)
            silk(card.parts[ref][1], cx, cy + 3.4, 0.9)
            # its colour too, beside it: the three look alike on the board, and green must mean PASS
            silk(fp.GetValue().split()[-1].upper()[:3].replace("GRE", "GRN"), br + 0.5, cy, 0.8, just=L)     # YEL, GRN, RED
        elif dev == "R":
            txt(fp.Reference(), cx, cy - 2.4); txt(fp.Value(), cx, cy + 2.4)
        elif ref == "SW1":
            txt(fp.Reference(), bl - 0.7, cy - 0.7, just=R); silk("TEST", bl - 0.7, cy + 0.8, 1.0, just=R, thick=0.2)
        elif ref == "SW2":                            # each lever's connector beside it; what to do, to its left
            txt(fp.Reference(), cx - 2.0, bt - 0.75); silk("TYPE", cx + 2.4, bt - 0.75, 0.9)
            for i in range(1, T.LEVERS + 1):
                px, py = pad(ref, str(i)); qx, qy = pad(ref, str(2 * T.LEVERS + 1 - i))
                assert abs(py - qy) < 0.01, "the TYPE switch must lie with its levers across"
                silk(T.TYPES[i][0] if i < len(T.TYPES) else "SPARE", max(px, qx) + 1.5, py, just=L)
            for i, line in enumerate(TYPE_LINES):
                silk(line, bl - 11.0, bt + 1.3 + 1.45 * i, 0.8, just=L)
        else:
            raise SystemExit("tester: no label for %s" % ref)
    for ref in ("X1", "X2"):                          # pin a1, beside it and outside the connector's body
        ax, ay = pad(ref, "A1"); bl, bt, br, bb = box(ref)
        silk("a1", br + 0.6, ay, 0.8, just=L)
    silk("%s   REV %s   cottageworker.com" % (card.title, getattr(card, "rev", "1")), 41.5, BH - 5.0, 1.5, thick=0.2)
    silk("ONLY ON A BOARD WITH NOTHING BUT ITS CONNECTORS FITTED, AND NO POWER ON IT", 41.5, BH - 2.2, 0.9)

if __name__ == "__main__":
    out, n = build()
    print("wrote %s (%.0f x %.0f mm, 4 layers, %d parts)" % (out, BW, BH, n))
