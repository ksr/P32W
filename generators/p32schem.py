#!/usr/bin/env python3
"""Render a P32W card netlist as a net-label schematic PDF.

Each part is a symbol with numbered pins; every pin carries the name of its
net (backplane signals in blue, power in red, local nets in black), so two
pins with the same label are connected. Sheets group the parts by function
(the card module's SHEETS). As in P8X's renderer, the output is checked: every
connected pin is drawn exactly once, and the PDF is byte-reproducible
(reportlab invariant mode), so `make test` can tell when it is stale.

    python3 p32schem.py CARD OUT.pdf
"""
import os, sys

from reportlab.pdfgen import canvas as pdfc
from reportlab import rl_config
from reportlab.lib.colors import Color
from reportlab.pdfbase.pdfmetrics import stringWidth

rl_config.invariant = 1                 # same input -> byte-identical PDF

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32net import DEV  # noqa: E402
from p32bus import SIGNALS, pinnet as busnet  # noqa: E402  (the bus, D50)

PAGE_W, PAGE_H = 1190.55, 841.89        # A3 landscape, points
MARGIN = 28
PITCH = 11.0                            # pin spacing
STUB = 16
FONT, FS, FS_PIN = "Helvetica", 6.6, 5.0
BLUE, RED, BLACK, GREY = Color(0, 0.25, 0.75), Color(0.75, 0.1, 0.1), Color(0, 0, 0), Color(0.45, 0.45, 0.45)

