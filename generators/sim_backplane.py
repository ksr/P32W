#!/usr/bin/env python3
"""The cards and a program on the Backplane's own netlist (Rev 1, hardware/backplane/kicad/backplane.kicad_pcb).

sim_sys.py joins the cards by their nets' NAMES and has the backplane's pull-ups typed in, so it cannot see how the
board is wired. Here the board is read from its KiCad board file, pad by pad (not from card_backplane.py or p32bus.py,
which made it), and the cards are plugged into its sockets: each card's connector pin is joined to the pad of the same
name in its slot. Nothing is joined by name; what reaches a card is what the board's copper carries to its slot. The
resistors, the termination and the supervisor that makes PWROK are the board's parts on the board's nets.

  The machine   every slot filled: 0 the debug card, 1 the CPU, 2 the MMU / data card, and in 3-9 the I/O, disk and
                video cards and four memory cards (windows 0-3), in an order that can be turned (--turn K), so that
                each sort of card is in each of those slots once in seven runs.
  The program   emulator/test/bus_test.asm. Its state is compared with p32uemu -C's instruction by instruction up to
                its DMA, which it checks itself. Then, the CPU halted, the debug card by hand: each -IRQ line from
                slot 0, -FAST and its own two clock lines, -LOCK, -WAIT, -ACK, a DMA read by -BREQ0 / -BGNT0.
  At each tick  (an eighth of a cycle) every line of the board with a card on it must have a level (a floating line
                is a fault, not a 0: p32sim reads a floating GAL input as 0), and each card must have on its pin the
                level every other card has on that signal's pin.
  Coverage      each line must have been seen at 0 and at 1, and no two lines the same throughout: two that never
                differ could be swapped or shorted unseen. What was not is listed.

    python3 sim_backplane.py run [--turn K]       one machine (the test)
    python3 sim_backplane.py turns                all seven, and what they cover together
    python3 sim_backplane.py static               the board as a netlist: its power, spares, termination, pull-ups
    python3 sim_backplane.py faults [NAME ...]    the faults below planted in the board read: each must be noticed

What it cannot see: how the pads are joined (the copper is pcb-check's; here a pad's net is what the board file says);
that a pad's name is the pin the part really has there (a mirrored footprint); a card's own copper; time, resistor and
capacitor values, reflections, ground bounce, the supply's current; the supervisor's threshold and delay (it is played:
low, then let go); one supply pin of a socket open among many (static counts them). bus_test.asm and the hand part
reach what they reach: run prints what stayed still.
"""
import copy, os, re, sys, subprocess, tempfile, struct, io

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_cpu, card_debug, card_disk, card_io, card_mem, card_mmu, card_video   # noqa: E402
import sim_cpu, sim_sys                                                  # noqa: E402
from sim_debug import Pico                                               # noqa: E402
from p32net import CONNECTORS, DEV                                       # noqa: E402
from p32bus import pinnet, SIGNALS                                       # noqa: E402  (what a CARD calls its pins)
from p32sim import Sim, SimError                                         # noqa: E402

ROOT = os.path.join(HERE, "..")
BOARD = os.path.join(ROOT, "hardware", "backplane", "kicad", "backplane.kicad_pcb")
TEST = os.path.join(ROOT, "emulator", "test")

def once(mod):
    """a card's build() is slow (its GALs' equations are minimised): made once, a copy each time after"""
    orig, kept = mod.build, []
    def build():
        if not kept:
            kept.append(orig())
        c, *rest = kept[0]
        return (copy.deepcopy(c),) + tuple(rest)
    mod.build = build
for _m in (card_cpu, card_debug, card_disk, card_io, card_mem, card_mmu, card_video):
    once(_m)

class Noticed(Exception):
    """the first thing that is not as it should be"""

# ---------------------------------------------------------------- the board, as its KiCad file has it
def parse(path=BOARD):
    """{ref: (value, {pad: net or None})}"""
    t = open(path).read()
    out = {}
    for f in re.split(r"\n\t\(footprint ", t)[1:]:
        f = f.split("\n\t(segment")[0].split("\n\t(gr_")[0].split("\n\t(zone")[0]
        ref = re.search(r'\(property "Reference" "([^"]+)"', f).group(1)
        val = re.search(r'\(property "Value" "([^"]*)"', f).group(1)
        pads = {}
        for m in re.finditer(r'\n\t\t\(pad "([^"]*)"(.*?)\n\t\t\)', f, re.S):
            if not m.group(1):
                continue
            n = re.search(r'\(net "([^"]*)"\)', m.group(2))
            n = n.group(1) if n else None
            if n is not None and n.startswith("unconnected-"):
                n = None
            if pads.setdefault(m.group(1), n) != n:
                raise Noticed("%s pad %s is on %s and %s" % (ref, m.group(1), pads[m.group(1)], n))
        if ref in out:
            raise Noticed("two parts named %s" % ref)
        out[ref] = (val, pads)
    return out

def device(ref, val):
    """the part's device (its pad numbers and pin names, p32net.DEV)"""
    if re.match(r"S\d+J\d$", ref):
        return "DIN96F"
    for pre, dev in (("RN", "RNET9"), ("R", "R"), ("U", "TL7705A"), ("D", "LED"), ("P", "LUG_M4"), ("H", "MH_M3")):
        if re.match(pre + r"\d+$", ref):
            return dev
    if ref.startswith("C"):
        return {"100nF": "C100N", "150pF": "C150P", "10uF 16V": "C10U", "1000uF 16V": "C1000U"}[val]
    raise Noticed("a part this does not know: %s (%s)" % (ref, val))

