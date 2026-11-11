#!/usr/bin/env python3
"""The project's own KiCad footprints (hardware/lib/P32W.pretty), for parts the
KiCad library does not have. Generated so the geometry is written down once.

    python3 generators/gen_footprints.py

Sipeed_Tang_Nano_20K_Socket: the CPU card's FPGA module, the ALU (D21), in two 1x20
female headers, 2.54 mm pitch, the rows 20.32 mm (0.8 in) apart, the module
outline 22.55 x 54.04 mm, the first pins 2.64 mm from the USB-C end and the
last 3.14 mm from the other (Sipeed's datasheet v1.3, "Dimension
Information", D70). Pads 1-20 are the left row from the USB-C end, 21-40 the
right row, top view, as the datasheet's "Pinout"; the USB-C plug stands out
past the board's end.

WIZ850io_Socket: WIZnet's WIZ850io (W5500 + RJ45; the I/O card's Ethernet,
D50) in two 1x6 female headers, 2.54 mm pitch, the rows 20.32 mm apart
(WIZnet's dimension drawing, docs.wiznet.io/Product/ioModule/WIZ850io).
Drawn with the RJ45 opening toward +x, so at rotation 0 it faces a card's
front edge. Seen from above with the RJ45 opening away from you, J1 is the
left row and J2 the right, pin 1 of each at the RJ45 end: pads 1-6 are J1
(GND, GND, MOSI, SCLK, SCNn, INTn), 7-12 are J2 (GND, 3.3V, 3.3V, NC,
RSTn, MISO). The module is 23 x 25 mm; its first pins sit 6.4 mm from the
RJ45 edge, and the RJ45 overhangs that edge by 2.7 mm.

MiniDIN-6_Female_Horizontal_SameSky_MD-60SM: the video card's KEYBOARD and
MOUSE jacks (PS/2), Same Sky (CUI) MD-60SM right-angle, data sheet MD-SM rev
1.09 (2024-09-12), the PCB layout for date codes after 03/03/2022 -- as on
YACC1's video card V2.0 (YACC1-D, video-v2.0-local.pretty). The mating face
at x = 0, opening toward +x; pads 1-6 the contacts, "0" the shell's three
tabs (the netlist's SHELL).

TIL311_DIP-14_W7.62mm: TI's TIL311 hexadecimal display with logic (SODS001D,
"mechanical data"): a red moulded body 0.72-0.76 x 0.38-0.42 in (drawn 18.8 x
10.16 mm) on a 14-pin DIP's pin field, rows 7.62 mm apart, with pins 6, 9
and 11 left out. All 14 pads are here, numbered as a DIP-14 (pad 1 at 0, 0,
pads 1-7 down the left row), so a 14-pin socket fits; pads 6, 9 and 11 stay
unconnected. The character, a 4 x 7 dot matrix 6.9 mm tall leaning 5 degrees,
is upright with pin 1 at the top left: its top row 1.78 mm below pin 1's
row, its decimal points 4.1 mm either side of the centre line. Its 3D models
(hardware/lib/P32W.3dshapes/TIL311_<digit>.wrl, written here too) show the
dots, so a render shows a display and which way up it is.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "hardware", "lib", "P32W.pretty")

def nano20k():
    W, L, ROW, P = 22.55, 54.04, 20.32, 2.54
    x0 = -(W - ROW) / 2                       # the outline relative to pad 1
    y0 = -2.64                                # (the USB-C end)
    def line(layer, xa, ya, xb, yb, w):
        return ('  (fp_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width %.2f) (type solid)) (layer "%s"))'
                % (xa, ya, xb, yb, w, layer))
    def rect(layer, xa, ya, xb, yb, w):
        return [line(layer, xa, ya, xb, ya, w), line(layer, xb, ya, xb, yb, w),
                line(layer, xb, yb, xa, yb, w), line(layer, xa, yb, xa, ya, w)]
    L_ = ['(footprint "Sipeed_Tang_Nano_20K_Socket"',
          '  (version 20240108)', '  (generator "p32")', '  (layer "F.Cu")',
          '  (descr "Sipeed Tang Nano 20K in 2 x 1x20 female headers, rows 20.32 mm apart, pads as the datasheet pinout (generators/gen_footprints.py)")',
          '  (tags "Tang Nano 20K GW2AR-18 socket")',
          '  (property "Reference" "REF**" (at %.2f %.2f 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))'
          % (ROW / 2, y0 - 1.5),
          '  (property "Value" "Sipeed Tang Nano 20K" (at %.2f %.2f 90) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))'
          % (ROW / 2, 9.5 * P),
          '  (attr through_hole)']
    L_ += rect("F.Fab", x0, y0, x0 + W, y0 + L, 0.1)
    L_ += rect("F.SilkS", x0 - 0.12, y0 - 0.12, x0 + W + 0.12, y0 + L + 0.12, 0.12)
    L_ += rect("F.CrtYd", x0 - 0.5, y0 - 0.5, x0 + W + 0.5, y0 + L + 0.5, 0.05)
    L_.append('  (fp_text user "USB-C" (at %.2f %.2f 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))'
              % (ROW / 2, y0 + 1.5))
    L_.append(line("F.SilkS", -1.6, -1.6, -1.6, 1.0, 0.12))                       # pin 1
    for k in range(40):
        x, y = (0.0, k * P) if k < 20 else (ROW, (k - 20) * P)
        shape = "rect" if k == 0 else "circle"
        L_.append('  (pad "%d" thru_hole %s (at %.3f %.3f) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))'
                  % (k + 1, shape, x, y))
    L_.append(")")
    return "\n".join(L_) + "\n"

def _line(layer, xa, ya, xb, yb, w):
    return ('  (fp_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width %.2f) (type solid)) (layer "%s"))'
            % (xa, ya, xb, yb, w, layer))

def _rect(layer, xa, ya, xb, yb, w):
    return [_line(layer, xa, ya, xb, ya, w), _line(layer, xb, ya, xb, yb, w),
            _line(layer, xb, yb, xa, yb, w), _line(layer, xa, yb, xa, ya, w)]

def wiz850io():
    P, ROW = 2.54, 20.32
    # the module in its own frame (pin 1 of J1 at 0,0; the RJ45 toward +x):
    # pin k of J1 at (-(k-1)P, 0), of J2 at (-(k-1)P, ROW)
    bx0, bx1 = -18.6, 6.4                     # the board: 25 mm, 6.4 mm past the first pins
    by0, by1 = -(23.0 - ROW) / 2, ROW + (23.0 - ROW) / 2
    jy0, jy1 = ROW / 2 - 8.05, ROW / 2 + 8.05 # the RJ45 body, 16.10 mm wide, 2.7 mm over the edge
    L_ = ['(footprint "WIZ850io_Socket"',
          '  (version 20240108)', '  (generator "p32")', '  (layer "F.Cu")',
          '  (descr "WIZnet WIZ850io in 2 x 1x6 female headers, rows 20.32 mm apart, RJ45 toward +x (generators/gen_footprints.py)")',
          '  (tags "WIZ850io W5500 Ethernet socket")',
          '  (property "Reference" "REF**" (at %.2f %.2f 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))'
          % (-8.0, ROW / 2),
          '  (property "Value" "WIZ850io" (at %.2f %.2f 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))'
          % (-8.0, ROW / 2 + 2.0),
          '  (attr through_hole)']
    L_ += _rect("F.Fab", bx0, by0, bx1, by1, 0.1)
    L_ += _rect("F.Fab", bx1 - 6.4, jy0, bx1 + 2.7, jy1, 0.1)
    L_ += _rect("F.SilkS", bx0 - 0.12, by0 - 0.12, bx1 + 0.12, by1 + 0.12, 0.12)
    L_ += _rect("F.CrtYd", bx0 - 0.5, by0 - 0.5, bx1 + 2.7 + 0.5, by1 + 0.5, 0.05)
    L_.append('  (fp_text user "RJ45" (at %.2f %.2f 90) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))'
              % (bx1 - 2.0, ROW / 2))
    for k in range(12):
        x, y = (-(k % 6) * P, 0.0 if k < 6 else ROW)
        shape = "rect" if k % 6 == 0 else "circle"
        L_.append('  (pad "%d" thru_hole %s (at %.3f %.3f) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))'
                  % (k + 1, shape, x, y))
    L_.append(")")
    return "\n".join(L_) + "\n"

def minidin6():
    L_ = ['(footprint "MiniDIN-6_Female_Horizontal_SameSky_MD-60SM"',
          '  (version 20240108)', '  (generator "p32")', '  (layer "F.Cu")',
          '  (descr "Mini-DIN-6 (PS/2) right-angle jack, Same Sky MD-60SM (data sheet MD-SM rev 1.09), mating face at x = 0, opening toward +x (generators/gen_footprints.py)")',
          '  (tags "Mini-DIN-6 PS/2 MD-60SM")',
          '  (property "Reference" "REF**" (at -6.5 -8.2 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))',
          '  (property "Value" "MD-60SM" (at -6.5 8.2 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))',
          '  (attr through_hole)']
    L_ += _rect("F.Fab", -13.0, -7.0, 0.0, 7.0, 0.1)
    L_.append(_line("F.Fab", 0.0, -4.0, 0.0, 4.0, 0.3))           # the mating face
    L_ += _rect("F.CrtYd", -13.25, -7.25, 0.25, 7.25, 0.05)
    for xa, ya, xb, yb in ((-13, -7, -13, 7), (-13, -7, -7.4, -7), (-3.6, -7, -0.5, -7),
                           (-13, 7, -7.4, 7), (-3.6, 7, -0.5, 7)):       # (clear of the shell tabs)
        L_.append(_line("F.SilkS", xa, ya, xb, yb, 0.12))
    for n, x, y in ((1, -8.5, -1.3), (2, -8.5, 1.3), (3, -8.5, -3.4), (4, -8.5, 3.4), (5, -11.0, -3.4), (6, -11.0, 3.4)):
        L_.append('  (pad "%d" thru_hole circle (at %.2f %.2f) (size 1.6 1.6) (drill 1.0) (layers "*.Cu" "*.Mask"))' % (n, x, y))
    for x, y, sx, sy, dx, dy in ((-4.7, 0.0, 1.5, 3.0, 0.8, 2.3), (-5.5, -6.75, 3.3, 1.9, 2.6, 1.2), (-5.5, 6.75, 3.3, 1.9, 2.6, 1.2)):
        L_.append('  (pad "0" thru_hole oval (at %.2f %.2f) (size %.1f %.1f) (drill oval %.1f %.1f) (layers "*.Cu" "*.Mask"))'
                  % (x, y, sx, sy, dx, dy))
    L_.append(")")
    return "\n".join(L_) + "\n"

# ---------------------------------------------------------------- the TIL311
TIL_ROW, TIL_P = 7.62, 2.54
TIL_W, TIL_L, TIL_H = 10.16, 18.8, 3.2            # the body (nominal), and its height above the seating plane
TIL_CX, TIL_CY = TIL_ROW / 2, 3 * TIL_P            # its centre, from pad 1
# the sixteen characters as SODS001D draws them (page 2), four dots a row, top row first
TIL_FONT = {
    0x0: (".XX.", "X..X", "X..X", "X..X", "X..X", "X..X", ".XX."),
    0x1: ("...X", "...X", "...X", "...X", "...X", "...X", "...X"),
    0x2: ("XXX.", "...X", "...X", ".XX.", "X...", "X...", "XXXX"),
    0x3: ("XXX.", "...X", "...X", ".XX.", "...X", "...X", "XXX."),
    0x4: ("X..X", "X..X", "X..X", "XXXX", "...X", "...X", "...X"),
    0x5: ("XXXX", "X...", "X...", "XXX.", "...X", "...X", "XXX."),
    0x6: (".XX.", "X...", "X...", "XXX.", "X..X", "X..X", ".XX."),
    0x7: ("XXXX", "...X", "...X", "...X", "...X", "...X", "...X"),
    0x8: (".XX.", "X..X", "X..X", ".XX.", "X..X", "X..X", ".XX."),
    0x9: (".XX.", "X..X", "X..X", ".XXX", "...X", "...X", ".XX."),
    0xA: (".XX.", "X..X", "X..X", "XXXX", "X..X", "X..X", "X..X"),
    0xB: ("XXX.", "X..X", "X..X", "XXX.", "X..X", "X..X", "XXX."),
    0xC: (".XXX", "X...", "X...", "X...", "X...", "X...", ".XXX"),
    0xD: ("XXX.", "X..X", "X..X", "X..X", "X..X", "X..X", "XXX."),
    0xE: ("XXXX", "X...", "X...", "XXX.", "X...", "X...", "XXXX"),
    0xF: ("XXXX", "X...", "X...", "XXX.", "X...", "X...", "X..."),
}

def til311_dots():
    """Each LED's centre (mm, from pad 1): {(row, column): (x, y)}, and the two decimal points."""
    import math
    rowp, colp, top = 6.90 / 6, 4.30 / 3, 1.78
    dots = {}
    for r in range(7):
        lean = (3 - r) * rowp * math.tan(math.radians(5.0))
        for c in range(4):
            dots[(r, c)] = (TIL_CX + (c - 1.5) * colp + lean, top + r * rowp)
    ydp = top + 6.90 + 0.38
    return dots, [(TIL_CX - 4.10, ydp), (TIL_CX + 4.10, ydp)]

