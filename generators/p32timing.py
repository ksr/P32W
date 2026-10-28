#!/usr/bin/env python3
"""Static timing analysis of the P32W cards, alone and together (D70):
docs/timing.md.

The gate-level simulator (p32sim) has zero delay: it checks that the cards
are wired to do the right thing, not that they do it in time. This module
checks the time. It joins the cards' netlists over the backplane, gives
every chip its datasheet delays (p32delays.py: the maximum over
temperature at 4.5 V and 50 pF), traces every register's clock back to its
oscillator, and finds, for every register input, how late its data can
arrive against the clock edge that takes it:

  * Clocks. Each analysis has one oscillator (the CPU card's 24 MHz for the
    machine, the video card's 25.175 MHz for its dots, the debug card's
    for the debug card alone). A clock pin is traced back through buffers
    and gates to a register clocked by that oscillator. If that register
    is a phase generator (a GAL like CLKGEN whose registers count the
    oscillator's edges), its rising and falling edges fall at known phases
    of the 8-phase microcycle; otherwise it is the oscillator itself, every
    edge. The clock's insertion delay (the GAL's clock-to-output, then the
    buffers and the backplane) is kept, so skew between cards is counted.
  * Launch: a register's output changes at its clock edge + insertion +
    clock-to-output. Arrival times propagate through every combinational
    arc (buffer, gate, mux, adder, SRAM address-to-data, GAL product term,
    3-state enable), the latest arrival kept.
  * Capture: a register (or an SRAM's write strobe) needs its data by the
    next of its own clock edges after the launching edge, less its setup.
    The slack, and the oscillator period at which it would be zero, give
    the fastest clock each path allows.
  * Exceptions, each with its reason (EXCEPTIONS): paths the design gives
    more than one edge (a multicycle path), and paths that cannot happen.
    Paths between oscillators (the video card's handshake, the debug card)
    cross through synchronizers and are listed, not timed.

    python3 p32timing.py [--doc FILE] [--paths N]
"""
import os, sys, importlib, itertools, collections, fnmatch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p32net import DEV                                          # noqa: E402
from p32bus import SIGNALS                                      # noqa: E402
import p32delays as PD                                          # noqa: E402

# ---------------------------------------------------------------- the machine as configured
SLOTS = {"cpu": 1, "mmu": 2, "mem": 3, "io": 4, "disk": 5, "video": 6, "debug": 0}
BUS_C = 200.0           # pF on a backplane line: 11 sockets, their cards' inputs, 30 cm of track

class Part:
    __slots__ = ("card", "ref", "dev", "value", "pins", "gal", "fn")
    def __init__(self, card, ref, dev, value, gal, fn=""):
        self.card, self.ref, self.dev, self.value, self.gal, self.fn = card, ref, dev, value, gal, fn
        self.pins = {}

    @property
    def name(self):
        return "%s/%s" % (self.card, self.ref)

def shared(net):
    """Is a card's net one of the backplane's (the same net on every card)?"""
    if net in ("VCC", "GND") or net.startswith("SPARE_"):
        return True
    return net in SIGNALS and SIGNALS[net][0] in ("bus", "local", "arb")

_BUILT = {}

def load_system(cards):
    """Every card's parts, with each pin's system net."""
    parts, pulls = [], {}
    for card in cards:
        if card not in _BUILT:
            _BUILT[card] = importlib.import_module("card_" + card).build()
        c, G, role = _BUILT[card]
        bygal = {"GAL_" + g.name: g for g in G.values()}
        slot = SLOTS[card]
        def sysnet(n):
            if shared(n):
                return n
            if n in SIGNALS and SIGNALS[n][0] == "own":
                return "%s%d" % (n, slot)
            return "%s/%s" % (card, n)
        for ref, (dev, fn, val) in c.parts.items():
            if dev in ("DIN96",):
                continue
            p = Part(card, ref, dev, val, bygal.get(dev), fn)
            parts.append(p)
        where = {}
        for n, pins in c.nets.items():
            for r, pin in pins:
                where[(r, pin)] = sysnet(n)
        for p in parts:
            if p.card == card:
                for pin in DEV[p.dev]["pins"]:
                    if (p.ref, pin) in where:
                        p.pins[pin] = where[(p.ref, pin)]
        # pull-ups (open-drain lines rise through them)
        for ref, (dev, fn, val) in c.parts.items():
            if dev == "R":
                a, b = where.get((ref, "1")), where.get((ref, "2"))
                for x, y in ((a, b), (b, a)):
                    if x == "VCC" and y not in (None, "GND", "VCC"):
                        pulls[y] = min(pulls.get(y, 1e9), _ohms(val))
    import card_backplane
    bp = card_backplane.build()
    for ref, (dev, fn, val) in bp.parts.items():
        if dev in ("R", "RNET9"):
            ends = [n for n, ps in bp.nets.items() for r, pin in ps if r == ref]
            if "VCC" in ends:
                for n in ends:
                    if n not in ("VCC", "GND") and not n.startswith("T_"):
                        pulls[n] = min(pulls.get(n, 1e9), _ohms(val))
    return parts, pulls

def _ohms(val):
    v = val.split()[0]
    return float(v[:-1]) * 1e3 if v.endswith("k") else float(v)

# ---------------------------------------------------------------- the chips' timing models
class Model:
    """A chip's timing: comb arcs (in pin -> out pin, delay), registers
    (clock pin, edge, data pins with their setups, outputs with their
    clock-to-output), 3-state enables (in pin -> out pin), and internal
    nodes (named '~x', local to the chip)."""
    def __init__(self):
        self.comb, self.regs = [], []
    def arc(self, a, b, d, inv=None):
        """inv: True if b falls when a rises (None: the chip's default)."""
        self.comb.append((a, b, d, inv))
    def reg(self, clk, edge, ins, outs, tco):
        """ins: [(pin, setup)]; outs: [pin]."""
        self.regs.append((clk, edge, ins, outs, tco))

def _grp(pins, k):
    return [p for p in pins if p[0] == k]

