#!/usr/bin/env python3
"""The video card with delays (docs/video-card-acrtc.md 9, step 4; D107): its
netlist run as a circuit, every part at its data sheet's delays, on the
engine of YACC1-D's own check of that card (tests/video/v30_netsim.py, of
which hardware/video-card/yacc1-v3/ holds a copy, used here unchanged).

1. THE MEMORY SIDE AND THE DOTS: YACC1-D's two scripts, interleaved() and
   superimposed(), run on the kept part of P32W's netlist, with the chip's
   memory side as that file models it, at its eight corners and both phases
   of the dot counter: 20 runs. The scripts, the models and the checks are
   that file's own; only the netlist is ours (adapter(), below: the kept
   parts under YACC1-D's references, their pins by number).

2. THE BUS SIDE, new, against the chip's host side as Hitachi's manual
   times it (the AC table; figures T-2, T-3; docs/video-card-acrtc.md 3):
   the whole card, the machine's cycles at its backplane pins, and at the
   chip's pins a model that takes CS with its own clock:
     - CS is taken by the first rising 2CLK edge it has been low 40 ns
       before; a read's data comes 80 ns after the falling edge that
       follows; DTACK 70 ns after the rising edge after that;
     - it checks what the chip asks: RS and R/W steady 50 ns before CS and
       until it rises; CS not rising before DTACK, and low three 2CLK
       cycles; CS high 60 ns between accesses; a write's data 40 ns before
       CS rises and 10 ns after.
   A write and a read of the chip are run AT EVERY PHASE of the chip's clock
   against the bus clock (2CLK's 159 ns in steps of 4 ns; --step N for
   others), at 3 MHz and at 4, at six sets of corners (the chip and the
   parts slow or fast, and the parts' rising edges against their falling
   ones). Between them go another master's cycles, one with an address that
   is the chip's for 40 ns on its way: nothing may answer, CS may not move.
   Then the card's own registers and the PS/2 section: a byte loaded and
   read, a command written (two clocks) and taken, the vector.
   The engine checks every flip-flop's and register's set-up and hold on the
   way (the GALs' registers and the two '74s among them), any pulse shorter
   than 5 ns on a clock, a clear or CS, and two outputs on a net for more
   than 3 ns at a handover.

The delays: the kept part's are YACC1-D's tables. The bus side's maximums
are generators/p32delays.py's (the data sheets', over temperature); a
minimum is assumed small where a sheet gives none (HCT 5 ns, a GAL 2 ns).
The master's lines are taken to move 60 ns after the edge that starts a
cycle and 5 ns after the one that ends it: what the machine really gives is
the static timing pass's business (docs/timing.md), not this file's. One
figure comes from that pass: READ_SETUP, how long before a cycle's last edge
the machine needs read data on the backplane's D lines; the chip's word is
checked against it.

    python3 sim_video_timing.py [-v] [--step NS]       exit status 0 when every run passes
"""
import os, sys, types, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_video                                       # noqa: E402
import p32delays as PD                                  # noqa: E402
from p32timing_config import READ_SETUP                 # noqa: E402  (what the machine needs of read data at the bus)
from p32net import DEV                                  # noqa: E402

REF = os.path.join(os.path.dirname(HERE), "hardware", "video-card", "yacc1-v3")
VERBOSE = "-v" in sys.argv
STEP = float(sys.argv[sys.argv.index("--step") + 1]) if "--step" in sys.argv else 4.0

def load_netsim():
    """YACC1-D's v30_netsim.py, as it is (it loads its own netlist from beside it; -v is ours, not its)."""
    s = importlib.util.spec_from_file_location("yacc1_v30_netsim", os.path.join(REF, "tests", "video", "v30_netsim.py"))
    m = importlib.util.module_from_spec(s)
    argv, sys.argv = sys.argv, sys.argv[:1]
    try:
        s.loader.exec_module(m)
    finally:
        sys.argv = argv
    m.VERBOSE = VERBOSE
    return m

ns = load_netsim()
REFN = ns.N                                             # YACC1-D's own netlist, as that file loaded it
DOT = ns.DOT
T2 = 4 * DOT                                            # a 2CLK cycle, 158.9 ns

