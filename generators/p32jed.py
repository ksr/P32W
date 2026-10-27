#!/usr/bin/env python3
"""JEDEC fuse files for the GALs (ATF22V10C, programmed as GAL22V10 maps:
minipro -p ATF22V10C -w NAME.jed on a TL866II+ or T48).

Every card's GALs are p32gal.Gal objects (pins, product terms, output
kinds, polarity). This module turns one into:

  jedec(g)   the 5892-fuse map and its JEDEC file, as GALasm 2.1 writes
             them (its fuse layout: GALASM-SPEC.md, sections 7 and 9);
  pld(g)     the same GAL as GALasm source, to cross-check with galasm;
  check(g, fuses)  the fuse map read back through a model of the 22V10's
             hardware (the column a pin drives, registered feedback from
             /Q, S0 the output's polarity, S1 the register bypassed, the
             OE row, AR) and every output compared, exhaustively, with the
             GAL's behavioural spec: the same check the equations passed.

    python3 p32jed.py OUTDIR [--galasm PATH]
        every card's GALs -> OUTDIR/<card>/<GAL>.jed, each checked; with
        --galasm, also assembled by galasm and the fuse maps compared.
"""
import os, sys, itertools, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32gal import eval_sop                                  # noqa: E402

NFUSE, NCOL = 5892, 44
COL = {1: 0, 2: 4, 3: 8, 4: 12, 5: 16, 6: 20, 7: 24, 8: 28, 9: 32, 10: 36, 11: 40, 13: 42,
       14: 38, 15: 34, 16: 30, 17: 26, 18: 22, 19: 18, 20: 14, 21: 10, 22: 6, 23: 2}
OLMC_ROWS = {23: (1, 9), 22: (10, 20), 21: (21, 33), 20: (34, 48), 19: (49, 65),
             18: (66, 82), 17: (83, 97), 16: (98, 110), 15: (111, 121), 14: (122, 130)}
OLMC_PINS = [23, 22, 21, 20, 19, 18, 17, 16, 15, 14]

def signature(g):
    return g.name[:8].upper()

def _pinof(g):
    return dict(g.pins())

def jedec(g):
    """The fuse map (a list of 0/1) and the JEDEC text."""
    pin = _pinof(g)
    outs = {o.pin: o for o in g.outs.values()}
    f = [1] * (132 * NCOL) + [0] * 20 + [0] * 64
    def row(r, terms_row):
        for c in range(NCOL):
            f[r * NCOL + c] = 1
        for n, val in terms_row:
            p = pin[n]
            k = COL[p]
            inv = not val
            o = outs.get(p)
            if o is not None and o.kind == "reg" and not o.inverted:
                inv = not inv                      # registered, active high: the array sees /Q
            f[r * NCOL + k + (1 if inv else 0)] = 0
    def zero(r):
        for c in range(NCOL):
            f[r * NCOL + c] = 0
    # AR, SP
    if g.reset:
        row(0, [(g.reset, 0)])
    else:
        zero(0)
    zero(131)
    for i, p in enumerate(OLMC_PINS):
        first, last = OLMC_ROWS[p]
        o = outs.get(p)
        s0 = s1 = 0
        if o is None:
            for r in range(first, last + 1):
                zero(r)
            s1 = 1 if p in pin.values() else 0      # (an input on an OLMC pin)
        else:
            if o.kind == "tri":
                assert len(o.oe) == 1, (g.name, o.name)
                row(first, o.oe[0])
            # (else the OE row stays true: always driving)
            sums = o.eq
            assert len(sums) <= last - first, (g.name, o.name)
            for j in range(last - first):
                r = first + 1 + j
                if j < len(sums):
                    row(r, sums[j])
                else:
                    zero(r)
            s0 = 0 if o.inverted else 1
            s1 = 0 if o.kind == "reg" else 1
        f[132 * NCOL + 2 * i] = s0
        f[132 * NCOL + 2 * i + 1] = s1
    sig = signature(g).encode()[:8]
    for k, b in enumerate(sig):
        for bit in range(8):
            f[5828 + 8 * k + bit] = b >> (7 - bit) & 1
    return f, _text(f)