# Pin order on each side of a symbol (power pins are noted inside the box).
LAYOUT = {
    "74HCT138": (["A", "B", "C", "G1", "-G2A", "-G2B"], ["Y%d" % i for i in range(8)]),
    "74HCT161": (["-CLR", "CLK", "-LOAD", "ENP", "ENT", "A", "B", "C", "D"],
                 ["QA", "QB", "QC", "QD", "RCO"]),
    "74HCT244": (["-1G", "1A1", "1A2", "1A3", "1A4", "-2G", "2A1", "2A2", "2A3", "2A4"],
                 [None, "1Y1", "1Y2", "1Y3", "1Y4", None, "2Y1", "2Y2", "2Y3", "2Y4"]),
    "74HCT245": (["DIR", "-OE"] + ["B%d" % i for i in range(1, 9)],     # B (card side) faces in,
                 [None, None] + ["A%d" % i for i in range(1, 9)]),     # A (bus side) faces out
    "74HCT08": (["1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B"],
                ["1Y", None, "2Y", None, "3Y", None, "4Y", None]),
    "CY7C199": (["A%d" % i for i in range(15)] + ["-CE", "-OE", "-WE"],
                ["IO%d" % i for i in range(8)]),
    "R": (["1"], ["2"]),
    "LED": (["A"], ["K"]),
    "C100N": (["1"], ["2"]),
    "C10U": (["+"], ["-"]),
}
LAYOUT["74HCT32"] = LAYOUT["74HCT08"]
LAYOUT["74HCT00"] = LAYOUT["74HCT08"]
LAYOUT.update({
    "74HCT374": (["-OE", "CLK"] + ["D%d" % i for i in range(8)], [None, None] + ["Q%d" % i for i in range(8)]),
    "74HCT377": (["-E", "CLK"] + ["D%d" % i for i in range(8)], [None, None] + ["Q%d" % i for i in range(8)]),
    "74HCT541": (["-OE1", "-OE2"] + ["A%d" % i for i in range(8)], [None, None] + ["Y%d" % i for i in range(8)]),
    "74HCT157": (["S", "-E", "1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B"],
                 [None, None, "1Y", None, "2Y", None, "3Y", None, "4Y", None]),
    "74HCT151": (["S0", "S1", "S2", "-E"] + ["D%d" % i for i in range(8)], ["Y", "-W"]),
    "74HCT153": (["S0", "S1", "-1E", "1I0", "1I1", "1I2", "1I3", "-2E", "2I0", "2I1", "2I2", "2I3"],
                 [None, None, None, "1Y", None, None, None, None, "2Y"]),
    "74HCT14": (["1A", "2A", "3A", "4A", "5A", "6A"], ["1Y", "2Y", "3Y", "4Y", "5Y", "6Y"]),
    "74HCT27": (["1A", "1B", "1C", "2A", "2B", "2C", "3A", "3B", "3C"], [None, "1Y", None, None, "2Y", None, None, "3Y"]),
    "74HCT4002": (["1A", "1B", "1C", "1D", "2A", "2B", "2C", "2D", "NC6", "NC8"],
                  [None, "1Y", None, None, None, "2Y"]),
    "74HCT125": (["-1OE", "1A", "-2OE", "2A", "-3OE", "3A", "-4OE", "4A"],
                 [None, "1Y", None, "2Y", None, "3Y", None, "4Y"]),
    "74HCT148": (["-EI"] + ["-I%d" % i for i in range(8)], ["-A0", "-A1", "-A2", "-GS", "-EO"]),
    "SST39SF010A": (["A%d" % i for i in range(17)] + ["-CE", "-OE", "-WE", "NC1", "NC30"],
                    ["DQ%d" % i for i in range(8)]),
    "OSC15M": (["EN"], ["OUT"]),
    "SW_PUSH": (["1"], ["2"]),
    "SW_DIP2": (["1", "2"], ["4", "3"]),
    "IDC50": ([str(n) for n in range(1, 51, 2)], [str(n) for n in range(2, 51, 2)]),
})
LAYOUT["74HCT540"] = LAYOUT["74HCT541"]
LAYOUT.update({
    "MCP23S17": (["-CS", "SCK", "SI", "SO", "-RESET", "A0", "A1", "A2", "INTA", "INTB"],
                 ["GPA%d" % i for i in range(8)] + ["GPB%d" % i for i in range(8)]),
    "PICO": (["GP%d" % g for g in (16, 17, 18, 19, 20, 21, 22, 26, 27, 28)] +
             ["GP%d" % g for g in range(16)],
             ["VBUS", "VSYS", "3V3", "3V3_EN", "RUN", "ADC_VREF"]),
    "D_SCHOTTKY": (["A"], ["K"]),
})
LAYOUT["SST39SF010A_PLCC"] = LAYOUT["SST39SF010A"]
LAYOUT.update({
    "SST39SF020A": (["A%d" % i for i in range(18)] + ["-CE", "-OE", "-WE", "NC1"],
                    ["DQ%d" % i for i in range(8)]),
    "AS6C4008": (["A%d" % i for i in range(19)] + ["-CE", "-OE", "-WE"], ["DQ%d" % i for i in range(8)]),
    "74HC05": (["1A", "2A", "3A", "4A", "5A", "6A"], ["1Y", "2Y", "3Y", "4Y", "5Y", "6Y"]),
    "JUMPER2": (["1"], ["2"]),
    "C1000U": (["+"], ["-"]),
    "TERM2": (["1"], ["2"]),
    "MH_M3": (["1"], []),
    "74HCT30": (list("ABCDEFGH") + ["NC9", "NC10", "NC13"], [None, None, None, "Y"]),
    "TL16C550": (["A0", "A1", "A2", "CS0", "CS1", "-CS2", "-ADS", "-RD1", "RD2", "-WR1", "WR2", "MR",
                  "XIN", "RCLK", "SIN", "-CTS", "-DSR", "-DCD", "-RI"],
                 ["D%d" % i for i in range(8)] + ["INTRPT", "SOUT", "-RTS", "-DTR", "-OUT1", "-OUT2",
                  "-BAUDOUT", "XOUT", "DDIS", "-TXRDY", "-RXRDY"]),
    "OSC1M8432": (["EN"], ["OUT"]),
    "RNET9": (["COM"], ["R%d" % i for i in range(1, 9)]),
    "RNET16": (["R%dA" % i for i in range(1, 9)], ["R%dB" % i for i in range(1, 9)]),
    "C150P": (["1"], ["2"]),
    "BAT85": (["A"], ["K"]),
    "SN74S1053": (["D%02d" % i for i in range(1, 9)], ["D%02d" % i for i in range(9, 17)]),
    "IDC40": ([str(n) for n in range(1, 41, 2)], [str(n) for n in range(2, 41, 2)]),
    "IDC20": ([str(n) for n in range(1, 21, 2)], [str(n) for n in range(2, 21, 2)]),
    "IDC10": ([str(n) for n in range(1, 11, 2)], [str(n) for n in range(2, 11, 2)]),
    "CD74HC4050": (["1A", "2A", "3A", "4A", "5A", "6A", "NC13", "NC16"], ["1Y", "2Y", "3Y", "4Y", "5Y", "6Y"]),
    "HDR1X6": ([str(n) for n in range(1, 7)], []),
    "SOCK2X8": ([str(n) for n in range(1, 17, 2)], [str(n) for n in range(2, 17, 2)]),
    "C2N2": (["1"], ["2"]),
    "WIZ850IO": (["MOSI", "SCLK", "SCNn", "RSTn", "NC10"], ["MISO", "INTn", "3V3A", "3V3B", "GND1", "GND2", "GND7"]),
})
LAYOUT["74HCT163"] = LAYOUT["74HCT161"]
LAYOUT["74LVC244"] = LAYOUT["74HCT244"]
LAYOUT["74HCT574"] = (["-OE", "CLK"] + ["D%d" % i for i in range(8)], [None, None] + ["Q%d" % i for i in range(8)])
LAYOUT["74LVC574"] = LAYOUT["74HCT574"]
LAYOUT["LD1117V33"] = (["IN"], ["OUT"])
LAYOUT["DS12C887"] = (["MOT", "-CS", "AS", "-WR", "-RD", "-RESET", "-RCLR", "NC2", "NC3", "NC16", "NC20", "NC22"],
                      ["AD%d" % i for i in range(8)] + ["-IRQ", "SQW"])
