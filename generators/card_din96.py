#!/usr/bin/env python3
"""The DIN 96 shorts tester (D129): docs/din96-tester.md.

A bench tool for P32W, YACC1 and any system on 96-pin DIN 41612 connectors (Ken, 2026-11-16): plugged onto ONE
connector of a board that has nothing on it yet but its connectors, it finds every pin joined to another. An
Arduino Nano and six MCP23S17 port expanders, one expander pin on each of the 96 pins; the Nano pulls a pin low
and reads all the others through the expanders' own pull-ups. TEST starts it; GREEN, only the supply pins are
joined, as the connector's type has them (the one lever of the TYPE switch that is on); RED, something else is. The Nano's USB powers
it and its serial port names the pins.

  * X1, a plug (the cards' right-angle part), goes into a backplane's socket; X2, a socket (the backplane's
    part), takes a card's plug. The two are wired pin for pin.
  * U1-U6: an expander for every sixteen pins, counted a1 b1 c1 a2 ... c32. Each has its own select from the
    Nano (D5-D10), so no expander needs an address. RN2 pulls the selects up, to fit or leave out (D132).
  * The connector types are pin tables, hardware/din96-tester/pins/*.txt (CONNECTOR PIN SIGNAL, as each
    system's bus definition prints them): p32w.txt is written here from p32bus.py, and a pin a card ties to its
    ground on purpose is marked gnd-ok; yacc1.txt is YACC1-D's tools/bus_pins.py, kept as a copy.
  * --firmware writes hardware/din96-tester/firmware/din96_tester/profiles.h: the types, and where each
    connector pin sits on the expanders, from this netlist. The sketch never has a pin number of its own.

    python3 card_din96.py [--bom] [--firmware] [--wiring]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.normpath(os.path.join(HERE, ".."))
from p32sheets import sheets                                        # noqa: E402
from p32net import Card, CARDS, check, bom                          # noqa: E402
from p32bus import ALLPINS, pinnet, CONNECTORS                      # noqa: E402

NAME = "din96-tester"
DIR = os.path.join(ROOT, "hardware", NAME)
ROWS, COLS = 32, "ABC"
NCHIP = 6

def pin_index(pin):
    """A1 -> 0, B1 -> 1, C1 -> 2, A2 -> 3 ... C32 -> 95: the firmware's number for a connector pin."""
    return 3 * (int(pin[1:]) - 1) + COLS.index(pin[0].upper())

PINS = sorted(("%s%d" % (c, r) for r in range(1, ROWS + 1) for c in COLS), key=pin_index)

# Which expander pin has which connector pin. An expander takes sixteen pins running, five and a third rows of
# the connector, and stands beside them on the board (p32din96.py): port B (its pins 1-8, one long side) the
# eight nearer pin 1, port A (pins 21-28, the other side) the eight after. The firmware reads this table.
def expander_pin(pin):
    """(chip 0-5, port 'A' or 'B', bit 0-7)"""
    k = pin_index(pin)
    chip, n = divmod(k, 16)
    return (chip, "B", n) if n < 8 else (chip, "A", 15 - n)

# the Nano's pins (the sketch's too: --firmware checks nothing here, the sketch names the same ones)
NANO = {"TEST": "D2", "PASS": "D3", "FAIL": "D4", "SCK": "D13", "MOSI": "D11", "MISO": "D12"}
NANO.update(("TYPE%d" % k, "A%d" % (k - 1)) for k in range(1, 9))      # the TYPE switch's levers 1-8: A0-A7
NANO_CS = ["D5", "D6", "D7", "D8", "D9", "D10"]

# ---------------------------------------------------------------- the connector types
def read_pins(path):
    """{connector: {pin: (signal, gnd-ok)}} of a pin table."""
    out = {}
    for line in open(path):
        f = line.split("#")[0].split()
        if len(f) >= 3:
            out.setdefault(f[0], {})[f[1].upper()] = (f[2], "gnd-ok" in f[3:])
    return out

_P32W = []
def p32w_pins():
    if not _P32W:
        _P32W.append(_p32w_pins())
    return _P32W[0]

