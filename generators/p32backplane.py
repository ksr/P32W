#!/usr/bin/env python3
"""p32backplane.py -- the P32W backplane's KiCad board (D49, D64, D68, D77,
D78), placed AND routed here: no autorouter. Run under KiCad's Python:

    PYK=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
    "$PYK" generators/p32backplane.py

Writes hardware/backplane/kicad/backplane.kicad_pcb; generators/build_board.sh
backplane adds the gerbers, renders, placement and DRC. The netlist is
card_backplane.py's (every socket pin on p32bus.busnet()).

  * Eleven slots, 25.4 mm (1 inch) apart, slot 0 at the left. Each has
    three female DIN 41612 sockets, S<n>J1-J3, rotated so that every pin sits
    exactly where the card generator puts the card's pin (pin 1 at the
    bottom: the sockets' heights come from p32kicad_pcb's own placement).
  * 6 layers (D77): F.Cu (column a), In1 GND, In2 (column b), In3 (column
    c), In4 VCC, B.Cu (the request lines' risers, short links).
  * Routing as on P32's backplane: a bused net is one pin in every slot, at
    one height; each pin drops a 1.27 mm stub into the channel between its
    row and the next (0.99 mm between 1.55 mm pads: a 0.2 mm track at 0.13 mm
    clearance) and one straight track runs along the channel, on its
    column's layer. A pulled-up or terminated net runs on past slot 10 to
    its resistor, standing on the net's own row.
  * The request lines (-BREQn / -BGNTn, D62): slot n's A16 / A17 to slot 2's
    (the arbiter) and slot 0's (the debug card) C pin. The C pins are one
    channel between slots 0 and 2, carried on along the same row to slot n;
    there a via and a riser on B.Cu, between two pin columns, climb to the
    A pin.
  * The resistor networks stand at the right, each on eight consecutive
    rows of one column (card_backplane.py groups them so), their common pin
    on the row below: every network pin is on its net's own row.
  * The power entry (D78): M4 bolts, +5V and GND at each end; the TL7705A
    supervisor at the left end, on PWROK's row (its channel runs left from
    slot 0 to it).
GENERATORS ARE CANON: edit this file or card_backplane.py, then rebuild.
"""
import os, sys, pcbnew
from pcbnew import VECTOR2I

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "microcode"))
import p32kicad_pcb as K                                  # noqa: E402  (the cards' geometry, helpers)
from p32net import DEV, FOOTPRINT                         # noqa: E402
from p32bus import ALL_SLOTS, CONNECTORS, DEBUG_SLOT, CPU_SLOT, MMU_SLOT   # noqa: E402
import card_backplane as B                                # noqa: E402

mm, P = K.mm, K.P
BH = K.BH9U                # the cards' connector edge: 366.7 mm
PITCH = 25.4               # slot pitch (D49, D64)
X0 = 42.0                  # slot 0's column a
COLX = {"a": 0.0, "b": -2.54, "c": -5.08}   # the columns, from column a (the socket turned 180 degrees)
ROWP = 2.54
CH = 1.27                  # a row's channel: half a pitch toward the next row
W = 0.2                    # every track (D19)
VIA_D, VIA_H = 0.6, 0.3
LAYER = {"a": pcbnew.F_Cu, "b": pcbnew.In2_Cu, "c": pcbnew.In3_Cu}
RISER = pcbnew.B_Cu

def slot_x(s):
    return X0 + s * PITCH