class Board:
    """The board's netlist, which the planted faults edit. pin[(ref, pin name)] = net or None"""
    def __init__(self, path=BOARD):
        self.parts, self.pin = {}, {}
        for ref, (val, pads) in parse(path).items():
            dev = device(ref, val)
            name = {num: p for p, (num, t) in DEV[dev]["pins"].items()}
            self.parts[ref] = (dev, "", val)
            for pad, net in pads.items():
                if pad not in name:
                    raise Noticed("%s (%s) has a pad %s the part has not" % (ref, dev, pad))
                self.pin[(ref, name[pad])] = net
            if set(name) != set(pads):
                raise Noticed("%s (%s): pads %s missing" % (ref, dev, sorted(set(name) - set(pads))[:4]))
        self.vcc, self.gnd = self.pin[("P1", "1")], self.pin[("P2", "1")]
    def sock(self, slot, conn):
        return "S%d%s" % (slot, conn)
    def on(self, net):
        return [rp for rp, n in self.pin.items() if n == net]
    # the edits
    def open(self, slot, conn, pin):
        self.pin[(self.sock(slot, conn), pin)] = None
    def swap(self, slot, conn, a, b):
        x, y = (self.sock(slot, conn), a), (self.sock(slot, conn), b)
        self.pin[x], self.pin[y] = self.pin[y], self.pin[x]
    def swap_nets(self, a, b, keep=()):
        """two lines crossed everywhere but at the parts in keep"""
        for rp, n in list(self.pin.items()):
            if n in (a, b) and rp[0] not in keep:
                self.pin[rp] = b if n == a else a
    def short(self, a, b):
        for rp in self.on(b):
            self.pin[rp] = a
    def off(self, ref):
        for rp in [rp for rp in self.pin if rp[0] == ref]:
            del self.pin[rp]
        del self.parts[ref]
    def move(self, ref, pin, net):
        self.pin[(ref, pin)] = net

# ---------------------------------------------------------------- the cards in their slots
DEVCARDS = ["MEM", "IO", "DSK", "VID", "MEM1", "MEM2", "MEM3"]           # slot 3 + (k + turn) % 7
def slots(turn):
    s = {"DBG": 0, "CPU": 1, "MMU": 2}
    s.update({tag: 3 + (k + turn) % 7 for k, tag in enumerate(DEVCARDS)})
    return s

class Joined:
    """The board and the cards as one netlist (what p32sim.Sim needs). A net with a line of the board in it has that
    line's name ("L_" of the local link's dropped), VCC and GND what the power lugs are on; a card's own is TAG.NET."""
    def __init__(self, board, cards, where):
        self.parts, self.nets, self.nc = {}, {}, set()
        self.conn = {}                                  # (tag, conn, pin) -> the joined net
        parent = {}
        def find(x):
            parent.setdefault(x, x)
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x
        pins = {}                                       # node -> [(ref, pin)]
        loose = 0
        bnode = {}
        for (ref, pin), net in board.pin.items():
            if net is None:
                loose += 1
                node = ("loose", "%s.%s" % (ref, pin))
            else:
                node = ("BP", net)
            bnode[(ref, pin)] = node
            if not re.match(r"S\d+J\d$", ref):
                pins.setdefault(node, []).append(("BP." + ref, pin))
        for ref, part in board.parts.items():
            if not re.match(r"S\d+J\d$", ref):
                self.parts["BP." + ref] = part
        plugs = []
        for tag, card in cards:
            for ref, part in card.parts.items():
                if ref not in CONNECTORS:
                    self.parts["%s.%s" % (tag, ref)] = part
            for net, ps in card.nets.items():
                for r, p in ps:
                    if r in CONNECTORS:
                        b = bnode[(board.sock(where[tag], r), p)]
                        parent[find((tag, net))] = find(b)
                        plugs.append(((tag, r, p), (tag, net)))
                    else:
                        pins.setdefault((tag, net), []).append(("%s.%s" % (tag, r), p))
        groups = {}
        for node in set(pins) | set(bnode.values()) | {n for _, n in plugs}:
            groups.setdefault(find(node), []).append(node)
        v, g = find(("BP", board.vcc)), find(("BP", board.gnd))
        if v == g:
            raise Noticed("+5 V and ground are one net")
        name = {}
        for root, nodes in groups.items():
            if root == v:
                name[root] = "VCC"
            elif root == g:
                name[root] = "GND"
            else:
                bp = sorted(n for t, n in nodes if t == "BP")
                own = sorted("%s.%s" % n for n in nodes if n[0] not in ("BP", "loose"))
                nm = re.sub(r"^L_", "", bp[0]) if bp else own[0] if own else "loose." + nodes[0][1]
                if nm in ("VCC", "GND"):                # (a board net of that name that is not the lugs')
                    nm = "loose." + nm
                name[root] = nm
            self.nets[name[root]] = [rp for n in nodes for rp in pins.get(n, [])]
        self.conn = {k: name[find(n)] for k, n in plugs}
        for tag, card in cards:                         # a card whose supply does not reach the board's has none
            for rail in ("VCC", "GND"):
                if find((tag, rail)) != (v if rail == "VCC" else g):
                    raise Noticed("the %s card's %s does not reach the board's" % (tag, rail))

