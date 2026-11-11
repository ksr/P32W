#!/usr/bin/env python3
"""p32kicad_pcb.py -- build a P32W card's KiCad board from its canonical
netlist (adapted from P8X's gen_kicad.py). Run under KiCad's bundled Python:

    PYK=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
    "$PYK" generators/p32kicad_pcb.py <card>

Writes hardware/<card>/kicad/<card>.kicad_pcb (next to the generated
schematic, in the same KiCad project):
  * 9U x 340 mm, 366.7 mm along the connector edge (D49); J1-J3 (DIN 41612,
    male, right angle) on that edge, centred 50, 183.35 and 316.7 mm from
    the top edge (one per 3U) -- VERIFY against a subrack before fab
  * 6 layers on every card (D77): F.Cu, In1 = GND plane, In2, In3, In4 = VCC
    plane, B.Cu; a card in LAYERS may have 4 (as on P8X: F.Cu / B.Cu signals,
    In1 = GND, In2 = VCC) or 8 (In2..In5 signals, In6 = VCC)
  * ICs in courtyard-spaced rows, each with its 100 nF cap beside its top
    edge; activity LEDs and their resistors in a bank on the far edge
  * copper keepouts around the connector mounting holes (metal screws)
Routing is done afterwards by Freerouting (generators/build_board.sh).
GENERATORS ARE CANON: edit this file or the card netlist, then rebuild.
"""
import os, re, sys, math, pcbnew
from pcbnew import VECTOR2I

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "microcode"))
import p32cards  # noqa: E402,F401  (loads every card)
from p32net import CARDS, DEV, FOOTPRINT  # noqa: E402

FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
P32LIB = os.path.join(ROOT, "hardware", "lib")          # the project's own (generators/gen_footprints.py)
BH = 366.7             # connector edge (9U), mm; a card may set its own (card.height), see card_size()
BH9U = BH
BW = 340.0             # card depth, mm (D49: 366.7 x 340)
CONN_Y = {"J1": 50.0, "J2": 183.35, "J3": 316.7}   # connector centres from the top edge (VERIFY)
LAYERS = {}            # copper layers per card, default 6 (D77; D19 had 4)
GX0 = 32.0             # placement starts this far from the connector edge
SCREW_KEEPOUT_R = 4.0
JSTEP = 8.0            # jumpers along the bottom edge, this far apart (room for their labels)

def mm(v): return pcbnew.FromMM(float(v))
def P(x, y): return VECTOR2I(mm(x), mm(y))

def classify(dev):
    if dev == "DIN96": return "conn"
    if dev == "C100N": return "decap"
    if dev == "LED":   return "led"
    if dev == "R":     return "res"
    if DEV[dev]["pkg"].startswith("DIP"): return "ic"
    return "misc"

def pad_bbox(fp):
    xs = [p.GetPosition().x for p in fp.Pads()]
    ys = [p.GetPosition().y for p in fp.Pads()]
    return min(xs), min(ys), max(xs), max(ys)