def card_a1_y():
    """Each connector's pin a1 height on the cards: p32kicad_pcb's own
    placement (rotation 90, centred on CONN_Y), so the sockets cannot disagree."""
    lib, name = FOOTPRINT["DIN41612-C96-R"].split(":")
    out = {}
    for j, yc in K.CONN_Y.items():
        fp = pcbnew.FootprintLoad(os.path.join(K.FP, lib + ".pretty"), name)
        fp.SetPosition(P(0, 0)); fp.SetOrientationDegrees(90)
        x0, y0, x1, y1 = K.pad_bbox(fp)
        fp.SetPosition(VECTOR2I(mm(4) - x0, mm(yc) - (y0 + y1) // 2))
        a1 = next(p for p in fp.Pads() if p.GetNumber() == "a1")
        out[j] = pcbnew.ToMM(a1.GetPosition().y)
    return out

A1Y = card_a1_y()

def row_y(conn, n):
    return A1Y[conn] - (n - 1) * ROWP           # pin 1 at the bottom, as on the cards

def pin_xy(slot, conn, pin):
    """A socket pin's centre ('A5' -> column a, row 5)."""
    col, n = pin[0].lower(), int(pin[1:])
    return slot_x(slot) + COLX[col], row_y(conn, n)

def build():
    card = B.build()
    parts, nets = card.parts, card.nets
    where = {rp: n for n, ps in nets.items() for rp in ps}
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(6)
    board.GetDesignSettings().m_NetSettings.GetDefaultNetclass().SetClearance(mm(0.13))
    netobj = {}
    for n in nets:
        ni = pcbnew.NETINFO_ITEM(board, n); board.Add(ni); netobj[n] = ni

    fps = {}
    def load(ref):
        dev, fn, val = parts[ref]
        lib, name = FOOTPRINT[DEV[dev]["pkg"]].split(":")
        path = os.path.join(K.P32LIB if lib == "P32W" else K.FP, lib + ".pretty")
        fp = pcbnew.FootprintLoad(path, name)
        if fp is None:
            raise SystemExit("backplane: no footprint %s for %s" % (name, ref))
        fp.SetFPID(pcbnew.LIB_ID(lib, name)); fp.SetReference(ref); fp.SetValue(val)
        board.Add(fp); fps[ref] = fp
        return fp
    def pad_of(ref, pin):
        return DEV[parts[ref][0]]["pins"][pin][0]
    def put(ref, pin, x, y, rot=0, other=None):
        """Place ref turned rot degrees with its pin `pin` at (x, y); with
        other=(pin2, dx, dy) try the four turns for the one that puts pin2
        in that direction from pin."""
        fp = fps.get(ref) or load(ref)
        rots = [rot] if other is None else [0, 90, 180, 270]
        for r in rots:
            fp.SetPosition(P(0, 0)); fp.SetOrientationDegrees(r)
            pd = {p.GetNumber(): p.GetPosition() for p in fp.Pads()}
            a = pd[pad_of(ref, pin)]
            if other:
                b = pd[pad_of(ref, other[0])]
                dx, dy = b.x - a.x, b.y - a.y
                if not ((dx > 0) == (other[1] > 0) and (dx == 0) == (other[1] == 0) and
                        (dy > 0) == (other[2] > 0) and (dy == 0) == (other[2] == 0)):
                    continue
            fp.SetPosition(VECTOR2I(mm(x) - a.x, mm(y) - a.y))
            return fp
        raise SystemExit("backplane: no turn of %s puts %s that way" % (ref, other[0]))

    # --- the sockets -----------------------------------------------------------
    for s in ALL_SLOTS:
        for conn in CONNECTORS:
            put(B.sock(s, conn), "A1", *pin_xy(s, conn, "A1"), rot=180)
    for s in ALL_SLOTS:                               # (check the turn: c32 where pin_xy says)
        fp = fps[B.sock(s, "J1")]
        c32 = next(p for p in fp.Pads() if p.GetNumber() == "c32").GetPosition()
        want = pin_xy(s, "J1", "C32")
        assert abs(pcbnew.ToMM(c32.x) - want[0]) < 0.01 and abs(pcbnew.ToMM(c32.y) - want[1]) < 0.01, (s, want)

    # --- the nets on the sockets ---------------------------------------------
    sockpins = {}                                     # net -> [(slot, conn, pin)]
    for n, ps in nets.items():
        if n in ("VCC", "GND"):
            continue
        for r, p in ps:
            if r.startswith("S") and "J" in r:
                s, conn = int(r[1:r.index("J")]), r[r.index("J"):]
                sockpins.setdefault(n, []).append((s, conn, p))

    tracks = []                                       # (layer, x1, y1, x2, y2, net), checked below
    vias = []
    def seg(layer, x1, y1, x2, y2, net):
        if (x1, y1) != (x2, y2):
            tracks.append((layer, x1, y1, x2, y2, net))

    # where a pulled-up or terminated net ends: (x, y) of its resistor's pin,
    # filled in when the resistors are placed; the channel runs on to it
    ends = {}

    # --- the right-hand field: networks, pull-ups, termination, supervisor ----
    XR = slot_x(max(ALL_SLOTS)) + 3.3 + 4.5           # past slot 10's courtyard
    rns = sorted(r for r in parts if parts[r][0] == "RNET9")
    def net_pin(net):
        """(conn, pin) of a bused net (the same pin in every slot)."""
        pins = {(conn, p) for s, conn, p in sockpins[net]}
        assert len(pins) == 1, (net, pins)
        return pins.pop()
    def net_y(net):
        conn, p = net_pin(net)
        return row_y(conn, int(p[1:]))
    # Packed from the left, each part as far left as its courtyard allows:
    # every pin of every part here stands on a socket row, so the channels
    # pass between the pins of whatever they run under.
    occupied = []
    def boxes(refs, top=0.0):
        """Courtyards (mm); top: room above for the reference."""
        out = []
        for r in refs:
            bb = fps[r].GetCourtyard(pcbnew.F_CrtYd).BBox()
            out.append((pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()) - top,
                        pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())))
        return out
    def clear(bs):
        return not any(a[0] < b[2] + 0.25 and b[0] < a[2] + 0.25 and a[1] < b[3] + 0.25 and b[1] < a[3] + 0.25
                       for a in bs for b in occupied)
    def pack(place_at, refs, x, step=0.5, top=0.0):
        while True:
            place_at(x)
            bs = boxes(refs, top)
            if clear(bs):
                occupied.extend(bs)
                return x
            x += step
    rn_rows = {}
    for ref in rns:
        used = [(i, where[(ref, "R%d" % i)]) for i in range(1, 9) if (ref, "R%d" % i) in where]
        cols = {net_pin(n)[1][0].lower() for i, n in used}
        conns = {net_pin(n)[0] for i, n in used}
        r0s = {int(net_pin(n)[1][1:]) - (i - 1) for i, n in used}
        if len(cols) != 1 or len(conns) != 1 or len(r0s) != 1:
            raise SystemExit("backplane: %s's nets are not eight rows of one column (card_backplane.py's "
                             "grouping): %s" % (ref, ", ".join("R%d %s %s" % (i, n, net_pin(n)) for i, n in used)))
        rn_rows[ref] = (conns.pop(), cols.pop(), r0s.pop(), used)
    for ref in sorted(rns, key=lambda r: (rn_rows[r][0], rn_rows[r][2], rn_rows[r][1])):
        conn, col, r0, used = rn_rows[ref]
        # COM on the row below R1, R1..R8 upward, a row each
        x = pack(lambda x: put(ref, "COM", x, row_y(conn, r0 - 1), other=("R1", 0, -1)), [ref], XR, 4.0, top=1.8)
        for i, n in used:
            ends[n] = (x, row_y(conn, r0 + i - 1))
    XD = max(b[2] for b in occupied) + 1.5            # the single resistors: pull-ups, termination
    singles = []                                      # (y, ref, net pin, net, termination cap)
    for ref, (dev, fn, val) in parts.items():
        if dev != "R" or ref == "R1":                 # (R1: the LED's)
            continue
        if "termination" in fn:
            n = where.get((ref, "1")); capref = next(cr for cr, (cd, cf, cv) in parts.items() if cd == "C150P" and cf == fn)
            singles.append((net_y(n), ref, "1", n, capref))
        else:
            n = where[(ref, "2")]
            singles.append((net_y(n), ref, "2", n, None))
    for y, ref, pin, n, cap in sorted(singles):
        other = "1" if pin == "2" else "2"
        def place_at(x, ref=ref, pin=pin, y=y, other=other, cap=cap):
            put(ref, pin, x, y, other=(other, 1, 0))
            if cap:                                   # the termination's 150 pF on the same row
                put(cap, "1", x + 12.7, y, other=("2", 1, 0))
        x = pack(place_at, [ref] + ([cap] if cap else []), XD)
        ends[n] = (x, y)
        if cap:                                       # (a B.Cu link from the resistor to it)
            seg(RISER, x + 10.16, y, x + 12.7, y, where[(ref, "2")])
    ypw = net_y("PWROK")                              # the supervisor: pin 5 (-RESET) on PWROK's row,
    u1 = next(r for r in parts if parts[r][0] == "TL7705A")   # pins 5-8 down the left column, 1-4 to the right
    capref = next(r for r, (d, f, v) in parts.items() if "REF" in f)
    capct = next(r for r, (d, f, v) in parts.items() if "CT" in f)
    capdec = next(r for r, (d, f, v) in parts.items() if f == "decoupling %s" % u1)
    def upos(pin):
        q = next(p for p in fps[u1].Pads() if p.GetNumber() == pad_of(u1, pin)).GetPosition()
        return pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)
    def place_u1(x):
        put(u1, "-RESET", x, ypw, other=("VCC", 0, 1))
        (xr, yr), (xc, yc_) = upos("REF"), upos("CT")
        put(capref, "1", xr + 4.0, yr, other=("2", 1, 0))
        put(capct, "+", xc + 9.0, yc_, other=("-", 1, 0))
        put(capdec, "1", x, ypw + 5 * ROWP, other=("2", 1, 0))
    XU = 8.0                                          # at the left end, beside slot 0: the far end is crowded
    place_u1(XU); occupied.extend(boxes([u1, capref, capct, capdec]))
    ends["PWROK_U1"] = (XU, ypw)
    (xref, yref), (xct, yct) = upos("REF"), upos("CT")
    seg(RISER, xref, yref, xref + 4.0, yref, "PWR_REF")
    seg(RISER, xct, yct, xct + 9.0, yct, "PWR_CT")
    BW = round(max(b[2] for b in occupied) + 15.0)   # room for the corner holes

    # --- routing: bused nets, then the request lines ---------------------------
    for n, sp in sockpins.items():
        groups = {}
        for s, conn, p in sp:
            groups.setdefault((conn, p), []).append(s)
        if len(groups) == 1:                          # bused: one channel
            (conn, p), slots = next(iter(groups.items()))
            col, y = p[0].lower(), row_y(conn, int(p[1:]))
            L, yc = LAYER[col], y - CH
            xs = [pin_xy(s, conn, p)[0] for s in slots]
            for x in xs:
                seg(L, x, y, x, yc, n)
            xa, xe = min(xs), max(xs)
            for key in (n, n + "_U1"):                # on to the resistor (and the supervisor)
                if key in ends:
                    ex, ey = ends[key]
                    seg(L, ex, yc, ex, ey, n); xa, xe = min(xa, ex), max(xe, ex)
            seg(L, xa, yc, xe, yc, n)
            continue
        # a request line: the trunk (the most pins on one pin; on a tie, column c)
        trunk = max(groups, key=lambda k: (len(groups[k]), k[1][0] == "C"))
        conn, p = trunk
        col, yt = p[0].lower(), row_y(conn, int(p[1:]))
        L, yc = LAYER[col], yt - CH
        xs = [pin_xy(s, conn, p)[0] for s in groups[trunk]]
        for x in xs:
            seg(L, x, yt, x, yc, n)
        for (bconn, bp), bslots in groups.items():
            if (bconn, bp) == trunk:
                continue
            for s in bslots:
                xb, yb = pin_xy(s, bconn, bp)
                xv = xb + (CH if int(bp[1:]) % 2 == 0 else -CH)   # even row: outside column a; odd: toward b
                xs.append(xv)
                seg(L, xv, yc, xv, yt, n)                          # up from the channel to the via, on the row
                vias.append((xv, yt, n))
                seg(RISER, xv, yt, xv, yb, n); seg(RISER, xv, yb, xb, yb, n)
        seg(L, min(xs), yc, max(xs), yc, n)

    # check: no two nets' tracks overlap on one layer (DRC is the full check)
    def overlap(a, b):
        (la, ax1, ay1, ax2, ay2, na), (lb, bx1, by1, bx2, by2, nb) = a, b
        if la != lb or na == nb:
            return False
        g = (W + 0.13) / 2 + W / 2
        return (min(ax1, ax2) - g < max(bx1, bx2) and min(bx1, bx2) - g < max(ax1, ax2) and
                min(ay1, ay2) - g < max(by1, by2) and min(by1, by2) - g < max(ay1, ay2))
    byrow = {}
    for t in tracks:
        byrow.setdefault((t[0], round(min(t[2], t[4]), 1)), []).append(t)
    clash = [(a, b) for k, ts in byrow.items() for i, a in enumerate(ts) for b in ts[i + 1:] if overlap(a, b)]
    if clash:
        raise SystemExit("backplane: tracks overlap: %s" % "; ".join("%s / %s" % (a[5], b[5]) for a, b in clash[:8]))
    for layer, x1, y1, x2, y2, n in tracks:
        t = pcbnew.PCB_TRACK(board); t.SetStart(P(x1, y1)); t.SetEnd(P(x2, y2))
        t.SetWidth(mm(W)); t.SetLayer(layer); t.SetNet(netobj[n]); board.Add(t)
    for x, y, n in vias:
        v = pcbnew.PCB_VIA(board); v.SetPosition(P(x, y)); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_H))
        v.SetNet(netobj[n]); board.Add(v)

    # --- the rest: the power entry, the LED, the bypass and bulk caps, holes ---
    gap1 = (A1Y["J1"] + 5.63 + 3.0, row_y("J2", 32) - 5.63 - 3.0)    # between J1 and J2 (clear of the sockets' pegs)
    gap2 = (A1Y["J2"] + 5.63 + 3.0, row_y("J3", 32) - 5.63 - 3.0)
    lugs = sorted(r for r in parts if parts[r][0] == "LUG_M4")
    left = [r for r in lugs if "slot 0 end" in parts[r][1]]
    right = [r for r in lugs if r not in left]
    for refs, x in ((left, 11.0), (right, BW - 11.0)):
        for k, r in enumerate(sorted(refs, key=lambda r: parts[r][1].startswith("GND"))):
            put(r, "1", x, gap1[0] + 8.0 + 16.0 * k)
    bulk = sorted(r for r in parts if parts[r][0] == "C1000U")
    put(bulk[0], "+", 22.0, gap2[0] + 10.0, other=("-", 1, 0))
    put(bulk[1], "+", BW - 16.0, gap2[0] + 10.0, other=("-", 1, 0))
    put("R1", "1", 6.0, gap2[0] + 26.0, other=("2", 1, 0))
    put("D1", "A", 21.0, gap2[0] + 26.0, other=("K", 1, 0))
    seg_link = [(6.0 + 10.16, gap2[0] + 26.0, 21.0, gap2[0] + 26.0)]
    for x1, y1, x2, y2 in seg_link:
        t = pcbnew.PCB_TRACK(board); t.SetStart(P(x1, y1)); t.SetEnd(P(x2, y2))
        t.SetWidth(mm(W)); t.SetLayer(RISER); t.SetNet(netobj["LED5V"]); board.Add(t)
    for s in ALL_SLOTS:                               # bypass: in the gaps beside each slot's sockets
        xs = slot_x(s) - 2.54 - 1.25
        for ref, (dev, fn, val) in parts.items():
            if fn == "bypass, slot %d J1" % s:
                put(ref, "1", xs, gap1[0] + 2.0, other=("2", 1, 0))
            elif fn == "bypass, slot %d J2" % s:
                put(ref, "1", xs, gap2[0] + 2.0, other=("2", 1, 0))
            elif fn == "bypass, slot %d J3" % s:
                put(ref, "1", xs, gap2[1] - 6.0, other=("2", 1, 0))
            elif fn == "bulk, slot %d" % s:
                put(ref, "+", xs + 0.25, gap1[0] + 9.0, other=("-", 1, 0))
    holes = sorted(r for r in parts if parts[r][0] == "MH_M3")
    # the corners, and four in the gaps between the sockets (the channels
    # fill the top and bottom edges from slot 0 to the networks)
    hpos = [(7.0, 7.0), (BW - 7.0, 7.0), (7.0, BH - 7.0), (BW - 7.0, BH - 7.0)]
    hpos += [(slot_x(s) + PITCH / 2, sum(g) / 2) for s, g in ((3, gap1), (7, gap1), (1, gap2), (9, gap2))]
    for r, (x, y) in zip(holes, hpos):
        put(r, "1", x, y)
    unplaced = [r for r in parts if r not in fps]
    if unplaced:
        raise SystemExit("backplane: not placed: %s" % ", ".join(unplaced))

    # --- pads to nets ------------------------------------------------------------
    for n, ps in nets.items():
        for r, pin in ps:
            num = pad_of(r, pin)
            for pd in fps[r].Pads():
                if pd.GetNumber() == num:
                    pd.SetNet(netobj[n])
    # unused pins: the net KiCad's schematic gives a no-connect pin, so the
    # board and the generated schematic agree (as p32kicad_pcb.py does)
    for r, pin in card.nc:
        num = pad_of(r, pin)
        kname = "~{%s}" % pin[1:] if pin.startswith("-") else pin
        ni = pcbnew.NETINFO_ITEM(board, "unconnected-(%s-Pad%s)" % (r, num) if kname == num
                                 else "unconnected-(%s-%s-Pad%s)" % (r, kname, num))
        board.Add(ni)
        for pd in fps[r].Pads():
            if pd.GetNumber() == num:
                pd.SetNet(ni)
    for r in parts:                                   # the bolts are parts to buy: KiCad's mounting-hole
        if parts[r][0] == "LUG_M4":                   # footprint leaves itself out of the BOM
            fps[r].SetAttributes(fps[r].GetAttributes() & ~pcbnew.FP_EXCLUDE_FROM_BOM)

    # --- outline, planes, silk -----------------------------------------------------
    for xa, ya, xb, yb in ((0, 0, BW, 0), (BW, 0, BW, BH), (BW, BH, 0, BH), (0, BH, 0, 0)):
        e = pcbnew.PCB_SHAPE(board); e.SetShape(pcbnew.SHAPE_T_SEGMENT)
        e.SetStart(P(xa, ya)); e.SetEnd(P(xb, yb)); e.SetLayer(pcbnew.Edge_Cuts); e.SetWidth(mm(0.15)); board.Add(e)
    for layer, n in ((pcbnew.In1_Cu, "GND"), (pcbnew.In4_Cu, "VCC")):
        z = pcbnew.ZONE(board); z.SetLayer(layer); z.SetNet(netobj[n])
        z.SetIsFilled(True); z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.13))
        o = z.Outline(); o.NewOutline()
        for x, y in ((1, 1), (BW - 1, 1), (BW - 1, BH - 1), (1, BH - 1)):
            o.Append(mm(x), mm(y))
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    label(board, card, fps, BW, gap1, gap2)
    out = os.path.join(ROOT, "hardware", "backplane", "kicad", "backplane.kicad_pcb")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    pcbnew.SaveBoard(out, board)
    return out, BW, len(tracks), len(vias)