def model(p):
    dev, pins = p.dev, DEV[p.dev]["pins"]
    m = Model()
    try:
        t = PD.timing(p.dev, p.value)
    except KeyError:
        t = {}
    ins = [n for n, (num, ty) in pins.items() if ty == "in"]
    outs = [n for n, (num, ty) in pins.items() if ty in ("out", "tri", "oc")]
    if p.gal is not None:
        return gal_model(p, m, t)
    import re
    from p32net import FASTER
    mm = re.match(r"(?:CD)?74[A-Z]*(\d+)", FASTER.get(dev, dev))
    base = mm.group(1) if mm else dev
    if base in ("00", "08", "32", "04", "14", "4002", "05", "4050"):
        for o in outs:
            for i in ins:
                if i[0] == o[0]:
                    m.arc(i, o, t["tpd"])
    elif base in ("541", "540"):
        for k in range(8):
            m.arc("A%d" % k, "Y%d" % k, t["tpd"])
            for oe in ("-OE1", "-OE2"):
                m.arc(oe, "Y%d" % k, t["en"])
    elif base == "244":
        for h in (1, 2):
            for k in range(1, 5):
                m.arc("%dA%d" % (h, k), "%dY%d" % (h, k), t["tpd"])
                m.arc("-%dG" % h, "%dY%d" % (h, k), t["en"])
    elif base == "125":
        for k in range(1, 5):
            m.arc("%dA" % k, "%dY" % k, t["tpd"])
            m.arc("-%dOE" % k, "%dY" % k, t["en"])
    elif base == "245":
        for k in range(1, 9):
            m.arc("A%d" % k, "B%d" % k, t["tpd"])
            m.arc("B%d" % k, "A%d" % k, t["tpd"])
            for c in ("DIR", "-OE"):
                m.arc(c, "A%d" % k, t["en"]); m.arc(c, "B%d" % k, t["en"])
    elif base in ("574", "374"):
        m.reg("CLK", "r", [("D%d" % k, t["tsu"]) for k in range(8)], ["Q%d" % k for k in range(8)], t["tco"])
        for k in range(8):
            m.arc("-OE", "Q%d" % k, t["en"])
    elif base == "377":
        m.reg("CLK", "r", [("D%d" % k, t["tsu"]) for k in range(8)] + [("-E", t["tsu_e"])],
              ["Q%d" % k for k in range(8)], t["tco"])
    elif base == "273":
        m.reg("CLK", "r", [("D%d" % k, t["tsu"]) for k in range(8)], ["Q%d" % k for k in range(8)], t["tco"])
        for k in range(8):
            m.arc("-MR", "Q%d" % k, t["clr"])
    elif base == "175":
        q = ["Q%d" % k for k in range(1, 5)] + ["-Q%d" % k for k in range(1, 5)]
        m.reg("CP", "r", [("D%d" % k, t["tsu"]) for k in range(1, 5)], q, t["tco"])
        for o in q:
            m.arc("-MR", o, t["clr"])
    elif base == "74":
        for k in (1, 2):
            q = ["%dQ" % k, "-%dQ" % k]
            m.reg("%dCLK" % k, "r", [("%dD" % k, t["tsu"])], q, t["tco"])
            for o in q:
                m.arc("-%dCLR" % k, o, t["clr"]); m.arc("-%dPRE" % k, o, t["clr"])
    elif base in ("161", "163"):
        q = ["QA", "QB", "QC", "QD"]
        sync = [(x, t["tsu"]) for x in "ABCD"] + [("ENP", t["tsu_en"]), ("ENT", t["tsu_en"]), ("-LOAD", t["tsu_ld"])]
        if base == "163":
            sync.append(("-CLR", t["tsu_clr"]))
        else:
            for o in q + ["RCO"]:
                m.arc("-CLR", o, t["clr"])
        m.reg("CLK", "r", sync, q, t["tco"])
        m.reg("CLK", "r", [], ["~rco"], t["tco_rco"] - t["ent_rco"])   # RCO = the count's terminal state & ENT
        m.arc("~rco", "RCO", t["ent_rco"]); m.arc("ENT", "RCO", t["ent_rco"])
    elif base == "393":
        for k in (1, 2):
            q = ["%dQ%d" % (k, i) for i in range(4)]
            m.reg("%dCP" % k, "f", [], q, t["tco"])                    # ripple: the worst bit's delay
            for o in q:
                m.arc("%dMR" % k, o, t["clr"])
    elif base == "595":
        m.reg("SRCLK", "r", [("SER", t["tsu"]), ("-SRCLR", t["tsu"])], ["QH'", "~sr"], t["tco_sr"])
        m.reg("RCLK", "r", [("~sr", t["tsu_rclk"])], ["Q" + x for x in "ABCDEFGH"], t["tco"])
        for x in "ABCDEFGH":
            m.arc("-OE", "Q" + x, t["en"])
        m.arc("-SRCLR", "QH'", t["clr"])
    elif base == "597":
        m.reg("RCK", "r", [(x, t["tsu"]) for x in "ABCDEFGH"], ["~st"], 0.0)
        m.reg("SRCK", "r", [("SER", t["tsu"]), ("-SRLOAD", t["tsu"])], ["QH'"], t["tco"])
        m.arc("~st", "QH'", t["load"]); m.arc("-SRLOAD", "QH'", t["load"]); m.arc("-SRCLR", "QH'", t["clr"])
    elif base in ("157", "257"):
        for k in range(1, 5):
            y = "%dY" % k
            m.arc("%dA" % k, y, t["tpd"]); m.arc("%dB" % k, y, t["tpd"]); m.arc("S", y, t["sel"])
            m.arc("-E" if base == "157" else "-OE", y, t["en"])
    elif base in ("153", "253"):
        for k in (1, 2):
            y = "%dY" % k
            for i in range(4):
                m.arc("%dI%d" % (k, i), y, t["tpd"])
            m.arc("S0", y, t["sel"]); m.arc("S1", y, t["sel"])
            m.arc("-%dE" % k if base == "153" else "-%dOE" % k, y, t["en"])
    elif base == "151":
        for o in ("Y", "-W"):
            for i in range(8):
                m.arc("D%d" % i, o, t["tpd"])
            for s in ("S0", "S1", "S2"):
                m.arc(s, o, t["sel"])
            m.arc("-E", o, t["en"])
    elif base in ("138", "154"):
        for o in outs:
            for i in ins:
                m.arc(i, o, t["sel"] if i in ("A", "B", "C", "D") else t["en"])
    elif base in ("148", "280", "145"):
        for o in outs:
            for i in ins:
                m.arc(i, o, t["tpd"])
    elif base == "85":
        for o in outs:
            for i in ins:
                m.arc(i, o, t["casc"] if i.startswith("I") else t["tpd"])
    elif base == "688":
        for i in ins:
            m.arc(i, "-P=Q", t["en"] if i == "-G" else t["tpd"])
    elif base == "283":
        s = ["S1", "S2", "S3", "S4", "C4"]
        for i in ins:
            for o in s:
                m.arc(i, o, t["c0"] if i == "C0" else t["tpd"])
    elif dev in ("CY7C199", "AS6C4008"):
        io = [n for n, (num, ty) in pins.items() if ty == "bidir"]
        addr = [n for n in ins if n.startswith("A")]
        for o in io:
            for a in addr:
                m.arc(a, o, t["tAA"])
            m.arc("-CE", o, t["tACE"]); m.arc("-OE", o, t["tDOE"])
        m.reg("-WE", "r", [(o, t["tSD"]) for o in io] + [(a, t["tAW"]) for a in addr] + [("-CE", t["tSCE"])],
              [], 0.0)
    elif dev.startswith("SST39SF"):
        dq = [n for n in outs if n.startswith("DQ")]
        for o in dq:
            for a in ins:
                if a.startswith("A"):
                    m.arc(a, o, t["tAA"])
            m.arc("-CE", o, t["tCE"]); m.arc("-OE", o, t["tOE"])
    elif dev == "TL16C550":
        d = ["D%d" % k for k in range(8)]
        for o in d:
            for a in ("-RD1", "RD2"):
                m.arc(a, o, t["tRVD"])
        m.reg("-WR1", "r", [(o, t["tDS"]) for o in d] + [(a, t["tAS"]) for a in ("A0", "A1", "A2", "CS0", "CS1", "-CS2")],
              [], 0.0)
    elif dev == "DS12C887":
        ad = ["AD%d" % k for k in range(8)]
        for o in ad:
            m.arc("-RD", o, t["tDDR"])
        m.reg("-WR", "r", [(o, t["tDSW"]) for o in ad], [], 0.0)
        m.reg("AS", "f", [(o, t["tASL"]) for o in ad] + [("-CS", t["tASL"])], [], 0.0)
    elif dev.startswith("OSC"):
        pass                                            # a clock root
    elif dev in ("TANG_NANO_20K", "PICO", "WIZ850IO", "MAX232A", "LD1117V33"):
        pass                                            # their own clocks: asynchronous to the bus (see FPGA)
    elif all(ty in ("pas", "pwr", "gnd", "pwr3") for num, ty in pins.values()):
        pass                                            # passive: resistors, capacitors, connectors, switches
    else:
        raise KeyError("no timing model for %s" % dev)
    return m