def adapter(c, only_kept=False):
    """P32W's card as v30_netsim reads a netlist (PARTS, PINS, NETS, TEXT_A): the kept parts under YACC1-D's
    references, the others under P + their own (PU8, PR2); every pin by its number; the nets ours. pulls: the nets
    P32W's own resistors pull, and which way (the engine finds the kept part's by their names)."""
    back = {ref: vref for vref, ref in c.kept.items()}
    name = lambda r: back.get(r, "P" + r)
    A = types.SimpleNamespace(PARTS={}, PINS={}, NETS={}, TEXT_A=list(REFN.TEXT_A), pulls={})
    for ref, (dev, fn, value) in c.parts.items():
        if (only_kept and ref not in back) or dev in ("DIN96", "C100N", "C10U", "C22P", "LED"):
            continue
        v = value if ref not in back or back[ref].startswith("IC") else REFN.PARTS[back[ref]][0]
        A.PARTS[name(ref)] = (v, "", "", fn)
        A.PINS[name(ref)] = [num for pin, (num, t) in DEV[dev]["pins"].items()]
    for net, pins in c.nets.items():
        A.NETS[net] = [(name(r), DEV[c.parts[r][0]]["pins"][p][0]) for r, p in pins if name(r) in A.PARTS]
    for ref, (dev, fn, value) in c.parts.items():
        if dev in ("R", "RNET9") and ref not in back:
            ends = [n for n, pins in c.nets.items() if any(r == ref for r, p in pins)]
            rail = 1 if "VCC" in ends else 0 if "GND" in ends else None
            for n in ends:
                if rail is not None and n not in ("VCC", "GND"):
                    A.pulls[n] = rail
    for n in ["-RESET", "-IO-WR", "-IO-RD"] + ["IO-ADDR%d" % i for i in range(4)]:   # (YACC1-D's bench forces its own bus's lines)
        A.NETS.setdefault(n, [])
    return A

CORNERS = (("chip slow, parts slow", "DMAX", "max", (1, 6)), ("chip slow, parts fast", "DMAX", "min", (1,)),
           ("chip fast, parts slow", "DMIN", "max", (1,)), ("chip fast, parts fast", "DMIN", "min", (1, 6)),
           ("chip slow, rises fast, falls slow", "DMAX", "rise", (1,)), ("chip slow, rises slow, falls fast", "DMAX", "fall", (1,)),
           ("chip fast, rises fast, falls slow", "DMIN", "rise", (1,)), ("chip fast, rises slow, falls fast", "DMIN", "fall", (1,)))

def memory_side(card):
    """YACC1-D's runs, as its main() lists them, on our netlist. Returns [(name, the faults)]."""
    ns.N = adapter(card, only_kept=True)
    out = []
    for cn, d, corner, phases in CORNERS:
        for c0 in phases:
            for kind, f in (("interleaved", ns.interleaved), ("superimposed", ns.superimposed)):
                out.append(("%s, %s, counter from %d" % (kind, cn, c0), f(getattr(ns, d), c0, corner)))
    return out

# ---------------------------------------------------------------- the bus side's parts
MIN = {"HCT": 5.0, "HC": 3.0, "LS": 5.0, "F": 2.0, "ACT": 1.5}      # the minimum assumed for a family whose sheet gives none
GAL_TCO, GAL_TSU, GAL_TH = (2.0, PD.T["GAL-10"]["tco"]), PD.T["GAL-10"]["tsu"], 0.0
GAL_TCF, GAL_AR = (1.0, PD.T["GAL-10"]["tcf"]), (3.0, 13.0)        # clock to feedback; reset to a register's output

def install():
    """The bus side's parts into the engine's tables, beside the kept part's (whose entries are not touched)."""
    def tpd(part, key="tpd"):
        fam = ns.family(part)[0]
        return (MIN[fam], PD.T[part][key], MIN[fam], PD.T[part][key])
    ns.TPD.update({("HCT", "541"): tpd("74HCT541"), ("HCT", "245"): tpd("74HCT245"), ("HCT", "688"): tpd("74HCT688"),
                   ("HCT", "574"): tpd("74HCT574", "tco"), ("HCT", "74"): tpd("74HCT74", "tco"), ("ACT", "74"): tpd("74ACT74", "tco"),
                   ("HC", "05"): tpd("74HC05"), ("LS", "145"): tpd("74LS145"), ("F", "138"): tpd("74F138", "sel"),
                   ("GAL", "22v10"): (2.0, PD.T["GAL-10"]["tpd"]) * 2, ("GAL", "22v10r"): (2.0, PD.T["GAL-10"]["tpd"]) * 2})
    ns.PRECLR.update({("ACT", "74"): tpd("74ACT74", "clr"), ("HCT", "74"): tpd("74HCT74", "clr")})
    for part in ("74HCT541", "74HCT245", "74HCT574"):
        fam, k = ns.family(part)
        ns.ENABLE[(fam, k)] = ns.DISABLE[(fam, k)] = (MIN[fam], PD.T[part]["en"])
    for k in ("22v10", "22v10r"):
        ns.ENABLE[("GAL", k)] = ns.DISABLE[("GAL", k)] = (2.0, PD.T["GAL-10"]["tea"])
    # set-up, and hold (the sheets': SN74ACT74 1 ns, SN74HCT74 3 ns at most, SN74HCT574 5 ns)
    ns.SETUP.update({("ACT", "74"): (PD.T["74ACT74"]["tsu"], 1.0), ("HCT", "74"): (PD.T["74HCT74"]["tsu"], 3.0),
                     ("HCT", "574"): (PD.T["74HCT574"]["tsu"], 5.0)})
    ns.EDGE_PINS.update({"22v10r": (1,), "chip": (4,)})     # a registered GAL's clock; the chip's CS
    ns.Gate.TABLE["05"] = ns.Gate.TABLE["16"]               # an open-drain inverter, the 7416's pins
    ns.MODELS.update({"05": ns.Gate, "541": Buf541, "688": Cmp688, "145": Dec145, "22v10": Gal22, "22v10r": Gal22,
                      "chip": HostChip})
    family = ns.family
    def with_ours(value):
        if value.startswith("ATF22V10"):
            return "GAL", "22v10r" if card_video.gals()[value.split("(")[1].rstrip(")")].clock else "22v10"
        if value.startswith("SN74"):
            return family(value[2:].rstrip("N"))
        return family(value)
    ns.family = with_ours

