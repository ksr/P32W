#!/usr/bin/env python3
"""The P32W backplane (D49, D50, D64, D68): docs/backplane.md.

Eleven positions, 1 inch (5 HP) apart: the debug slot 0, then slots 1-10.
Each has three female DIN 41612 C 3x32 sockets, S<n>J1-S<n>J3, mating the
cards' J1-J3. The nets ARE p32bus.busnet(conn, pin, slot), so the backplane
cannot disagree with the bus definition or with the cards:
  * J1 and J2 bused to every slot (their spare pins too, D68);
  * the radial -BREQn / -BGNTn: slot n's -BREQ / -BGNT pin to slot 2's
    (the arbiter, on the MMU / data card) and to slot 0's (the debug card);
  * J3 (the CPU <-> MMU / data local link) in slots 0, 1 and 2 only; in
    slots 3-10 only its power pins.
Plus the parts no card can hold:
  * pull-ups on every bus line that no card drives with a totem-pole output
    (the open-drain lines, and the three-state ones a master lets go), the
    rule checked against every card's netlist;
  * AC termination (100 R + 150 pF) at the far end of CLK and -CLK;
  * the power entry (D78): +5 V and GND on M4 bolts at each end, and a
    TL7705A supervisor making PWROK;
  * a +5 V LED, bulk and per-socket bypass capacitors.

    python3 card_backplane.py [--bom]
"""
import os, sys, importlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32sheets import sheets  # noqa: E402
from p32net import Card, CARDS, check, bom, DEV                      # noqa: E402
from p32bus import (busnet, pinnet, ALLPINS, SIGNALS, CONNECTORS, ALL_SLOTS,   # noqa: E402
                    DEBUG_SLOT, CPU_SLOT, MMU_SLOT, DEV_SLOTS, NSLOTS)

FAR_SLOT = NSLOTS                      # slot 10: the end farthest from the CPU card (slot 1)
# where each card may sit (its window jumpers place it, not its slot: D50, D52)
CARD_SLOTS = {"card_cpu": [CPU_SLOT], "card_mmu": [MMU_SLOT], "card_debug": [DEBUG_SLOT],
              "card_mem": list(DEV_SLOTS), "card_io": list(DEV_SLOTS),
              "card_disk": list(DEV_SLOTS), "card_video": list(DEV_SLOTS)}

# The pull-ups. Open-drain lines: strong enough to rise within half a bus
# clock through about 200 pF (11 slots, their cards' inputs, the tracks):
# 0.5 RC to reach HCT's 2.0 V; a 74HC05 sinks the current at <= 0.45 V.
# -FAST is the CPU card's clock jumper, a static level: 10k (D74).
PULL_R = [("-ACK", "1k"), ("-WAIT", "680"), ("-LOCK", "1k"), ("PWROK", "10k"), ("-FAST", "10k")]
IRQS = ["-IRQ%d" % i for i in range(8)]
# three-state lines (no card drives them between masters, or with the MMU /
# data card absent): 10k only holds them, so no input floats
HOLD = ["D%d" % i for i in range(32)] + ["A%d" % i for i in range(2, 32)] + \
       ["BE%d" % i for i in range(4)] + ["-MRD", "-MWR", "-IACK"]

def pull_networks():
    """The bussed networks, each on eight consecutive pins of one connector
    column (rows r..r+7), so that on the board each network stands beside
    the socket rows with every pin on its own net's row (p32backplane.py);
    a row with no such net leaves that resistor unused. (name, value, nets
    by resistor R1..R8, None where unused)"""
    pin = {}
    for conn, p in ALLPINS:
        n = busnet(conn, p, DEV_SLOTS[0])
        if n:
            pin[n] = (conn, p[0], int(p[1:]))
    out = []
    for value, names in (("2.2k x 8 bussed", IRQS), ("10k x 8 bussed", HOLD)):
        groups = {}
        for n in names:
            conn, col, row = pin[n]
            groups.setdefault((conn, col), {})[row] = n
        for (conn, col), rows in sorted(groups.items()):
            todo = sorted(rows)
            while todo:
                r0 = todo[0]
                win = [rows.get(r0 + i) for i in range(8)]
                todo = [r for r in todo if r >= r0 + 8]
                used = [n for n in win if n]
                out.append(("%s-%s (%s %s%d-%d)" % (used[0], used[-1], conn, col, r0, r0 + 7), value, win))
    return out

PULL_RN = pull_networks()
TERMINATED = ["CLK", "-CLK"]

def sock(slot, conn):
    return "S%d%s" % (slot, conn)