def til311():
    x0, x1 = TIL_CX - TIL_W / 2, TIL_CX + TIL_W / 2
    y0, y1 = TIL_CY - TIL_L / 2, TIL_CY + TIL_L / 2
    L_ = ['(footprint "TIL311_DIP-14_W7.62mm"',
          '  (version 20240108)', '  (generator "p32")', '  (layer "F.Cu")',
          '  (descr "TI TIL311 hexadecimal display with logic (SODS001D): a DIP-14 pin field, rows 7.62 mm apart, pins 6, 9 and 11 omitted on the part; body 18.8 x 10.16 mm; the character upright with pin 1 top left (generators/gen_footprints.py)")',
          '  (tags "TIL311 hexadecimal LED display DIP-14")',
          '  (property "Reference" "REF**" (at %.2f %.2f 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))'
          % (TIL_CX, y0 - 1.3),
          '  (property "Value" "TIL311" (at %.2f %.2f 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))'
          % (TIL_CX, y1 + 1.3),
          '  (attr through_hole)']
    L_ += _rect("F.Fab", x0, y0, x1, y1, 0.1)
    dots, dps = til311_dots()
    for (r, c), (x, y) in sorted(dots.items()):          # the dot matrix, on the fab layer
        L_ += _rect("F.Fab", x - 0.3, y - 0.3, x + 0.3, y + 0.3, 0.05)
    L_ += _rect("F.CrtYd", x0 - 0.25, y0 - 0.25, x1 + 0.25, y1 + 0.25, 0.05)
    s = 0.16                                              # the outline on the silk, just outside the body; a tick at pin 1
    L_ += _rect("F.SilkS", x0 - s, y0 - s, x1 + s, y1 + s, 0.12)
    L_.append(_line("F.SilkS", x0 - s - 0.5, y0 - s, x0 - s - 0.5, y0 - s + 2.0, 0.12))
    for n in range(1, 15):
        x, y = (0.0, (n - 1) * TIL_P) if n <= 7 else (TIL_ROW, (14 - n) * TIL_P)
        L_.append('  (pad "%d" thru_hole %s (at %.3f %.3f) (size 1.6 1.6) (drill 0.8) (layers "*.Cu" "*.Mask"))'
                  % (n, "rect" if n == 1 else "circle", x, y))
    L_.append('  (model "${KIPRJMOD}/../../lib/P32W.3dshapes/TIL311_8.wrl"\n'
              '    (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))')
    L_.append(")")
    return "\n".join(L_).replace("\\n", "\n") + "\n"