def and3(vals):
    return 0 if 0 in vals else (None if None in vals else 1)

def sop3(terms, v):
    """A sum of products over 0, 1 and None (not known)."""
    out = 0
    for t in terms:
        x = and3([None if v[n] is None else int(bool(v[n]) == want) for n, want in t])
        if x == 1:
            return 1
        if x is None:
            out = None
    return out

class Gal22(ns.Part):
    """An ATF22V10C-10 with its equations (card_video.gals()): a combinational output tPD after its inputs; a register
    tCO after the clock, its inputs checked for set-up and hold, and what the array makes of it tCF + tPD after the
    clock; a three-state output on and off by its one term; the registers cleared by the reset input."""
    def init(self):
        self.g = card_video.gals()[ns.N.PARTS[self.ref][0].split("(")[1].rstrip(")")]
        g = self.g
        self.pinof = dict(g.inputs)
        self.pinof.update({o.name: o.pin for o in g.outs.values()})
        self.st = {o.name: 0 for o in g.outs.values() if o.kind == "reg"}
        self.regdeps = sorted({n for o in g.outs.values() if o.kind == "reg" for t in o.eq for n, _ in t})
        self.was_oe = {}
        self.fresh = True
    def values(self):
        g, v = self.g, {}
        for n, p in g.inputs.items():
            v[n] = self.v(p)
        for o in g.outs.values():
            v[o.name] = (self.st[o.name] ^ (1 if o.inverted else 0)) if o.kind == "reg" else self.v(o.pin)
        return v
    def eval(self):
        g, cn = self.g, self.s.corner
        v = self.values()
        regs = [o for o in g.outs.values() if o.kind == "reg"]
        moved = False
        if g.reset and v[g.reset] == 0:
            if any(self.st.values()) or self.fresh:
                for o in regs:
                    self.st[o.name] = 0
                    self.s.drive(self.ref, o.pin, 1 if o.inverted else 0, GAL_AR[0 if cn == "min" else 1])
                moved = True
            if g.clock:
                self.rose(self.pinof[g.clock])
        elif g.clock and self.rose(self.pinof[g.clock]):
            for n in self.regdeps:
                if n in g.inputs:
                    self.s.setup(self.ref, g.inputs[n], GAL_TSU, GAL_TH, "GAL %s input" % g.name)
            nxt = {o.name: sop3(o.eq, v) for o in regs}
            for o in regs:
                if nxt[o.name] != self.st[o.name] or self.fresh:
                    self.st[o.name] = nxt[o.name]
                    pin = None if nxt[o.name] is None else nxt[o.name] ^ (1 if o.inverted else 0)
                    self.s.drive(self.ref, o.pin, pin, GAL_TCO[0 if cn in ("min", "rise" if pin == 1 else "fall") else 1])
                    moved = True
        elif self.fresh:
            for o in regs:
                self.s.drive(self.ref, o.pin, self.st[o.name] ^ (1 if o.inverted else 0), 0.0)
        self.fresh = False
        if moved:
            v = self.values()
        more = GAL_TCF[0 if cn == "min" else 1] if moved else 0.0
        for o in g.outs.values():
            if o.kind == "reg":
                continue
            x = sop3(o.eq, v)
            if x is not None and o.inverted:
                x = 1 - x
            if o.kind == "tri":
                on = sop3(o.oe, v)
                sw = on != self.was_oe.get(o.name)
                self.was_oe[o.name] = on
                self.out(o.pin, x if on else None, (self.onoff(bool(on)) if sw else self.tpd(x)) + more)
            else:
                self.out(o.pin, x, self.tpd(x) + more)