def _p32w_pins():
    """P32W's three connectors from the bus definition, and the pins a card ties to its own ground (the "I am
    in" pins, D102): found in the cards' netlists, not listed here."""
    import importlib
    tied = set()
    power = {(c, p) for c, p in ALLPINS if pinnet(c, p) in ("VCC", "GND")}
    for mod in ("card_cpu", "card_mmu", "card_mem", "card_io", "card_disk", "card_video", "card_debug"):
        card = importlib.import_module(mod).build()[0]
        for net, pins in card.nets.items():
            cp = [(r, p) for r, p in pins if r in CONNECTORS]
            if net == "GND":
                tied |= {x for x in cp if x not in power}
            elif net == "VCC" and any(x not in power for x in cp):
                raise SystemExit("%s ties a bus signal to +5 V: the tester's types have no word for that" % card.name)
            elif net not in ("VCC", "GND") and len(cp) > 1:
                raise SystemExit("%s joins two bus pins (%s): the tester's types have no word for that" % (card.name, net))
    lines = ["# P32W's bus connectors, from generators/p32bus.py (written by generators/card_din96.py: do not edit).",
             "# gnd-ok: a signal that a card ties to its own ground on purpose."]
    for conn, pin in ALLPINS:
        sig = pinnet(conn, pin) or "-"
        lines.append("%s %s %s%s" % (conn, pin, sig, " gnd-ok" if (conn, pin) in tied else ""))
    return "\n".join(lines) + "\n"

# The connector types: (name, pin table, connector). The first, with no table, is the tester by itself: no pin
# may be joined to another. Each of the others is ONE LEVER of the TYPE switch, in this order, with its name
# printed beside it (Ken, 2026-11-16: "The switch is labeled 1-2-4-8 but types is 0,1,2,3,4": no adding up).
# No lever on is the first; more than one on is no test.
TYPES = [("the tester by itself: no pin may be joined to another", None, None),
         ("P32W J1", "p32w.txt", "J1"), ("P32W J2", "p32w.txt", "J2"),
         ("P32W J3", "p32w.txt", "J3"), ("YACC1 X1", "yacc1.txt", "X1")]
LEVERS = 8                 # (Ken: "8 position switch for future expansion": four have a connector, four are spare)
assert len(TYPES) - 1 <= LEVERS, "more connector types than the TYPE switch has levers"

def types():
    out = []
    for name, f, conn in TYPES:
        if f is None:
            out.append((name, None)); continue
        t = read_pins(os.path.join(DIR, "pins", f))
        if conn not in t or sorted(t[conn], key=pin_index) != PINS:
            raise SystemExit("%s: %s has not all 96 pins of %s" % (name, f, conn))
        out.append((name, t[conn]))
    return out

def pin_class(sig, gndok):
    return 1 if sig in ("VCC", "+5V") else 2 if sig == "GND" else 3 if gndok else 0

def firmware_header(card):
    """profiles.h: the types (two bits a pin: signal, +5 V, ground, signal that may be on ground; and the
    pins' names), and the place of each connector pin on the expanders, read back from the netlist."""
    where = {rp: n for n, ps in card.nets.items() for rp in ps}
    tmap = []
    for pin in PINS:
        net = where[("X1", pin)]
        (u, gp), = [(r, p) for r, p in card.nets[net] if r.startswith("U")]
        tmap.append(16 * (int(u[1:]) - 1) + 8 * (gp[2] == "B") + int(gp[3]))
    ty = types()
    out = ["/* profiles.h -- the connector types, and where each connector pin sits on the expanders.",
           " * WRITTEN BY generators/card_din96.py --firmware from the tester's netlist and hardware/din96-tester/pins:",
           " * do not edit. */", "#define T_NTYPES %d" % len(ty)]
    for k, (name, pins) in enumerate(ty):
        out.append('static const char t_tn%d[] PROGMEM = "%s";' % (k, name))
    out.append("static const char *const t_typename[T_NTYPES] PROGMEM = {%s};" % ", ".join("t_tn%d" % k for k in range(len(ty))))
    rows = []
    for name, pins in ty:
        c = [0] * 96
        for pin, (sig, ok) in (pins or {}).items():
            c[pin_index(pin)] = pin_class(sig, ok)
        rows.append("{" + ", ".join("0x%02X" % sum(c[4 * b + j] << (2 * j) for j in range(4)) for b in range(24)) + "}")
    out.append("static const uint8_t t_typeclass[T_NTYPES][24] PROGMEM = {\n    " + ",\n    ".join(rows) + "};")
    for k, (name, pins) in enumerate(ty):
        if pins:
            nm = {pin_index(p): ("+5V" if pin_class(s, ok) == 1 else s) for p, (s, ok) in pins.items()}
            out.append("static const char t_pn%d[] PROGMEM =\n    %s;" % (k, "\n    ".join(
                " ".join('"%s\\0"' % nm[i] for i in range(a, a + 12)) for a in range(0, 96, 12))))
    out.append("static const char *const t_pinnames[T_NTYPES] PROGMEM = {%s};"
               % ", ".join("t_pn%d" % k if pins else "0" for k, (n, pins) in enumerate(ty)))
    out.append("/* 16 * (expander - 1) + 8 * (port B) + bit, for pins a1 b1 c1 a2 ... c32 */")
    out.append("static const uint8_t t_map[96] PROGMEM = {\n    " + ",\n    ".join(
        ", ".join("%2d" % m for m in tmap[a:a + 16]) for a in range(0, 96, 16)) + "};")
    return "\n".join(out) + "\n"

