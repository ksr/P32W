#!/usr/bin/env python3
"""ATF22V10 GAL equations for the P32W cards (D17, D18).

Each GAL is defined in Python, next to the card that uses it:

    g = Gal("SEQ", "sequencer: microaddress state", clock="CLK", reset="-RES")
    g.inp("SEQF0", 2) ...                    # input pins (pin 1 = clock/input)
    g.reg("STEP0", 14, sop("...") , spec=lambda v: ...)   # registered output
    g.com("TRAPGO", 23, sop("..."), spec=...)             # combinational output
    g.tri("D2", 22, sop(...), oe=sop(...), spec=..., oe_spec=...)  # 3-state

Equations are sums of products ("A & !B | C") over the GAL's input pins and
its own outputs (feedback: registered outputs feed back their register,
combinational ones their pin). Every equation is checked EXHAUSTIVELY
against its behavioural spec (a Python function written from the
microcode-level emulator's semantics) over all values of the signals it
depends on, and against the ATF22V10's product-term limit for its pin.
`emit_pld()` writes the equations for a GAL assembler (galasm syntax);
p32jed.py writes the JEDEC fuse files the programmer takes (D67).

Polarity: an equation gives the logic level of the PIN (registered: the
level after the clock edge). A register resets (AR) to 0, so an output that
must reset HIGH is declared `reset_high=True`: the pin is then the inverse of
the register (the 22V10's output polarity fuse), and its equation is written
for the register, i.e. for the COMPLEMENT of the pin. The check knows this.
"""
import itertools, re

# product terms available per I/O pin of the 22V10 (pins 14-23)
TERMS = {14: 8, 15: 10, 16: 12, 17: 14, 18: 16, 19: 16, 20: 14, 21: 12, 22: 10, 23: 8}
INPUT_PINS = [1] + list(range(2, 12)) + [13]

def sop(text):
    """'A & !B | C' -> [[('A', True), ('B', False)], [('C', True)]]. '0' / '1' allowed."""
    text = text.strip()
    if text == "0":
        return []
    if text == "1":
        return [[]]
    terms = []
    for t in text.split("|"):
        lits = []
        for l in t.split("&"):
            l = l.strip()
            if not l:
                raise ValueError("empty literal in %r" % text)
            lits.append((l[1:], False) if l.startswith("!") else (l, True))
        terms.append(lits)
    return terms

def sop_str(terms):
    if not terms:
        return "0"
    return " | ".join(" & ".join(("" if v else "!") + n for n, v in t) or "1" for t in terms)

def eval_sop(terms, v):
    return any(all(bool(v[n]) == want for n, want in t) for t in terms)

class Out:
    def __init__(self, name, pin, kind, eq, spec, oe=None, oe_spec=None, inverted=False):
        self.name, self.pin, self.kind = name, pin, kind
        self.eq, self.spec, self.oe, self.oe_spec = eq, spec, oe, oe_spec
        self.inverted = inverted                      # pin = NOT(equation): polarity fuse

