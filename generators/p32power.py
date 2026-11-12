#!/usr/bin/env python3
"""The P32W power budget (D69): every card's +5 V current, typical and
worst case, from its netlist's parts and a table of supply currents.

    python3 p32power.py [--doc FILE]       # print the totals; write docs/power-budget.md

Each device has (typical mA, maximum mA, where the figure comes from).
Typical is the datasheet's typical (or our estimate at the bus clock);
maximum is the datasheet's maximum at its fastest rated frequency, every
part at once: an upper bound, never all reached together. Parts on a 3.3 V
regulator (LD1117) are counted at 5 V: a linear regulator passes its load
current through. Off-card loads that draw from the cards' +5 V (USB
drives, PS/2 devices) are counted on their card.
"""
import os, sys, importlib, collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

DS, EST = "datasheet", "estimate"
# device -> (typ mA, max mA, source, note)
I = {
    "GAL":          (85, 130, DS, "ATF22V10C-7 / -10, ICC (commercial)"),
    "CY7C199":      (80, 155, DS, "max: CY7C199-15 at 1/tRC (66 MHz); typ: the CY7C199D's specified maximum"),
    "AS6C4008":     (30, 60, DS, "ICC at 55 ns cycle; deselected 4 / 50 uA"),
    "SST39SF010A":  (10, 30, DS, "read; standby 30 uA typ (max read to confirm on the datasheet)"),
    "SST39SF020A":  (10, 30, DS, "read; standby 30 uA typ"),
    "TL16C550":     (8, 10, DS, ""),
    "MAX232A":      (8, 20, DS, "8 / 10 mA unloaded, plus two RS-232 loads"),
    "DS12C887":     (7, 15, DS, "max from the distributors' listings"),
    "74LS145":      (7, 13, DS, ""),
    "74F163":       (37, 55, DS, ""),
    "74F161":       (37, 55, DS, "as the 74F163"),
    "74F283":       (36, 55, EST, "the F family's adders"),
    "74F151":       (15, 22, EST, "the F family's muxes"),
    "74F138":       (13, 20, DS, ""),
    "74F153":       (12, 20, DS, ""),
    "74F253":       (12, 20, DS, ""),
    "74F257":       (15, 23, DS, ""),
    "74F521":       (20, 30, EST, "the F family's comparators"),
    "74ABT377":     (5, 30, DS, "30 mA with its outputs low"),
    "TANG_NANO_20K": (100, 300, EST, "the ALU bitstream, SDRAM idle, the BL616: to measure"),
    "WIZ850IO":     (132, 150, DS, "132 mA (WIZnet / DigiKey), through the LD1117"),
    "LD1117V33":    (5, 10, DS, "quiescent"),
    "OSC24M":       (15, 30, EST, "a DIP-14 can oscillator"),
    "OSC32M":       (15, 30, EST, "a DIP-14 can oscillator"),
    "OSC25M":       (15, 30, EST, "a DIP-14 can oscillator"),
    "OSC1M8432":    (10, 20, EST, "a DIP-14 can oscillator"),
    "LED":          (3, 5, EST, "with its resistor"),
    "TL7705A":      (1.8, 3, DS, "ICC"),
    "TIL311":       (105, 180, DS, "a hex display: its logic 60 / 90 mA and its LEDs 45 / 90 mA (TI SODS001D)"),
    "SOCK2X8":      (112, 230, EST, "a CH376S module (12 / 30 mA) and its USB drive (100 / 200 mA, the design's budget)"),
    "MINIDIN6F_RA": (50, 250, EST, "a PS/2 device; both share the 0.5 A polyfuse"),
    # the video card, revision 2 (D103, D106). The chip's supply current is "T.B.D." in Hitachi's manual:
    # one is to be measured (docs/video-card-acrtc.md 8)
    "HD63484":      (60, 100, EST, "CMOS at 6.3 MHz: Hitachi's manual gives no figure; to measure"),
    "AS6C1008":     (30, 60, EST, "taken as the AS6C4008's; to confirm on its datasheet"),
    "27C256":       (10, 30, EST, "an EPROM read all the time; to confirm on the datasheets (27C256, AT27C256R-70)"),
    "ATMEGA328P":   (10, 20, EST, "active at 16 MHz, 5 V"),
    "74F299":       (68, 95, EST, "the F family's shift registers; to confirm on the datasheet"),
    "74F74":        (11, 16, EST, "the F family's flip-flops; to confirm on the datasheet"),
    "74F21":        (4, 8, EST, "the F family's gates; to confirm on the datasheet"),
}
# the fast logic, by family: dynamic current at its clock (the quiescent
# current of CMOS is microamps): HCT / HC / LVC at the bus clock, ACT / LVC
# on the dot or capture clock
FAMILY = [("74ACT", 4, 10, EST, "switching at 12.5-24 MHz"),
          ("74HCT", 1, 4, EST, "switching at the bus clock"),
          ("74HC", 1, 4, EST, "switching at the bus clock"), ("74LVC", 1, 4, EST, "switching at the bus clock, 3.3 V"),
          ("CD74HC", 1, 2, EST, "")]