# ---------------------------------------------------------------- the netlist
def build():
    c = Card(NAME, "DIN 96 SHORTS TESTER")
    for j in CONNECTORS:                    # (a card's three bus connectors: this board has two of its own)
        del c.parts[j]
    c.part("X1", "DIN96", "PLUG: into a backplane's socket")
    c.part("X2", "DIN96F", "SOCKET: onto a card's plug")
    c.part("A1", "NANO", "the Nano: USB to power it and to report")
    for k in range(NCHIP):
        u = "U%d" % (k + 1)
        c.part(u, "MCP23S17", "pins %s to %s" % (PINS[16 * k].lower(), PINS[16 * k + 15].lower()))
        c.net("VCC", (u, "VDD")); c.net("GND", (u, "VSS"), (u, "A0"), (u, "A1"), (u, "A2"))
        c.net("SCK", (u, "SCK")); c.net("MOSI", (u, "SI")); c.net("MISO", (u, "SO"))
        c.net("-CS%d" % (k + 1), (u, "-CS"), ("A1", NANO_CS[k]))
        c.net("-RESET", (u, "-RESET"))
        c.unused(u, "INTA", "INTB")
        cap = "C%d" % (k + 1)
        c.part(cap, "C100N", "decoupling " + u)
        c.net("VCC", (cap, "1")); c.net("GND", (cap, "2"))
    c.part("C7", "C10U", "bulk")
    c.net("VCC", ("C7", "+"), ("A1", "+5V")); c.net("GND", ("C7", "-"), ("A1", "GND1"), ("A1", "GND2"))
    c.net("SCK", ("A1", NANO["SCK"])); c.net("MOSI", ("A1", NANO["MOSI"])); c.net("MISO", ("A1", NANO["MISO"]))
    c.net("-RESET", ("A1", "-RESET2"))      # (the Nano's reset, its own button and the USB's, resets the expanders)
    c.unused("A1", "-RESET1", "D0", "D1", "3V3", "AREF", "VIN")
    # the select lines' pull-ups, to fit or leave out (D132; Ken: "might as well design in pullups for tester, i can
    # decide later to populate"): while the Nano is in its bootloader D5-D10 float and D13, the SPI clock, blinks;
    # a 10k each holds every expander deselected. RN1's part, its last two resistors spare
    c.part("RN2", "RNET9", "the select lines' pull-ups, to fit or not", value="10k x 8 bussed")
    c.net("VCC", ("RN2", "COM"))
    for k in range(NCHIP):
        c.net("-CS%d" % (k + 1), ("RN2", "R%d" % (k + 1)))
    c.unused("RN2", *["R%d" % i for i in range(NCHIP + 1, 9)])
    for pin in PINS:                        # every connector pin: the plug's, the socket's, an expander's
        chip, port, bit = expander_pin(pin)
        c.net("P_" + pin, ("X1", pin), ("X2", pin), ("U%d" % (chip + 1), "GP%s%d" % (port, bit)))
    c.part("SW1", "SW_PUSH", "TEST")
    c.net("TEST", ("A1", NANO["TEST"]), ("SW1", "1")); c.net("GND", ("SW1", "2"))
    # TYPE: a lever a connector type; on, it grounds its line. A 10k each to +5 V: the Nano's A6 and A7 have no
    # pull-ups of their own (and are read as voltages), so all eight get the same
    c.part("SW2", "SW_DIP8", "TYPE: the connector's type, a lever each")
    c.part("RN1", "RNET9", "the TYPE levers' pull-ups", value="10k x 8 bussed")
    c.net("VCC", ("RN1", "COM"))
    for i in range(1, LEVERS + 1):
        c.net("TYPE%d" % i, ("A1", NANO["TYPE%d" % i]), ("SW2", str(i)), ("RN1", "R%d" % i))
        c.net("GND", ("SW2", str(17 - i)))
    for ref, res, fn, val, net, rv in (("D1", "R1", "POWER", "LED 3mm yellow", "VCC", "1k"),
                                       ("D2", "R2", "PASS", "LED 3mm green", "PASS", "470"),
                                       ("D3", "R3", "FAIL", "LED 3mm red", "FAIL", "470")):
        c.part(ref, "LED", fn, value=val)
        c.part(res, "R", "%s LED resistor" % fn, value=rv)
        if net == "VCC":
            c.net("VCC", (res, "1"))
        else:
            c.net(net, ("A1", NANO[net]), (res, "1"))
        c.net("%s_A" % fn, (res, "2"), (ref, "A")); c.net("GND", (ref, "K"))
    # the schematic's sheets (generators/p32sheets.py); the capacitors and the POWER LED go on its power sheet
    c.sheets = sheets(c, [
        ("The plug and the socket, pin for pin", [["X1", "X2"]]),
        ("The Nano: TEST, TYPE, PASS and FAIL", [["A1", "SW1", "SW2", "RN1", "RN2"], ["R2", "D2", "R3", "D3"]]),
        ("Expanders U1 to U3: pins a1 to c16", [["U1", "U2", "U3"]]),
        ("Expanders U4 to U6: pins a17 to c32", [["U4", "U5", "U6"]]),
    ])
    CARDS[c.name] = c
    return c