# ---------------------------------------------------------------- the machine
class Machine(sim_sys.SystemSim):
    """SystemSim with the backplane's own netlist: nothing of the bus modelled, every slot filled."""
    def __init__(self, image, d1, ref, board, turn=0, dma_pc=None):
        self.board, self.where = board, slots(turn)
        self.ref, self.refline, self.dma_pc = ref, 0, dma_pc
        self.card_objs = {}
        extra = [("DBG",) + tuple(card_debug.build()[:2])]
        for k in (1, 2, 3):
            mem, Gm, rm = card_mem.build()
            for role, r in rm.items():                  # (banks 1-7 and the ROM not fitted)
                if re.match(r"ram[1-7]_", role) or role.startswith("rom"):
                    self._unfit(mem, r)
            extra.append(("MEM%d" % k, mem, Gm))
        opts = ["-V", os.devnull, "-d1", d1]            # (-V: the video card is fitted)
        super().__init__(image, None, 0, memcard=True, iocard=True, ioargs=opts, diskcard=True, extra=extra, videocard=True)
        s = self.sim
        self.pico = Pico(s, "DBG.A1")
        s.models["DBG.A1"] = self.pico.model
        s._force("DBG.DRIVE", 0)
        s._force("DBG.OSC", 0)
        for k in (1, 2, 3):                             # windows 1-3, one bank, no ROM (jumpers: fitted = 0)
            for n, v in (("WIN0", k & 1), ("WIN1", k >> 1), ("BANKS0", 0), ("BANKS1", 0), ("BANKS2", 0), ("ROMEN", 0)):
                if v == 0:
                    s._force("MEM%d.%s" % (k, n), 0)
        # the supervisor: PWROK low until it has its supply and its delay is over
        self.delay_over = False
        caps = {p: any(self._cap(r) for r, q in self.card.nets.get(s.pinnet.get(("BP.U1", p)), [])) for p in ("CT", "REF")}
        if "BP.U1" in self.card.parts:
            if not all(caps.values()):
                raise Noticed("the supervisor's capacitors: %s" % caps)
            def u1(ref, edge):
                if edge:
                    return {}
                ok = self.delay_over and all(s.get(ref, p) == v for p, v in (("VCC", 1), ("SENSE", 1), ("-RESIN", 1), ("GND", 0)))
                return {"-RESET": None if ok else 0}
            s.models["BP.U1"] = u1
            for p in ("VCC", "SENSE", "-RESIN", "GND"):
                n = s.pinnet.get(("BP.U1", p))
                s.listen.setdefault(n, set()).add("BP.U1")
        # nothing of the bus is played: the modelled slaves of sim_sys are off
        s.models["BUS"] = s.models["OTHERIRQ"] = lambda ref, edge: {}
        s._poke.update({"BUS", "OTHERIRQ"} | ({"BP.U1"} if "BP.U1" in self.card.parts else set()))
        s._force("PWROK", None)
        s.settle()
        self.setup_watch()

    @staticmethod
    def _unfit(card, ref):
        """a chip left out of its socket"""
        del card.parts[ref]
        for net in card.nets:
            card.nets[net] = [(r, p) for r, p in card.nets[net] if r != ref]
        card.nc = {(r, p) for r, p in card.nc if r != ref}

    def _cap(self, r):
        return self.card.parts.get(r, ("",))[0] in ("C10U", "C100N")

    def join(self, cards):
        self.card_objs = dict((tag, c) for tag, c in cards)
        return Joined(self.board, cards, self.where)
    def pull_up(self, *nets):
        pass                                            # (the board's resistors are parts of the netlist)
    def irq(self, ipend):
        pass

    # ---- what is watched at each tick
    def setup_watch(self):
        """groups: a signal's name as the cards have it (a radial line by its slot) -> the joined nets its pins are on"""
        self.groups = {}
        for (tag, conn, pin), net in self.card.conn.items():
            sig = pinnet(conn, pin)
            if sig in ("VCC", "GND"):
                if net != sig:
                    raise Noticed("%s %s %s: %s is on %s" % (tag, conn, pin, sig, net))
                continue
            if sig in ("-BREQ", "-BGNT"):
                sig = "%s%d" % (sig, self.where[tag])
            self.groups.setdefault(sig, {}).setdefault(net, []).append("%s %s.%s" % (tag, conn, pin))
        # (an output nothing reads, and a line of J3 no other card has, still have their level here)
        self.sigs = sorted(self.groups)
        self.one = {sig: next(iter(g)) for sig, g in self.groups.items()}
        self.many = {sig: g for sig, g in self.groups.items() if len(g) > 1}
        self.seen = {sig: 0 for sig in self.sigs}       # bit 0: seen at 0; bit 1: seen at 1
        self.hist = {sig: 0 for sig in self.sigs}       # a hash of its levels so far

    def watch(self):
        val = self.sim.val.get
        for sig in self.sigs:
            v = val(self.one[sig])
            if v is None:
                raise Noticed("%s floats (%s) at cycle %d" % (sig, ", ".join(sum(self.groups[sig].values(), []))[:90], self.cycles))
            self.seen[sig] |= 1 << v
            self.hist[sig] = hash((self.hist[sig], v))
        for sig, g in self.many.items():
            vs = {n: val(n) for n in g}
            if len(set(vs.values())) > 1:
                raise Noticed("%s is not one level: %s at cycle %d"
                              % (sig, "; ".join("%s at %s" % (v, g[n][0]) for n, v in vs.items())[:120], self.cycles))

    def hook(self, phase):
        s = self.sim
        s._force("DBG.OSC", 1)
        s.settle()
        self.watch()
        s._force("DBG.OSC", 0)

    def write_trace(self):
        pass

    def tick(self):
        want = self.pending_trace
        super().tick()
        if want and not self.pending_trace and self.refline is not None:
            buf = io.StringIO()
            self.trace, keep = buf, self.trace
            sim_cpu.CPU.write_trace(self)
            self.trace = keep
            got = buf.getvalue()
            if int(got[:8], 16) == self.dma_pc:
                self.refline = None                     # (from here the program checks itself)
                return
            must = self.ref[self.refline] if self.refline < len(self.ref) else "(the end)"
            if got != must:
                a, b = next(((a, b) for a, b in zip(got.split(), must.split()) if a != b), (got, must))
                raise Noticed("state differs at instruction %d (pc %s): %s, not %s" % (self.refline + 1, got[:8], a, b))
            self.refline += 1

    def stop(self, msg, status):
        raise Halted(msg, status)

    def run(self, limit=4000):
        for _ in range(3 * 8):                          # the supervisor's delay
            self.tick()
        if self.sim.val.get("PWROK") != 0 or self.sim.val.get("-RES") != 0:
            raise Noticed("before the supervisor lets go: PWROK %s, -RES %s" % (self.sim.val.get("PWROK"), self.sim.val.get("-RES")))
        self.delay_over = True
        self.sim._poke.add("BP.U1")
        self.sim.settle()
        if self.sim.val.get("PWROK") != 1:
            raise Noticed("the supervisor let go: PWROK %s" % self.sim.val.get("PWROK"))
        start = self.osc_ticks
        last, still = self.icount, self.osc_ticks
        while True:
            self.tick()
            if self.icount != last:
                last, still = self.icount, self.osc_ticks
            if self.osc_ticks - still > 8 * 200 or self.osc_ticks - start > 8 * limit:
                raise Noticed("the program stands still at pc %08X (%d instructions)" % (self.pc(), self.icount))