# card-specific: the Pico is a Pico 2 W (Wi-Fi) on the I/O card, a Pico 2 on the debug card
PICO = {"io": (60, 300, EST, "Pico 2 W: Wi-Fi on (peaks while sending)"),
        "debug": (25, 60, EST, "Pico 2: USB to the Mac")}
PASSIVE = {"C100N", "C10U", "C1000U", "C150P", "C2N2", "R", "RNET9", "RNET16", "JUMPER2", "DIN96", "DIN96F",
           "DB9M_RA", "HD15F_RA", "HDR2X20_RA", "IDC10", "IDC20", "IDC40", "IDC50", "SW_DIP2", "SW_PUSH",
           "SW_SPDT_RA", "D_SCHOTTKY", "PTC", "HDR1X6", "ATX24", "MH_M3", "LUG_M4", "TERM2", "BAT85",
           "XTAL", "C22P", "JUMPER3", "TESTPOINT"}

CARDS = [("cpu", "CPU"), ("mmu", "MMU / data"), ("mem", "memory"), ("io", "I/O"),
         ("disk", "disk"), ("video", "video"), ("debug", "debug")]

def current(card, dev):
    """(typ, max, source, note) of one part, or None for a passive one."""
    k = "GAL" if dev.startswith("GAL_") else dev
    if k == "PICO":
        return PICO[card]
    if k in I:
        return I[k]
    for prefix, t, m, src, note in FAMILY:
        if k.startswith(prefix):
            return (t, m, src, note)
    if k in PASSIVE:
        return None
    raise KeyError("no supply current for %s (%s card)" % (k, card))

def card_budget(card):
    """{device: [count, typ total, max total, source, note]} and the card's totals.
    The memory card: one bank (four chips) of one card is active at a time
    (typical: a third of the cycles; maximum: always), the rest deselected;
    the boot ROM read only while booting."""
    mod = importlib.import_module("card_" + card)
    c = mod.build()[0]
    rows = collections.OrderedDict()
    for ref, (dev, fn, val) in sorted(c.parts.items()):
        x = current(card, dev)
        if x is None:
            continue
        k = "GAL" if dev.startswith("GAL_") else dev
        r = rows.setdefault(k, [0, 0.0, 0.0, x[2], x[3]])
        r[0] += 1
    for k, r in rows.items():
        t, m = current(card, k if k != "GAL" else "GAL_X")[:2]
        n = r[0]
        if k == "AS6C4008":                     # one bank of four active
            r[1] = 4 * t / 3 + (n - 4) * 0.004
            r[2] = 4 * m + (n - 4) * 0.05
            r[4] = "one bank of 4 active (typ: a third of the cycles); the others deselected (4 / 50 uA)"
        elif k == "SST39SF020A":                # the boot ROM: read while booting
            r[1] = n * 0.03
            r[2] = n * m
            r[4] = "deselected after booting (30 uA); max: all four read"
        else:
            r[1], r[2] = n * t, n * m
    typ = sum(r[1] for r in rows.values())
    mx = sum(r[2] for r in rows.values())
    return rows, typ, mx