def own_check(c):
    """Every connector pin on one expander pin, the plug and the socket pin for pin, every expander pin used."""
    errs = []
    used = {}
    for pin in PINS:
        net = c.nets["P_" + pin]
        refs = sorted(r for r, p in net)
        if refs != ["U%d" % (expander_pin(pin)[0] + 1), "X1", "X2"] or {p for r, p in net if r[0] == "X"} != {pin}:
            errs.append("P_%s: %s" % (pin, net))
        gp = [(r, p) for r, p in net if r[0] == "U"][0]
        if gp in used:
            errs.append("%s %s has %s and %s" % (gp[0], gp[1], used[gp], pin))
        used[gp] = pin
    if len(used) != 96:
        errs.append("%d expander pins used, not 96" % len(used))
    # The tester's own supply (the Nano's 5 V and ground) reaches no connector pin (Ken, 2026-11-16: "The
    # Nano's 5V and GND shouldn't route to DIN96"): it would join the board under test's pins through the
    # tester. A connector pin is on its own net with its twin and one expander pin, and on nothing else.
    for net, pins in c.nets.items():
        conn = [(r, p) for r, p in pins if r in ("X1", "X2")]
        if conn and (not net.startswith("P_") or len(pins) != 3):
            errs.append("net %s has connector pins %s besides %d other pins" % (net, conn, len(pins) - len(conn)))
    return errs

PINS_P32W = os.path.join(DIR, "pins", "p32w.txt")
PROFILES = os.path.join(DIR, "firmware", "din96_tester", "profiles.h")

def main():
    if "--wiring" in sys.argv:              # "A1 1 B 0": pin a1 is on U1's GPB0. Straight from the nets, for the
        c = build()                         # firmware's simulation (emulator/test/din96_sim.cc), which must not
        for pin in PINS:                    # take its wiring from profiles.h, the thing it is checking
            (u, gp), = [(r, p) for r, p in c.nets["P_" + pin] if r.startswith("U")]
            print("%s %s %s %s" % (pin, u[1:], gp[2], gp[3]))
        return
    if "--firmware" in sys.argv:            # P32W's pin table first: the types are read from the files
        os.makedirs(os.path.dirname(PINS_P32W), exist_ok=True)
        open(PINS_P32W, "w").write(p32w_pins())
    c = build()
    errs = check(c) + own_check(c)
    if errs:
        print("\n".join(errs[:40]))
        sys.exit("DIN 96 tester: %d errors" % len(errs))
    if "--firmware" in sys.argv:
        open(PROFILES, "w").write(firmware_header(c))
        print("wrote %s and %s" % (os.path.relpath(PINS_P32W, ROOT), os.path.relpath(PROFILES, ROOT)))
    stale = [os.path.relpath(f, ROOT) for f, want in ((PINS_P32W, p32w_pins()), (PROFILES, firmware_header(c)))
             if not os.path.exists(f) or open(f).read() != want]
    if stale:
        sys.exit("DIN 96 tester: stale: %s (python3 generators/card_din96.py --firmware)" % ", ".join(stale))
    print("DIN 96 tester OK: %d parts, %d nets; 96 connector pins on 6 expanders, none on the tester's own "
          "supply; %d connector types; its pin table and profiles.h as the bus has them"
          % (len(c.parts), len(c.nets), len(TYPES)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