class Buf541(ns.Part):
    def init(self): self.on = None
    def eval(self):
        on = self.v(1) == 0 and self.v(19) == 0
        sw, self.on = on != self.on, on
        for i in range(8):
            self.out(18 - i, self.v(2 + i) if on else None, self.onoff(on) if sw else None)

class Cmp688(ns.Part):
    def eval(self):
        p = [self.v(n) for n in (2, 4, 6, 8, 11, 13, 15, 17)]
        q = [self.v(n) for n in (3, 5, 7, 9, 12, 14, 16, 18)]
        differ = any(a is not None and b is not None and a != b for a, b in zip(p, q))
        self.out(19, 1 if self.v(1) != 0 or differ else None if None in p or None in q else 0)

class Dec145(ns.Part):
    def eval(self):
        x = [self.v(p) for p in (15, 14, 13, 12)]
        n = None if None in x else x[0] | x[1] << 1 | x[2] << 2 | x[3] << 3
        for k, p in enumerate((1, 2, 3, 4, 5, 6, 7, 9, 10, 11)):
            self.out(p, 0 if n == k else None)

# the chip's host side (the 8 MHz grade), ns: what it asks, and what it gives as (the fast end, assumed where the
# manual gives none; the manual's figure)
ASKS = {"rs": 50.0, "cs_su": 40.0, "cs_high": 60.0, "wdata": 40.0, "whold": 10.0}
GIVES = {"rd": (20.0, 80.0), "dtack": (10.0, 70.0), "roh": (10.0, 10.0), "roff": (15.0, 60.0), "ron": (5.0, 5.0)}

class HostChip(ns.Chip):
    """The HD63484: YACC1-D's model of its memory side, with the host side added (see the top of this file)."""
    D = tuple(range(17, 33))
    def init(self):
        super().init()
        self.cs = 1; self.h2 = None; self.state = "idle"; self.t_rise = -1e9; self.t_fall = None
        self.read_word = 0xA55A; self.writes = []; self.widths = []
        self.out(10, None, 0); self.out(11, None, 0)
        for p in self.D:
            self.out(p, None, 0)
    def gives(self, k):
        return GIVES[k][0 if self.s.hostfast else 1]
    def eval(self):
        self.host()
        super().eval()
    def host(self):
        s, t = self.s, self.s.t
        cs, ck = self.v(4), self.v(50)
        was, self.cs = self.cs, cs
        w2, self.h2 = self.h2, ck
        nets = {p: s.net(self.ref, p) for p in (3, 5)}
        if was == 1 and cs == 0:                        # CS falls
            self.t_fall = t; self.state = "wait"
            for p, name in ((5, "RS"), (3, "R/W")):
                st = t - s.last[nets[p]]
                s.note("chip: RS, R/W before CS", st - ASKS["rs"], "%s steady %.1f ns" % (name, st))
                if st < ASKS["rs"]:
                    s.faults.append("%.1f ns: %s moved %.1f ns before CS fell (%.0f wanted)" % (t, name, st, ASKS["rs"]))
            s.note("chip: CS high between accesses", t - self.t_rise - ASKS["cs_high"], "%.1f ns" % (t - self.t_rise))
            if t - self.t_rise < ASKS["cs_high"]:
                s.faults.append("%.1f ns: CS high only %.1f ns" % (t, t - self.t_rise))
            self.rw, self.rs = self.v(3), self.v(5)
        if cs == 0 and w2 == 0 and ck == 1:             # a rising 2CLK edge with CS low
            if self.state == "wait" and t - self.t_fall >= ASKS["cs_su"] - 1e-9:
                self.state = "taken"
            elif self.state == "data":
                self.state = "ack"
                s.drive(self.ref, 10, 0, self.gives("dtack"))
                self.t_dtack = t + self.gives("dtack")
        if cs == 0 and w2 == 1 and ck == 0 and self.state == "taken":      # the falling edge after
            self.state = "data"
            if self.rw == 1:
                for i, p in enumerate(self.D):
                    self.out(p, "X", self.gives("ron")); self.out(p, self.read_word >> i & 1, self.gives("rd"))
        if was == 0 and cs == 1:                        # CS rises: the access ends
            self.t_rise = t
            self.widths.append((self.rw, t - self.t_fall))
            for p, name in ((5, "RS"), (3, "R/W")):
                if s.last[nets[p]] > self.t_fall:
                    s.faults.append("%.1f ns: %s moved while CS was low" % (t, name))
            if self.state != "ack" or t < self.t_dtack - 1e-9:
                s.faults.append("%.1f ns: CS rose before the chip's DTACK (CS low %.1f ns; the chip had got as far as: %s)"
                                % (t, t - self.t_fall, self.state))
            else:
                s.note("chip: CS rises after DTACK", t - self.t_dtack,
                       "CS low %.1f ns, DTACK %.1f ns after its fall" % (t - self.t_fall, self.t_dtack - self.t_fall))
            if self.rw == 0:
                bits = [self.v(p) for p in self.D]
                st = min(t - s.last[s.net(self.ref, p)] for p in self.D)
                s.note("chip: write data before CS rises", st - ASKS["wdata"], "steady %.1f ns" % st)
                if st < ASKS["wdata"] or None in bits:
                    s.faults.append("%.1f ns: the chip's write data steady %.1f ns as CS rises (%r)" % (t, st, bits))
                for p in self.D:
                    s.holds.append((s.net(self.ref, p), t, ASKS["whold"], "IC1 write data"))
                self.writes.append((self.rs, None if None in bits else sum(b << i for i, b in enumerate(bits))))
            else:
                for p in self.D:
                    self.out(p, "X", self.gives("roh")); self.out(p, None, self.gives("roff"))
            s.drive(self.ref, 10, None, 20.0)
            self.state = "idle"

