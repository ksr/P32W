#!/usr/bin/env python3
"""The P32W memory card (slots 3-10): docs/mem-card.md, D50, D52, D60.

16 MiB of static RAM in eight banks of 2 MiB (four AS6C4008, one per byte
lane), fitted from bank 0 up (D60), and the 1 MiB boot ROM (four
SST39SF020A) on the card jumpered for it. Jumpers: the card's window
(window n = n x 16 MiB, D52), the number of banks fitted, the ROM. The card
answers (-ACK) only in its fitted banks (and the ROM, for reads): anything
else in its window is a bus error, as a missing card is. 55 ns parts: no
wait state at 3 MHz.

    python3 card_mem.py [--bom]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32net import Card, CARDS, check, gal_device, bom, DEV   # noqa: E402
from card_cpu import GB, num                                  # noqa: E402

AH = ["A%d" % i for i in range(24, 32)]
WIN = ["WIN0", "WIN1"]
BANKS = ["BANKS0", "BANKS1", "BANKS2"]
AB = ["A20", "A21", "A22", "A23"]

def gal_msel():
    """This card's window: A31:A26 zero, A25:A24 = the window jumpers."""
    deps = AH + WIN
    b = GB("MSEL", "memory card: window select").inp(*deps).inp("MRDN", "MWRN")
    mine = lambda v: num(v, AH) == num(v, WIN)
    b.com("CARDRD", deps + ["MRDN"], lambda v: mine(v) and not v["MRDN"])
    b.com("CARDWR", deps + ["MWRN"], lambda v: mine(v) and not v["MWRN"])
    return b.build()

def gal_mbank():
    """The ROM or a fitted bank; -ACK (a write to the ROM gets none: a bus
    error, as p32uemu); the data transceivers' enable and direction. FIT
    (the bank A23:A21 is fitted) and ROMR (the ROM's first MiB) are helpers."""
    b = GB("MBANK", "memory card: bank, ROM, -ACK").inp("CARDRD", "CARDWR", *AB).inp(*BANKS).inp("ROMEN")
    b.com("ROMR", AB + ["ROMEN"], lambda v: v["ROMEN"] and num(v, AB) == 0)
    b.com("FIT", AB[1:] + BANKS, lambda v: (num(v, AB) >> 1) <= num(v, BANKS))
    H = ["CARDRD", "CARDWR", "ROMR", "FIT"]
    ram = lambda v: v["FIT"] and not v["ROMR"]
    ack = lambda v: (v["CARDRD"] and (v["ROMR"] or ram(v))) or (v["CARDWR"] and ram(v))
    b.com("ROMCSN", H, lambda v: not (v["CARDRD"] and v["ROMR"]))
    b.com("RAMENN", H, lambda v: not ((v["CARDRD"] or v["CARDWR"]) and ram(v)))
    b.com("RAMWR", H, lambda v: v["CARDWR"] and ram(v))
    b.com("ACK", H, ack)
    b.com("XOEN", H, lambda v: not ack(v))
    b.com("DIRW", ["CARDWR"], lambda v: v["CARDWR"])
    return b.build()

def gal_mwe():
    """Each lane's -WE in the bus clock's second half (CLK low: the address
    and data settled since the first), only for the enabled lanes; -OE for
    a read."""
    b = GB("MWE", "memory card: write strobes, output enable").inp("RAMWR", "BE0", "BE1", "BE2", "BE3", "CLK", "CARDRD")
    for i in range(4):
        b.com("WE%dN" % i, ["RAMWR", "BE%d" % i, "CLK"], lambda v, i=i: not (v["RAMWR"] and v["BE%d" % i] and not v["CLK"]))
    b.com("RDOEN", ["CARDRD"], lambda v: not v["CARDRD"])
    return b.build()

def gals():
    return {"MSEL": gal_msel(), "MBANK": gal_mbank(), "MWE": gal_mwe()}