LAYOUT["74HCT257"] = (["S", "-OE", "1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B"],
                      [None, None, "1Y", None, "2Y", None, "3Y", None, "4Y", None])
LAYOUT["74HCT253"] = (["S0", "S1", "-1OE", "1I0", "1I1", "1I2", "1I3", "-2OE", "2I0", "2I1", "2I2", "2I3"],
                      [None, None, None, "1Y", None, None, None, None, "2Y"])
LAYOUT["74HCT688"] = (["-G"] + ["P%d" % i for i in range(8)] + ["Q%d" % i for i in range(8)], [None] * 8 + ["-P=Q"])

def layout(dev):
    """Pin order on each side of a symbol; GALs: inputs left, outputs right."""
    if dev in LAYOUT:
        return LAYOUT[dev]
    pins = DEV[dev]["pins"]
    if dev.startswith("GAL_") or dev == "TANG_NANO_20K":      # by pin number: inputs left, outputs right
        by_num = sorted(pins, key=lambda p: int(pins[p][0]))
        left = [p for p in by_num if pins[p][1] in ("in", "pas")]
        right = [p for p in by_num if pins[p][1] in ("out", "tri", "bidir", "oc")]
        return left, right
    raise KeyError("no schematic layout for device %s" % dev)

class Sheet:
    def __init__(self, card, title):
        self.card, self.title = card, title
        self.items = []                 # (x, y, draw function, width, height)

def net_of(card):
    m = {}
    for net, pins in card.nets.items():
        for rp in pins:
            m[rp] = net
    return m

def label_color(net):
    if net in ("VCC", "GND"):
        return RED
    if net in SIGNALS:
        return BLUE
    return BLACK