def label(board, card, fps, BW, gap1, gap2):
    """References beside their parts, never under them (D48); what each slot
    is for; +5V / GND at each bolt; the board's name, revision and
    cottageworker.com."""
    C_ = pcbnew.GR_TEXT_H_ALIGN_CENTER
    def txt(field, x, y, size=0.9, angle=0):
        field.SetVisible(True); field.SetLayer(pcbnew.F_SilkS)
        field.SetTextSize(VECTOR2I(mm(size), mm(size))); field.SetTextThickness(mm(0.15))
        field.SetHorizJustify(C_); field.SetPosition(VECTOR2I(int(x), int(y))); field.SetTextAngleDegrees(angle)
    def silk(s, x, y, size=1.2):
        t = pcbnew.PCB_TEXT(board); t.SetText(s); t.SetPosition(P(x, y)); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextThickness(mm(0.2)); t.SetTextSize(VECTOR2I(mm(size), mm(size))); t.SetHorizJustify(C_); board.Add(t)
    for ref, fp in fps.items():
        dev, fn, val = card.parts[ref]
        pkg = DEV[dev]["pkg"]
        crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        cx, cy = (crt.GetLeft() + crt.GetRight()) // 2, (crt.GetTop() + crt.GetBottom()) // 2
        fp.Value().SetVisible(False)
        if dev == "DIN96F":                           # beside the socket, along it
            txt(fp.Reference(), crt.GetRight() + mm(1.2), cy, 1.0, 90)
        elif pkg == "SIP9":                           # above it (the networks stand 0.8 mm apart)
            txt(fp.Reference(), cx, crt.GetTop() - mm(0.9), 0.8)
        elif dev == "TL7705A":
            txt(fp.Reference(), cx, crt.GetTop() - mm(2.4), 0.9)
            txt(fp.Value(), cx, crt.GetTop() - mm(1.0), 0.8)
        elif dev in ("MH_M3",):
            fp.Reference().SetVisible(False)
        elif dev == "LUG_M4":
            txt(fp.Reference(), cx, crt.GetBottom() + mm(1.4), 0.9)
            silk("+5V" if "+5V" in fn else "GND", pcbnew.ToMM(cx), pcbnew.ToMM(crt.GetTop()) - 1.6, 1.5)
        elif dev == "LED":
            txt(fp.Reference(), cx, crt.GetTop() - mm(1.0), 0.8)
            silk("+5V", pcbnew.ToMM(cx), pcbnew.ToMM(crt.GetBottom()) + 1.6, 1.0)
        else:                                         # resistors, caps: just below the body
            txt(fp.Reference(), cx, crt.GetBottom() + mm(0.9), 0.8)
    what = {DEBUG_SLOT: "DEBUG", CPU_SLOT: "CPU", MMU_SLOT: "MMU / DATA"}
    for s in ALL_SLOTS:
        x = slot_x(s) - 2.54
        silk("SLOT %d" % s, x, gap1[0] + 20.0, 1.2)
        if s in what:
            silk(what[s], x, gap1[0] + 22.0, 1.0)
    silk("%s   REV %s   cottageworker.com" % (card.title, getattr(card, "rev", "1")), slot_x(5) - 2.54, sum(gap2) / 2, 1.5)

if __name__ == "__main__":
    out, BW, nt, nv = build()
    print("wrote %s (%.1f x %.1f mm, 6 layers, %d tracks, %d vias)" % (out, BW, BH, nt, nv))
    sys.stdout.flush(); os._exit(0)