def gal_model(p, m, t):
    """A GAL: combinational outputs from their equations' inputs (tPD);
    registered outputs at the clock (tCO), their inputs setting up (tSU);
    3-state enables (tEA); internal feedback of a registered output to
    the array (tCF in place of tCO). The array sees a registered output
    through ~NAME, a combinational one on its pin."""
    g = p.gal
    regs = {o.name for o in g.outs.values() if o.kind == "reg"}
    src = lambda d: "~" + d if d in regs else d             # a register reaches the array as feedback
    for o in g.outs.values():
        deps = sorted({n for term in o.eq for n, v in term})
        if o.kind == "reg":
            m.reg("~CLK", "r", [(src(d), t["tsu"]) for d in deps], [o.name], t["tco"])
            m.reg("~CLK", "r", [], ["~" + o.name], t["tcf"])
        else:
            for d in deps:
                pol = {v for term in o.eq for n, v in term if n == d}
                inv = None if len(pol) != 1 else ((pol == {False}) != bool(o.inverted))
                m.arc(src(d), o.name, t["tpd"], inv)
            if o.kind == "tri":
                for d in sorted({n for term in o.oe for n, v in term}):
                    m.arc(src(d), o.name, t["tea"], None)
    return m

# ---------------------------------------------------------------- the graph
class Graph:
    """Nodes are nets (and chips' internal nodes, 'CARD/REF~x'); arcs carry
    a delay and the part that makes them."""
    def __init__(self, parts, pulls, drop=(), corner="max"):
        self.corner = corner
        self.parts = parts
        self.part = {p.name: p for p in parts}
        self.pulls = pulls
        self.fanout = collections.defaultdict(list)       # net -> [(to net, delay, part, pin in, pin out)]
        self.regs = []                                     # (part, clock net, edge, [(data net, setup, pin)], [out nets], tco)
        self.drivers = collections.defaultdict(list)       # net -> [(part, pin, kind)]
        self.sense = {}                                    # (from, to, part) -> True if inverting
        for p in parts:
            if p.name in drop:
                continue
            m = model(p)
            f = PD.typical(p.dev, p.value) if corner == "typ" else 1.0
            m.comb = [(a, b, d * f, inv) for a, b, d, inv in m.comb]
            m.regs = [(clk, edge, [(i, su * f) for i, su in ins], outs, tco * f) for clk, edge, ins, outs, tco in m.regs]
            node = lambda pin: self.node(p, pin)
            for a, b, d, inv in m.comb:
                na, nb = node(a), node(b)
                if na is None or nb is None or na in ("VCC", "GND"):
                    continue
                d = d + self.load(p, b, nb)
                inv = PD.inverts(p.dev) if inv is None else inv
                self.fanout[na].append((nb, d, p, a, b))
                self.sense[(na, nb, p.name)] = inv
                self.drivers[nb].append((p, b, "comb"))
            for clk, edge, ins, outs, tco in m.regs:
                nc = node(clk)
                if nc is None:
                    continue
                dins = [(node(i), s, i) for i, s in ins if node(i) not in (None, "VCC", "GND")]
                douts = [node(o) for o in outs if node(o) is not None]
                douts_d = [(n, tco + self.load(p, o, n)) for o, n in zip([o for o in outs if node(o) is not None], douts)]
                self.regs.append((p, nc, edge, dins, douts_d, [o for o in outs if node(o) is not None]))
                for n, d in douts_d:
                    self.drivers[n].append((p, None, "reg"))

    def net_kind(self, net, depth=0):
        """'w' if the net can only be active in a write cycle (made, through
        the equations that drive it, from -MWR alone), 'r' likewise for a
        read (-MRD, -IACK), else None."""
        if not hasattr(self, "_kind"):
            self._kind = {}
            self._drv = collections.defaultdict(list)
            self._ins = collections.defaultdict(set)
            for a_, arcs_ in self.fanout.items():
                for b_, d_, p_, pa_, pb_ in arcs_:
                    if (p_, pb_) not in self._drv[b_]:
                        self._drv[b_].append((p_, pb_))
                    self._ins[(b_, p_.name)].add(a_)
        if net == "-MWR":
            return "w"
        if net in ("-MRD", "-IACK"):
            return "r"
        if net in self._kind or depth > 12:
            return self._kind.get(net)
        self._kind[net] = None
        ds = self._drv.get(net, [])
        k = None
        if len(ds) == 1:
            p_, pin_ = ds[0]
            if p_.gal is not None and pin_ in p_.gal.outs and p_.gal.outs[pin_].kind != "reg":
                o = p_.gal.outs[pin_]
                regs = {r.name for r in p_.gal.outs.values() if r.kind == "reg"}
                deps = sorted(set(getattr(o.spec, "deps", ())) | {d for t in o.eq for d, v in t})
                dk = {d: self.net_kind(self.node(p_, ("~" + d) if d in regs else d) or "", depth + 1) for d in deps}
                for c_ in ("w", "r"):
                    W = [d for d in deps if dk[d] == c_]
                    rest = [d for d in deps if dk[d] != c_]
                    if not W or len(rest) > 16:
                        continue
                    for wb in itertools.product((0, 1), repeat=len(W)):
                        vals = set()
                        for rb in itertools.product((0, 1), repeat=len(rest)):
                            v = collections.defaultdict(int, zip(W, wb)); v.update(zip(rest, rb))
                            r_ = o.spec(v)
                            if r_ is not None:
                                vals.add(bool(r_))
                            if len(vals) > 1:
                                break
                        if len(vals) <= 1:
                            k = c_
                            break
                    if k:
                        break
            elif p_.gal is None:
                ins = self._ins.get((net, p_.name), set())
                if len(ins) == 1:                      # a buffer or an inverter
                    k = self.net_kind(next(iter(ins)), depth + 1)
        self._kind[net] = k
        return k

    def blocked_arcs(self, mode, master=("mmu",)):
        """Case analysis of a bus cycle: the arcs that cannot carry data in
        it. In a read the backplane's data lines D0-31 flow from the slave
        cards to the master (the MMU / data card): a slave's arcs from D
        into the card, and the master's from its side onto D, are off. A
        write is the reverse. (The debug card listens in both.)"""
        D = {"D%d" % i for i in range(32)}
        R = {"LR%d" % i for i in range(32)}
        out = set()
        # in the machine the debug card listens: nothing of it drives the backplane (D64);
        # a device's DMA drivers (function "DMA ...") drive it only in its own cycles as
        # master, timed in a run of their own (D75)
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if "/" in b or "~" in b:
                    continue
                if p.card == "debug" or (p.card not in master and p.fn.startswith("DMA ")):
                    out.add((a, b, p.name))
        # R, the CPU's result bus, has one source a microcycle (RSRC): in a
        # read the load steering, in a write the store data; in an internal
        # cycle anything but those two, and nothing drives D at all
        rdrv = collections.defaultdict(set)
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if b in R and a not in R:
                    rdrv[p.name].add(a in D)
        steer = {n for n, v in rdrv.items() if True in v}
        store = {n for n in rdrv if "store data" in self.part[n].fn}
        if mode == "read":
            roff = set(rdrv) - steer
        elif mode == "write":
            roff = set(rdrv) - store
        else:
            roff = steer | store
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if p.name in roff and b in R:
                    out.add((a, b, p.name))
                if mode == "internal" and b in D:
                    out.add((a, b, p.name))
        # the bus strobes a cycle cannot have, and every signal made from them
        dead = {"read": ("-MWR",), "write": ("-MRD", "-IACK"), "internal": ("-MRD", "-MWR", "-IACK")}[mode]
        still = {"read": ("w",), "write": ("r",), "internal": ("w", "r")}[mode]
        for a, arcs in self.fanout.items():
            ka = self.net_kind(a)
            for b, d, p, pa, pb in arcs:
                if b in dead or ka in still or self.net_kind(b) in still:
                    out.add((a, b, p.name))
        if mode == "internal":
            return out
        # the master's chips that take D in (its load steering) are off in a
        # write, those that drive D (its store drivers) off in a read: all their arcs
        takes, drives = set(), set()
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if p.card in master and a in D and b not in D:
                    takes.add(p.name)
                if p.card in master and b in D and a not in D:
                    drives.add(p.name)
        off = takes if mode == "write" else drives if mode == "read" else set()
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if p.name in off:
                    out.add((a, b, p.name))
        # a slave's chips on D: in a read nothing drives the card's side of
        # them (their enables included), in a write nothing drives D
        inside = collections.defaultdict(set)    # part -> the card-side nets its D inputs drive
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if a in D and b not in D:
                    inside[p.name].add(b)
        for a, arcs in self.fanout.items():
            for b, d, p, pa, pb in arcs:
                if p.card == "debug":
                    continue
                if p.card in master:
                    if (mode == "read" and b in D and a not in D) or (mode == "write" and a in D and b not in D):
                        out.add((a, b, p.name))
                    continue
                if mode == "read" and b in inside.get(p.name, ()):
                    out.add((a, b, p.name))
                if mode == "write" and b in D and p.name in inside:
                    out.add((a, b, p.name))
                if mode == "write" and b in D and a not in D:
                    out.add((a, b, p.name))
        return out

    def node(self, p, pin):
        if pin.startswith("~"):
            if p.gal is not None and pin == "~CLK":
                return p.pins.get(p.gal.clock)            # (the GAL's clock pin)
            return "%s~%s" % (p.name, pin[1:])
        if p.gal is not None and pin in p.gal.outs and p.gal.outs[pin].kind == "reg":
            pass
        return p.pins.get(pin, "%s~%s" % (p.name, pin) if p.gal is not None else None)

    def load(self, p, pin, net):
        """Extra delay for a driver on a backplane line (its 200 pF against
        the datasheet's 50 pF), and an open-drain line's rise through its
        pull-up (0.5 RC to reach 2.0 V)."""
        if net is None or "/" in net or "~" in net:
            return 0.0
        # a bused line reaches every socket; a radial or local one two or three
        cap = BUS_C if net in SIGNALS and SIGNALS[net][0] == "bus" or net.startswith("SPARE") else 50.0
        ty = DEV[p.dev]["pins"].get(pin, (0, "in"))[1]
        if ty == "oc":
            # an open-drain line is asserted by its driver pulling down (the
            # delay in the table is its pull-down's); its release, rising
            # through the pull-up, is checked on its own (releases())
            return PD.bus_derate(p.dev, p.value, cap) * (PD.typical(p.dev) if self.corner == "typ" else 1.0)
        return PD.bus_derate(p.dev, p.value, cap) * (PD.typical(p.dev) if self.corner == "typ" else 1.0)