class Sim(ns.Sim):
    """The engine, with a release time for each line the backplane pulls up: -ACK and -WAIT rise much sooner than the
    300 ns it gives a 10k (docs/timing.md 4: 0.51 RC with the pull-up of generators/card_backplane.py)."""
    release = {"-ACK": 102.0, "-WAIT": 69.0}
    def resolve(self, net):
        if net in self.release and not [x for x in self.drv[net].values() if x is not None]:
            if self.val[net] != self.pull[net] and not self.floating.get(net):
                self.floating[net] = True
                self.post(net, ("PULL", net), None, self.release[net])
            return self.val[net] if self.val[net] is not None else self.pull.get(net)
        return super().resolve(net)

PAGE = 0xFFFF8F00 + card_video.WINDOW * 0x1000
TAV, THOLD = 60.0, 5.0                                  # the master's lines after a cycle's first edge, and after its last

class Bench:
    """The machine at the card's backplane pins: CLK and -CLK, a master's cycles, the dot clock at its own phase;
    and the ATmega328P's pins."""
    def __init__(self, tc, phase, corner, hostfast, chipd, count0=1):
        ns.Sim.count0, ns.Sim.corner = count0, corner
        s = self.s = Sim(fitted_out=ns.N.TEXT_A, joined=ns.jp4())
        s.count0, s.d, s.hostfast, s.samples = count0, chipd, hostfast, {}
        s.qb = ns.TPD[("F", "163")][2 if corner in ("min", "fall") else 3]
        for net, v in ns.N.pulls.items():
            s.pull[net] = v; s.val[net] = v
        for n in ["-ACK", "-WAIT"] + ["-IRQ%d" % k for k in range(8)]:
            s.pull[n] = 1; s.val[n] = 1
        s.parts["IC7"].q[0] = 1                         # (as YACC1-D's bench starts the -WE flip-flop)
        self.tc, self.phase = tc, phase
        for net, v in card_video.defaults().items():
            if v == 0:
                s.force(net, 0)
        for n, v in (("CLK", 0), ("-CLK", 1), ("-RES", 0), ("-MRD", 1), ("-MWR", 1), ("-IACK", 1), ("LDDATA", 1), ("RDCMD", 1),
                     ("INTREQ", 0), ("SRC", 0)):
            s.force(n, v)
        for i in range(2, 32):
            s.force("A%d" % i, 0)
        for k in range(4):
            s.force("BE%d" % k, 0)
        s.run(0.0)
        for m in s.parts.values():
            m.eval()
        self.dot = self.bus = 0                         # the next half-edge of each clock
        self.todo, self.log = [], {}
        self.at(self.edge(1) + 3.0, lambda: s.force("-RES", 1))
    def at(self, t, f):
        self.todo.append((t, len(self.todo), f))
        self.todo.sort()
    def run(self, until):
        """Run to a time: the two clocks' edges and the bench's own moves, in order."""
        s = self.s
        while True:
            td, tb = self.phase + self.dot * DOT / 2, self.bus * self.tc / 2
            tt = self.todo[0][0] if self.todo else 1e18
            t = min(td, tb, tt)
            if t > until:
                break
            s.run(t - 0.001)
            if t == tt:
                self.todo.pop(0)[2]()
            elif t == tb:
                hi = self.bus % 2 == 0
                s.force("CLK", 1 if hi else 0); s.force("-CLK", 0 if hi else 1)
                self.bus += 1
            else:
                s.force("CK", 1 if self.dot % 2 == 0 else 0)
                self.dot += 1
        s.run(until)
    def edge(self, k):
        return k * self.tc                              # the bus clock's rising edge k
    def first(self):
        """The first bus clock edge for a cycle: the dot counter has fallen into step with the chip by then."""
        return 4 + int(8 * 8 * DOT / self.tc)
    def cycle(self, k, a, be, rd, data=None, clocks=1, iack=False, late=()):
        """A cycle from the bus clock's edge k: its lines move TAV after it (the address bits in late 40 ns later
        still) and THOLD after edge k + clocks. As each clock ends -ACK and -WAIT are looked at, and at the last D."""
        s, t0, t1 = self.s, self.edge(k), self.edge(k + clocks)
        strobe = "-IACK" if iack else "-MRD" if rd else "-MWR"
        def start():
            for i in range(2, 32):
                s.force("A%d" % i, a >> i & 1, 40.0 if i in late else 0.0)
            for b in range(4):
                s.force("BE%d" % b, be >> b & 1)
            s.force(strobe, 0)
            if not rd and not iack:
                for i in range(32):
                    s.force("D%d" % i, data >> i & 1, 20.0)
        def end():
            s.force(strobe, 1)
            for i in range(32):
                s.force("D%d" % i, None)
        self.at(t0 + TAV, start); self.at(t1 + THOLD, end)
        rec = self.log[k] = {"ack": [], "wait": [], "acksteady": [], "waitsteady": [], "d": None, "dsteady": None}
        def look(j):
            def f():
                rec["ack"].append(s.val["-ACK"]); rec["wait"].append(s.val["-WAIT"])
                rec["acksteady"].append(s.t - s.last["-ACK"]); rec["waitsteady"].append(s.t - s.last["-WAIT"])
                if j == clocks - 1:
                    nets = ["D%d" % (8 * b + i) for b in ([0] if iack else [b for b in range(4) if be >> b & 1]) for i in range(8)]
                    vals = [s.val[n] for n in nets]
                    rec["d"] = None if None in vals else sum(x << i for i, x in enumerate(vals))
                    rec["dsteady"] = min(s.t - s.last[n] for n in nets)
            return f
        for j in range(clocks):
            self.at(self.edge(k + j + 1) - 0.01, look(j))
        return rec
    def finish(self, name, chk):
        s = self.s
        s.fights = {k: v for k, v in s.fights.items() if "TB" not in k[1]}      # (not the bench's own drivers)
        return ns.finish(s, name, chk)