def build():
    G = gals()
    c = Card("mem-card", "P32W MEMORY CARD")
    role = {}
    n = [0]
    def U(dev, function, r=None):
        n[0] += 1
        ref = "U%d" % n[0]
        c.part(ref, dev, function)
        if r:
            role[r] = ref
        return ref
    def w(ref, pin, net):
        c.net(net, (ref, pin))
    def gal(name, function, netmap):
        g = G[name]
        ref = U(gal_device(g), function, name)
        for p, (num_, t) in DEV[c.parts[ref][0]]["pins"].items():
            if t in ("pwr", "gnd"):
                continue
            if p.startswith("NC") and t == "pas":
                c.unused(ref, p)
                continue
            w(ref, p, netmap.get(p, p))
        return ref
    gal("MSEL", "window select", {"MRDN": "-MRD", "MWRN": "-MWR"})
    gal("MBANK", "bank, ROM, -ACK", {})
    gal("MWE", "write strobes, output enable", {})
    for k in range(3):
        x = U("74HCT541", "address buffer, A%d-A%d" % (2 + 8 * k, min(9 + 8 * k, 20)))
        w(x, "-OE1", "GND"); w(x, "-OE2", "GND")
        for j in range(8):
            a = 8 * k + j
            if a < 19:
                w(x, "A%d" % j, "A%d" % (a + 2)); w(x, "Y%d" % j, "MA%d" % a)
            else:
                w(x, "A%d" % j, "GND"); c.unused(x, "Y%d" % j)
    for k in range(4):
        x = U("74HCT245", "data transceiver, D%d-D%d" % (8 * k, 8 * k + 7))
        w(x, "DIR", "DIRW"); w(x, "-OE", "XOEN")
        for j in range(8):
            w(x, "A%d" % (j + 1), "D%d" % (8 * k + j)); w(x, "B%d" % (j + 1), "MD%d" % (8 * k + j))
    bd = U("74HCT138", "bank select (A23:A21)")
    w(bd, "A", "A21"); w(bd, "B", "A22"); w(bd, "C", "A23"); w(bd, "G1", "VCC"); w(bd, "-G2A", "RAMENN"); w(bd, "-G2B", "GND")
    for b in range(8):
        w(bd, "Y%d" % b, "BANK%d_N" % b)
    for b in range(8):
        for lane in range(4):
            s = U("AS6C4008", "RAM bank %d, lane %d (D%d-D%d)" % (b, lane, 8 * lane, 8 * lane + 7), "ram%d_%d" % (b, lane))
            for a in range(19):
                w(s, "A%d" % a, "MA%d" % a)
            for j in range(8):
                w(s, "DQ%d" % j, "MD%d" % (8 * lane + j))
            w(s, "-CE", "BANK%d_N" % b); w(s, "-OE", "RDOEN"); w(s, "-WE", "WE%dN" % lane)
    for lane in range(4):
        s = U("SST39SF020A", "boot ROM, lane %d (D%d-D%d)" % (lane, 8 * lane, 8 * lane + 7), "rom%d" % lane)
        for a in range(18):
            w(s, "A%d" % a, "MA%d" % a)
        for j in range(8):
            w(s, "DQ%d" % j, "MD%d" % (8 * lane + j))
        w(s, "-CE", "ROMCSN"); w(s, "-OE", "RDOEN"); w(s, "-WE", "VCC"); c.unused(s, "NC1")
    oc = U("74HC05", "-ACK (open drain)")
    w(oc, "1A", "ACK"); w(oc, "1Y", "-ACK")
    for g in range(2, 7):
        w(oc, "%dA" % g, "GND"); c.unused(oc, "%dY" % g)
    # the jumpers (at the card's edge): fitted = 0, open = 1 (pulled up)
    jumpers = [("WIN0", "window bit 0"), ("WIN1", "window bit 1"), ("BANKS0", "banks fitted - 1, bit 0"),
               ("BANKS1", "banks fitted - 1, bit 1"), ("BANKS2", "banks fitted - 1, bit 2"), ("ROMEN", "the boot ROM: open = fitted")]
    c.part("RN1", "RNET9", "jumper pull-ups", value="10k x 8 bussed")
    w("RN1", "COM", "VCC")
    for i, (net, fn) in enumerate(jumpers, 1):
        c.part("JP%d" % i, "JUMPER2", "jumper %s: %s" % (net, fn))
        w("JP%d" % i, "1", net); w("JP%d" % i, "2", "GND")
        w("RN1", "R%d" % i, net)
    c.unused("RN1", "R7", "R8")
    c.finish()
    CARDS[c.name] = c
    return c, G, role

def main():
    c, G, role = build()
    errs = check(c)
    ics = [r for r in c.parts if r.startswith("U")]
    if errs:
        print("\n".join(errs[:60]))
        sys.exit("memory card: %d errors" % len(errs))
    print("memory card OK: %d ICs (%d GALs), %d nets, %d parts" % (len(ics), len(G), len(c.nets), len(c.parts)))
    if "--bom" in sys.argv:
        for (v, pkg), refs in bom(c):
            print("%4d  %-32s %-16s %s" % (len(refs), v, pkg, " ".join(refs[:8]) + (" ..." if len(refs) > 8 else "")))

if __name__ == "__main__":
    main()