def place_centered(fp, x_mm, y_mm, rot=0):
    fp.SetPosition(P(0, 0))
    fp.SetOrientationDegrees(rot)
    x0, y0, x1, y1 = pad_bbox(fp)
    fp.SetPosition(VECTOR2I(mm(x_mm) - (x0 + x1) // 2, mm(y_mm) - (y0 + y1) // 2))

def size_after(fp, rot):
    """(w, h) in mm of the courtyard (the part body) after rotation."""
    fp.SetPosition(P(0, 0))
    fp.SetOrientationDegrees(rot)
    try:
        bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
        if w > 0.5 and h > 0.5:
            return w, h
    except Exception:
        pass
    x0, y0, x1, y1 = pad_bbox(fp)
    return pcbnew.ToMM(x1 - x0), pcbnew.ToMM(y1 - y0)

def affinity_order(grid, nets, anchors=("J1", "J2"), decay=0.7):
    """Order parts so that ones sharing nets come out next to each other.
    Affinity between two parts: over the nets they share (not GND/VCC), 1/(k-1)
    for a net joining k parts -- a point-to-point net binds harder than a
    bus. Start from the part most bound to the backplane connectors (they sit
    on the left edge, where the packing starts); then repeatedly take the part
    most bound to those already placed, recent ones counting most (decay per
    step), so tight clusters stay together."""
    from collections import defaultdict
    aff = defaultdict(float)
    for net, mem in nets.items():
        if net in ("GND", "VCC"):
            continue
        refs = sorted({r for r, _ in mem})
        if len(refs) < 2:
            continue
        wt = 1.0 / (len(refs) - 1)
        for a in refs:
            for b in refs:
                if a != b:
                    aff[a, b] += wt
    rank = {r: i for i, r in enumerate(grid)}            # ties keep netlist order
    left = set(grid)
    first = max(grid, key=lambda r: (sum(aff[r, j] for j in anchors), -rank[r]))
    order = [first]; left.discard(first)
    score = defaultdict(float)
    while left:
        for r in left:                                    # age the old pull, add the newest part's
            score[r] = score[r] * decay + aff[r, order[-1]]
        nxt = max(left, key=lambda r: (score[r], -rank[r]))
        order.append(nxt); left.discard(nxt)
    return order

def wirelength(board):
    """Half-perimeter wire length (mm) over all signal nets: the usual quick
    estimate of how hard a placement is to route."""
    box = {}
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if not n or n in ("GND", "VCC") or n.startswith("unconnected-"):
                continue
            x, y = pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
            b = box.setdefault(n, [x, y, x, y])
            b[0], b[1], b[2], b[3] = min(b[0], x), min(b[1], y), max(b[2], x), max(b[3], y)
    return sum((b[2] - b[0]) + (b[3] - b[1]) for b in box.values())

def refine_by_swaps(footp, movable, capfor, passes=30, same_row=False):
    """Swap pairs of parts with the same footprint (identical slots, so the
    packing stays valid; each IC's decoupling cap moves with it) whenever the
    swap shortens the half-perimeter wire length; repeat until no swap helps.
    same_row: only parts in the same row swap (a card's rows by function keep
    their parts; across rows the I/O card's swaps had carried a byte lane's
    374 into another row, and its load bus could not be routed).
    Returns the number of swaps made."""
    mm = pcbnew.ToMM
    centre, pins, fixed = {}, {}, {}
    mov = set(movable)
    for ref, fp in footp.items():
        c = fp.GetPosition()
        for p in fp.Pads():
            n = p.GetNetname()
            if not n or n in ("GND", "VCC"):
                continue
            q = p.GetPosition()
            if ref in mov:
                pins.setdefault(ref, []).append((n, mm(q.x - c.x), mm(q.y - c.y)))
            else:
                fixed.setdefault(n, []).append((mm(q.x), mm(q.y)))
        if ref in mov:
            centre[ref] = (mm(c.x), mm(c.y))
    nets_of = {r: {n for n, _, _ in pins.get(r, [])} for r in mov}
    def hpwl(n):
        pts = list(fixed.get(n, []))
        for r in users[n]:
            cx, cy = centre[r]
            pts += [(cx + ox, cy + oy) for m, ox, oy in pins[r] if m == n]
        xs = [x for x, _ in pts]; ys = [y for _, y in pts]
        return (max(xs) - min(xs)) + (max(ys) - min(ys)) if pts else 0.0
    users = {}
    for r in mov:
        for n in nets_of[r]:
            users.setdefault(n, set()).add(r)
    groups = {}
    for r in movable:
        key = str(footp[r].GetFPIDAsString())
        groups.setdefault((key, footp[r].GetPosition().y) if same_row else key, []).append(r)
    made = 0
    for _ in range(passes):
        better = False
        for g in groups.values():
            for i in range(len(g)):
                for j in range(i + 1, len(g)):
                    a, b = g[i], g[j]
                    touched = nets_of[a] | nets_of[b]
                    before = sum(hpwl(n) for n in touched)
                    centre[a], centre[b] = centre[b], centre[a]
                    if sum(hpwl(n) for n in touched) < before - 1e-6:
                        better = True; made += 1
                    else:
                        centre[a], centre[b] = centre[b], centre[a]
        if not better:
            break
    for r in movable:                                  # move the parts (and their caps) to their new slots
        old = footp[r].GetPosition()
        new = pcbnew.VECTOR2I(pcbnew.FromMM(centre[r][0]), pcbnew.FromMM(centre[r][1]))
        if old != new:
            if r in capfor:
                cp = footp[capfor[r]].GetPosition()
                footp[capfor[r]].SetPosition(pcbnew.VECTOR2I(cp.x + new.x - old.x, cp.y + new.y - old.y))
            footp[r].SetPosition(new)
    return made

def thermals(z):
    """plane pins on spokes, so an iron can heat them on a 6-layer board (D90)"""
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
    z.SetThermalReliefGap(mm(0.25)); z.SetThermalReliefSpokeWidth(mm(0.5))

def copper_layers(board):
    n = board.GetCopperLayerCount()
    return [pcbnew.F_Cu] + [getattr(pcbnew, "In%d_Cu" % i) for i in range(1, n - 1)] + [pcbnew.B_Cu]

def plane_layers(board):
    """(GND layer, VCC layer): GND right under the top signal layer, VCC right
    above the bottom one -- In1/In2 on 4 layers, In1/In4 on 6."""
    inner = copper_layers(board)[1:-1]
    return inner[0], inner[-1]

def add_mounting_keepouts(board, footp, r=SCREW_KEEPOUT_R):
    """Copper keepout (no fill, tracks or vias) around every DIN mounting hole
    (P8X: metal screws must not short to copper)."""
    n = 0
    for fp in footp.values():
        if "DIN41612" not in str(fp.GetFPIDAsString()):
            continue
        for p in fp.Pads():
            if p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH:
                continue
            cx, cy = pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
            z = pcbnew.ZONE(board)
            ls = pcbnew.LSET()
            for lyr in copper_layers(board):
                ls.AddLayer(lyr)
            z.SetLayerSet(ls)
            z.SetIsRuleArea(True)
            z.SetDoNotAllowZoneFills(True); z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True)
            z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
            o = z.Outline(); o.NewOutline()
            for i in range(32):
                a = 2 * math.pi * i / 32
                o.Append(mm(cx + r * math.cos(a)), mm(cy + r * math.sin(a)))
            board.Add(z); n += 1
    return n

def add_rf_keepouts(board, footp, want):
    """A wireless module's antenna end (card.rf_keepout = {ref: (length mm,
    side)}, side "+x" for the end facing the front edge, "-x" for the end
    facing inward, "-y" for the end facing the top edge): no copper on any
    layer -- planes, tracks or vias --
    between its pin rows over that length, nor from the module to the board's
    edge. A ground plane under an antenna detunes it."""
    for ref, (length, side) in want.items():
        fp = footp[ref]
        px0, py0, px1, py1 = [pcbnew.ToMM(v) for v in pad_bbox(fp)]
        crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        cx0, cx1 = pcbnew.ToMM(crt.GetLeft()), pcbnew.ToMM(crt.GetRight())
        cy0, cy1 = pcbnew.ToMM(crt.GetTop()), pcbnew.ToMM(crt.GetBottom())
        if side == "+x":
            areas = ((cx1 - length, py0 + 1.6, cx1, py1 - 1.6),         # between the pin rows
                     (cx1, cy0 - 1.0, BW, cy1 + 1.0))                   # module to board edge
        elif side == "-x":                                              # the antenna end facing inward (its
            areas = ((cx0, py0 + 1.6, cx0 + length, py1 - 1.6),         # USB port at the edge): between the pin
                     (cx0 - 6.0, cy0 - 1.0, cx0, cy1 + 1.0))            # rows, and 6 mm beyond the module
        else:
            areas = ((px0 + 1.6, cy0, px1 - 1.6, cy0 + length),
                     (cx0 - 1.0, 0.0, cx1 + 1.0, cy0))
        for xa, ya, xb, yb in areas:
            z = pcbnew.ZONE(board)
            ls = pcbnew.LSET()
            for lyr in copper_layers(board):
                ls.AddLayer(lyr)
            z.SetLayerSet(ls); z.SetIsRuleArea(True)
            z.SetDoNotAllowZoneFills(True); z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True)
            z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
            o = z.Outline(); o.NewOutline()
            for x, y in ((xa, ya), (xb, ya), (xb, yb), (xa, yb)):
                o.Append(mm(x), mm(y))
            board.Add(z)

# What a switch, button or LED does is printed beside it (D48). card.panel
# maps a part to its labels: a string is a line printed above the part (a
# button: "RESET"); a pair (ON, OFF) labels one lever of a DIP switch at each
# end, ON at the end the footprint marks "on" (pins n+1..2n, toward the front
# edge: the switch lies with its levers across the edge) and OFF at the other.
PANEL_CH = 0.7                                         # mm a character of the 0.9 mm label text

EDGE_MODULES = ("PICO", "TANG_NANO_20K", "WIZ850IO", "SOCK2X8")   # plug-in modules with a USB or network port