def chip_run(tc, phase, corner, hostfast, chipd):
    """A write of +04 and a read of +00 with other masters' cycles between, at one phase of the dot clock."""
    b = Bench(tc, phase, corner, hostfast, chipd)
    s, chk, k = b.s, [], b.first()
    w = b.cycle(k, PAGE + 0x04, 0b0011, False, 0x0000C3A5, clocks=4)
    f1 = b.cycle(k + 5, 0x00000040, 0b1111, True)
    r = b.cycle(k + 7, PAGE + 0x00, 0b0011, True, clocks=4)
    f2 = b.cycle(k + 12, (PAGE + 0x04) & ~0x3000, 0b0011, True, late=(12, 13))     # (the chip's +04 for 40 ns, then window 2's)
    b.run(b.edge(k + 15))
    chip = s.parts["IC1"]
    if [rw for rw, width in chip.widths] != [0, 1]:
        chk.append("the chip saw %r, not a write and a read" % ([("read" if rw else "write") for rw, width in chip.widths],))
    if chip.writes != [(1, 0xC3A5)]:
        chk.append("the chip was written %r, not RS 1, C3A5" % (chip.writes,))
    for rw, width in chip.widths:                       # (the application note's rule for a bus that does not wait for DTACK)
        s.note("chip: CS low, over three 2CLK cycles", width - 3 * T2, "a %s: %.1f ns" % ("read" if rw else "write", width))
        if width < 3 * T2:
            chk.append("a %s's CS was low %.1f ns (%.1f wanted)" % ("read" if rw else "write", width, 3 * T2))
        s.note("CS low, against %s clocks" % ("2.5" if rw else "2"), -abs(width - (2.5 if rw else 2) * tc), "%.1f ns" % width)
    for name, rec in (("the write", w), ("the read", r)):
        if rec["ack"][0] != 0:
            chk.append("%s: no -ACK as clock 1 ends" % name)
        if rec["wait"] != [0, 0, 0, 1]:
            chk.append("%s: -WAIT as the four clocks end is %r" % (name, rec["wait"]))
        s.note("bus: -ACK low before clock 1 ends", rec["acksteady"][0], name)
        s.note("bus: -WAIT low before clock 1 ends", rec["waitsteady"][0], name)
        s.note("bus: -WAIT up before clock 4 ends", rec["waitsteady"][3], name)
    if r["d"] != 0xA55A:
        chk.append("the read gave %r, not A55A" % (r["d"] if r["d"] is None else hex(r["d"]),))
    else:                                               # (the static timing pass says what the machine needs: READ_SETUP)
        s.note("bus: the chip's word on D, over the machine's need", r["dsteady"] - READ_SETUP, "steady %.1f ns" % r["dsteady"])
        if r["dsteady"] < READ_SETUP:
            chk.append("the chip's word is on D only %.1f ns before the cycle ends (the machine needs %.0f)" % (r["dsteady"], READ_SETUP))
    for name, rec in (("a memory cycle", f1), ("a cycle whose address is the chip's on its way", f2)):
        if rec["ack"][0] != 1:
            chk.append("%s: answered" % name)
    return b.finish("chip", chk), s