# ---------------------------------------------------------------- clocks
class Clock:
    """Where a register's clock pin gets its edges: the oscillator (root),
    the phase generator register (if any), the edges' times in the frame
    (the microcycle), and the insertion delay from the oscillator edge."""
    def __init__(self, root, edges, ins, via):
        self.root, self.edges, self.ins, self.via = root, edges, ins, via

def phase_sequence(p, gen_inputs, n=8, warm=16):
    """The value of each registered output of an oscillator-clocked GAL at
    each oscillator edge, its other inputs held: a list (one per edge) of
    {output: value}, starting from all zero."""
    g = p.gal
    regs = {o.name: o for o in g.outs.values() if o.kind == "reg"}
    st = {k: 0 for k in regs}
    gen_inputs = collections.defaultdict(int, gen_inputs)
    seq = []
    for step in range(warm + n):
        v = collections.defaultdict(int, gen_inputs); v.update(st)
        nxt = {}
        for k, o in regs.items():
            r = o.spec(v)
            nxt[k] = int(bool(r)) if r is not None else 0
        st = nxt
        if step >= warm:
            seq.append(dict(st))
    return seq

class Clocks:
    def __init__(self, graph, root, period, gen_inputs, nphase=8):
        self.g, self.root, self.T = graph, root, period
        self.n = nphase
        self.gen_inputs = gen_inputs
        self.cache = {}
        self.unknown = collections.Counter()
        self.H = nphase * period                    # the frame: one microcycle
        self.rev = collections.defaultdict(list)    # net -> [(from net, delay, part)]
        for a, arcs in graph.fanout.items():
            for b, d, p, pa, pb in arcs:
                self.rev[b].append((a, d, p))
        self.regout = collections.defaultdict(list) # net -> [(part, register's clock net, tco)]
        for p, nc, edge, dins, douts, opins in graph.regs:
            for n, tco in douts:
                self.regout[n].append((p, nc, tco))
        self.virtual = {}                           # net -> (source net, delay): a clock made off the board's logic
        self.gens = {}                              # phase generators: part -> sequence
        self.ph = None                              # Phases, set by analyse()

    def of(self, net, edge="r", depth=0):
        """[Clock] for a clock pin on this net (several if it can come from
        more than one place)."""
        key = (net, edge)
        if key in self.cache:
            return self.cache[key]
        self.cache[key] = []                        # (in progress: a loop gives nothing)
        out = []
        if net == self.root:
            out = [Clock(self.root, [k * self.T + (0 if edge == "r" else self.T / 2) for k in range(self.n)], 0.0, [net])]
        elif net in self.virtual and depth < 20:
            a, d = self.virtual[net]
            for c in self.of(a, edge, depth + 1):
                out.append(Clock(c.root, c.edges, c.ins + d, c.via + [net]))
        elif depth < 20:
            for p, nc, tco in self.regout.get(net, []):
                redge = "f" if PD.falling_clock(p.dev) else "r"
                src = [c for c in self.of(nc, "r", depth + 1) if c.root == self.root] if p.gal is not None else []
                if p.gal is not None and self.ph is not None and src and all(len(c.edges) == self.n for c in src) \
                        and p.gal.name not in self.gen_inputs:
                    # a clock made by a phase-aware GAL: only where it can rise (fall)
                    name = [o.name for o in p.gal.outs.values() if self.g.node(p, o.name) == net]
                    if name:
                        for c in src:
                            ts = self.ph.changes(p, name[0], c.edges, (0, 1) if edge == "r" else (1, 0))
                            out.append(Clock(c.root, ts, c.ins + tco, c.via + [net]))
                        continue
                if nc != self.root or p.gal is None or p.gal.name not in self.gen_inputs:
                    # a clock made by a register: it may change at any of that
                    # register's own clock edges, its clock-to-output later
                    for c in self.of(nc, redge, depth + 1):
                        out.append(Clock(c.root, c.edges, c.ins + tco, c.via + [net]))
                    continue
                seq = self.gens.get(p.name) or phase_sequence(p, self.gen_inputs.get(p.gal.name, {}), self.n)
                self.gens[p.name] = seq
                name = [o.name for o in p.gal.outs.values() if self.g.node(p, o.name) == net]
                if name:
                    s = [x[name[0]] for x in seq]
                    want = (0, 1) if edge == "r" else (1, 0)
                    edges = [k * self.T for k in range(self.n) if (s[k - 1], s[k]) == want]
                    out.append(Clock(self.root, edges, tco, [self.root, net]))
            for a, d, p in self.rev.get(net, []):
                inv = self.g.sense.get((a, net, p.name))
                if inv is None:
                    inv = PD.inverts(p.dev)
                for c in self.of(a, ("f" if edge == "r" else "r") if inv else edge, depth + 1):
                    out.append(Clock(c.root, c.edges, c.ins + d, c.via + [net]))
        if out:                                     # the clock input, not the decode gating it: the most direct route
            m = min(len(c.via) for c in out)
            out = [c for c in out if len(c.via) == m]
        self.cache[key] = out
        return out