def panel_lines(card, r):
    return [x for x in getattr(card, "panel", {}).get(r, []) if isinstance(x, str)]

def panel_levers(card, r):
    return [x for x in getattr(card, "panel", {}).get(r, []) if isinstance(x, tuple)]

def panel_margins(card, r):
    """(right, left) room (mm) a DIP switch's lever labels take beside it."""
    lv = panel_levers(card, r)
    if not lv:
        return 0.0, 0.0
    return (1.6 + PANEL_CH * max(len(on) for on, _ in lv), 1.6 + PANEL_CH * max(len(off) for _, off in lv))

def card_size(card):
    """Set the board height for this card: 9U unless it says otherwise."""
    global BH
    BH = getattr(card, "height", BH9U)

TYPES = {"DIN96": "DIN41612", "DIN96F": "DIN41612", "DB9M_RA": "DB9", "HD15F_RA": "DE-15 VGA", "MINIDIN6F_RA": "MINI-DIN-6",
         "SOCK2X8": "CH376S", "PTC": "MF-RG500", "TANG_NANO_20K": "TANG NANO 20K", "WIZ850IO": "WIZ850io"}

def short_value(dev, val):
    """what a part is, as the silk says it: 100n, 10k x8, DB9 (Ken, 2026-11-02)"""
    if dev in TYPES:
        return TYPES[dev]
    if dev.startswith("IDC") and not dev.endswith("_RA"):
        return dev
    m = re.match(r"([\d.]+)\s*([pnu])F", val)
    if m:
        return m.group(1) + m.group(2)
    m = re.match(r"([\d.]+[kM]?) x (\d+)", val)
    if m:
        return "%s x%s" % m.groups()
    return val.replace("Raspberry Pi ", "").split(",")[0].split(" (")[0]