class Halted(Exception):
    def __init__(self, msg, status):
        super().__init__(msg)
        self.status = status

# ---------------------------------------------------------------- the program and what it must do
class Program:
    def __init__(self, d):
        self.dir = d
        asm = os.path.join(ROOT, "assembler", "p32asm.py")
        self.image, lst = os.path.join(d, "bus_test.bin"), os.path.join(d, "bus_test.lst")
        subprocess.run([sys.executable, asm, os.path.join(TEST, "bus_test.asm"), "-o", self.image, "-l", lst], check=True)
        m = re.search(r"^([0-9A-F]{8})  [0-9A-F]{8}\s+dma:", open(lst).read(), re.M)
        self.dma_pc = int(m.group(1), 16)
        b = bytearray(16 * 512)
        for j in range(256):
            struct.pack_into("<H", b, 512 + 2 * j, 0x5A00 + j)
        self.disk = bytes(b)
        subprocess.run(["make", "-s", "-C", sim_cpu.EMU, "p32uemu"], check=True)
        trace = os.path.join(d, "ref.trace")
        r = subprocess.run([os.path.join(sim_cpu.EMU, "p32uemu"), "-u", os.path.join(ROOT, "microcode", "p32ucode.bin"), "-C",
                            "-M", "16,2,2,2", "-V", os.path.join(d, "screen"), "-d1", self.drive(), "-s", trace, self.image],
                           stdin=subprocess.DEVNULL, capture_output=True, text=True)
        if r.returncode:
            sys.exit("sim_backplane: bus_test on p32uemu -C: case %d: %s" % (r.returncode, r.stderr.strip()))
        self.ref = open(trace).read().splitlines(True)
    def drive(self):
        f = os.path.join(self.dir, "d1.img")
        with open(f, "wb") as fh:
            fh.write(self.disk)
        return f

def one(prog, board, turn=0, hand=True):
    """a machine from power-on to the end; Noticed at the first thing wrong"""
    try:
        m = Machine(prog.image, prog.drive(), prog.ref, board, turn, prog.dma_pc)
        try:
            m.run()
        except Halted as h:
            if h.status:
                raise Noticed("the program: %s" % h)
            if m.refline is not None:
                raise Noticed("the program ended before its DMA")
        if hand:
            byhand(m)
            still, alike = coverage([m])                # each line moved, and none always as another, but what cannot be
            idle = {"%s%d" % (x, s) for x in ("-BREQ", "-BGNT") for s in range(3, 11) if s != m.where["DSK"]}
            fixed = {"-CPUIN", "-MMUIN"}
            for sig, v in still.items():
                if sig not in idle | fixed:
                    raise Noticed("%s was only ever %s" % (sig, v))
            for g in alike:
                if not (set(g) <= idle or set(g) <= fixed):
                    raise Noticed("never seen to differ: %s" % " ".join(g))
        return m
    except SimError as e:
        raise Noticed(str(e)[:160])

# ---------------------------------------------------------------- by hand, the CPU halted: the debug card's lines
SL = {name: (k, i) for k, (g, lines) in enumerate(card_debug.SLICES) for i, name in enumerate(lines)}