def build():
    c = Card("backplane", "P32W BACKPLANE")
    for j in CONNECTORS:                    # (a card's own edge connectors: the backplane has sockets)
        del c.parts[j]
    # the sockets: every pin on its busnet()
    for slot in ALL_SLOTS:
        for conn in CONNECTORS:
            ref = sock(slot, conn)
            c.part(ref, "DIN96F", "slot %d, the card's %s" % (slot, conn))
            for cn, pin in ALLPINS:
                if cn != conn:
                    continue
                n = busnet(conn, pin, slot)
                if n is None:
                    c.unused(ref, pin)
                else:
                    c.net(n, (ref, pin))
    # power entry (D78): a dedicated 5 V supply (30-40 A) on two pairs of M4
    # bolts and ring lugs, one pair at each end of the board, both from the
    # same supply, so each carries about half the current into the planes
    for ref, n, fn in (("P1", "VCC", "+5V in, the slot 0 end"), ("P2", "GND", "GND in, the slot 0 end"),
                       ("P3", "VCC", "+5V in, the slot %d end" % FAR_SLOT), ("P4", "GND", "GND in, the slot %d end" % FAR_SLOT)):
        c.part(ref, "LUG_M4", fn)
        c.net(n, (ref, "1"))
    # PWROK from a supply supervisor (no ATX PWR_OK): low until +5 V is above
    # 4.55 V and then for 1.3e4 x CT (10 uF: about 130 ms), low again on a dip
    c.part("U1", "TL7705A", "PWROK: the +5 V supervisor")
    for pin, n in (("VCC", "VCC"), ("SENSE", "VCC"), ("-RESIN", "VCC"), ("GND", "GND"),
                   ("-RESET", "PWROK"), ("REF", "PWR_REF"), ("CT", "PWR_CT")):
        c.net(n, ("U1", pin))
    c.unused("U1", "RESET")
    c.part("D1", "LED", "+5V: the supply is on")
    c.part("R1", "R", "+5V LED's resistor", value="1k")
    c.net("VCC", ("R1", "1")); c.net("LED5V", ("R1", "2"), ("D1", "A")); c.net("GND", ("D1", "K"))
    # pull-ups
    r = 2
    for n, val in PULL_R:
        ref = "R%d" % r; r += 1
        c.part(ref, "R", "%s pull-up" % n, value=val)
        c.net("VCC", (ref, "1")); c.net(n, (ref, "2"))
    for k, (what, val, names) in enumerate(PULL_RN, 1):
        ref = "RN%d" % k
        c.part(ref, "RNET9", "%s pull-ups" % what, value=val)
        c.net("VCC", (ref, "COM"))
        for i, n in enumerate(names, 1):
            if n:
                c.net(n, (ref, "R%d" % i))
            else:
                c.unused(ref, "R%d" % i)
    # AC termination at the far end (slot 10): no current in the steady state
    for k, n in enumerate(TERMINATED):
        rr, cc = "R%d" % r, "C%d" % (k + 1); r += 1
        c.part(rr, "R", "%s termination at slot %d" % (n, FAR_SLOT), value="100")
        c.part(cc, "C150P", "%s termination at slot %d" % (n, FAR_SLOT))
        c.net(n, (rr, "1")); c.net("T_" + n, (rr, "2"), (cc, "1")); c.net("GND", (cc, "2"))
    # bypass: one 100 nF beside each socket, a 10 uF per slot, a 1000 uF at each power entry
    k = len(TERMINATED) + 1
    for slot in ALL_SLOTS:
        for conn in CONNECTORS:
            ref = "C%d" % k; k += 1
            c.part(ref, "C100N", "bypass, slot %d %s" % (slot, conn))
            c.net("VCC", (ref, "1")); c.net("GND", (ref, "2"))
        ref = "C%d" % k; k += 1
        c.part(ref, "C10U", "bulk, slot %d" % slot)
        c.net("VCC", (ref, "+")); c.net("GND", (ref, "-"))
    for end in (0, FAR_SLOT):
        ref = "C%d" % k; k += 1
        c.part(ref, "C1000U", "bulk, at the slot %d end's power entry" % end)
        c.net("VCC", (ref, "+")); c.net("GND", (ref, "-"))
    # the supervisor's: REF's 100 nF (TI), CT, its own bypass
    for fn, dev, a, b in (("the supervisor's REF", "C100N", "PWR_REF", "GND"),
                          ("the supervisor's delay (CT, about 130 ms)", "C10U", "PWR_CT", "GND"),
                          ("decoupling U1", "C100N", "VCC", "GND")):
        ref = "C%d" % k; k += 1
        c.part(ref, dev, fn)
        pa, pb = ("+", "-") if dev == "C10U" else ("1", "2")
        c.net(a, (ref, pa)); c.net(b, (ref, pb))
    # mounting holes for the subrack, plated and on GND (p32backplane.py
    # puts them at the corners and along the top and bottom edges)
    for i in range(1, 9):
        c.part("H%d" % i, "MH_M3", "mounting hole (GND: the subrack)")
        c.net("GND", ("H%d" % i, "1"))
    # the schematic's sheets (generators/p32sheets.py)
    sockets = lambda a, b: [["S%dJ%d" % (s, j) for j in (1, 2, 3)] for s in range(a, b + 1)]
    c.sheets = sheets(c, [
        ("Power entry and the supervisor", [["P1", "P2", "P3", "P4", "C47", "C48"],
                                           ["U1", "C49", "C50", "R5", "D1", "R1"], ["H%d" % i for i in range(1, 9)]]),
        ("Bus pull-ups and the clock's termination", [["R2", "R3", "R4", "R6"], ["RN%d" % i for i in range(1, 14)],
                                                     ["R7", "C1", "R8", "C2"]]),
        ("Slots 0-1 (the debug card, the CPU)", sockets(0, 1)),
        ("Slots 2-3", sockets(2, 3)),
        ("Slots 4-5", sockets(4, 5)),
        ("Slots 6-7", sockets(6, 7)),
        ("Slots 8-9", sockets(8, 9)),
        ("Slot 10", sockets(10, 10)),
    ])
    CARDS[c.name] = c
    return c