def _text(f):
    lines = ["\x02", "Used Program:   P32W p32jed.py", "GAL-Assembler:  P32W p32jed.py",
             "Device:         GAL22V10", "", "*F0", "*G0", "*QF%d" % NFUSE]
    for r in range(132):
        bits = f[r * NCOL:(r + 1) * NCOL]
        if any(bits):
            lines.append("*L%04d %s" % (r * NCOL, "".join(map(str, bits))))
    lines.append("*L%04d %s" % (5808, "".join(map(str, f[5808:5828]))))
    lines.append("*L%04d %s" % (5828, "".join(map(str, f[5828:5892]))))
    cs = 0
    for i in range(0, NFUSE, 8):
        cs += sum(f[i + b] << b for b in range(8) if i + b < NFUSE)
    lines.append("*C%04x" % (cs & 0xFFFF))
    lines.append("*")
    body = "\n".join(lines) + "\n\x03"
    return body + "%04x\n" % (sum(body.encode()) & 0xFFFF)

def read_jedec(text):
    """The fuse map of a JEDEC file (*F default, *L lines)."""
    f = None
    default = 0
    for field in text.replace("\x02", "").split("*"):
        field = field.strip()
        if field.startswith("QF"):
            f = [default] * int(field[2:].split()[0])
        elif field.startswith("F") and field[1:].strip().isdigit():
            default = int(field[1:].strip())
            if f is not None:
                f = [default] * len(f)
        elif field.startswith("L"):
            addr, bits = field[1:].split(None, 1)
            bits = "".join(ch for ch in bits if ch in "01")
            for i, b in enumerate(bits):
                f[int(addr) + i] = int(b)
    return f

# ---------------------------------------------------------------- the hardware's view
def check(g, f):
    """Read the fuses as the 22V10 does and compare every output (and its
    OE) with the GAL's spec, over every combination of what it reads."""
    errs = []
    pin = _pinof(g)
    bypin = {p: n for n, p in pin.items()}
    outs = {o.pin: o for o in g.outs.values()}
    arch = {p: (f[5808 + 2 * i], f[5808 + 2 * i + 1]) for i, p in enumerate(OLMC_PINS)}
    def literals(r):
        lits = []
        bits = f[r * NCOL:(r + 1) * NCOL]
        for p, k in COL.items():
            for side in (0, 1):
                if bits[k + side] == 0:
                    lits.append((p, side))
        return lits
    def colval(p, v):
        """What the array sees on pin p's column (the true side)."""
        n = bypin.get(p)
        x = v.get(n, 0)
        if p in arch and p in outs:
            s0, s1 = arch[p]
            if s1 == 0:                            # registered: feedback from /Q
                q = x if s0 else 1 - x             # (the pin is Q, or /Q when active low)
                return 1 - q
        return x
    def false_row(r):
        return any(f[r * NCOL + c] == 0 and f[r * NCOL + c + 1] == 0 for c in range(0, NCOL, 2))
    def term(r, v):
        return all((colval(p, v) ^ side) == 1 for p, side in literals(r))
    for p in OLMC_PINS:
        first, last = OLMC_ROWS[p]
        o = outs.get(p)
        if o is None:
            for r in range(first, last + 1):
                if not false_row(r):
                    errs.append("%s: pin %d unused, row %d not constantly false" % (g.name, p, r))
            continue
        s0, s1 = arch[p]
        if s1 != (0 if o.kind == "reg" else 1) or s0 != (0 if o.inverted else 1):
            errs.append("%s.%s: S0/S1 %d%d" % (g.name, o.name, s0, s1))
        deps = sorted(set(getattr(o.spec, "deps", ())) | {n for t in o.eq for n, _ in t})
        rows = range(first + 1, last + 1)
        for bits in itertools.product((0, 1), repeat=len(deps)):
            v = dict(zip(deps, bits))
            want = o.spec(v)
            if want is None:
                continue
            s = any(term(r, v) for r in rows)
            got = s if s0 else not s
            if bool(got) != bool(want):
                errs.append("%s.%s differs from its spec at %s" % (g.name, o.name, v))
                break
        oe_true = literals(first) == []
        if o.kind == "tri":
            odeps = sorted(getattr(o.oe_spec, "deps", ()))
            for bits in itertools.product((0, 1), repeat=len(odeps)):
                v = dict(zip(odeps, bits))
                if bool(term(first, v)) != bool(eval_sop(o.oe, v)):
                    errs.append("%s.%s: its OE row differs" % (g.name, o.name))
                    break
        elif not oe_true:
            errs.append("%s.%s: its OE row is not always true" % (g.name, o.name))
    if g.reset:
        if literals(0) != [(pin[g.reset], 1)]:
            errs.append("%s: AR row is not /%s" % (g.name, g.reset))
    elif not false_row(0):
        errs.append("%s: AR row not false" % g.name)
    if not false_row(131):
        errs.append("%s: SP row not false" % g.name)
    return errs