# ---------------------------------------------------------------- the analysis
EXCEPTIONS = []        # filled by p32timing_exceptions (pattern, extra edges or None for false, reason)

class Result:
    def __init__(self):
        self.ends = []          # (slack, Tmin, capture part, pin, data net, arrival, required, path, launch edge, cap edge)
        self.crossings = collections.Counter()
        self.unclocked = collections.Counter()
        self.excepted = collections.Counter()

def analyse(graph, root, period, gen_inputs, nphase=8, exceptions=(), virtual=None, launch_only=None, mode=None,
            known_nets=None, master=("mmu",)):
    clocks = Clocks(graph, root, period, gen_inputs, nphase)
    clocks.virtual.update(virtual or {})
    known, seqs = phase_nets(graph, root, gen_inputs, nphase, known_nets)
    ph = clocks.ph = Phases(known, seqs, period, nphase)
    H = nphase * period
    res = Result()
    # launches, grouped by the launching edge's time in the frame
    launch = collections.defaultdict(list)       # edge time -> [(net, arrival, part, clock)]
    capture = []                                 # (part, clock, data net, setup, pin)
    blocked = graph.blocked_arcs(mode, master) if mode else set()
    quiet = {(pn, b_) for a_, b_, pn in blocked}       # (part, net) it cannot drive in this cycle
    regdins = collections.defaultdict(list)            # register output net -> its data input nets
    for p_, nc_, e_, dins_, douts_, op_ in graph.regs:
        for n_, t_ in douts_:
            regdins[n_].extend(x[0] for x in dins_)
    regby = {}                                         # net -> (part, output name) of a phase-aware GAL register
    for p_, nc_, e_, dins_, douts_, op_ in graph.regs:
        if p_.gal is not None and op_ and not op_[0].startswith("~"):
            regby[douts_[0][0]] = (p_, op_[0])
    def can_be(net, t, val):
        """Can this net hold val just before the edge at time t?"""
        if net == "VCC":
            return val == 1
        if net == "GND":
            return val == 0
        if net in known:
            return known[net][ph.before(t)] == val
        if net in regby:
            p_, o_ = regby[net]
            cl_ = [c for c in clocks.of(graph.part[p_.name].pins.get(p_.gal.clock), "r") if c.root == root]
            if cl_ and all(len(c.edges) == nphase for c in cl_):
                prev = [e for e in cl_[0].edges if e < t - 1e-9] or [cl_[0].edges[-1] - nphase * period]
                return val in ph.values(p_, o_, max(prev) % (nphase * period))
        return True
    def counter_moves(p, edges):
        """A 74x161 / 163 changes at an edge only if it counts (ENP and ENT
        high), loads or clears there."""
        out = set()
        for t in edges:
            pin = lambda x: p.pins.get(x)
            if (can_be(pin("ENP"), t, 1) and can_be(pin("ENT"), t, 1)) or can_be(pin("-LOAD"), t, 0) \
                    or can_be(pin("-CLR"), t, 0):
                out.add(t)
        return out
    def cycle_has(net):
        k = graph.net_kind(net)
        if k is None or mode is None:
            return True
        return {"read": k == "r", "write": k == "w", "internal": False}[mode]
    for p, nc, edge, dins, douts, opins in graph.regs:
        cl = clocks.of(nc, edge)
        mine = [c for c in cl if c.root == root]
        if not mine:
            if cl:
                for n, s, pin in dins:
                    res.crossings[p.card] += 1
            else:
                res.unclocked[(p.name, p.dev, nc)] += 1
            continue
        if not cycle_has(nc):
            res.excepted["a capture whose strobe this cycle does not have"] += len(dins)
            dins = []
        # a GAL on every edge of the oscillator: phase-aware (it listens to an
        # input, and can change, only at the edges where the phases let it)
        gated = p.gal is not None and all(len(c.edges) == nphase for c in mine)
        only = next((v for k, v in (launch_only or {}).items()
                     if (fnmatch.fnmatch(p.fn, k[3:]) if k.startswith("fn:") else fnmatch.fnmatch(p.name, k))), None)
        oname = opins[0].lstrip("~") if gated and opins else None
        for c in mine:
            when = set(ph.changes(p, oname, c.edges)) if gated and oname else None
            if when is None and p.dev[-3:] in ("161", "163") and len(c.edges) == nphase:
                when = counter_moves(p, c.edges)
            for e in c.edges:
                if when is not None and e not in when:
                    continue                              # it cannot change at this edge
                if only is not None and int(round(e / period)) % nphase not in only[0]:
                    continue
                for n, tco in douts:
                    if (p.name, n) in quiet:
                        continue                          # its output enable is off in this cycle
                    launch[e].append((n, e + c.ins + tco, p, c))
            for n, s, pin in dins:
                if gated:
                    ts = ph.sensitive(p, oname, pin.lstrip("~"), c.edges)
                    if not ts:
                        continue
                    capture.append((p, Clock(c.root, ts, c.ins, c.via), n, s, pin))
                elif only is not None:                    # it takes its inputs only where it changes
                    ts = [e for e in c.edges if int(round(e / period)) % nphase in only[0]]
                    capture.append((p, Clock(c.root, ts, c.ins, c.via), n, s, pin))
                else:
                    capture.append((p, c, n, s, pin))
    order, comp_of, comps = condensation(graph)
    # buses: nets named alike, eight or more (D0-31, LR0-31, a card's XD0-31, ...)
    stem = lambda n: n.rstrip("0123456789").rstrip("_").rstrip("0123456789")
    sizes = collections.Counter(stem(n) for comp in comps for n in comp)
    busstem = {n: stem(n) for comp in comps if len(comp) > 1 for n in comp if sizes[stem(n)] >= 8}
    def pass_from(e, starts):
        """Latest arrival at every net from these launches: in the order of
        the graph's strongly connected parts; inside a loop (a bus through
        transceivers and back) only simple paths, never a net twice."""
        arr, pred = {}, {}
        for n, t, p, c in starts:
            if t > arr.get(n, -1e9):
                arr[n] = t; pred[n] = ("launch", p, c)
        for ci in order:
            members = comps[ci]
            if len(members) == 1:
                a = members[0]
                if a not in arr:
                    continue
                ta = arr[a]
                for b, d, p, pa, pb in graph.fanout.get(a, ()):
                    if (a, b, p.name) in blocked:
                        continue
                    if ta + d > arr.get(b, -1e9) + 1e-9:
                        arr[b] = ta + d; pred[b] = ("arc", a, p, pa, pb, d)
                continue
            inside = set(members)
            entries = [(arr[a], a) for a in members if a in arr]
            budget = [50000]
            def dfs(a, t, nets, hist, steps):
                budget[0] -= 1
                if budget[0] < 0:
                    return
                for b, d, p, pa, pb in graph.fanout.get(a, ()):
                    if (a, b, p.name) in blocked:
                        continue
                    tb = t + d
                    st = steps + ((a, p, pa, pb, d, b),)
                    if b not in inside:
                        if tb > arr.get(b, -1e9) + 1e-9:
                            arr[b] = tb; pred[b] = ("path", entry[0], st)
                        continue
                    if b in nets:
                        continue
                    sb = busstem.get(b)
                    h = hist
                    if sb is not None and (not hist or hist[-1] != sb):
                        if sb in hist:
                            continue              # back onto a bus this path left: a read and a write at once
                        h = hist + (sb,)
                    if tb > arr.get(b, -1e9) + 1e-9:
                        arr[b] = tb; pred[b] = ("path", entry[0], st)
                    nets.add(b); dfs(b, tb, nets, h, st); nets.discard(b)
            entry = [None]
            saved = {a: pred.get(a) for t, a in entries}
            for t, a in sorted(entries, reverse=True):
                sa = busstem.get(a)
                entry[0] = (a, saved[a])
                dfs(a, t, {a}, (sa,) if sa else (), ())
        return arr, pred
    worst = {}
    for e, starts in sorted(launch.items()):
        arr, pred = pass_from(e, starts)
        for p, c, n, s, pin in capture:
            if n not in arr:
                continue
            # the capture edge: the first of this clock's edges after the launch edge
            later = [ce + k * H for k in (0, 1, 2) for ce in c.edges if ce + k * H > e + 1e-9]
            if not later:
                continue
            ce = min(later)
            path = trace(pred, n)
            if path and path[-1][1] is p and path[-1][4] is not None:
                res.excepted["a chip's own output into its own capture"] += 1
                continue
            extra, why = exception(exceptions, path, p, pin, mode, c.via[-1] if c.via else None)
            if extra is None:
                res.excepted[why] += 1
                continue
            if extra:
                ahead = sorted(later)
                ce = ahead[min(extra, len(ahead) - 1)]
            req = ce + c.ins - s
            slack = req - arr[n]
            # the oscillator period at which this path's slack is zero: the
            # edges' distances scale with the period, the delays do not
            k = (ce - e) / period
            fixed = arr[n] - e - c.ins + s
            tmin = fixed / k if k > 0 else float("inf")
            key = (p.name, pin)
            if key not in worst or tmin > worst[key][1]:
                worst[key] = (slack, tmin, p, pin, n, arr[n] - e, ce - e, path, e, ce)
    res.ends = sorted(worst.values(), key=lambda x: -x[1])
    return res