# ---------------------------------------------------------------- checks against the bus and the cards
def system_check(c):
    errs = []
    nets = c.nets
    where = {rp: n for n, ps in nets.items() for rp in ps}
    # every slot's every pin is its busnet()
    for slot in ALL_SLOTS:
        for conn, pin in ALLPINS:
            want, got = busnet(conn, pin, slot), where.get((sock(slot, conn), pin))
            if got != want:
                errs.append("slot %d %s %s: %s, the bus says %s" % (slot, conn, pin, got, want))
    # every bus signal and spare connects at least two sockets
    for n, ps in nets.items():
        socks = {r for r, p in ps if r.startswith("S")}
        if n not in ("VCC", "GND") and not n.startswith(("T_", "PWR_", "LED")) and n != "PWROK" and len(socks) < 2:
            errs.append("net %s reaches %d socket(s)" % (n, len(socks)))
    # each card in each slot it may sit in: every connector pin it uses is wired there
    drivers = {}
    for m, slots in CARD_SLOTS.items():
        card = importlib.import_module(m).build()[0]
        used = {(r, p): n for n, ps in card.nets.items() for r, p in ps if r in CONNECTORS}
        for slot in slots:
            for (conn, pin), n in used.items():
                if n in ("VCC", "GND"):
                    continue
                if busnet(conn, pin, slot) is None:
                    errs.append("%s in slot %d: %s %s (%s) is not wired on the backplane" % (m, slot, conn, pin, n))
        for n, ps in card.nets.items():
            for r, p in ps:
                if r in CONNECTORS:
                    continue
                drivers.setdefault(n, set()).add(DEV[card.parts[r][0]]["pins"][p][1])
    # the pull-up rule: a J1 / J2 bus line no card drives totem-pole ("out")
    # has a resistor to VCC here, and only such lines do
    pulled = {n for n, ps in nets.items() if n != "VCC"
              for r, p in ps if c.parts[r][0] in ("R", "RNET9") and
              any(where.get((r, q)) == "VCC" for q in DEV[c.parts[r][0]]["pins"] if q != p)}
    for n, (scope, drv, rcv, desc) in SIGNALS.items():
        if scope != "bus":
            continue
        t = drivers.get(n, set())
        need = "out" not in t
        if need and n not in pulled:
            errs.append("bus line %s: no totem-pole driver on any card (%s) and no pull-up" % (n, sorted(t)))
        if not need and n in pulled:
            errs.append("bus line %s: pulled up, but a card drives it totem-pole" % n)
    for n in TERMINATED:
        if "out" not in drivers.get(n, set()):
            errs.append("%s terminated, but no card drives it totem-pole" % n)
    return errs

def main():
    c = build()
    errs = check(c) + system_check(c)
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("backplane: %d errors" % len(errs))
    socks = [r for r in c.parts if r.startswith("S")]
    sig = [n for n in c.nets if n in SIGNALS or n.startswith(("L_", "SPARE_")) or n[:5] in ("-BREQ", "-BGNT")]
    print("backplane OK: %d slots (0-%d), %d sockets, %d bus nets, %d parts; every card fits its slots"
          % (len(ALL_SLOTS), NSLOTS, len(socks), len(sig), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