# ---------------------------------------------------------------- GALasm source (the cross-check)
def pld(g):
    """The GAL as GALasm source. Pins are declared plainly; an output whose
    pin is its equation's inverse is written /NAME = ... (active low)."""
    pin = _pinof(g)
    used = {}
    def ident(n):
        base = "".join(ch for ch in n.replace("-", "N") if ch.isalnum() or ch == "_")[:7] or "S"
        name, k = base, 0
        while name in used.values() and used.get(n) != name:
            k += 1
            name = (base[:6] + str(k))[:8]
        used.setdefault(n, name)
        return used[n]
    names = {p: ident(n) for n, p in sorted(pin.items(), key=lambda kv: kv[1])}
    decl = [names.get(p, "NC") for p in range(1, 25)]
    decl[11], decl[23] = "GND", "VCC"
    L = ["GAL22V10", signature(g), " ".join(decl[:12]), " ".join(decl[12:]), ""]
    def sop(terms):
        if not terms:
            return "GND"
        if terms == [[]]:
            return "VCC"
        return "\n    + ".join(" * ".join(("" if val else "/") + names[pin[n]] for n, val in t) for t in terms)
    for o in sorted(g.outs.values(), key=lambda o: o.pin):
        suffix = {"reg": ".R", "com": "", "tri": ".T"}[o.kind]
        L.append("%s%s%s = %s" % ("/" if o.inverted else "", names[o.pin], suffix, sop(o.eq)))
        if o.kind == "tri":
            L.append("%s.E = %s" % (names[o.pin], sop(o.oe)))
    if g.reset:
        L.append("AR = /%s" % names[pin[g.reset]])
    L += ["", "DESCRIPTION", "%s -- %s" % (g.name, g.function), ""]
    return "\n".join(L)

def galasm_fuses(g, exe):
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "g.pld")
        open(src, "w").write(pld(g))
        r = subprocess.run([exe, src], capture_output=True, text=True, cwd=d)
        jed = os.path.join(d, "g.jed")
        if r.returncode or not os.path.exists(jed):
            return None, (r.stdout + r.stderr).strip()
        return read_jedec(open(jed).read()), ""

# ---------------------------------------------------------------- every card
def all_gals():
    """(card, GAL) for every GAL on every card."""
    import importlib
    out = []
    for m in ("card_cpu", "card_mmu", "card_mem", "card_io", "card_disk", "card_video", "card_debug"):
        mod = importlib.import_module(m)
        G = mod.build()[1]
        card = m[len("card_"):]
        for name, g in sorted(G.items()):
            out.append((card, g))
    return out

def main():
    args = sys.argv[1:]
    exe = None
    if "--galasm" in args:
        i = args.index("--galasm")
        exe = args[i + 1]
        del args[i:i + 2]
    outdir = args[0] if args else os.path.join(os.path.dirname(HERE), "hardware", "gal")
    n = bad = same = 0
    for card, g in all_gals():
        f, text = jedec(g)
        errs = check(g, read_jedec(text))
        if exe:
            gf, msg = galasm_fuses(g, exe)
            if gf is None:
                errs.append("%s: galasm: %s" % (g.name, msg))
            elif gf != f:
                diff = [i for i in range(NFUSE) if gf[i] != f[i]]
                errs.append("%s: galasm's fuses differ at %d places (first %d)" % (g.name, len(diff), diff[0]))
            else:
                same += 1
        d = os.path.join(outdir, card)
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, g.name + ".jed"), "w", newline="\n").write(text)
        n += 1
        if errs:
            bad += 1
            print("\n".join(errs[:5]))
    print("%d GALs -> %s: %d checked against their specs through the fuse map%s%s"
          % (n, outdir, n - bad, ", %d identical to galasm's" % same if exe else "", "" if not bad else ", %d FAILED" % bad))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