class Renderer:
    def __init__(self, card):
        self.card = card
        self.net = net_of(card)
        self.drawn = {}                 # (ref, pin) -> sheet number

    def mark(self, ref, pin, sheet):
        key = (ref, pin)
        assert key not in self.drawn, "pin drawn twice: %s %s" % key
        self.drawn[key] = sheet

    # ---------------------------------------------------------------- part symbol
    def symbol_size(self, ref):
        dev = self.card.parts[ref][0]
        left, right = layout(dev)
        rows = max(len(left), len(right))
        names = [p for p in left + right if p]
        bw = max(46, max(stringWidth(p, FONT, FS) for p in names) * 2 + 18)
        if len(left) <= 1 and len(right) <= 1:
            bw = 30
        lw = max([stringWidth(self.net.get((ref, p), "x"), FONT, FS) for p in left if p] + [8])
        rw = max([stringWidth(self.net.get((ref, p), "x"), FONT, FS) for p in right if p] + [8])
        h = rows * PITCH + 30
        return lw + STUB + 4, bw, rw + STUB + 4, h

    def draw_part(self, c, ref, x, y, sheet):
        """x, y = top-left of the whole symbol including the left labels."""
        dev, func, value = self.card.parts[ref]
        lw, bw, rw, h = self.symbol_size(ref)
        left, right = layout(dev)
        bx = x + lw
        top = y - 14
        c.setStrokeColor(BLACK); c.setLineWidth(0.8)
        c.rect(bx, y - h, bw, h - 14)
        c.setFillColor(BLACK); c.setFont("Helvetica-Bold", 7.5)
        c.drawString(bx, y - 10, ref)
        c.setFont(FONT, 6)
        c.drawString(bx + stringWidth(ref, "Helvetica-Bold", 7.5) + 4, y - 10, value)
        pw = [(p, n) for p, (n, t) in DEV[dev]["pins"].items() if t in ("pwr", "pwr3", "gnd")]
        if pw:
            c.setFont(FONT, FS_PIN); c.setFillColor(RED)
            txt = "  ".join("%s %s" % (p, n) for p, n in sorted(pw))
            c.drawCentredString(bx + bw / 2, y - h + 4, txt)
            for p, n in pw:
                self.mark(ref, p, sheet)
        if func and len(left) > 1:
            c.setFillColor(GREY); c.setFont("Helvetica-Oblique", 5.2)
            c.drawCentredString(bx + bw / 2, y - h - 7, func[:48])
        for side, pins in ((0, left), (1, right)):
            for i, p in enumerate(pins):
                if not p:
                    continue
                py = top - 10 - i * PITCH
                num = DEV[dev]["pins"][p][0]
                x0 = bx if side == 0 else bx + bw
                x1 = x0 - STUB if side == 0 else x0 + STUB
                c.setStrokeColor(BLACK); c.setLineWidth(0.6)
                c.line(x0, py, x1, py)
                c.setFillColor(BLACK); c.setFont(FONT, FS)
                if side == 0:
                    c.drawString(x0 + 3, py - 2.2, p)
                else:
                    c.drawRightString(x0 - 3, py - 2.2, p)
                c.setFillColor(GREY); c.setFont(FONT, FS_PIN)
                c.drawCentredString((x0 + x1) / 2, py + 1.6, num)
                net = self.net.get((ref, p))
                if net is None:                         # deliberately unconnected
                    c.setStrokeColor(GREY)
                    c.line(x1 - 2, py - 2, x1 + 2, py + 2); c.line(x1 - 2, py + 2, x1 + 2, py - 2)
                else:
                    c.setFillColor(label_color(net)); c.setFont(FONT, FS)
                    if side == 0:
                        c.drawRightString(x1 - 2, py - 2.2, net)
                    else:
                        c.drawString(x1 + 2, py - 2.2, net)
                self.mark(ref, p, sheet)

    # ---------------------------------------------------------------- connectors
    def draw_connector(self, c, ref, x, y, sheet, colw=150):
        """J1/J2 as a pin table: the used signal pins, then the power pins."""
        sig, power = [], {"VCC": [], "GND": []}
        for conn_ref, pin in sorted(((r, p) for r, p in self.net if r == ref),
                                    key=lambda rp: ("ABC".index(rp[1][0]), int(rp[1][1:]))):
            net = self.net[(conn_ref, pin)]
            if net in power:
                power[net].append(pin)
            else:
                sig.append((pin, net))
            self.mark(conn_ref, pin, sheet)
        rows_per_col = 44
        cols = (len(sig) + rows_per_col - 1) // rows_per_col or 1
        width = max(cols, 2) * colw
        plines = []                                     # power pin lists, wrapped to the box
        for name in ("VCC", "GND"):
            line = name + ":"
            for p in power[name]:
                if stringWidth(line + " " + p, FONT, FS_PIN) > width - 10:
                    plines.append(line)
                    line = "      "
                line += " " + p
            plines.append(line)
        h = min(len(sig), rows_per_col) * 9 + 30 + 7 * len(plines)
        c.setStrokeColor(BLACK); c.setLineWidth(0.8)
        c.rect(x, y - h, width, h - 14)
        c.setFillColor(BLACK); c.setFont("Helvetica-Bold", 7.5)
        c.drawString(x, y - 10, ref)
        c.setFont(FONT, 6)
        c.drawString(x + 16, y - 10, self.card.parts[ref][2] + " — " + self.card.parts[ref][1])
        for i, (pin, net) in enumerate(sig):
            cx = x + (i // rows_per_col) * colw
            cy = y - 24 - (i % rows_per_col) * 9
            c.setFillColor(GREY); c.setFont(FONT, FS_PIN); c.drawString(cx + 4, cy, pin)
            c.setFillColor(label_color(net)); c.setFont(FONT, FS); c.drawString(cx + 24, cy, net)
        c.setFillColor(RED); c.setFont(FONT, FS_PIN)
        for i, line in enumerate(plines):
            c.drawString(x + 4, y - h + 5 + 7 * (len(plines) - 1 - i), line)
        unused = len([1 for r, p in self.card.nc if r == ref])
        c.setFillColor(GREY); c.drawString(x + 4, y - h - 7, "%d further pins not used on this card" % unused)
        for r, p in self.card.nc:
            if r == ref:
                self.drawn[(r, p)] = sheet
        return width, h + 10

    # ---------------------------------------------------------------- pages
    def title_block(self, c, sheet_no, n_sheets, title):
        w, h = 330, 46
        x, y = PAGE_W - MARGIN - w, MARGIN
        c.setStrokeColor(BLACK); c.setLineWidth(1)
        c.rect(MARGIN, MARGIN, PAGE_W - 2 * MARGIN, PAGE_H - 2 * MARGIN)
        c.rect(x, y, w, h)
        c.setFillColor(BLACK); c.setFont("Helvetica-Bold", 10)
        c.drawString(x + 6, y + h - 14, self.card.title)
        c.setFont(FONT, 8)
        c.drawString(x + 6, y + h - 27, "Sheet %d of %d: %s" % (sheet_no, n_sheets, title))
        c.setFont(FONT, 6.5); c.setFillColor(GREY)
        c.drawString(x + 6, y + 8, "Generated by generators/p32schem.py from generators/card_%s.py"
                     % self.card.name.split("-")[0])
        c.setFont(FONT, 6.5)
        c.drawString(MARGIN + 6, MARGIN + 8,
                     "Pins with the same label are connected.  Blue = backplane signal,"
                     " red = power, black = local net.  -X = active low.  x = not connected.")

    def render(self, sheets, out):
        c = pdfc.Canvas(out, pagesize=(PAGE_W, PAGE_H))
        c.setTitle(self.card.title + " schematic")
        c.setAuthor("P32W")
        for n, (title, rows) in enumerate(sheets, 1):
            self.title_block(c, n, len(sheets), title)
            c.setFont("Helvetica-Bold", 12); c.setFillColor(BLACK)
            c.drawString(MARGIN + 8, PAGE_H - MARGIN - 18, title)
            # measure rows, then scale the sheet to fit the drawing area
            meas = []
            for row in rows:
                items = []
                for ref in row:
                    if self.card.parts[ref][0] in ("DIN96", "DIN96F"):
                        items.append((ref, 300, 9 * min(44, 60) + 50))
                    else:
                        lw, bw, rw, h = self.symbol_size(ref)
                        items.append((ref, lw + bw + rw, h + 10))
                meas.append(items)
            total_w = max(sum(w for _, w, _ in items) + 24 * (len(items) - 1) for items in meas)
            total_h = sum(max(h for _, _, h in items) for items in meas) + 20 * (len(meas) - 1)
            avail_w, avail_h = PAGE_W - 2 * MARGIN - 20, PAGE_H - 2 * MARGIN - 90
            s = min(1.0, avail_w / total_w, avail_h / total_h)
            c.saveState()
            c.translate(MARGIN + 10, PAGE_H - MARGIN - 30)
            c.scale(s, s)
            y = 0
            for items in meas:
                x = 0
                for ref, w, h in items:
                    if self.card.parts[ref][0] in ("DIN96", "DIN96F"):
                        w, _ = self.draw_connector(c, ref, x, y, n)
                    else:
                        self.draw_part(c, ref, x, y, n)
                    x += w + 24
                y -= max(h for _, _, h in items) + 20
            c.restoreState()
            c.showPage()
        c.save()
        # every connected pin drawn exactly once (power pins noted in their box)
        missing = [rp for rps in self.card.nets.values() for rp in rps if rp not in self.drawn]
        if missing:
            raise SystemExit("schematic: %d pins not drawn, e.g. %s" % (len(missing), missing[:5]))
        return len(self.drawn)

def render(card, sheets, out):
    return Renderer(card).render(sheets, out)

if __name__ == "__main__":
    import p32cards  # noqa: F401  (loads every card)
    from p32net import CARDS
    name, out = sys.argv[1], sys.argv[2]
    mod = sys.modules["card_" + name.split("-")[0]]
    n = render(CARDS[name], mod.SHEETS, out)
    print("%s: %d pins drawn on %d sheets -> %s" % (name, n, len(mod.SHEETS), out))