def label(board, card):
    """Silkscreen labels, each clear of its part's outline and of the
    neighbours, and never under a part's body: a reference and a value must
    stay readable with the chips, sockets and modules in (D48, D90). Also run
    on a routed board (--label): labels are silk only, so relabelling never
    needs a re-route."""
    fps = list(board.GetFootprints())                   # (copy first: Remove() below upsets live iterators)
    old = [d for d in board.GetDrawings()               # free text from an earlier labelling
           if d.Type() == pcbnew.PCB_TEXT_T and d.GetLayer() == pcbnew.F_SilkS]
    for d in old:
        board.Remove(d)
    C_ = pcbnew.GR_TEXT_H_ALIGN_CENTER
    def txt(field, x, y, size=0.85, angle=0, just=C_):
        field.SetVisible(True); field.SetLayer(pcbnew.F_SilkS)
        field.SetTextSize(VECTOR2I(mm(size), mm(size))); field.SetTextThickness(mm(0.15))
        field.SetHorizJustify(just); field.SetPosition(VECTOR2I(int(x), int(y)))
        field.SetTextAngleDegrees(angle)
    def above(fp, gap=0.7):                           # just above the courtyard (the body and its socket)
        return fp.GetCourtyard(pcbnew.F_CrtYd).BBox().GetTop() - mm(gap)
    def silk(s, x, y, size=1.2):
        t = pcbnew.PCB_TEXT(board); t.SetText(s); t.SetPosition(P(x, y))
        t.SetLayer(pcbnew.F_SilkS); t.SetTextThickness(mm(0.2))
        t.SetTextSize(VECTOR2I(mm(size), mm(size))); t.SetHorizJustify(C_); board.Add(t)
    def silk_at(s, x, y, just=C_, size=0.9):          # (x, y in board units)
        t = pcbnew.PCB_TEXT(board); t.SetText(s); t.SetPosition(VECTOR2I(int(x), int(y)))
        t.SetLayer(pcbnew.F_SilkS); t.SetTextThickness(mm(0.15)); t.SetTextSize(VECTOR2I(mm(size), mm(size)))
        t.SetHorizJustify(just); board.Add(t)
        return t
    def inside_other(fp, x, y):                       # the courtyard of another part at this point, or None
        for o in fps:
            box = o.GetCourtyard(pcbnew.F_CrtYd).BBox()
            if o is not fp and box.Contains(VECTOR2I(int(x), int(y))):
                return box
        return None
    for fp in fps:
        ref = fp.GetReference(); dev = card.parts[ref][0]; kind = classify(dev)
        x0, y0, x1c, y1c = pad_bbox(fp); cxp = (x0 + x1c) // 2; cyp = (y0 + y1c) // 2
        val = short_value(dev, fp.GetValue())
        if kind == "conn":
            fp.Value().SetVisible(False)
            crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()      # beside it, along it
            txt(fp.Reference(), crt.GetRight() + mm(1.2), cyp - mm(6), size=0.9, angle=90)
            silk_at(val, crt.GetRight() + mm(1.2), cyp + mm(4)).SetTextAngleDegrees(90)
        elif kind == "decap":                         # small, beside the standing cap (its 0.1" pads leave no room between)
            fp.Value().SetVisible(False)
            if (y1c - y0) > (x1c - x0):
                # (the capacitor of a chip at the front edge stands at the chip's inner end: its label on the far side)
                sx = mm(-2.3) if card.parts[ref][1].split()[-1] in getattr(card, "front", []) else mm(2.3)
                txt(fp.Reference(), cxp + sx, cyp - mm(1.9), size=0.8, angle=90)
                silk_at(val, cxp + sx, cyp + mm(1.9), size=0.8).SetTextAngleDegrees(90)
            else:                                     # (under it, or over it if a module lies below)
                y = cyp + (mm(-2.3) if inside_other(fp, cxp, cyp + mm(2.3)) else mm(2.3))
                txt(fp.Reference(), cxp - mm(1.7), y, size=0.8)
                silk_at(val, cxp + mm(1.7), y, size=0.8)
        elif kind == "led":                           # its reference over it, what it shows under it
            fp.Value().SetVisible(False)
            txt(fp.Reference(), cxp, cyp - mm(3.0), size=0.8)
            cathode = next(p for p in fp.Pads() if p.GetNumber() == "1").GetPosition()
            silk_at("K", cathode.x + (mm(-1.9) if cathode.x < cxp else mm(1.9)), cathode.y, C_, 0.8)
            silk(getattr(card, "led_label", {}).get(ref, card.parts[ref][1]),     # what it shows, right under it (D48;
                 pcbnew.ToMM(cxp), pcbnew.ToMM(cyp) + 3.4, 0.9)                     # card.led_label: a short label)
        elif kind == "res":
            txt(fp.Reference(), cxp, cyp - mm(2.5)); txt(fp.Value(), cxp, cyp + mm(2.5))
        elif kind == "ic":                            # one line just above the top pad row: the reference
            # over pin 1's end, the type over the other end (the chip covers its own body)
            yl = y0 - mm(2.9 if dev == "TIL311" else 1.55)   # (a TIL311's body is 10.2 mm across: 1.3 mm past its pins,
            #                                                    and its pin 1 tick stands 0.5 mm outside the outline)
            if dev == "TIL311" and inside_other(fp, cxp, yl):   # (the second digit of a pair: under it)
                yl = y1c + mm(2.4)
            txt(fp.Reference(), x0 - mm(0.8), yl, size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_LEFT)
            txt(fp.Value(), x1c + mm(0.8), yl, size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_RIGHT)
        else:                                         # other bodies: reference only (value in the BOM)
            fp.Value().SetVisible(False)
            if DEV[dev]["pkg"] == "CP_RADIAL":        # pads fill the circle: label just below it
                txt(fp.Reference(), cxp, cyp + mm(3.6), size=0.9)
                silk_at(val, cxp, cyp + mm(4.8), size=0.8)
            elif DEV[dev]["pkg"].startswith("PINHDR"):  # jumper / header: above the pins, and what a
                txt(fp.Reference(), cxp, y0 - mm(2.7), size=0.9)   # jumper sets above that ("jumper WIN0: ...")
                fn = card.parts[ref][1]
                if fn.startswith("jumper "):
                    silk_at(fn.split()[1].rstrip(":"), cxp, y0 - mm(4.2), size=0.8)
                else:
                    silk_at(val, cxp, y0 - mm(4.2), size=0.8)
            elif DEV[dev]["pkg"] == "C_DISC_P254":   # a small disc: too small to write on, label below it
                txt(fp.Reference(), cxp, cyp + mm(2.4), size=0.8)
                silk_at(val, cxp, cyp + mm(3.6), size=0.8)
            elif fp.GetPadCount() == 2:               # a diode, a fuse: above its body, what it is below
                crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
                txt(fp.Reference(), cxp, crt.GetTop() - mm(0.8), size=0.9)
                silk_at(val, cxp, crt.GetBottom() + mm(0.8), size=0.8)
            elif DEV[dev]["pkg"] == "TO220":         # the regulator: below its body
                crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
                txt(fp.Reference(), cxp, crt.GetBottom() + mm(1.0), size=0.9)
                silk_at(val, cxp, crt.GetBottom() + mm(2.2), size=0.8)
            elif DEV[dev]["pkg"].startswith("SW_"):    # a switch or button: its reference, and above it what
                # it does -- for a switch with positions, each position (card.panel, D48)
                lines, levers = panel_lines(card, ref), panel_levers(card, ref)
                if not lines and not levers:
                    raise SystemExit("%s: switch %s has no function label (card.panel)" % (card.name, ref))
                y = above(fp, 0.9)
                crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
                ends = getattr(card, "panel_ends", {}).get(ref)
                if ends:                              # a slide switch turned so its lever points out past the
                    # edge (card.panel_ends: what pin 1's end and pin 3's end select): each position's name at
                    # its end of the lever's travel, the reference and the function beside it, to the left
                    pd = {p.GetNumber(): p.GetPosition() for p in fp.Pads()}
                    if pd["1"].x != pd["3"].x:
                        raise SystemExit("%s: %s must be turned with its lever across the edge (front_rot)" % (card.name, ref))
                    xe = pd["1"].x + mm(1.2)
                    top_is_1 = pd["1"].y < pd["3"].y
                    silk_at(ends[0 if top_is_1 else 1], xe, crt.GetTop() - mm(0.9))
                    silk_at(ends[1 if top_is_1 else 0], xe, crt.GetBottom() + mm(0.9))
                    xl, yl_ = crt.GetLeft() - mm(0.7), cyp - mm(0.65 * len(lines))
                    txt(fp.Reference(), xl, yl_, size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_RIGHT)
                    for ln in lines:
                        yl_ += mm(1.3); silk_at(ln, xl, yl_, pcbnew.GR_TEXT_H_ALIGN_RIGHT)
                    continue
                hit = any(o is not fp and o.GetCourtyard(pcbnew.F_CrtYd).BBox().Intersects(
                          pcbnew.BOX2I(VECTOR2I(crt.GetLeft(), y - mm(1.5 + 1.3 * len(lines))), VECTOR2I(crt.GetWidth(), mm(1.5 + 1.3 * len(lines)))))
                          for o in fps)
                if hit:                               # (the part above leaves no room: beside it, to the left)
                    x, y = crt.GetLeft() - mm(0.7), cyp - mm(0.65 * len(lines))
                    txt(fp.Reference(), x, y, size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_RIGHT)
                    for ln in lines:
                        y += mm(1.3); silk_at(ln, x, y, pcbnew.GR_TEXT_H_ALIGN_RIGHT)
                else:
                    txt(fp.Reference(), cxp, y, size=0.8)
                    for ln in reversed(lines):
                        y -= mm(1.3); silk_at(ln, cxp, y)
                pad = {p.GetNumber(): p.GetPosition() for p in fp.Pads()}
                n = len(pad) // 2
                if levers and len(levers) != n:
                    raise SystemExit("%s: %s has %d levers, card.panel labels %d" % (card.name, ref, n, len(levers)))
                for k, (on, off) in enumerate(levers):
                    p_off, p_on = pad[str(k + 1)], pad[str(2 * n - k)]
                    if p_off.y != p_on.y:
                        raise SystemExit("%s: %s must lie with its levers across the edge" % (card.name, ref))
                    sgn = 1 if p_on.x > p_off.x else -1
                    L, R = pcbnew.GR_TEXT_H_ALIGN_LEFT, pcbnew.GR_TEXT_H_ALIGN_RIGHT
                    silk_at(on, p_on.x + sgn * mm(1.4), p_on.y, L if sgn > 0 else R)
                    silk_at(off, p_off.x - sgn * mm(1.4), p_off.y, R if sgn > 0 else L)
            elif DEV[dev]["pkg"] == "SIP9" or DEV[dev]["pkg"].startswith("PINSOCK"):   # resistor network, module socket:
                # clear of its outline
                txt(fp.Reference(), cxp, y1c + mm(2.9), size=0.9)
                silk_at(val, cxp, y1c + mm(4.1), size=0.8)
            elif min(x1c - x0, y1c - y0) < mm(4) and not dev.endswith("_RA"):     # a narrow pad field (ribbon header):
                crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()                        # below its shroud
                txt(fp.Reference(), cxp, crt.GetBottom() + mm(0.9), size=0.9)
                silk_at(val, cxp, crt.GetBottom() + mm(2.1), size=0.8)
            elif DEV[dev]["pkg"].startswith("PLCC"):  # a chip in a socket: one line above the socket, as a
                # DIP's -- the reference at the left, the part number (not the package) at the right
                crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
                fp.Value().SetText(fp.Value().GetText().split()[0])
                txt(fp.Reference(), crt.GetLeft(), above(fp, 0.55), size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_LEFT)
                txt(fp.Value(), crt.GetRight(), above(fp, 0.55), size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_RIGHT)
            else:                                     # a module or a cable's connector: above it
                crt = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
                x = crt.GetLeft()
                hit = inside_other(fp, x + mm(1), above(fp, 0.55))    # (a small part over this corner: past it)
                if hit:
                    x = hit.GetRight() + mm(0.5)
                txt(fp.Reference(), x, above(fp, 0.55), size=0.8, just=pcbnew.GR_TEXT_H_ALIGN_LEFT)
                silk_at(val, crt.GetRight(), above(fp, 0.55), pcbnew.GR_TEXT_H_ALIGN_RIGHT, 0.8)
    silk("%s   REV %s   cottageworker.com" % (card.title, getattr(card, "rev", "1")), BW / 2 + 10, BH - 3.0, 1.5)