def backplane():
    """The backplane: its LED and the supply supervisor; its pull-ups, with
    every pulled line low (max)."""
    import card_backplane as B
    c = B.build()
    worst = 0.0
    for ref, (dev, fn, val) in c.parts.items():
        if dev in ("R", "RNET9"):
            ohms = float(val.split()[0].replace("k", "e3"))
            n = sum(1 for ps in c.nets.values() for r, p in ps if r == ref and p.startswith("R")) if dev == "RNET9" else 1
            if "LED" not in fn and "termination" not in fn:
                worst += n * 5.0 / ohms * 1e3
    return 3.0 + 1.8, worst + 5.0 + 3.0

def system(budgets, bp, nmem):
    """Typical and maximum for a machine: every card once, nmem memory cards."""
    t = sum(b[1] for k, b in budgets.items()) + (nmem - 1) * budgets["mem"][1] + bp[0]
    m = sum(b[2] for k, b in budgets.items()) + (nmem - 1) * (budgets["mem"][2] - 4 * 60 + 4 * 0.05) + bp[1]
    return t, m            # (only one memory card's bank is active at a time)

# drives powered from the supply directly (D78), not through the backplane
DRIVES = [("2 CompactFlash cards (in CF-to-IDE adapters)", 2 * 100, 2 * 200),
          ("2 IDE hard drives, 3.5\" (their +5 V; +12 V for the motor)", 2 * 500, 2 * 800)]

def amps(ma):
    return "%.2f A" % (ma / 1e3) if ma >= 100 else "%.0f mA" % ma