def card_run(tc, phase, corner, hostfast, chipd):
    """The card's own registers and the PS/2 section: a byte loaded by the ATmega and read at +0C (FULL cleared in
    the clock after); a command written to +08 (two clocks) and taken by the ATmega; the status, the ID, the
    revision, the interrupt status; the vector."""
    b = Bench(tc, phase, corner, hostfast, chipd)
    s, chk, k = b.s, [], b.first()
    md = ["MD%d" % i for i in range(8)]
    def load(byte):
        def f():
            for i, n in enumerate(md):
                s.force(n, byte >> i & 1)
            s.force("LDDATA", 0, 100.0); s.force("LDDATA", 1, 300.0)
            for n in md:
                s.force(n, None, 400.0)
        return f
    got = {}
    def see(name, net):
        return lambda: got.__setitem__(name, s.val[net])
    b.at(b.edge(k) - 600.0, load(0x41))
    b.at(b.edge(k) - 400.0, lambda: s.force("INTREQ", 1))
    b.at(b.edge(k) + 20.0, see("FULL after the load", "FULL"))
    b.at(b.edge(k) + 20.0, see("-IRQ4 with FULL and INTREQ", "-IRQ%d" % card_video.LEVEL))
    other = lambda a: a ^ 0x3000                        # the same address in another window: A13 and A12 differ, and come late
    cycles = [("the status", b.cycle(k, PAGE + 0x08, 0b0001, True), 0x01),
              # another master's read and write whose addresses are +0C's and +08's for 40 ns on their way: FULL must
              # stay, no command may be latched
              ("another window's +0C", b.cycle(k + 1, other(PAGE + 0x0C), 0b0001, True, late=(12, 13)), None),
              ("the interrupt status", b.cycle(k + 2, PAGE + 0xF4, 0b1111, True), 1),
              ("the vector", b.cycle(k + 4, card_video.LEVEL << 2, 0, True, iack=True), card_video.WINDOW * 16),
              ("the status again", b.cycle(k + 5, PAGE + 0x08, 0b0001, True), 0x01),
              ("another window's +08", b.cycle(k + 6, other(PAGE + 0x08), 0b0001, False, 0xFFFFFFFF, late=(12, 13)), None),
              ("the PS/2 byte", b.cycle(k + 7, PAGE + 0x0C, 0b0001, True), 0x41),
              ("the ID", b.cycle(k + 9, PAGE + 0xF0, 0b0001, True), card_video.CARD_ID),
              ("the revision", b.cycle(k + 11, PAGE + 0xF0, 0b0010, True), card_video.CARD_REV)]
    b.at(b.edge(k + 7) + TAV, see("FULL after another window's +0C", "FULL"))
    b.at(b.edge(k + 7) + TAV, see("BUSY after another window's +08", "BUSY"))
    b.at(b.edge(k + 8) + tc / 2, see("FULL in the clock after the read", "FULL"))
    b.at(b.edge(k + 12) - 5.0, see("-IRQ4 after the read", "-IRQ%d" % card_video.LEVEL))     # (its pull-up needs 224 ns)
    cmd = b.cycle(k + 13, PAGE + 0x08, 0b0001, False, 0xFFFFFF05, clocks=2)
    b.at(b.edge(k + 15) + tc / 2, see("BUSY after the command", "BUSY"))
    def take():
        s.force("RDCMD", 0)
        b.at(s.t + 100.0, lambda: got.__setitem__("the command", sum((s.val[n] or 0) << i for i, n in enumerate(md))))
        s.force("RDCMD", 1, 150.0)
    b.at(b.edge(k + 16), take)
    b.at(b.edge(k + 17), see("BUSY after the ATmega took it", "BUSY"))
    b.run(b.edge(k + 18))
    for name, rec, want in cycles:
        if want is None:
            if rec["ack"] != [1]:
                chk.append("%s: answered" % name)
            continue
        if rec["ack"] != [0] or rec["d"] != want:
            chk.append("%s: -ACK %r, D %r (wanted %X)" % (name, rec["ack"], rec["d"], want))
        else:
            s.note("bus: a register on D before its cycle ends", rec["dsteady"], name)
        s.note("bus: -ACK low before clock 1 ends", rec["acksteady"][0], name)
    if cmd["ack"][0] != 0 or cmd["wait"] != [0, 1]:
        chk.append("the command: -ACK %r, -WAIT %r as its two clocks end" % (cmd["ack"], cmd["wait"]))
    want = {"FULL after the load": 1, "-IRQ4 with FULL and INTREQ": 0, "FULL after another window's +0C": 1,
            "BUSY after another window's +08": 0, "FULL in the clock after the read": 0,
            "-IRQ4 after the read": 1, "BUSY after the command": 1, "the command": 0x05, "BUSY after the ATmega took it": 0}
    chk += ["%s: %r (wanted %r)" % (n, got.get(n), x) for n, x in want.items() if got.get(n) != x]
    if s.parts["IC1"].widths:
        chk.append("CS moved: %r" % (s.parts["IC1"].widths,))
    return b.finish("card", chk), s