def round_led(fp):
    """pad 1 of an LED round, as pad 2: routes hug a round pad, and the footprint's
    square one would touch them when an LED is turned (D90). The silk marks the
    cathode, its flat and a K."""
    for p in fp.Pads():
        p.SetShape(pcbnew.F_Cu, pcbnew.PAD_SHAPE_CIRCLE)

def rework(board, card):
    """D90 on a routed board, the tracks kept: each LED turned about its pads with
    its nets swapped, so the same net stays at each hole and pad 1 is the cathode;
    the planes on spokes, and filled again."""
    for fp in board.GetFootprints():
        if not fp.GetFPID().GetUniStringLibId().startswith("LED_THT"):
            continue
        round_led(fp)
        pads = {p.GetNumber(): p for p in fp.Pads()}
        anode = next(n for n, mem in card.nets.items() if (fp.GetReference(), "A") in mem)
        if pads["2"].GetNetname() == anode:                 # (done already)
            continue
        one, two = pads["1"].GetNetCode(), pads["2"].GetNetCode()
        a, b = pads["1"].GetPosition(), pads["2"].GetPosition()
        place_centered(fp, pcbnew.ToMM((a.x + b.x) // 2), pcbnew.ToMM((a.y + b.y) // 2), (fp.GetOrientationDegrees() + 180) % 360)
        pads["1"].SetNetCode(two); pads["2"].SetNetCode(one)
    for z in board.Zones():
        if not z.GetIsRuleArea():
            thermals(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())

def build(name):
    if name not in CARDS:                             # a card p32cards does not load (the debug card)
        __import__("card_" + name.split("-")[0]).build()
    card = CARDS[name]
    card_size(card)
    parts, nets = card.parts, card.nets
    cls = {ref: classify(dev) for ref, (dev, f, v) in parts.items()}
    capfor = {f.split()[1]: ref for ref, (dev, f, v) in parts.items() if f.startswith("decoupling ")}
    for r, (dev, f, v) in parts.items():               # the ALU module's USB-C end must stay clear:
        if DEV[dev]["pkg"] == "NANO20K_SOCKET":        # its cap goes with the loose parts
            capfor.pop(r, None)

    board = pcbnew.BOARD()
    nlayers = int(os.environ.get("P32_LAYERS") or LAYERS.get(name, 6))   # (P32_LAYERS: a trial on another count)
    board.SetCopperLayerCount(nlayers)
    # clearance 0.2 -> 0.13 mm (5 mil, a standard fab tier; P8X backplane): the
    # DIN41612 pads are 1.55 mm on 2.54 mm, a 0.99 mm gap. A card using most
    # connector pins must escape two tracks through each gap: at 0.2 / 0.2 that
    # needs 1.0 mm and Freerouting crawled for hours; at 0.2 / 0.13 it is 0.79 mm.
    # Tracks stay 0.2 mm. Flows into the DRC and the DSN export (netclass).
    board.GetDesignSettings().m_NetSettings.GetDefaultNetclass().SetClearance(mm(0.13))
    netobj = {}
    for n in nets:
        ni = pcbnew.NETINFO_ITEM(board, n); board.Add(ni); netobj[n] = ni

    footp, missing, smd = {}, [], []
    for ref, (dev, f, val) in parts.items():
        lib, fname = FOOTPRINT[DEV[dev]["pkg"]].split(":")
        fp = pcbnew.FootprintLoad(os.path.join(P32LIB if lib == "P32W" else FP, lib + ".pretty"), fname)
        if fp is None:
            missing.append("%s (%s)" % (ref, fname)); continue
        fp.SetFPID(pcbnew.LIB_ID(lib, fname))          # full library id, as the schematic has it
        fp.SetReference(ref); fp.SetValue(val); board.Add(fp); footp[ref] = fp
        m3 = getattr(card, "model3d", {}).get(ref)     # this part's own 3D model, beside the footprint's (a display's digit)
        if m3:
            old = fp.Models()[0]
            new = pcbnew.FP_3DMODEL()
            new.m_Filename = os.path.dirname(old.m_Filename) + "/" + m3
            new.m_Scale, new.m_Rotation, new.m_Offset, new.m_Show = old.m_Scale, old.m_Rotation, old.m_Offset, True
            fp.Models().clear(); fp.Add3DModel(new)
        if fp.GetAttributes() & pcbnew.FP_SMD:         # D19: through-hole only
            smd.append("%s (%s)" % (ref, fname))

    if smd:
        raise SystemExit("%s: SMD footprints, but every part must be through-hole (D19): %s"
                         % (name, ", ".join(smd)))

    # --- status LEDs and their resistors: a bank in the top far corner -----
    leds = [r for r in parts if cls[r] == "led"]
    led_res = {}
    for led in leds:
        anet = next(n for n, mem in nets.items() if (led, "A") in mem)
        led_res[led] = next(r for r, p in nets[anet] if r != led and cls[r] == "res")
    BANKW = 34.0 if leds else 0.0
    BANKH = 6.0 + 12.0 * len(leds) if leds else 0.0

    # --- parts a card wants on the front edge (card.front: probe headers and
    # their jumpers, where a cable reaches them in the card cage): stacked
    # down the far edge below the LED bank, the long side along the edge
    # (D48: every switch and plug-in module belongs on the front edge -- LEDs go in the bank there)
    stray = [r for r in parts if (DEV[parts[r][0]]["pkg"].startswith("SW_") or parts[r][0] in EDGE_MODULES)
             and r not in getattr(card, "front", []) + getattr(card, "front_bottom", [])]
    if stray:
        raise SystemExit("%s: %s must be on the front edge (card.front / front_bottom, D48)" % (name, ", ".join(stray)))
    FCAP = 5.5                                        # a chip at the front edge (a display): its 100 nF stands at its inner end
    front, blocks, FRONTW, fy = [], [], 0.0, max(BANKH + 4.0 if leds else 6.0, getattr(card, "front_top", 0.0))   # (a card may
    # start the stack lower, so a part sits at the same place on two cards)
    for r in getattr(card, "front", []):
        if r not in footp: continue
        rot = 0
        w, h = size_after(footp[r], 0)
        if r in getattr(card, "front_rot", {}):        # (a module whose socket must face the edge)
            rot = card.front_rot[r]; w, h = size_after(footp[r], rot)
        elif w > h and not panel_levers(card, r):
            rot = 90; w, h = size_after(footp[r], 90)
        top = fy
        fy += 1.3 * len(panel_lines(card, r)) if r not in getattr(card, "panel_ends", {}) else 1.6   # (an end label above it)
        rm, lm = panel_margins(card, r)
        if cls[r] == "ic" and r in capfor:
            lm += FCAP
        front.append((r, rot, w, h, fy + h / 2, rm))
        fy += h + getattr(card, "front_gap", {}).get(r, 4.0)   # (card.front_gap: the next part closer: a display's second digit)
        FRONTW = max(FRONTW, w + rm + lm + (4.0 if rm else 8.0))
        blocks.append((top, fy - 2.0, w + rm + lm + (4.0 if rm else 8.0)))
    # and parts it wants at the bottom of the front edge (card.front_bottom,
    # stacked up from the title line): the sequencer's panel switches, next to
    # their debounce logic in the bottom rows (D50)
    fb = BH - 8.0
    for r in getattr(card, "front_bottom", []):
        if r not in footp: continue
        w, h = size_after(footp[r], 0)
        rot = 0
        if r in getattr(card, "front_rot", {}):        # (a module whose USB end must face the edge)
            rot = card.front_rot[r]; w, h = size_after(footp[r], rot)
        elif w > h and not panel_levers(card, r):     # (a DIP switch with lever labels keeps its levers across the edge)
            rot = 90; w, h = size_after(footp[r], 90)
        rm, lm = panel_margins(card, r)
        front.append((r, rot, w, h, fb - h / 2, rm))
        bot = fb + 2.0
        fb -= h + 4.0 + 1.3 * len(panel_lines(card, r))
        blocks.append((fb + 2.0, bot, w + rm + lm + (4.0 if rm else 8.0)))   # (lever labels bring their own margin)

    # --- everything else packed in rows (shelf packing), courtyard-spaced:
    # ICs lie lengthwise with their 100 nF cap standing at the right-hand end;
    # reference and value are printed inside the body. Parts go in affinity
    # order (strongly connected parts adjacent), rows snaking left-right then
    # right-left so the order stays continuous across row breaks.
    others = [r for r in parts if ((cls[r] in ("misc", "res") and r not in led_res.values())
              or (cls[r] == "decap" and r not in capfor.values()))
              and r not in getattr(card, "front", []) and r not in getattr(card, "front_bottom", [])
              and r not in getattr(card, "edge_row", [])]
    order = getattr(card, "place_order", None)       # a card may give its own order (memory card: by lane)
    edged = set(getattr(card, "front", []) + getattr(card, "front_bottom", []))
    rowics = [r for r in parts if cls[r] == "ic" and r not in edged]   # (a chip at the front edge is not in the rows)
    if order:
        grid = list(order) + [r for r in rowics + others if r not in order]
    else:
        grid = affinity_order(rowics + others, nets)
    GAP, ROWGAP, CAPW = 2.5, 3.0, 4.5
    EDGEH = 16.0 if getattr(card, "edge_row", []) else 0.0     # the jumper row's strip along the bottom edge
    FRONTH = fy                                       # the front strip ends below its last part: rows lower
    def xlimit(y, h=0.0):                             # the width left beside the LED bank and the front-edge parts
        # for a row from y, h tall (rows only ever move down later, so the top stack keeps its whole height)
        return BW - 2.0 - max([BANKW if y < BANKH else 0.0, FRONTW if y < FRONTH else 0.0] +
                              [bw for bt, bb, bw in blocks if bt > FRONTH - 1.0 and y < bb and y + h + 2.0 > bt])
    def shape(r):
        rot = 90 if cls[r] == "ic" else 0
        w, h = size_after(footp[r], rot)
        if cls[r] != "ic" and h > 1.5 * w:            # long parts (the ribbon header) lie flat
            rot = 90
            if DEV[parts[r][0]]["pkg"] == "NANO20K_SOCKET":
                rot = 270                             # the ALU module's USB-C toward the front edge
            w, h = size_after(footp[r], rot)
        return rot, w, h, w + (CAPW if r in capfor else 0.0)
    shapes = {r: shape(r) for r in grid}
    LOOK = 12                                         # a row's tail may take a part up to 12 places ahead
    def pack(top, rowgap):
        rows, cur, cx, cy, rowh, todo = [], [], GX0, top, 0.0, list(grid)
        while todo:
            fit = next((r for r in todo[:LOOK] if cx + shapes[r][3] <= xlimit(cy, max(rowh, shapes[r][2]))
                        and all(x + c_ <= xlimit(cy, shapes[r][2]) for _, x, _, _, c_, _ in cur)), None)
            if fit is None and cur:
                rows.append((cy, rowh, cur)); cur, cx = [], GX0; cy += rowh + rowgap; rowh = 0.0
                continue
            r = fit or todo[0]
            todo.remove(r)
            rot, w, h, cw = shapes[r]
            cur.append((r, cx, w, h, cw, rot))
            cx += cw + GAP; rowh = max(rowh, h)
        rows.append((cy, rowh, cur))
        return rows, cy + rowh
    # roomiest first: a 7 mm top margin (routing room above the top row) and
    # 3 mm between rows; tighter only if the parts would not fit
    def grid_rows(top, rowgap):
        # card.place_rows: the card's own grid, a list of rows of refs (None: an
        # empty cell); every column as wide as its widest cell, so a part sits
        # straight below the one above it (the memory card: a bank a row, a
        # byte lane a column, and the address lines run straight down them)
        # card.place_free: no columns, each row packed at its own parts' widths
        # (the I/O card: a row per function); a ">" in a row puts the rest of
        # it against card.place_right (the UARTs' level shifters by their DB9s),
        # ">276" against x = 276 mm (a row beside a wide front part, the Pico)
        free = getattr(card, "place_free", False)
        mark = lambda r: isinstance(r, str) and r.startswith(">")
        ncol = max(len(r) for r in card.place_rows)
        colw = [max([shapes[r][3] for row in card.place_rows if i < len(row) and row[i] and not mark(row[i])
                     for r in [row[i]]] or [0.0]) for i in range(ncol)]
        # card.place_right on a grid with columns: the columns spread out until
        # the widest row ends there (the memory card's array across the card)
        cgap = GAP
        if not free and getattr(card, "place_right", None):
            cgap = max(GAP, (card.place_right - GX0 - sum(colw)) / max(1, ncol - 1))
        rows, cy = [], top
        widen.clear()
        for row in card.place_rows:
            cur, cx, rowh = [], GX0, 0.0
            k = next((j for j, r in enumerate(row) if mark(r)), None)
            if k is not None:
                tail = sum(shapes[r][3] + GAP for r in row[k + 1:] if r) - GAP
                edge = float(row[k][1:]) if row[k][1:] else card.place_right
            if free and getattr(card, "place_spread", False) and k is None:
                widen.add(len(rows))                  # (spread out once the row's height on the card is known)
            for i, r in enumerate(row):
                if mark(r):
                    cx = max(cx, edge - tail); continue
                if r:
                    rot, w, h, cw = shapes[r]
                    cur.append((r, cx, w, h, cw, rot)); rowh = max(rowh, h)
                cx += shapes[r][3] + GAP if free and r else colw[i] + cgap
            rows.append((cy, rowh, cur)); cy += rowh + rowgap
        return rows, cy - rowgap
    widen = set()                                     # card.place_spread: the free rows whose gaps widen to place_right
    if getattr(card, "place_rows", None):
        missing = [r for r in grid if all(r not in row for row in card.place_rows)]
        if missing:
            raise SystemExit("%s: place_rows leaves out %s" % (name, ", ".join(missing)))
    for top, ROWGAP in ((7.0, 3.0), (5.0, 3.0), (4.0, 2.5), (4.0, 2.0), (3.0, 1.6)):   # (the last: a full card)
        rows, bottom = (grid_rows if getattr(card, "place_rows", None) else pack)(top, ROWGAP)
        if bottom <= BH - 10.0 - EDGEH:
            break
    if bottom > BH - 3 - EDGEH:
        print("  WARNING: %s overflows %.0fx%.0f (bottom=%.0f)" % (name, BW, BH, bottom))
    # --- then spread the rows down the whole connector edge: the space left
    # below the last row goes evenly into the gaps between rows, as routing
    # channels (rows packed 3 mm apart left Freerouting no room on dense cards)
    nrow = len(rows) - 1
    spread = (BH - 10.0 - EDGEH - bottom) / nrow if nrow and bottom < BH - 10.0 - EDGEH else 0.0   # title on the last 5 mm
    snake = not getattr(card, "place_rows", None)     # (a card's own grid keeps its columns)
    def right_end(k, y0, rh, items):                   # where row k's last part ends (x, mm)
        xl = xlimit(y0, rh)
        return max((GX0 + xl - x0 if snake and k % 2 else x0 + cw) for _, x0, _, _, cw, _ in items)
    def offsets(sp):
        """How far each row moves down: sp a gap; and on a card that asks (card.place_clear_front, with its
        own rows) a row that would run into a part of the front edge's top stack starts below that part."""
        offs, extra = [], 0.0
        for k, (y0, rh, items) in enumerate(rows):
            y = y0 + k * sp + extra
            if getattr(card, "place_clear_front", False):
                packed = max(x0 + cw for _, x0, _, _, cw, _ in items)
                for bt, bb, bw in sorted(b for b in blocks if b[0] <= FRONTH - 1.0):
                    if y < bb and y + rh + 2.0 > bt and packed > BW - 2.0 - bw:
                        extra += bb + 1.0 - y; y = bb + 1.0
            offs.append(y - y0)
        return offs
    def hits(sp):                                     # a spread row reaching a front-edge part at the bottom,
        offs = offsets(sp)                            # or the rows running off the card
        return (any(y0 + offs[k] < bb and y0 + offs[k] + rh + 2.0 > bt and right_end(k, y0, rh, items) > BW - 2.0 - bw
                    for k, (y0, rh, items) in enumerate(rows) for bt, bb, bw in blocks if bt > FRONTH - 1.0)
                or (getattr(card, "place_clear_front", False)
                    and rows[-1][0] + offs[-1] + rows[-1][1] > BH - 10.0 - EDGEH + 0.01))
    while spread > 0 and hits(spread):                # (rows only move down: less spread, until clear)
        spread = max(0.0, spread - 0.25)
    offs = offsets(spread)
    bottom = rows[-1][0] + offs[-1] + rows[-1][1]
    pos = {}
    for k, (y0, rh, items) in enumerate(rows):
        xl = xlimit(y0, rh)
        if k in widen and len(items) > 1:             # a spread row: its gaps widen until it ends at place_right, or
            yk = y0 + offs[k]                         # short of the LED bank or the front-edge parts beside it
            right = min([card.place_right, BW - 2.0 - (BANKW if yk < BANKH else 0.0)] +
                        [BW - 2.0 - bw for bt, bb, bw in blocks if yk < bb and yk + rh + 2.0 > bt])
            gap = max(GAP, (right - GX0 - sum(it[4] for it in items)) / (len(items) - 1))
            x, wide = GX0, []
            for r, x0, w, h, cw, rot in items:
                wide.append((r, x, w, h, cw, rot)); x += cw + gap
            items = wide
        for r, x0, w, h, cw, rot in items:
            if snake and k % 2:                       # odd rows run right to left
                x0 = GX0 + xl - (x0 + cw)
            pos[r] = (x0 + w / 2, y0 + h / 2 + offs[k], rot, w)
    placed = set()
    for r, (x, y, rot, w) in pos.items():
        place_centered(footp[r], x, y, rot); placed.add(r)
        if r in capfor:                               # cap standing at the IC's right end
            c = capfor[r]                             # (size measured while packing: size_after moves parts)
            place_centered(footp[c], x + w / 2 + CAPW / 2, y, 90)
            placed.add(c)
    ledx, resx, by = BW - 9.0, BW - 24.0, 6.0
    for led in leds:
        place_centered(footp[led], ledx, by + 2.5, 0); placed.add(led); round_led(footp[led])
        place_centered(footp[led_res[led]], resx, by + 2.5, 0); placed.add(led_res[led])
        by += 12.0
    for r, rot, w, h, yc, rm in front:
        place_centered(footp[r], BW - 4.0 - rm - w / 2, yc, rot); placed.add(r)
        if cls[r] == "ic" and r in capfor:            # its capacitor, standing at its inner end
            place_centered(footp[capfor[r]], BW - 4.0 - rm - w - FCAP / 2, yc, 90); placed.add(capfor[r])
        edge = getattr(card, "front_edge", {}).get(r)
        if edge:                                      # a cable connector or a module's port at the edge itself,
            kind, d = edge                            # facing out (D48): pad 1 d mm from the edge (a D-sub's
            fp = footp[r]                             # pin-to-edge offset), or its body's far end d mm in
            if kind == "pad1":
                x_now = next(p.GetPosition().x for p in fp.Pads() if p.GetNumber() == "1")
            else:
                x_now = fp.GetCourtyard(pcbnew.F_CrtYd).BBox().GetRight()
            pos = fp.GetPosition()
            fp.SetPosition(VECTOR2I(pos.x + mm(BW - d) - x_now, pos.y))
    # jumpers along the bottom edge (D48: jumper blocks near an edge, never in the middle)
    jx = GX0
    for r in getattr(card, "edge_row", []):
        place_centered(footp[r], jx, BH - EDGEH + 4.0, 0); placed.add(r)
        jx += JSTEP

    # --- connectors on the left edge, pads 4 mm in --------------------------
    for j, yc in CONN_Y.items():
        fp = footp[j]; fp.SetPosition(P(0, 0)); fp.SetOrientationDegrees(90)
        x0, y0, x1c, y1c = pad_bbox(fp)
        fp.SetPosition(VECTOR2I(mm(4) - x0, mm(yc) - (y0 + y1c) // 2))
        placed.add(j)
    left = [r for r in parts if r not in placed and r in footp]
    for i, r in enumerate(left):                      # anything left over: park at the bottom, side by side
        place_centered(footp[r], GX0 + 10 * i, BH - 6, 0)
    if left:
        print("  WARNING: %s: parked unplaced parts %s" % (name, ", ".join(left)))

    # --- pads to nets ---------------------------------------------------------
    badpad = []
    for net, mem in nets.items():
        for ref, pin in mem:
            dev = parts[ref][0]
            pad = DEV[dev]["pins"][pin][0]
            hit = False
            for p in footp[ref].Pads():
                if p.GetNumber() == pad:
                    p.SetNet(netobj[net]); hit = True
            if not hit:
                badpad.append("%s.%s (pad %s)" % (ref, pin, pad))

    swaps = 0 if order or not getattr(card, "place_swaps", True) else \
        refine_by_swaps(footp, [r for r in grid if cls[r] == "ic"], capfor,
                        same_row=getattr(card, "place_swaps", True) == "row")   # (a card's rows by function keep their parts)

    # unused pins: the net name KiCad's schematic gives a no-connect pin, so the
    # board and the generated schematic agree ("update PCB from schematic" clean)
    for ref, pin in card.nc:
        dev = parts[ref][0]
        num = DEV[dev]["pins"][pin][0]
        kname = "~{%s}" % pin[1:] if pin.startswith("-") else pin
        if kname == num:                              # KiCad leaves out a pin name that is just its number
            ni = pcbnew.NETINFO_ITEM(board, "unconnected-(%s-Pad%s)" % (ref, num))
        else:
            ni = pcbnew.NETINFO_ITEM(board, "unconnected-(%s-%s-Pad%s)" % (ref, kname, num))
        board.Add(ni)
        for p in footp[ref].Pads():
            if p.GetNumber() == num:
                p.SetNet(ni)

    # --- outline, planes, keepouts -------------------------------------------
    def edge(xa, ya, xb, yb):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(P(xa, ya)); s.SetEnd(P(xb, yb))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.15)); board.Add(s)
    edge(0, 0, BW, 0); edge(BW, 0, BW, BH); edge(BW, BH, 0, BH); edge(0, BH, 0, 0)
    def plane(layer, netname):
        z = pcbnew.ZONE(board); z.SetLayer(layer); z.SetNet(netobj[netname])
        z.SetIsFilled(True); thermals(z)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.13))
        o = z.Outline(); o.NewOutline()
        for x, y in ((1, 1), (BW - 1, 1), (BW - 1, BH - 1), (1, BH - 1)):
            o.Append(mm(x), mm(y))
        board.Add(z)
    gnd_l, vcc_l = plane_layers(board)
    plane(gnd_l, "GND"); plane(vcc_l, "VCC")
    nk = add_mounting_keepouts(board, footp)
    add_rf_keepouts(board, footp, getattr(card, "rf_keepout", {}))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())

    label(board, card)

    outdir = os.environ.get("P32_PCB_OUT") or os.path.join(ROOT, "hardware", name, "kicad")   # (P32_PCB_OUT: a trial elsewhere)
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, name + ".kicad_pcb")
    pcbnew.SaveBoard(out, board)
    return out, missing, badpad, nk, bottom, nlayers, wirelength(board)