def byhand(m):
    s, p = m.sim, m.pico
    val = s.val.get
    def ticks(n):
        for _ in range(n):
            m.hook(m.phase())
            s._force(m.osc, 1); s.settle()
            s._force(m.osc, 0); s.settle()
    def do(gen):
        for _ in gen:
            ticks(1)
    wr = lambda a, v: do(p.write(a, v))
    def rd(a):
        out = []
        do(p.read(a, out))
        return out[0]
    def want(what, got, must):
        if got != must:
            raise Noticed("by hand: %s: %s, not %s" % (what, got, must))
    at = lambda tag, sig: val(next(n for (t_, conn, pin), n in m.card.conn.items() if t_ == tag and pinnet(conn, pin) == sig))
    s._force("DBG.DRIVE", 1)
    p.set(GP18=1)
    ticks(8)
    want("-CPUIN and -MMUIN at the debug card", (at("DBG", "-CPUIN"), at("DBG", "-MMUIN")), (0, 0))
    OD = ["-IRQ%d" % i for i in range(8)] + ["-LOCK", "-WAIT", "-FAST", "PWROK"]
    def rest(sig):                                      # (one line pulled low: the other open-drain lines stay up)
        for x in OD:
            if x != sig and val(m.one[x]) != 1:
                raise Noticed("by hand: %s is low with %s pulled low" % (x, sig))
    for n in range(8):                                  # each interrupt line, from slot 0 to the CPU's
        wr(0x4E, 1 << n)
        ticks(8)
        rest("-IRQ%d" % n)
        want("-IRQ7-0 at the CPU card with bit %d of 4E" % n, [at("CPU", "-IRQ%d" % i) for i in range(8)],
             [0 if i == n else 1 for i in range(8)])
    wr(0x4E, 0)
    # a word the program left (BUF + 12, by the disk card's DMA) read back: -BREQ0, -BGNT0, the bus cycle's lines
    addr = 0x0011000C
    regs = {"A%d" % i: addr >> i & 1 for i in range(2, 32)}
    regs.update(BE0=1, BE1=1, BE2=1, BE3=1)
    regs.update({"-MRD": 0, "-MWR": 1, "-IACK": 1})
    bytes_ = {}
    for line, v in regs.items():
        k, i = SL[line]
        bytes_[k] = bytes_.get(k, 0) | v << i
    wr(0x41, 0)
    wr(0x4C, 0)
    for k, b in sorted(bytes_.items()):
        wr(k, b)
    wr(0x44, 0)                                         # GO
    for _ in range(40):
        st = rd(0x40)
        if not st >> 2 & 1:
            break
    else:
        raise Noticed("by hand: the DMA cycle did not finish (status %02X)" % st)
    ticks(24)
    wr(0x4C, 0)
    got = {}
    for k in {SL[x][0] for x in ["D%d" % i for i in range(32)] + ["-ACK"]}:
        b = rd(k)
        got.update({line: b >> i & 1 for i, line in enumerate(card_debug.SLICES[k][1])})
    want("a DMA read's -ACK", got["-ACK"], 0)
    want("a DMA read's data", "%08X" % sum(got["D%d" % i] << i for i in range(32)), "5A075A06")
    # -ACK, -WAIT and -LOCK by open drain, at the MMU / data card that reads them
    for reg, bit, sig in ((0x40, 0x40, "-ACK"), (0x40, 0x80, "-WAIT"), (0x41, 0x01, "-LOCK")):
        wr(reg, bit)
        ticks(4)
        want("%s pulled low, at the MMU / data card" % sig, at("MMU", sig), 0)
        rest(sig)
        wr(reg, 0)
        ticks(4)
        want("%s let go" % sig, at("MMU", sig), 1)
    # XCLK: the CPU card on the debug card's clock, then back
    wr(0x41, 0x82)                                      # XCLKEN, CLKRUN
    seen = set()
    for _ in range(32):
        s._force("DBG.OSC", 1); s.settle(); m.watch()
        seen.add(at("CPU", "XCLK"))
        s._force("DBG.OSC", 0); s.settle(); m.watch()
    want("-XCLKEN and XCLK at the CPU card", (at("CPU", "-XCLKEN"), sorted(seen)), (0, [0, 1]))
    wr(0x41, 0)
    # -FAST: the CPU card's clock jumper, read by the I/O card
    n = next(n for (t_, conn, pin), n in m.card.conn.items() if t_ == "CPU" and pinnet(conn, pin) == "-FAST")
    s._force(n, 0); s.settle(); m.watch()
    want("-FAST at the I/O card with the CPU card's jumper", at("IO", "-FAST"), 0)
    s._force(n, None); s.settle()
    # the machine's reset: PWROK pulled low from slot 0, -RES on every card
    wr(0x49, 0x80)
    ticks(24)
    want("PWROK and -RES with the debug card's reset", (at("CPU", "PWROK"), [at(t_, "-RES") for t_ in ("IO", "DSK", "VID", "MMU")]), (0, [0] * 4))
    wr(0x49, 0)
    ticks(24)
    want("PWROK and -RES let go", (at("CPU", "PWROK"), at("IO", "-RES")), (1, 1))

# ---------------------------------------------------------------- what a run moved
def coverage(ms):
    """(lines never seen at both levels, sets of lines never seen to differ), over machines"""
    seen, hist = {}, {}
    for m in ms:
        for sig in m.sigs:
            seen[sig] = seen.get(sig, 0) | m.seen[sig]
    still = {sig: {1: "0", 2: "1"}.get(v, "?") for sig, v in seen.items() if v != 3}
    alike = {}
    for sig in seen:
        alike.setdefault(tuple(m.hist.get(sig) for m in ms), []).append(sig)
    return still, [sorted(g) for g in alike.values() if len(g) > 1]

def report(ms, out=sys.stderr):
    still, alike = coverage(ms)
    had, rest = set(), []                               # the sockets' signal pins a card stood on
    for m in ms:
        had |= {(m.where[t_], conn, pin) for (t_, conn, pin) in m.card.conn if pinnet(conn, pin) in SIGNALS}
    b = Board()
    every = {(s, c, p) for s in range(10) for c in CONNECTORS for p in DEV["DIN96F"]["pins"]
             if pinnet(c, p) in SIGNALS and b.pin[(b.sock(s, c), p)] is not None}
    per = {}
    for s, c, p in every - had:
        per.setdefault(s, []).append(pinnet(c, p))
    out.write("run: of the sockets' %d signal pins %d had a card's pin on them\n" % (len(every), len(every & had)))
    for s in sorted(per):
        out.write("run: slot %d, no card's pin on: %s\n" % (s, " ".join(sorted(per[s]))))
    out.write("run: %d machines, %d lines with a card on them; seen at 0 and at 1: %d\n"
              % (len(ms), len({x for m in ms for x in m.sigs}), len({x for m in ms for x in m.sigs}) - len(still)))
    for v in "01":
        l = sorted(x for x, w in still.items() if w == v)
        if l:
            out.write("run: only ever %s: %s\n" % (v, " ".join(l)))
    for g in alike:
        out.write("run: never seen to differ: %s\n" % " ".join(g))

# ---------------------------------------------------------------- the board as a netlist: what no program can show
DOC_PULLS = dict([("-ACK", "1k"), ("-LOCK", "1k"), ("-WAIT", "680"), ("PWROK", "10k"), ("-FAST", "10k"), ("-MRD", "1k"), ("-MWR", "1k"),
                  ("-IACK", "1k")] + [("-IRQ%d" % i, "2.2k") for i in range(8)] + [("D%d" % i, "10k") for i in range(32)] +
                 [("A%d" % i, "10k") for i in range(2, 32)] + [("BE%d" % i, "10k") for i in range(4)])    # docs/backplane.md