class Gal:
    def __init__(self, name, function, clock=None, reset=None):
        self.name, self.function = name, function
        self.clock, self.reset = clock, reset          # clock: net on pin 1; reset: input pin name (active low)
        self.inputs = {}                               # name -> pin
        self.outs = {}                                 # name -> Out
        if clock:
            self.inputs[clock] = 1

    def inp(self, name, pin):
        assert pin in INPUT_PINS + list(TERMS), (self.name, name, pin)
        self.inputs[name] = pin
        return self

    def reg(self, name, pin, eq, spec, reset_high=False):
        assert self.clock, "%s: registered output needs a clock" % self.name
        self.outs[name] = Out(name, pin, "reg", eq, spec, inverted=reset_high)
        return self

    def com(self, name, pin, eq, spec, inverted=False):
        """inverted=True: the equation is for the complement of the pin (the
        polarity fuse), e.g. an active-low enable written as its active-high sum."""
        self.outs[name] = Out(name, pin, "com", eq, spec, inverted=inverted)
        return self

    def tri(self, name, pin, eq, spec, oe, oe_spec):
        self.outs[name] = Out(name, pin, "tri", eq, spec, oe, oe_spec)
        return self

    # ---------------------------------------------------------------- checks
    def pins(self):
        p = dict(self.inputs)
        for o in self.outs.values():
            p[o.name] = o.pin
        return p

    def check(self):
        errs = []
        used = {}
        for n, pin in self.pins().items():
            if pin in used:
                errs.append("%s: pin %d used by %s and %s" % (self.name, pin, used[pin], n))
            used[pin] = n
            if pin in (12, 24) or not 1 <= pin <= 23:
                errs.append("%s: %s on pin %d (power pin or out of range)" % (self.name, n, pin))
        known = set(self.pins())
        for o in self.outs.values():
            if o.pin not in TERMS:
                errs.append("%s.%s: outputs must be on pins 14-23" % (self.name, o.name))
                continue
            for what, eq, spec in (("", o.eq, o.spec), (".oe", o.oe, o.oe_spec)):
                if eq is None:
                    continue
                if len(eq) > TERMS[o.pin] and what == "":
                    errs.append("%s.%s: %d product terms, pin %d has %d" % (self.name, o.name, len(eq), o.pin, TERMS[o.pin]))
                if what == ".oe" and len(eq) > 1:
                    errs.append("%s.%s.oe: the 22V10 output enable is one product term" % (self.name, o.name))
                deps = sorted({n for t in eq for n, _ in t} | set(getattr(spec, "deps", ())))
                for n in deps:
                    if n not in known:
                        errs.append("%s.%s%s: unknown signal %s" % (self.name, o.name, what, n))
                if any(n not in known for n in deps):
                    continue
                if len(deps) > 20:
                    errs.append("%s.%s%s: %d signals, too many to verify exhaustively" % (self.name, o.name, what, len(deps)))
                    continue
                for bits in itertools.product((0, 1), repeat=len(deps)):
                    v = dict(zip(deps, bits))
                    want = spec(v)
                    if want is None:
                        continue                       # a combination that cannot occur (don't care)
                    want = bool(want)
                    got = eval_sop(eq, v)
                    if what == "" and o.inverted:
                        got = not got                  # the equation is for the register; the pin is its inverse
                    if got != want:
                        errs.append("%s.%s%s differs from its spec at %s: equation %d, spec %d"
                                    % (self.name, o.name, what, {k: v[k] for k in deps}, got, want))
                        break
        return errs

    # ---------------------------------------------------------------- output
    def emit_pld(self):
        """The GAL as GALasm source: p32jed.pld(), which galasm assembles to
        the same fuses as p32jed.jedec() writes (D67). (This used to declare
        an inverted output's pin with a / as well as its equation, which in
        GALasm cancels: the wrong polarity. Never assembled until D67.)"""
        import p32jed
        return p32jed.pld(self)

def spec(deps, f):
    """Attach the list of signals a behavioural spec reads (for exhaustive checking)."""
    f.deps = tuple(deps)
    return f

def minimize(f, max_terms=None):
    """A sum of products for a spec (see spec()): Quine-McCluskey prime
    implicants over f.deps, using the spec's None results as don't-cares,
    then a greedy cover. Good for the small functions a GAL pin holds; the
    result is verified exhaustively by Gal.check() like any hand equation."""
    deps = list(f.deps)
    n = len(deps)
    on, dc = set(), set()
    for m in range(1 << n):
        v = {d: (m >> (n - 1 - i)) & 1 for i, d in enumerate(deps)}
        r = f(v)
        if r is None:
            dc.add(m)
        elif r:
            on.add(m)
    if not on:
        return []
    if len(on) + len(dc) == 1 << n:
        return [[]]
    # implicants as (value, mask): mask bit 1 = that variable is free
    current = {(m, 0) for m in on | dc}
    primes = set()
    while current:
        nxt, used = set(), set()
        groups = {}
        for val, mask in current:
            groups.setdefault((mask, bin(val).count("1")), []).append(val)
        for (mask, ones), vals in groups.items():
            for a in vals:
                for b in groups.get((mask, ones + 1), []):
                    d = a ^ b
                    if d & (d - 1) == 0:              # differ in exactly one bit
                        nxt.add((a & ~d, mask | d))
                        used.add((a, mask)); used.add((b, mask))
        primes |= current - used
        current = nxt
    def covers(imp, m):
        val, mask = imp
        return (m & ~mask) == (val & ~mask)
    primes = sorted(primes, key=lambda p: -bin(p[1]).count("1"))
    left, cover = set(on), []
    while left:                                       # essential first, then the largest gain
        best = max(primes, key=lambda p: (sum(covers(p, m) for m in left), bin(p[1]).count("1")))
        cover.append(best)
        left = {m for m in left if not covers(best, m)}
    terms = []
    for val, mask in cover:
        t = []
        for i, d in enumerate(deps):
            bit = 1 << (n - 1 - i)
            if not mask & bit:
                t.append((d, bool(val & bit)))
        terms.append(t)
    return terms