def phase_nets(graph, root, gen_inputs, n, extra=None):
    """The phase generators (the GALs named in gen_inputs, on the root
    oscillator): each one's register sequence over the n edges, and every
    net they drive: (known: net -> [value after edge k], seqs: part -> sequence)."""
    known, seqs = {}, {}
    for p in graph.parts:
        if p.gal is None or p.gal.name not in gen_inputs or p.pins.get(p.gal.clock) != root:
            continue
        seq = phase_sequence(p, gen_inputs[p.gal.name], n)
        seqs[p.name] = seq
        for name in seq[0]:
            known[graph.node(p, name)] = [x[name] for x in seq]
            known[graph.node(p, "~" + name)] = [x[name] for x in seq]
    known.update(extra or {})
    return known, seqs

class Phases:
    """What is known of the oscillator-clocked logic at each edge: the phase
    generators' outputs (and any nets a run names, like the video card's dot
    counter), each a value after every edge of the frame. For a GAL register
    on that clock it finds, edge by edge, which inputs its next state can
    depend on, and what values it can take (the unknown inputs tried both
    ways)."""
    def __init__(self, known, seqs, period, n, cap=12):
        self.known, self.seqs, self.T, self.n, self.cap = known, seqs, period, n, cap
        self.memo = {}

    def before(self, t):
        """The frame index whose values hold just before time t."""
        import math
        return (math.ceil(t / self.T - 1e-9) - 1) % self.n

    def split(self, p, deps, t, skip=None):
        regs = {r.name for r in p.gal.outs.values() if r.kind == "reg"}
        k = self.before(t)
        fixed, free = {}, []
        for d in deps:
            if d == skip:
                continue
            if d in regs and p.name in self.seqs:
                fixed[d] = self.seqs[p.name][k][d]
                continue
            net = p.pins.get(d)
            if net in self.known:
                fixed[d] = self.known[net][k]
            else:
                free.append(d)
        return fixed, free

    def deps(self, o):
        return sorted(set(getattr(o.spec, "deps", ())) | {d for term in o.eq for d, v in term})

    def sensitive(self, p, oname, x, times):
        """The times (of these edges) at which oname's next state can depend on x."""
        o = p.gal.outs.get(oname)
        if o is None or o.kind != "reg":
            return list(times)
        deps = self.deps(o)
        if x not in deps:
            return []
        out = []
        for t in times:
            fixed, free = self.split(p, deps, t, skip=x)
            if len(free) > self.cap:
                out.append(t); continue
            for bits in itertools.product((0, 1), repeat=len(free)):
                v = collections.defaultdict(int, fixed); v.update(zip(free, bits))
                v[x] = 0; a = o.spec(v)
                v[x] = 1; b = o.spec(v)
                if a is not None and b is not None and bool(a) != bool(b):
                    out.append(t); break
        return out

    def values(self, p, oname, t):
        """The values oname can take at the edge at time t."""
        key = (p.name, oname, round(t, 3))
        if key in self.memo:
            return self.memo[key]
        o = p.gal.outs.get(oname)
        deps = self.deps(o)
        fixed, free = self.split(p, deps, t)
        if len(free) > self.cap:
            vals = {0, 1}
        else:
            vals = set()
            for bits in itertools.product((0, 1), repeat=len(free)):
                v = collections.defaultdict(int, fixed); v.update(zip(free, bits))
                r = o.spec(v)
                vals.add(1 if r is None else int(bool(r)))
                if r is None:
                    vals.add(0)
                if len(vals) == 2:
                    break
        self.memo[key] = vals
        return vals

    def changes(self, p, oname, times, want=None):
        """The times at which oname can change (or, with want=(a, b), go from a to b)."""
        times = sorted(times)
        out = []
        for i, t in enumerate(times):
            prev, now = self.values(p, oname, times[i - 1]), self.values(p, oname, t)
            pairs = {(a, b) for a in prev for b in now if a != b}
            if (want in pairs) if want else pairs:
                out.append(t)
        return out