def static(b):
    """the errors; what is counted"""
    errs, notes = [], []
    sockets = [(s, j) for s in range(10) for j in CONNECTORS]
    if sorted(r for r in b.parts if r.startswith("S")) != sorted(b.sock(s, j) for s, j in sockets):
        errs.append("the sockets are not ten threes")
    # supply: every socket's every supply pin, by what the cards call it
    npow = 0
    for s, j in sockets:
        for p in DEV["DIN96F"]["pins"]:
            sig, net = pinnet(j, p), b.pin[(b.sock(s, j), p)]
            if sig in ("VCC", "GND"):
                npow += 1
                if net != (b.vcc if sig == "VCC" else b.gnd):
                    errs.append("%s %s: %s is on %s" % (b.sock(s, j), p, sig, net))
            elif net in (b.vcc, b.gnd):
                errs.append("%s %s: %s is on the supply (%s)" % (b.sock(s, j), p, sig, net))
    if (b.pin[("P3", "1")], b.pin[("P4", "1")]) != (b.vcc, b.gnd):
        errs.append("the second pair of lugs: %s, %s" % (b.pin[("P3", "1")], b.pin[("P4", "1")]))
    notes.append("%d supply pins of 30 sockets, both pairs of lugs" % npow)
    # every signal's pins on one line of its own, in the slots it reaches: also those no card of the machine has there
    lines = {}
    for s, j in sockets:
        for p in DEV["DIN96F"]["pins"]:
            sig = pinnet(j, p)
            if sig not in SIGNALS:
                continue
            scope, net = SIGNALS[sig][0], b.pin[(b.sock(s, j), p)]
            if scope == "local":
                if s <= 2:
                    lines.setdefault(sig, []).append((net, s))
            elif scope == "own":                        # (a slot's own pair; the arbiter's slot and the CPU's have none)
                if s == 0 or s >= 3:
                    lines.setdefault("%s%d" % (sig, s), []).append((net, s))
                elif net is not None:
                    errs.append("%s %s: %s is on %s" % (b.sock(s, j), p, sig, net))
            elif scope == "arb":                        # (their far ends, in slot 2; slot 0 watches all but its own)
                if s == 2 or (s == 0 and sig not in ("-BREQ0", "-BGNT0")):
                    lines.setdefault(sig, []).append((net, s))
                elif net is not None:
                    errs.append("%s %s: %s is on %s" % (b.sock(s, j), p, sig, net))
            else:
                lines.setdefault(sig, []).append((net, s))
    of = {}
    for sig, l in lines.items():
        nets = {n for n, s in l}
        if len(nets) != 1 or None in nets:
            most = max(nets, key=lambda n: sum(1 for x, s in l if x == n))
            errs.append("%s is not one line: %s" % (sig, ", ".join("slot %d on %s" % (s, n) for n, s in l if n != most)))
        for n in nets - {None}:
            of.setdefault(n, set()).add(sig)
    errs += ["%s are one line" % " and ".join(sorted(x)) for n, x in of.items() if len(x) > 1]
    notes.append("%d signals, each one line in every slot it reaches and alone on it" % len(lines))
    # the spares: J1 and J2's bused to all ten slots, J3's to 0, 1 and 2; each alone
    nsp = 0
    for j in CONNECTORS:
        for p in DEV["DIN96F"]["pins"]:
            if not pinnet(j, p).startswith("SPARE_"):
                continue
            nsp += 1
            where_ = range(10) if j != "J3" else range(3)
            nets = {b.pin[(b.sock(s, j), p)] for s in where_}
            if len(nets) != 1 or None in nets:
                errs.append("spare %s %s is on %s" % (j, p, sorted(map(str, nets))))
            elif sorted(b.on(nets.pop())) != sorted((b.sock(s, j), p) for s in where_):
                errs.append("spare %s %s has something else on it" % (j, p))
    notes.append("%d spare lines, each alone on its pin in every slot it should reach" % nsp)
    # J3 in slots 3-9: no signal pin
    for s in range(3, 10):
        for p in DEV["DIN96F"]["pins"]:
            if pinnet("J3", p) not in ("VCC", "GND") and b.pin[(b.sock(s, "J3"), p)] is not None:
                errs.append("%s %s is on %s" % (b.sock(s, "J3"), p, b.pin[(b.sock(s, "J3"), p)]))
    # the pull-ups: which line each is on, by the signal its net is at slot 3
    sig_of = {}
    for j in CONNECTORS[:2]:
        for p in DEV["DIN96F"]["pins"]:
            sig_of[b.pin[(b.sock(3, j), p)]] = pinnet(j, p)
    pulls = {}
    for ref, (dev, f, val) in b.parts.items():
        if dev not in ("R", "RNET9"):
            continue
        ends = {p: b.pin[(ref, p)] for p in DEV[dev]["pins"]}
        if b.vcc not in ends.values():
            continue
        if dev == "RNET9" and ends["COM"] != b.vcc:
            errs.append("%s's common pin is on %s" % (ref, ends["COM"]))
        for p, n in ends.items():
            if n not in (b.vcc, None) and p != "COM" and n in sig_of:
                pulls.setdefault(sig_of[n], []).append(val.split(" ")[0])
    for sig in sorted(set(DOC_PULLS) | set(pulls)):
        if pulls.get(sig, []) != ([DOC_PULLS[sig]] if sig in DOC_PULLS else []):
            errs.append("%s: pull-ups %s, not %s" % (sig, pulls.get(sig, []), DOC_PULLS.get(sig, "none")))
    notes.append("%d pull-ups, each on its line at its value" % len(pulls))
    # CLK and -CLK: 100 ohm and 150 pF in series to ground, and nothing else of the board
    for sig in ("CLK", "-CLK"):
        net = b.pin[(b.sock(9, "J2"), {"CLK": "A3", "-CLK": "A4"}[sig])]
        rs = [(r, p) for r, p in b.on(net) if r.startswith("R")]
        ok = False
        if len(rs) == 1 and b.parts[rs[0][0]][0] == "R" and b.parts[rs[0][0]][2] == "100":
            mid = b.pin[(rs[0][0], "2" if rs[0][1] == "1" else "1")]
            cs = [(r, p) for r, p in b.on(mid) if r != rs[0][0]]
            ok = len(cs) == 1 and b.parts[cs[0][0]][0] == "C150P" and b.pin[(cs[0][0], "2" if cs[0][1] == "1" else "1")] == b.gnd
        if not ok:
            errs.append("%s's termination" % sig)
    notes.append("CLK's and -CLK's terminations")
    # the supervisor
    u = {p: b.pin.get(("U1", p)) for p in DEV["TL7705A"]["pins"]}
    pw = b.pin[(b.sock(1, "J2"), "A6")]
    if (u["VCC"], u["SENSE"], u["-RESIN"], u["GND"], u["-RESET"], u["RESET"]) != (b.vcc, b.vcc, b.vcc, b.gnd, pw, None):
        errs.append("the supervisor's pins: %s" % u)
    for p, dev in (("CT", "C10U"), ("REF", "C100N")):
        on = [r for r, q in b.on(u[p]) if r != "U1"]
        if len(on) != 1 or b.parts[on[0]][0] != dev or sorted(b.pin[(on[0], q)] for q in DEV[dev]["pins"]) != sorted([u[p], b.gnd]):
            errs.append("the supervisor's %s: %s" % (p, on))
    notes.append("the supervisor")
    # the electrolytics' polarity, the LED
    for ref, (dev, f, val) in b.parts.items():
        if dev in ("C10U", "C1000U") and (b.pin[(ref, "-")] != b.gnd or b.pin[(ref, "+")] == b.gnd):
            errs.append("%s the wrong way round" % ref)
        if dev == "C100N" and b.gnd not in (b.pin[(ref, "1")], b.pin[(ref, "2")]):
            errs.append("%s has no end on ground" % ref)
    if b.pin[("D1", "K")] != b.gnd or b.pin[("D1", "A")] != b.pin[("R1", "2")] or b.pin[("R1", "1")] != b.vcc:
        errs.append("the +5 V LED")
    return errs, notes