BUS_CORNERS = (("the chip and the parts slow", False, "DMAX", "max"), ("the chip and the parts fast", True, "DMIN", "min"),
               ("the chip slow, the parts fast", False, "DMAX", "min"), ("the chip fast, the parts slow", True, "DMIN", "max"),
               ("rising edges fast, falling slow", False, "DMAX", "rise"), ("rising edges slow, falling fast", False, "DMAX", "fall"))

def bus_side(card):
    """Returns [(name, runs, the faults of the first phases that failed)] and the worst of each margin, by name."""
    ns.N = adapter(card)
    out, worst = [], {}
    def keep(s, where):
        for key, (slack, what) in s.margin.items():
            if key not in worst or slack < worst[key][0]:
                worst[key] = (slack, "%s: %s" % (where, what))
    for tc, mhz in ((1000.0 / 3, 3), (250.0, 4)):
        for cn, hostfast, d, corner in BUS_CORNERS:
            bad, n, ph = [], 0, 0.0
            while ph < T2:
                faults, s = chip_run(tc, ph, corner, hostfast, getattr(ns, d))
                n += 1
                keep(s, "%d MHz, %s, phase %.0f ns" % (mhz, cn, ph))
                if faults:
                    bad += ["phase %.0f ns: %s" % (ph, x) for x in faults[:3]]
                ph += STEP
            out.append(("the chip's cycle at %d MHz, %s" % (mhz, cn), n, bad))
            bad = []
            for ph in (0.0, T2 / 3, 2 * T2 / 3):
                faults, s = card_run(tc, ph, corner, hostfast, getattr(ns, d))
                keep(s, "%d MHz, %s" % (mhz, cn))
                bad += faults[:3]
            out.append(("the registers and the PS/2 section at %d MHz, %s" % (mhz, cn), 3, bad))
    return out, worst

SHOW = ("chip:", "bus:", "GAL", "74ACT74", "74HCT74", "74HCT574", "CS low", "outputs overlapping")

def main():
    card = card_video.build()[0]
    fails, lines = 0, []
    mem = memory_side(card)
    for name, faults in mem:
        if faults or VERBOSE:
            print("%-62s %s" % (name, "FAIL" if faults else "PASS"))
            for x in faults[:8]:
                print("    " + x)
        fails += bool(faults)
    install()
    bus, worst = bus_side(card)
    for name, n, faults in bus:
        if faults or VERBOSE:
            print("%-62s %s" % (name, "FAIL" if faults else "PASS (%d runs)" % n))
            for x in faults[:8]:
                print("    " + x)
        fails += bool(faults)
    if VERBOSE or fails:
        for key in sorted(worst):
            if key.startswith(SHOW):
                print("    %-44s %7.1f ns to spare   %s" % (key, worst[key][0], worst[key][1]))
    if fails:
        sys.exit("video card with delays: %d of %d sets of runs failed" % (fails, len(mem) + len(bus)))
    m = lambda key: worst[key][0]
    print("video card with delays: YACC1-D's %d memory-side runs on this netlist (8 corners); the bus side at %d phases of the "
          "chip's clock, 3 and 4 MHz, 6 corner sets (%d runs): CS rises %.0f ns after DTACK at the least, is low %.0f ns over "
          "three 2CLK cycles, RS and R/W %.0f ns over their 50 before it; a read's word on D %.0f ns before the cycle ends "
          "(the machine needs %.0f)"
          % (len(mem), int(T2 / STEP) + 1, sum(n for name, n, f in bus), m("chip: CS rises after DTACK"),
             m("chip: CS low, over three 2CLK cycles"), m("chip: RS, R/W before CS"),
             m("bus: the chip's word on D, over the machine's need") + READ_SETUP, READ_SETUP))

if __name__ == "__main__":
    main()
