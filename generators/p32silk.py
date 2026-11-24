"""p32silk.py -- a card's silkscreen labels, every text upright with the card standing in the bus (D150; Ken,
2026-11-18, every board of every system: "all silkscreen text reads correctly with the card held connector down").

A card is drawn with its plugs on the left edge. Standing, it is turned a quarter to the left: the drawing's right
is up (the front edge), the drawing's bottom is the right-hand side. So an upright text is at KiCad's 270 as drawn:
it runs down the drawing, its lines stacked toward smaller x. The name line alone runs up the right-hand side (0 as
drawn), the rule's one exception.

Each part's lines are tried in a few places round it, in an order, and go to the first that is clear of every pad,
every part's courtyard, the silk of every footprint, the board's edge and the labels already placed. Vias
(they are tented) are kept off too where a near place allows it. A part with no clear place stops the run. Copper is never touched.
p32kicad_pcb.label() comes here for a card with card.upright set.
"""
import pcbnew
from pcbnew import VECTOR2I

UP = 270
PITCH = 1.35                                            # from one 0.8 mm line to the next
GAP = 0.55                                              # from a courtyard to a label
mm = pcbnew.FromMM

class Grid:
    """boxes (l, t, r, b, in mm), found by where they are"""
    def __init__(self): self.cells = {}
    def _c(self, b):
        for i in range(int(b[0] // 8), int(b[2] // 8) + 1):
            for j in range(int(b[1] // 8), int(b[3] // 8) + 1):
                yield i, j
    def add(self, b, who):
        for c in self._c(b): self.cells.setdefault(c, []).append((b, who))
    def hits(self, b):
        for c in self._c(b):
            for o, who in self.cells.get(c, ()):
                if o[0] < b[2] and b[0] < o[2] and o[1] < b[3] and b[1] < o[3]:
                    yield o, who

def box(bb, by=0.0):
    return (pcbnew.ToMM(bb.GetLeft()) - by, pcbnew.ToMM(bb.GetTop()) - by,
            pcbnew.ToMM(bb.GetRight()) + by, pcbnew.ToMM(bb.GetBottom()) + by)

def label(board, card, BW, BH, DEV, classify, short_value):
    L, R, C = pcbnew.GR_TEXT_H_ALIGN_LEFT, pcbnew.GR_TEXT_H_ALIGN_RIGHT, pcbnew.GR_TEXT_H_ALIGN_CENTER
    fps = list(board.GetFootprints())
    old = [d for d in board.GetDrawings() if d.Type() == pcbnew.PCB_TEXT_T and d.GetLayer() == pcbnew.F_SilkS]
    for d in old:                                         # (the list is kept: pcbnew's Python breaks if it is let go here)
        board.Remove(d)
    hard, soft = Grid(), Grid()
    for fp in fps:
        hard.add(box(fp.GetCourtyard(pcbnew.F_CrtYd).BBox()), fp.GetReference())
        for p in fp.Pads(): hard.add(box(p.GetBoundingBox(), 0.2), "pad")
        for g in fp.GraphicalItems():
            if g.GetLayer() != pcbnew.F_SilkS: continue
            if isinstance(g, pcbnew.PCB_TEXT):            # a footprint's own text (a diode's K)
                g.SetTextAngleDegrees(UP); g.SetKeepUpright(False)
            hard.add(box(g.GetBoundingBox(), 0.12), "silk of " + fp.GetReference())
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T: soft.add(box(t.GetBoundingBox(), 0.05), "via")

    def style(t, x, y, just, size, thick, angle=UP):
        t.SetLayer(pcbnew.F_SilkS); t.SetTextSize(VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(thick))
        t.SetHorizJustify(just); t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
        t.SetPosition(VECTOR2I(mm(x), mm(y))); t.SetTextAngleDegrees(angle); t.SetKeepUpright(False)
    def new(s):
        t = pcbnew.PCB_TEXT(board); t.SetText(s); board.Add(t); return t
    def show(field):
        field.SetVisible(True); return field
    def extent(t):
        return box(t.GetBoundingBox(), 0.1)
    def clear(b):
        if b[0] < 0.5 or b[1] < 0.5 or b[2] > BW - 0.5 or b[3] > BH - 0.5: return None
        if any(True for _ in hard.hits(b)): return None
        return sum(1 for _ in soft.hits(b))
    def put(ref, texts, places, size=0.8, thick=0.15):
        """texts: the items. places: lists of (x, y, just), one for each text"""
        best = None
        for i, pl in enumerate(places):
            n = 0.3 * i                                   # (a place further down the list costs a third of a via)
            for t, (x, y, j) in zip(texts, pl):
                style(t, x, y, j, size, thick)
                c = clear(extent(t))
                if c is None: n = None; break
                n += c
            if n is not None and (best is None or n < best[0]): best = (n, pl)
            if best and 0.3 * (i + 1) >= best[0]: break
        if best is None:
            raise SystemExit("%s: no clear place for %s's label (%s)" % (card.name, ref, ", ".join(t.GetText() for t in texts)))
        for t, (x, y, j) in zip(texts, best[1]):
            style(t, x, y, j, size, thick); hard.add(extent(t), "label of " + ref)
    # ---- places round a box, said of the standing card. n lines, the first the upper one
    def stack(x, y, j, n): return [(x - PITCH * i, y, j) for i in range(n)]
    def over(c, n, dy=0.0, g=GAP):  return stack(c[2] + g + 0.5 + PITCH * (n - 1), (c[1] + c[3]) / 2 + dy, C, n)
    def under(c, n, dy=0.0, g=GAP): return stack(c[0] - g - 0.5, (c[1] + c[3]) / 2 + dy, C, n)
    def rightof(c, n, dx=0.0, g=GAP): return stack((c[0] + c[2]) / 2 + dx + PITCH * (n - 1) / 2, c[3] + g, L, n)
    def leftof(c, n, dx=0.0, g=GAP):  return stack((c[0] + c[2]) / 2 + dx + PITCH * (n - 1) / 2, c[1] - g, R, n)
    def sides(c, dx=0.0, g=GAP):                          # two texts, one at each side
        x = (c[0] + c[2]) / 2 + dx
        return [(x, c[1] - g, R), (x, c[3] + g, L)]
    def round_about(c, n):
        out = []
        for g in (GAP, GAP + 0.6, GAP + 1.4):
            out += [rightof(c, n, 0, g), leftof(c, n, 0, g), under(c, n, 0, g), over(c, n, 0, g)]
            for d in (1.5, -1.5, 3, -3):
                out += [rightof(c, n, d, g), leftof(c, n, d, g), under(c, n, d, g), over(c, n, d, g)]
        return out

    rank = {"conn": 0, "led": 1, "misc": 2, "ic": 3, "decap": 4, "res": 5}
    for fp in sorted(fps, key=lambda f: (rank[classify(card.parts[f.GetReference()][0])], f.GetReference())):
        ref = fp.GetReference(); dev = card.parts[ref][0]; kind = classify(dev); pkg = DEV[dev]["pkg"]
        c = box(fp.GetCourtyard(pcbnew.F_CrtYd).BBox()); val = short_value(dev, fp.GetValue())
        cy = (c[1] + c[3]) / 2
        fp.Value().SetVisible(False)
        pads = {p.GetNumber(): p for p in fp.Pads()}
        if kind == "conn":                                # over it, toward the parts, in the middle of it
            x = c[2] + 1.0
            put(ref, [show(fp.Reference()), new(val)], [[(x + dx, cy + d - 1, R), (x + dx, cy + d + 1, L)]
                for d in (0, 6, -6, 12, -12, 20, -20) for dx in (0, 1.2, 2.4)], 0.9)
        elif kind == "led":                               # what it shows over it, its reference at its left
            put(ref, [new(getattr(card, "led_label", {}).get(ref, card.parts[ref][1]))],
                [over(c, 1, 0, GAP + 0.2), over(c, 1, 0, GAP + 0.8)], 0.9, 0.2)
            put(ref, [show(fp.Reference())], [leftof(c, 1), rightof(c, 1)])
            k, a = (box(pads[n].GetBoundingBox()) for n in ("1", "2"))
            put(ref, [new("K")], [under(c, 1) if k[0] < a[0] else over(c, 1, 0, GAP + 1.6)])
        elif kind == "ic":                                # two lines at one end: which it is, over what it is
            two = [show(fp.Reference()), show(fp.Value())]
            pl = []
            for g in (GAP, GAP + 0.3, GAP + 0.8):
                pl += [under(c, 2, 0, g), over(c, 2, 0, g)]
                for d in (1.5, -1.5, 3, -3, 4.5, -4.5): pl += [under(c, 2, d, g), over(c, 2, d, g)]
            pl += round_about(c, 2)
            if ref in getattr(card, "place_turn", ()) or ref in getattr(card, "silk_note", {}):
                raise SystemExit("%s: %s: no upright form of its note yet (p32silk.py)" % (card.name, ref))
            put(ref, two, pl)
        elif kind == "decap":                             # one at each side of it, at its own height
            two = [show(fp.Reference()), new(val)]
            put(ref, two, [sides(c), sides(c, 0.5), sides(c, -0.5), sides(c, 1), sides(c, -1)] + round_about(c, 2))
        elif kind == "res":
            put(ref, [show(fp.Reference()), show(fp.Value())], [rightof(c, 2), leftof(c, 2)] + round_about(c, 2))
        elif pkg.startswith("SW_") and ref in getattr(card, "panel_ends", {}):  # a slide switch, its lever over the edge:
            ends = card.panel_ends[ref]                   # each position's name at its end of the travel
            p1, p3 = pads["1"].GetPosition().y, pads["3"].GetPosition().y
            x = pcbnew.ToMM(pads["1"].GetPosition().x) + 1.2
            lo, hi = (ends[0], ends[1]) if p1 < p3 else (ends[1], ends[0])     # (left and right of it, standing)
            put(ref, [new(lo), new(hi)], [[(x + d, c[1] - 0.4, R), (x + d, c[3] + 0.4, L)] for d in (0, -1, 1)], 0.9)
            lines = [new(s) for s in card.panel[ref]]
            put(ref, [show(fp.Reference())] + lines, [under(c, 1 + len(lines))] + round_about(c, 1 + len(lines)))
        elif pkg.startswith("PINHDR") and card.parts[ref][1].startswith("jumper "):     # what the jumper sets, its reference
            put(ref, [new(card.parts[ref][1].split()[1].rstrip(":")), show(fp.Reference())],
                [under(c, 2), over(c, 2), rightof(c, 2), leftof(c, 2)])
        elif pkg.startswith(("SW_", "PINHDR")) or dev.endswith("_RA"):
            raise SystemExit("%s: %s (%s): no upright label yet (p32silk.py)" % (card.name, ref, pkg))
        else:                                             # a module, a capacitor, a network, a diode
            put(ref, [show(fp.Reference()), new(val)], round_about(c, 2))
    t = new("%s   REV %s   cottageworker.com" % (card.title, getattr(card, "rev", "1")))
    style(t, BW / 2 + 10, BH - 3.0, C, 1.5, 0.2, 0)       # as drawn, along the bottom edge: standing, up the right side
    if clear(extent(t)) is None:
        raise SystemExit("%s: the name line is not clear" % card.name)