def doc(path, budgets, bp):
    gals = sum(budgets[k][0]["GAL"][1] for k, _ in CARDS if "GAL" in budgets[k][0])
    L = ["# The power budget", "",
         "*Generated by `generators/p32power.py` (D69) from every card's netlist — edit the generator, not this file.*", "",
         "Every card's **+5 V** current: **typical** (the datasheets' typical figures, or",
         "our estimate at the bus clock) and **maximum** (each part at its datasheet",
         "maximum and fastest rated frequency, all at once: an upper bound that is",
         "never reached together). Parts behind a card's 3.3 V regulator count at",
         "5 V (a linear regulator passes its current through). Rows marked",
         "*estimate* have no datasheet figure yet; the Tang Nano 20K and the Picos",
         "are to measure.", "",
         "**CMOS logic** (74HCT, HC, ACT, LVC) draws microamps standing still; its",
         "current is switching: C_pd × V × f for each gate and its loads, and",
         "HCT's extra current for an input between the rails (a GAL's or TTL's",
         "high). Estimated as 1 mA a chip at the bus clock (4 mA at most), 4 mA a",
         "chip for ACT on the dot or capture clock (10 mA at most).", "",
         "## The cards", "",
         "| Card | Typical | Maximum | The largest share |", "|---|---|---|---|"]
    for k, name in CARDS:
        rows, t, m = budgets[k]
        big = max(rows.items(), key=lambda kv: kv[1][2])
        L.append("| %s | %s | %s | %d × %s: %s at most |" % (name, amps(t), amps(m), big[1][0], big[0], amps(big[1][2])))
    L.append("| backplane | %s | %s | the +5V LED and the supervisor; the pull-ups with every pulled line low |" % (amps(bp[0]), amps(bp[1])))
    L += ["", "## The machine", "",
          "| Machine | Typical | Maximum |", "|---|---|---|"]
    for n in (1, 4):
        t, m = system({k: budgets[k] for k, _ in CARDS}, bp, n)
        L.append("| every card, %d memory card%s | %.1f A | %.1f A |" % (n, "s" if n > 1 else "", t / 1e3, m / 1e3))
    tw, mw = system({k: budgets[k] for k, _ in CARDS}, bp, 4)
    dt, dm = sum(d[1] for d in DRIVES), sum(d[2] for d in DRIVES)
    L += ["", "Wired from the supply directly, not through the backplane, but from the same +5 V rail (D78):", "",
          "| Drives | Typical | Maximum |", "|---|---|---|"]
    for name, t, m in DRIVES:
        L.append("| %s | %.1f A | %.1f A |" % (name, t / 1e3, m / 1e3))
    L += ["", "**The supply** (D78): a dedicated +5 V supply on the backplane's M4",
          "bolts. The full machine (four memory cards) is **%.0f A typical, %.0f A at"
          % (tw / 1e3, mw / 1e3),
          "most** on +5 V; with its drives on the same rail, %.0f A typical, %.0f A"
          % ((tw + dt) / 1e3, (mw + dm) / 1e3),
          "at most. So **30 A at least, 40 A recommended**: the worst case adds every",
          "part's datasheet maximum, which never happen together, but a cheap",
          "supply sags near its rating. The drives' +12 V (the hard drives' motors)",
          "comes from the supply's own 12 V rail or a second, 12 V supply; it is",
          "not on the backplane.", "",
          "**The connectors carry it:** each card has 32 VCC and 60 GND pins (DIN",
          "41612 contacts, rated 1–2 A each), against the largest card's %.1f A at"
          % (max(budgets[k][2] for k, _ in CARDS) / 1e3),
          "most. The backplane takes its %.0f A at most on two pairs of M4 bolts"
          % (mw / 1e3),
          "(+5V and GND, one pair at each end, 10 AWG wire), about half each.", "",
          "**The 3.3 V regulators** (LD1117V33, 800 mA, TO-220): the CPU card's",
          "feeds the ALU's 74LVC latches and buffers (tens of mA); the I/O card's",
          "the WIZ850io (132 mA) and its 3.3 V buffers, about 150 mA: 0.25 W in the",
          "regulator, no heatsink needed. The Picos and the Tang Nano make their",
          "own 3.3 V from 5 V.", "",
          "**Where the current goes:** the GALs (ATF22V10C, about 85 mA each,",
          "whether they switch or not) are %.1f A of the %.1f A typical. Microchip's"
          % (gals / 1e3, tw / 1e3),
          "quarter-power and zero-power 22V10s (CQ, CQZ) draw a fraction of that",
          "but are slower (15–20 ns); which GALs could take them is a question for",
          "the timing pass against the datasheets (backlog).", ""]
    for k, name in CARDS:
        rows, t, m = budgets[k]
        L += ["## The %s card" % name, "", "| Part | Count | Typical | Maximum | Source |", "|---|---|---|---|---|"]
        for dev, (n, tt, mm, src, note) in sorted(rows.items(), key=lambda kv: -kv[1][2]):
            L.append("| %s | %d | %s | %s | %s%s |" % (dev, n, amps(tt), amps(mm), src, ": " + note if note else ""))
        L += ["| **total** | | **%s** | **%s** | |" % (amps(t), amps(m)), ""]
    with open(path, "w") as fh:
        fh.write("\n".join(L))

def main():
    budgets = {k: card_budget(k) for k, _ in CARDS}
    bp = backplane()
    for k, name in CARDS:
        print("%-12s typ %5.2f A  max %5.2f A" % (name, budgets[k][1] / 1e3, budgets[k][2] / 1e3))
    for n in (1, 4):
        t, m = system(budgets, bp, n)
        print("machine, %d memory card(s): typ %.1f A, max %.1f A" % (n, t / 1e3, m / 1e3))
    if "--doc" in sys.argv:
        doc(sys.argv[sys.argv.index("--doc") + 1], budgets, bp)

if __name__ == "__main__":
    main()