# ---------------------------------------------------------------- planted faults
def faults():
    """(name, edit, who should notice: "run" the machine, "static" the netlist's own checks, or both tried)"""
    F = []
    def add(name, edit, by="run"):
        F.append((name, edit, by))
    J = lambda sig: next((c, p) for c in CONNECTORS for p in DEV["DIN96F"]["pins"] if pinnet(c, p) == sig)
    net = lambda b, sig, slot=3: b.pin[(b.sock(slot, J(sig)[0]), J(sig)[1])]
    for sig, slot in (("D0", 1), ("D17", 2), ("D31", 3), ("A2", 3), ("A19", 4), ("A31", 5), ("BE2", 3), ("-MRD", 4), ("-MWR", 9),
                      ("-ACK", 3), ("-WAIT", 5), ("-RES", 6), ("CLK", 2), ("CLK", 7), ("-CLK", 4), ("-IACK", 4), ("-IRQ0", 1), ("-IRQ5", 0),
                      ("-LOCK", 2), ("PWROK", 1), ("-FAST", 4), ("XCLK", 1), ("-XCLKEN", 1), ("LR0", 1), ("LR31", 2), ("RSRC2", 2),
                      ("FAULT", 1), ("FC0", 1), ("FC1", 2), ("STALL", 1), ("CCLK", 2), ("-WSTB", 2), ("LOCKREQ", 2), ("SUPER", 2),
                      ("TLBW", 2), ("-CPUIN", 0), ("-MMUIN", 2), ("-BREQ", 5), ("-BGNT", 5), ("-BREQ", 0), ("-BGNT0", 2), ("-BREQ5", 2)):
        add("open %s at slot %d" % (sig, slot), lambda b, sig=sig, slot=slot: b.open(slot, *J(sig)),
            "static" if (sig, slot) in (("D0", 1), ("-CLK", 4)) else "run")     # (no card there has the pin)
    for x, y, slot in (("D3", "D4", 3), ("D8", "D9", 1), ("A5", "A6", 3), ("A20", "A21", 4), ("A30", "A31", 9), ("BE0", "BE1", 3),
                       ("-MRD", "-MWR", 3), ("-ACK", "-WAIT", 5), ("CLK", "-CLK", 4), ("-IRQ2", "-IRQ3", 1), ("LR5", "LR6", 2),
                       ("RSRC0", "RSRC1", 2), ("FC2", "FC3", 1), ("XCLK", "-XCLKEN", 1), ("-BREQ", "-BGNT", 5), ("PCINC", "MARPC", 2)):
        add("%s and %s swapped at slot %d" % (x, y, slot), lambda b, x=x, y=y, slot=slot: b.swap(slot, J(x)[0], J(x)[1], J(y)[1]),
            "static" if (x, slot) == ("D8", 1) else "run")
    add("-BREQ5 and -BREQ6 swapped at the arbiter", lambda b: b.swap(2, "J2", J("-BREQ5")[1], J("-BREQ6")[1]))
    add("-BGNT5 and -BGNT6 swapped at the arbiter", lambda b: b.swap(2, "J2", J("-BGNT5")[1], J("-BGNT6")[1]))
    add("-BREQ0 and -BGNT0 swapped at the arbiter", lambda b: b.swap(2, "J2", J("-BREQ0")[1], J("-BGNT0")[1]))
    for x, y in (("D0", "D1"), ("D31", "A2"), ("A10", "A11"), ("A31", "BE0"), ("-MRD", "-MWR"), ("-ACK", "-WAIT"), ("CLK", "-CLK"),
                 ("-IRQ6", "-IRQ7"), ("-RES", "PWROK"), ("-LOCK", "-IRQ0"), ("XCLK", "-XCLKEN")):
        add("%s shorted to %s" % (x, y), lambda b, x=x, y=y: b.short(net(b, x), net(b, y)))
    for x, y in (("LR7", "LR8"), ("STALL", "LOCKREQ"), ("SIZE0", "SIZE1"), ("-BGNT5", "-BGNT6"), ("-BREQ5", "-BREQ6")):
        add("%s shorted to %s" % (x, y), lambda b, x=x, y=y: b.short(net(b, x, 2), net(b, y, 2)))
    add("D5 shorted to ground", lambda b: b.short(b.gnd, net(b, "D5")))
    add("-IRQ4 shorted to +5 V", lambda b: b.short(b.vcc, net(b, "-IRQ4")))
    add("+5 V shorted to ground", lambda b: b.short(b.gnd, b.vcc))
    for sig in ("-ACK", "-WAIT", "-LOCK", "PWROK", "-FAST", "-MRD", "-MWR", "-IACK"):
        add("%s's pull-up missing" % sig, lambda b, sig=sig: b.off(next(
            r for r, p in b.on(net(b, sig)) if re.match(r"R\d+$", r))))
    for sig in ("-IRQ3", "D12", "A9", "BE3"):
        def edit(b, sig=sig):
            r, p = next((r, p) for r, p in b.on(net(b, sig)) if r.startswith("RN"))
            b.move(r, p, None)
        add("%s's pull-up open" % sig, edit, "both")
    def wrong(b):
        r = next(r for r, p in b.on(net(b, "-ACK")) if re.match(r"R\d+$", r))
        for p in "12":
            if b.pin[(r, p)] == net(b, "-ACK"):
                b.move(r, p, net(b, "-LOCK"))
    add("-ACK's pull-up on -LOCK", wrong)
    add("a network's common pin on ground", lambda b: b.move(next(r for r, p in b.on(net(b, "D12")) if r.startswith("RN")), "COM", b.gnd), "static")
    add("a network's common pin open", lambda b: b.move(next(r for r, p in b.on(net(b, "D12")) if r.startswith("RN")), "COM", None))
    add("the supervisor's output on -FAST", lambda b: b.move("U1", "-RESET", net(b, "-FAST")))
    add("the supervisor's -RESIN on ground", lambda b: b.move("U1", "-RESIN", b.gnd))
    add("the supervisor's SENSE open", lambda b: b.move("U1", "SENSE", None))
    add("the supervisor's CT capacitor missing", lambda b: b.off(next(r for r, p in b.on(b.pin[("U1", "CT")]) if r != "U1")))
    add("slot 4's GND pins open", lambda b: [b.open(4, c, p) for c in CONNECTORS for p in DEV["DIN96F"]["pins"] if pinnet(c, p) == "GND"])
    # what only the board's own checks see (the machine is tried too, and may not)
    add("one VCC pin of slot 6 open", lambda b: b.open(6, "J2", "B4"), "static")
    add("a spare open at slot 8", lambda b: b.open(8, *next((c, p) for c in ("J1", "J2") for p in DEV["DIN96F"]["pins"]
                                                            if pinnet(c, p).startswith("SPARE"))), "static")
    add("CLK's termination resistor missing", lambda b: b.off("R7"), "static")
    add("-CLK's termination capacitor on +5 V", lambda b: b.move("C2", "2" if b.pin[("C2", "2")] == b.gnd else "1", b.vcc), "static")
    add("-BREQ10 open at the arbiter", lambda b: b.open(2, "J2", J("-BREQ10")[1]), "run")
    add("a pin of J3 in slot 7 on A9", lambda b: b.move(b.sock(7, "J3"), "A9", net(b, "A9")), "static")
    return F