def condensation(graph):
    """The graph's strongly connected components (Tarjan, iterative) in
    topological order: (order, component of each net, components)."""
    nodes = set(graph.fanout)
    for a, arcs in graph.fanout.items():
        for b, *_ in arcs:
            nodes.add(b)
    index, low, onst, st, comps, comp_of = {}, {}, set(), [], [], {}
    counter = 0
    for root in sorted(nodes):                  # (sorted: the same report every run)
        if root in index:
            continue
        work = [(root, 0)]
        while work:
            v, i = work.pop()
            if i == 0:
                index[v] = low[v] = counter; counter += 1
                st.append(v); onst.add(v)
            arcs = graph.fanout.get(v, ())
            recurse = False
            while i < len(arcs):
                w = arcs[i][0]; i += 1
                if w not in index:
                    work.append((v, i)); work.append((w, 0)); recurse = True
                    break
                elif w in onst:
                    low[v] = min(low[v], index[w])
            if recurse:
                continue
            if low[v] == index[v]:
                comp = []
                while True:
                    w = st.pop(); onst.discard(w); comp.append(w); comp_of[w] = len(comps)
                    if w == v:
                        break
                comps.append(comp)
            if work:
                u = work[-1][0]
                low[u] = min(low[u], low[v])
    # Tarjan finds components in reverse topological order
    order = list(range(len(comps) - 1, -1, -1))
    return order, comp_of, comps

def trace(pred, n):
    """The path to n: [(net, part, pin in, pin out, delay)] from the launch."""
    out = []
    seen = set()
    x = pred.get(n)
    while x is not None and len(out) < 400:
        if x[0] == "launch":
            out.append((n, x[1], "clk", "q", None))
            break
        if x[0] == "path":                      # a whole path through a loop, from its entry
            (a, before), steps = x[1], x[2]
            for (fa, p, pa, pb, d, b) in reversed(steps):
                out.append((b, p, pa, pb, d))
            n, x = a, before
            continue
        _, a, p, pa, pb, d = x
        if (n, a) in seen:
            break
        seen.add((n, a))
        out.append((n, p, pa, pb, d))
        n, x = a, pred.get(a)
    return list(reversed(out))

def exception(exceptions, path, p, pin, mode=None, clocknet=None):
    """(extra edges, reason): 0 = timed normally; None = not timed (false)."""
    first = next((x for x in path if x[4] is None), None)
    start = first[1].name if first else ""
    startfn = first[1].fn if first else ""
    nets = [x[0] for x in path]
    fns = [x[1].fn for x in path if x[4] is not None and x[4] != "mode"]
    for ex in exceptions:
        if ex.matches(start, nets, p, pin, mode, clocknet, startfn, fns):
            return ex.extra, ex.why
    return 0, ""

class Exc:
    """An exception: a path from a launching part (or any) through a net
    (or any) to a capturing part (or any): extra edges (multicycle), or
    None (a false path), and why."""
    def __init__(self, extra, why, frm=None, thru=None, to=None, pin=None, mode=None, clock=None, tofn=None, frmfn=None,
                 thrufn=None):
        self.extra, self.why, self.frm, self.thru, self.to, self.pin = extra, why, frm, thru, to, pin
        self.mode, self.clock, self.tofn, self.frmfn, self.thrufn = mode, clock, tofn, frmfn, thrufn
    def matches(self, start, nets, p, pin, mode=None, clocknet=None, startfn="", fns=()):
        if self.thrufn and not any(fnmatch.fnmatch(f, t) for f in fns for t in self.thrufn):
            return False
        if self.frmfn and not any(fnmatch.fnmatch(startfn, f) for f in self.frmfn):
            return False
        if self.mode and self.mode != mode:
            return False
        if self.clock and not any(fnmatch.fnmatch(clocknet or "", c) for c in self.clock):
            return False
        if self.frm and not any(fnmatch.fnmatch(start, f) for f in self.frm):
            return False
        if self.to and not any(fnmatch.fnmatch(p.name, f) for f in self.to):
            return False
        if self.tofn and not any(fnmatch.fnmatch(p.fn, f) for f in self.tofn):
            return False
        if self.pin and not any(fnmatch.fnmatch(pin, f) for f in self.pin):
            return False
        if self.thru and not any(fnmatch.fnmatch(n, t) for n in nets for t in self.thru):
            return False
        return True

# ---------------------------------------------------------------- reports
def releases(g):
    """Each open-drain line's release: the time to rise through its pull-up
    to HCT's 2.0 V (0.51 RC), its capacitance by what it reaches."""
    out = []
    for net in sorted(g.pulls):
        oc = any(DEV[p.dev]["pins"].get(pin, (0, ""))[1] == "oc" for p, pin, k in g.drivers.get(net, []) if pin)
        if not oc or "/" in net:
            continue
        cap = BUS_C if net in SIGNALS and SIGNALS[net][0] == "bus" else 50.0
        out.append((net, g.pulls[net], cap, 0.51 * g.pulls[net] * cap * 1e-3))
    return out

def run_modes(g, run):
    """analyse() in each of the run's bus-cycle cases; the worse per endpoint."""
    merged = None
    for mode in run.get("modes", [None]):
        r = analyse(g, run["root"], run["period"], run.get("gen", {}), run.get("phases", 8), run.get("exceptions", ()),
                    run.get("virtual"), run.get("launch_only"), mode, run.get("known"), run.get("master", ("mmu",)))
        for x in r.ends:
            x[7].insert(0, ("[%s cycle]" % mode, None, "", "", "mode")) if mode else None
        if merged is None:
            merged = r
            continue
        best = {(x[2].name, x[3]): x for x in merged.ends}
        for x in r.ends:
            k = (x[2].name, x[3])
            if k not in best or x[1] > best[k][1]:
                best[k] = x
        merged.ends = sorted(best.values(), key=lambda x: -x[1])
    return merged

def describe(path):
    out = []
    for n, p, pa, pb, d in path:
        if d == "mode":
            out.append(n)
        elif d is None:
            out.append("%s %s (%s) launches %s" % (p.name, p.dev, p.value if p.gal else "", n))
        else:
            out.append("  %5.1f  %s %s %s->%s  %s" % (d, p.name, p.dev, pa, pb, n))
    return out

def groups(res, period):
    """Failing endpoints, grouped: (card, chip, the cycle, the edges' distance) -> [ends], worst first."""
    g = collections.defaultdict(list)
    for x in res.ends:
        slack, tm, p, pin, n, arr, budget, path, e, ce = x
        if tm > period + 1e-6:
            mode = path[0][0] if path and path[0][4] == "mode" else ""
            g[(p.card, p.dev, mode, round(budget, 1))].append(x)
    return sorted(g.items(), key=lambda kv: -max(x[1] for x in kv[1]))

def analyse_all(corners=("max", "typ")):
    import p32timing_config as CFG
    out = []
    for run in CFG.RUNS:
        parts, pulls = load_system(run["cards"])
        for corner in run.get("corners", corners):
            g = Graph(parts, pulls, drop=run.get("drop", ()), corner=corner)
            res = run_modes(g, run)
            out.append((run, corner, g, res))
    return out

def summary_line(run, corner, res):
    bad = [x for x in res.ends if x[1] > run["period"] + 1e-6]
    tmin = max((x[1] for x in res.ends), default=0)
    per = tmin * run.get("phases", 8) if run.get("phases", 8) == 8 and run["root"].endswith("OSC") else tmin
    return bad, tmin

