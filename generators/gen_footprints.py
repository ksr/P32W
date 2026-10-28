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

FOOTPRINTS = {"Sipeed_Tang_Nano_20K_Socket": nano20k, "WIZ850io_Socket": wiz850io}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, f in FOOTPRINTS.items():
        p = os.path.join(OUT, name + ".kicad_mod")
        open(p, "w").write(f())
        print("wrote " + p)