if __name__ == "__main__":
    if sys.argv[1] in ("--label", "--rework"):         # relabel a routed board in place
        brd = os.path.join(ROOT, "hardware", sys.argv[2], "kicad", sys.argv[2] + ".kicad_pcb")
        if sys.argv[2] not in CARDS:
            __import__("card_" + sys.argv[2].split("-")[0]).build()
        card_size(CARDS[sys.argv[2]])
        b = pcbnew.LoadBoard(brd)
        if sys.argv[1] == "--rework":
            rework(b, CARDS[sys.argv[2]])
        label(b, CARDS[sys.argv[2]]); pcbnew.SaveBoard(brd, b)
        print("reworked" if sys.argv[1] == "--rework" else "relabelled", brd); sys.exit(0)
    out, missing, badpad, nk, bottom, nlayers, hpwl = build(sys.argv[1])
    print("wrote %s (%.2f x %.0f mm, %d layers, %d mounting keepouts, parts down to y=%.0f mm, "
          "wire length est. %.0f mm)" % (out, BH, BW, nlayers, nk, bottom, hpwl))
    if missing:
        print("  MISSING FOOTPRINTS:", missing); sys.exit(1)
    if badpad:
        print("  PAD MISMATCH (%d):" % len(badpad), badpad[:12]); sys.exit(1)
    sys.stdout.flush(); os._exit(0)