def plant(prog, names):
    missed = []
    n = 0
    try:                                                # the control: the board as it is. Else a fault may be "noticed"
        one(prog, Board())                              # for something that is wrong without it
    except Noticed as e:
        sys.exit("faults: the undamaged board: %s" % e)
    errs = static(Board())[0]
    if errs:
        sys.exit("faults: the undamaged board: %s" % errs[0])
    for name, edit, by in faults():
        if names and not any(x in name for x in names):
            continue
        n += 1
        b = Board()
        edit(b)
        st = static(b)[0]
        try:
            one(prog, b)
            got = None
        except Noticed as e:
            got = str(e)
        sys.stderr.write("%-46s run: %s%s\n" % (name, got or "NOT noticed", "; static: %s" % st[0] if st else ""))
        sys.stderr.flush()
        if (by == "run" and not got) or (by == "static" and not st) or (by == "both" and not (got or st)):
            missed.append(name)
    sys.stderr.write("faults: %d planted, %d missed%s\n" % (n, len(missed), ": " + "; ".join(missed) if missed else ""))
    return 1 if missed else 0

def main():
    a = sys.argv[1:]
    if not a or a[0] not in ("run", "turns", "static", "faults"):
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory(prefix="bpsim-") as d:
        os.chdir(d)
        try:
            if a[0] == "static":
                errs, notes = static(Board())
                if errs:
                    raise Noticed("; ".join(errs[:6]))
                sys.stderr.write("static: %s\n" % "; ".join(notes))
            if a[0] == "faults":
                sys.exit(plant(Program(d), a[1:]))
            if a[0] in ("run", "turns"):
                prog, ms = Program(d), []
                for t in (range(7) if a[0] == "turns" else [int(a[a.index("--turn") + 1]) if "--turn" in a else 0]):
                    ms.append(one(prog, Board(), t))
                report(ms)
        except Noticed as e:
            sys.exit("sim_backplane: WRONG: %s" % e)

if __name__ == "__main__":
    main()