def til311_wrl(digit):
    """The display as a VRML model (KiCad's unit: 0.1 inch; y up the board), showing `digit`."""
    U = 2.54
    shapes = []
    def box(xa, ya, xb, yb, za, zb, rgb, emit=(0, 0, 0), shiny=0.2):
        (xa, xb), (ya, yb), (za, zb) = sorted((xa / U, xb / U)), sorted((-ya / U, -yb / U)), sorted((za / U, zb / U))
        pts = [(x, y, z) for z in (za, zb) for y in (ya, yb) for x in (xa, xb)]
        faces = ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5))
        shapes.append(
            "Shape {\n  appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f emissiveColor %.3f %.3f %.3f "
            "specularColor 0.3 0.3 0.3 shininess %.2f } }\n"
            "  geometry IndexedFaceSet { solid FALSE coord Coordinate { point [ %s ] }\n    coordIndex [ %s ] }\n}"
            % (rgb + emit + (shiny, ", ".join("%.4f %.4f %.4f" % p for p in pts),
                             ", ".join("%d, %d, %d, %d, -1" % f for f in faces))))
    z0 = 0.5                                              # the seating plane, as KiCad's DIP models
    x0, x1 = TIL_CX - TIL_W / 2, TIL_CX + TIL_W / 2
    y0, y1 = TIL_CY - TIL_L / 2, TIL_CY + TIL_L / 2
    box(x0, y0, x1, y1, z0, z0 + TIL_H, (0.50, 0.04, 0.05), (0.10, 0.0, 0.0), shiny=0.6)   # the red moulded body
    dots, dps = til311_dots()
    glyph = TIL_FONT[digit]
    zt = z0 + TIL_H
    for (r, c), (x, y) in sorted(dots.items()):
        if glyph[r][c] == "X":
            box(x - 0.36, y - 0.36, x + 0.36, y + 0.36, zt, zt + 0.06, (1.0, 0.16, 0.10), (0.95, 0.10, 0.05))
        else:
            box(x - 0.30, y - 0.30, x + 0.30, y + 0.30, zt, zt + 0.03, (0.28, 0.02, 0.03))
    for x, y in dps:
        box(x - 0.30, y - 0.30, x + 0.30, y + 0.30, zt, zt + 0.03, (0.28, 0.02, 0.03))
    for n in range(1, 15):                                # the pins it has
        if n in (6, 9, 11):
            continue
        x, y = (0.0, (n - 1) * TIL_P) if n <= 7 else (TIL_ROW, (14 - n) * TIL_P)
        box(x - 0.25, y - 0.25, x + 0.25, y + 0.25, -3.0, z0, (0.75, 0.75, 0.72), shiny=0.8)
    return "#VRML V2.0 utf8\n# TI TIL311, showing %X (generators/gen_footprints.py)\n" % digit + "\n".join(shapes) + "\n"

FOOTPRINTS = {"Sipeed_Tang_Nano_20K_Socket": nano20k, "WIZ850io_Socket": wiz850io,
              "MiniDIN-6_Female_Horizontal_SameSky_MD-60SM": minidin6, "TIL311_DIP-14_W7.62mm": til311}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, f in FOOTPRINTS.items():
        p = os.path.join(OUT, name + ".kicad_mod")
        open(p, "w").write(f())
        print("wrote " + p)
    shapes = os.path.join(OUT, "..", "P32W.3dshapes")
    os.makedirs(shapes, exist_ok=True)
    for d in sorted(TIL_FONT):
        p = os.path.normpath(os.path.join(shapes, "TIL311_%X.wrl" % d))
        open(p, "w").write(til311_wrl(d))
    print("wrote %s/TIL311_0.wrl .. TIL311_F.wrl" % os.path.normpath(shapes))