def doc(outpath, results):
    import p32timing_config as CFG
    L = ["# Timing", "",
         "*Generated by `generators/p32timing.py` (D71) from every card's netlist and",
         "`generators/p32delays.py`'s datasheet figures — edit the generators, not this file.*", "",
         "A static timing analysis of the cards, alone and together over the",
         "backplane. The gate-level simulations (D61–D67) check that the cards are",
         "wired to do the right thing; this checks that they do it in time. Method",
         "and assumptions: §4. **Worst case** is every chip at its datasheet",
         "maximum over temperature (4.5 V, 50 pF, -40..+85 °C); **typical** at its",
         "25 °C typical (`p32delays.typical`). A path fails if its data can arrive",
         "after the clock edge that takes it, less that register's setup time.", "",
         "## 1. Results", "",
         "| Analysis | Corner | Endpoints | Failing at the design clock | Fastest clock every path allows |",
         "|---|---|---|---|---|"]
    for run, corner, g, res in results:
        bad, tmin = summary_line(run, corner, res)
        if run.get("phases", 8) == 8 and run["root"] == "cpu/OSC":
            fast = "oscillator %.1f ns: a **%.2f MHz** bus clock (design: %.0f MHz)" % (tmin, 1e3 / tmin / 8, 1e3 / run["period"] / 8)
        else:
            fast = "%.1f ns (design %.1f ns)" % (tmin, run["period"])
        L.append("| %s | %s | %d | %d | %s |" % (run["name"], "worst case" if corner == "max" else "typical",
                                                len(res.ends), len(bad), fast))
    L += ["", "## 2. What fails", "",
          "Each group is the endpoints of one kind of chip on one card that fail in",
          "one kind of bus cycle, with the edges' distance; its worst path in full",
          "(each step: the delay, the chip, its pins, the net reached).", ""]
    for run, corner, g, res in results:
        gs = groups(res, run["period"])
        if not gs:
            continue
        L += ["### %s, %s" % (run["name"], "worst case" if corner == "max" else "typical"), ""]
        for (card, dev, mode, budget), xs in gs[:14]:
            x = max(xs, key=lambda x: x[1])
            slack, tm, p, pin, n, arr, bud, path, e, ce = x
            L.append("- **%s %s** (%s), %d endpoint%s, %s: needs an oscillator of %.1f ns; "
                     "%s.%s, arriving %.0f ns after its launching edge, %.0f ns allowed:"
                     % (card, dev, p.fn, len(xs), "s" if len(xs) > 1 else "", mode.strip("[]") or "every cycle",
                        tm, p.name, pin, arr, bud - 0))
            L.append("")
            L.append("  ```")
            L += ["  " + l for l in describe(path)]
            L.append("  ```")
        if len(gs) > 14:
            L.append("- and %d smaller groups." % (len(gs) - 14))
        L.append("")
    # exceptions
    L += ["## 3. What is not timed, and why", "",
          "**Case analysis.** The machine is timed three times: a **read** (the",
          "slaves drive D, the MMU / data card takes it in through its load steering,",
          "which alone drives R), a **write** (the MMU / data card drives D from R,",
          "which only the store data drives; the slaves take D in) and an",
          "**internal** cycle (nothing on D; R from any source but those two). Each",
          "card's chips on D point one way in each; signals made only from `-MWR`",
          "(or only from `-MRD` / `-IACK`) do not move in the other cycles, and a",
          "register clocked by such a strobe takes nothing then. Paths never cross",
          "a bus twice (out of D onto R and back, say). A device's DMA drivers",
          "(the disk card's, D75) drive the backplane only in its own cycles as",
          "master: off in the CPU's, and timed in a run of their own with the slave",
          "cards answering (the CPU and the MMU / data card held).", "",
          "**Phases.** The CPU card's GAL CLKGEN makes every bus and CPU clock from",
          "the 24 MHz oscillator, so the analysis knows each one's edges in the",
          "eight-phase microcycle, and for every GAL on the oscillator which inputs",
          "it listens to at which edges. The video card's dot counter does the same",
          "for its eight-dot character time.", ""]
    for run in CFG.RUNS:
        lo = run.get("launch_only") or {}
        ex = run.get("exceptions") or ()
        if not lo and not ex:
            continue
        L += ["**%s:**" % run["name"], ""]
        for k, (edges, why) in lo.items():
            L.append("- %s changes only at edge%s %s of the frame: %s." % (k.replace("fn:", "the counters "), "s" if len(edges) > 1 else "",
                                                                       ", ".join(map(str, edges)), why))
        for e in ex:
            kind = "not a path" if e.extra is None else "%d more edge%s allowed" % (e.extra, "s" if e.extra > 1 else "")
            L.append("- %s (%s)." % (e.why, kind))
        L.append("")
    L += ["**Crossings.** Paths between oscillators are not timed: the video card's",
          "bus side and its dots meet in synchronizers (D67's handshake), the",
          "`-IRQ` lines are sampled by IPEND, the debug card has its own",
          "oscillator for its timing mode, the UARTs and the Picos their own clocks.", ""]
    # releases
    L += ["## 4. Open-drain lines: the release", "",
          "An open-drain line is asserted by a driver pulling it low (timed above);",
          "it is released by its pull-up, a rise of 0.51 RC to HCT's 2.0 V:", "",
          "| Line | Pull-up | Capacitance | Rise |", "|---|---|---|---|"]
    g0 = results[0][2]
    for net, r, c, t in releases(g0):
        L.append("| `%s` | %s | %.0f pF | %.0f ns |" % (net, ("%gk" % (r / 1e3)) if r >= 1e3 else "%.0f" % r, c, t))
    L += ["", "`-WAIT` must be up within half a bus clock of its release (167 ns at",
          "3 MHz, 100 at 5 MHz, less the gates: D68); `-ACK` before the next",
          "cycle's decision; a radial `-BREQ` before the arbiter's next look.", ""]
    L += ["## 5. Method and assumptions", "",
          "- **Delays:** `generators/p32delays.py`, each part's datasheet and the",
          "  figures used (§6). An output into a backplane line carries 200 pF, not",
          "  the datasheet's 50: about 0.06 ns a pF more for HCT, 0.02 for ACT, F,",
          "  LVC and the GALs, and 3 ns for the line. A radial or local line: 50 pF.",
          "- **Clocks:** each register's clock pin is traced back to its oscillator",
          "  through the most direct route (its clock input, not the decode that gates",
          "  it); its insertion delay counts, so skew between cards does too.",
          "- **The ALU's FPGA** clocks its result latches at most 200 ns after",
          "  `ACLK` (D21: a division takes 18 of its clocks), and the result is read",
          "  in a later microcycle.",
          "- **Not checked here:** hold times (no datasheet gives minimum delays for",
          "  most HCT parts), the FPGA's inside, metastability at the synchronizers.", "",
          "## 6. The datasheets", "", "| Part | Source |", "|---|---|"]
    for k in sorted(PD.SOURCE):
        L.append("| %s | %s |" % (k, PD.SOURCE[k]))
    L.append("")
    with open(outpath, "w") as fh:
        fh.write("\n".join(L))

def main():
    args = sys.argv[1:]
    results = analyse_all()
    for run, corner, g, res in results:
        bad, tmin = summary_line(run, corner, res)
        print("%s, %s: %d endpoints, %d failing; fastest %.1f ns" % (run["name"], corner, len(res.ends), len(bad), tmin))
    if "--doc" in args:
        doc(args[args.index("--doc") + 1], results)

if __name__ == "__main__":
    main()
