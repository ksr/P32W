#!/usr/bin/env python3
"""The debug card (docs/debug-card.md, D64) at gate level, in a machine.

The card's netlist joins the CPU, MMU / data and memory cards (sim_sys.py)
in its slot 0; only its Pico is a model here: a script of local-bus
operations (each a few oscillator periods: the address and data out, the
strobe, the strobe back), as its firmware would run them over USB.

    python3 sim_debug.py listen IMAGE     the card captures a running program's bus
        cycles (one sample a CLK); after HALT, they are read back through
        the local bus and compared with the cycles the bus really carried
    python3 sim_debug.py dma IMAGE        DRIVE: while the program polls a word, the
        card writes it by DMA (slot 0's -BREQ, the arbiter's first
        priority); the program halts with it; then the card reads the word
        back by DMA, and the read cycle's snapshot in the capture SRAMs
        must hold it
    python3 sim_debug.py alone            only the debug and memory cards: the card
        makes CLK itself (STEP), writes RAM and reads it back
    python3 sim_debug.py xclk             the debug and CPU cards (D76): with XCLKEN off
        the CPU card keeps its own oscillator; on, its clock is XCLK -- one
        phase of the CPU's eight per host STEP, none between; with CLKRUN,
        one phase every two of the debug card's oscillator periods; and the
        CPU card's own CLOCK switch at STEP: one phase per CLOCK STEP press

Exit status 0 when every check passes.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_debug, card_mem                             # noqa: E402
import sim_sys                                          # noqa: E402
from p32sim import Sim                                  # noqa: E402

FLAG = 0x00180000                                       # the word dma polls (dma_test.asm)
SL = {name: k for k, (g, lines) in enumerate(card_debug.SLICES) for name in lines}   # line -> slice

def slice_bits(lines, values):
    """The bytes for each slice holding these lines: {slice: byte}."""
    out = {}
    for k, (g, ls) in enumerate(card_debug.SLICES):
        b = 0
        hit = False
        for i, x in enumerate(ls):
            if x in values:
                b |= (values[x] & 1) << i
                hit = True
        if hit:
            out[k] = b
    return out

class Pico:
    """The debug card's Pico as its local bus sees it: GP0-7 data (through
    the 74LVC245), GP8-14 the address, GP15 -XWR, GP16 -XRD, GP17 the
    245's direction, GP18 CRESN."""
    def __init__(self, sim, ref):
        self.sim, self.ref = sim, ref
        self.out = {"GP%d" % i: None for i in range(8)}
        self.out.update({"GP%d" % (8 + i): 0 for i in range(7)})
        self.out.update({"GP15": 1, "GP16": 1, "GP17": 0, "GP18": 0})
        self.steps = iter(())
        self.result = None
    def model(self, ref, edge):
        return {} if edge else dict(self.out)
    def set(self, **kw):
        self.out.update(kw)
        self.sim._poke.add(self.ref)
    def addr(self, a):
        self.set(**{"GP%d" % (8 + i): a >> i & 1 for i in range(7)})
    # each operation is a generator of oscillator periods
    def write(self, a, v):
        self.addr(a)
        self.set(GP17=1, **{"GP%d" % i: v >> i & 1 for i in range(8)})
        yield
        self.set(GP15=0)
        yield
        yield
        self.set(GP15=1)
        yield
        self.set(GP17=0, **{"GP%d" % i: None for i in range(8)})
        yield
    def read(self, a, into):
        self.addr(a)
        self.set(GP17=0, GP16=0)
        yield
        yield
        into.append(sum((self.sim.get(self.ref, "GP%d" % i) or 0) << i for i in range(8)))
        self.set(GP16=1)
        yield

class DebugSim(sim_sys.SystemSim):
    def __init__(self, image, mode):
        dbg, G, role = card_debug.build()
        self.mode = mode
        super().__init__(image, None, 0, memcard=True, extra=[("DBG", dbg, G)])
        s = self.sim
        self.pico = Pico(s, "DBG.A1")
        s.models["DBG.A1"] = self.pico.model
        s._force("DBG.DRIVE", 1 if mode == "dma" else 0)
        s._force("DBG.OSC", 0)
        self.bus_log = []                               # (A, BE, strobe, D) at each cycle's end, as the bus had them
        self.script = self.plan()
        self.done = False

    # ---- the Pico's work, a period at a time
    def run_script(self):
        try:
            next(self.script)
        except StopIteration:
            self.done = True

    def plan(self):
        p = self.pico
        for _ in range(4):
            yield
        p.set(GP18=1)                                   # the card out of reset
        yield
        if self.mode == "listen":
            yield from p.write(0x41, 0)                 # CTRL1: the sample clock = CLK, not live
            yield from p.write(0x42, 0xFF)              # post-trigger: 1 sample (65536 - 1)
            yield from p.write(0x43, 0xFF)
            yield from p.write(0x4C, 0)                 # the capture address 0
            yield from p.write(0x45, 0)                 # ARM
            while True:                                 # (the program runs; stop() reads back)
                yield
        else:
            yield from self.dma_plan()
            while True:
                yield

    def dma_plan(self):
        p = self.pico
        for _ in range(24 * 60):                        # (let the program start polling)
            yield
        yield from p.write(0x41, 0x40)                  # CTRL1: DMA write
        regs = dict({"A%d" % i: FLAG >> i & 1 for i in range(2, 32)}, BE0=1, BE1=1, BE2=1, BE3=1)
        regs.update({"-MRD": 1, "-MWR": 0, "-IACK": 1})
        regs.update({"D%d" % i: 0x5A5A1234 >> i & 1 for i in range(32)})
        for k, b in sorted(slice_bits(list(regs), regs).items()):
            yield from p.write(k, b)
        yield from p.write(0x44, 0)                     # GO
        st = []
        while True:                                     # until GOQ (status bit 2) clears
            st.clear()
            yield from p.read(0x40, st)
            if not st[0] >> 2 & 1:
                break
        self.dma_wrote = True

    # ---- each oscillator period
    def hook(self, phase):
        s = self.sim
        if phase == 7:                                  # (CLK rises at this edge: the cycle's end)
            g = s.val.get
            if g("-MRD") == 0 or g("-MWR") == 0:
                a = sum((g("A%d" % i) or 0) << i for i in range(2, 32))
                be = sum((g("BE%d" % i) or 0) << i for i in range(4))
                d = sum((g("D%d" % i) or 0) << i for i in range(32))
                self.bus_log.append((a, be, "R" if g("-MRD") == 0 else "W", d))
        s._force("DBG.OSC", 1)
        self.run_script()

    def stop(self, msg, status):
        if self.mode == "listen" and msg.startswith("HALT"):
            sys.stderr.write(msg + "\n")
            sys.exit(self.readback())
        if self.mode == "dma":
            sys.stderr.write(msg + "\n")
            ok = status == 0x34 and getattr(self, "dma_wrote", False)
            sys.stderr.write("dma: the program %s the DMA write\n" % ("saw" if ok else "did NOT see"))
            sys.exit(0 if ok and self.dma_readback() else 1)
        super().stop(msg, status)

    def ticks(self, n):
        """Oscillator periods with the CPU held (after HALT): the clocks run on."""
        s = self.sim
        for _ in range(n):
            self.hook(self.phase())
            s._force(self.osc, 1)
            s.settle()
            s._force(self.osc, 0)
            s._force("DBG.OSC", 0)
            s.settle()

    def dma_readback(self):
        """A DMA read of the word the write left, and its snapshot (D101): the capture SRAMs must hold
        the read cycle as it ended -- its address, -MRD low, -ACK low, the data the memory card
        answered -- at the capture address, which then moves on by one."""
        p = self.pico
        def do(gen):
            for _ in gen:
                self.ticks(1)
        do(p.write(0x41, 0))                            # CTRL1: a DMA read (DMAWR off)
        do(p.write(0x4C, 0))                            # the capture address 0
        regs = dict({"A%d" % i: FLAG >> i & 1 for i in range(2, 32)}, BE0=1, BE1=1, BE2=1, BE3=1)
        regs.update({"-MRD": 0, "-MWR": 1, "-IACK": 1})
        for k, b in sorted(slice_bits(list(regs), regs).items()):
            do(p.write(k, b))
        do(p.write(0x44, 0))                            # GO
        st = []
        for _ in range(200):                            # until GOQ (status bit 2) clears
            st.clear()
            do(p.read(0x40, st))
            if not st[0] >> 2 & 1:
                break
        else:
            sys.stderr.write("dma: the read cycle did not finish (status %02X)\n" % st[0])
            return False
        self.ticks(24)
        lo, hi = [], []
        do(p.read(0x41, lo)); do(p.read(0x42, hi))
        ca = lo[0] | hi[0] << 8
        do(p.write(0x4C, 0))                            # back to the snapshot's address
        want = ["A%d" % i for i in range(2, 32)] + ["-MRD", "-MWR", "-ACK"] + ["D%d" % i for i in range(32)]
        vals = {}
        for k in sorted({SL[x] for x in want}):
            b = []
            do(p.read(k, b))
            for i, x in enumerate(card_debug.SLICES[k][1]):
                vals[x] = b[0] >> i & 1
        a = sum(vals["A%d" % i] << i for i in range(2, 32))
        d = sum(vals["D%d" % i] << i for i in range(32))
        ok = (ca, a, vals["-MRD"], vals["-MWR"], vals["-ACK"], d) == (1, FLAG, 0, 1, 0, 0x5A5A1234)
        sys.stderr.write("dma: a DMA read's snapshot: address %08X, -MRD %d, -ACK %d, data %08X, the capture address then %d: %s\n"
                         % (a, vals["-MRD"], vals["-ACK"], d, ca, "ok" if ok else "WRONG"))
        return ok

    def readback(self):
        p = self.pico
        def do(gen):
            for _ in gen:
                self.ticks(1)
        st = []
        do(p.write(0x41, 0x10))                         # TNOW: trigger, then 1 more sample: DONE
        self.ticks(64)
        do(p.read(0x40, st))
        if not st[0] >> 4 & 1:
            sys.stderr.write("listen: the capture did not finish (status %02X)\n" % st[0])
            return 1
        lo, hi = [], []
        do(p.read(0x41, lo)); do(p.read(0x42, hi))
        n = lo[0] | hi[0] << 8
        do(p.write(0x41, 0))                            # (TNOW off)
        do(p.write(0x4C, 0))                            # back to address 0
        want = ["A%d" % i for i in range(2, 32)] + ["BE%d" % i for i in range(4)] + ["-MRD", "-MWR", "-ACK"] + \
               ["D%d" % i for i in range(32)]
        need = sorted({SL[x] for x in want})
        cycles = []
        for addr in range(n):
            vals = {}
            for k in need:
                b = []
                do(p.read(k, b))
                for i, x in enumerate(card_debug.SLICES[k][1]):
                    vals[x] = b[0] >> i & 1
            if vals["-MRD"] == 0 or vals["-MWR"] == 0:
                a = sum(vals["A%d" % i] << i for i in range(2, 32))
                be = sum(vals["BE%d" % i] << i for i in range(4))
                d = sum(vals["D%d" % i] << i for i in range(32))
                cycles.append((a, be, "R" if vals["-MRD"] == 0 else "W", d))
            do(p.write(0x47, 0))                        # CSTEP: the next sample
        # the capture began at ARM: find its first cycle among the bus's, then every one must follow
        start = next((o for o in range(len(self.bus_log)) if self.bus_log[o:o + len(cycles)] == cycles), None)
        if start is None or not cycles:
            ref = self.bus_log[:len(cycles)]
            bad = next((i for i, (x, y) in enumerate(zip(cycles, ref)) if x != y), 0)
            sys.stderr.write("listen: %d cycles captured, %d on the bus, not in sequence; e.g. %s / %s\n"
                             % (len(cycles), len(self.bus_log), cycles[bad:bad + 1], ref[bad:bad + 1]))
            return 1
        if start + len(cycles) != len(self.bus_log):
            sys.stderr.write("listen: the capture missed the last %d cycles\n" % (len(self.bus_log) - start - len(cycles)))
            return 1
        sys.stderr.write("listen: %d samples, %d bus cycles captured, each as the bus carried it\n" % (n, len(cycles)))
        return 0

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("listen", "dma", "alone", "xclk"):
        sys.exit(__doc__)
    mode = sys.argv[1]
    if mode == "alone":
        sys.exit(alone())
    if mode == "xclk":
        sys.exit(xclk())
    DebugSim(sys.argv[2], mode).run()

# ---------------------------------------------------------------- alone: the debug and memory cards only
def alone():
    dbg, Gd, rd = card_debug.build()
    mem, Gm, rm = card_mem.build()
    card = sim_sys.System([("DBG", dbg), ("MEM", mem)])
    s = Sim(card, list(Gd.values()) + list(Gm.values()))
    for n in ["-ACK", "-WAIT", "-LOCK", "-FAST"] + ["-IRQ%d" % i for i in range(8)]:   # (the backplane's pull-ups)
        s.pull[n] = 1
    for j in ("WIN0", "WIN1"):
        s._force("MEM." + j, 0)
    pico = Pico(s, "DBG.A1")
    s.models["DBG.A1"] = pico.model
    s._force("DBG.DRIVE", 1)
    s._force("DBG.OSC", 0)
    s.settle()
    def tick():
        s._force("DBG.OSC", 1); s.settle()
        s._force("DBG.OSC", 0); s.settle()
    def do(gen):
        for _ in gen:
            tick()
    def step():                                         # one CLK period (STEP), and its eight phases
        do(pico.write(0x46, 0))
        for _ in range(10):
            tick()
    for _ in range(4):
        tick()
    pico.set(GP18=1)
    tick()
    notes, bad = [], []
    def status():
        b = []
        do(pico.read(0x40, b))
        return b[0]
    irqs = lambda: sum((s.val.get("-IRQ%d" % i) or 0) << i for i in range(8))
    # the reset (D101): GO and ARM, set, are cleared while the Pico holds CRESN low, and so is STEP's flip-flop
    # (STEP clears itself after one period, so its clear line is what is looked at)
    do(pico.write(0x44, 0)); do(pico.write(0x45, 0))    # GO, ARM: there is no clock yet, so they stay
    before = (status() >> 2 & 1, status() >> 5 & 1, s.val.get("DBG.STEPCLRN"))
    pico.set(GP18=0); tick()
    held = s.val.get("DBG.STEPCLRN")
    pico.set(GP18=1); tick()
    after = (status() >> 2 & 1, status() >> 5 & 1, s.val.get("DBG.STEPCLRN"))
    if before != (1, 1, 1) or held != 0 or after != (0, 0, 1):
        bad.append("the reset: GO, ARM, STEP's clear were %s, in reset the clear %s, after it %s" % (before, held, after))
    notes.append("CRESN cleared GO, ARM and STEP")
    # the interrupt lines (D101): a 1 at 4E pulls its line low, in DRIVE only; LISTEN clears the register
    do(pico.write(0x4E, 0x05)); s.settle(); a = irqs()
    s._force("DBG.DRIVE", 0); s.settle(); b = irqs()
    s._force("DBG.DRIVE", 1); s.settle(); c_ = irqs()
    do(pico.write(0x4E, 0x80)); s.settle(); d = irqs()
    do(pico.write(0x4E, 0x00)); s.settle(); e = irqs()
    if (a, b, c_, d, e) != (0xFA, 0xFF, 0xFF, 0x7F, 0xFF):
        bad.append("the interrupt lines: %02X %02X %02X %02X %02X, not FA FF FF 7F FF" % (a, b, c_, d, e))
    notes.append("4E pulled -IRQ0 and -IRQ2, then -IRQ7, low; none in LISTEN")
    do(pico.write(0x40, 0x20))                          # ALONE: the card's CLK on the bus
    step()                                              # (from power-on, the first period has no rising edge)
    ADDR = 0x00100040
    def cycle(write, value=0):
        regs = dict({"A%d" % i: ADDR >> i & 1 for i in range(2, 32)}, BE0=1, BE1=1, BE2=1, BE3=1)
        regs.update({"-MRD": 0 if not write else 1, "-MWR": 0 if write else 1, "-IACK": 1, "-RES": 1, "-FAST": 1})
        regs.update({"-BREQ%d" % n: 1 for n in range(3, 11)})
        regs.update({"-BGNT%d" % n: 1 for n in range(3, 11)})
        if write:
            regs.update({"D%d" % i: value >> i & 1 for i in range(32)})
        for k, b in sorted(slice_bits(list(regs), regs).items()):
            do(pico.write(k, b))
        # CTRL0: GD (writes), GBUS, GSYS, ALONE; then one step: the strobe for a whole CLK period
        do(pico.write(0x40, (1 if write else 0) | 2 | 4 | 0x20))
        step()
        do(pico.write(0x40, 0x20))                      # every group off again
    cycle(True, 0xC0FFEE42)
    do(pico.write(0x41, 0x20))                          # LIVE: read the lines as the latches hold them
    regs = dict({"A%d" % i: ADDR >> i & 1 for i in range(2, 32)}, BE0=1, BE1=1, BE2=1, BE3=1)
    regs.update({"-MRD": 0, "-MWR": 1, "-IACK": 1, "-RES": 1, "-FAST": 0})
    regs.update({"-BREQ%d" % n: 1 for n in range(3, 11)})
    regs.update({"-BGNT%d" % n: 0 if n == 5 else 1 for n in range(3, 11)})   # (D101: slot 5 granted, the 4 MHz setting)
    for k, b in sorted(slice_bits(list(regs), regs).items()):
        do(pico.write(k, b))
    do(pico.write(0x40, 2 | 4 | 0x20))                  # the bus cycle's group (a read), alone
    step()
    sysl = {n: s.val.get(n) for n in ["-FAST"] + ["-BGNT%d" % n for n in range(3, 11)]}
    if sysl != dict({"-BGNT%d" % n: 0 if n == 5 else 1 for n in range(3, 11)}, **{"-FAST": 0}):
        bad.append("the system lines alone: %s" % sysl)
    notes.append("alone it drove -BGNT5 and -FAST low, the other grants high")
    got = 0
    for k in sorted({SL["D%d" % i] for i in range(32)}):
        b = []
        do(pico.read(k, b))
        for i, x in enumerate(card_debug.SLICES[k][1]):
            if x.startswith("D"):
                got |= (b[0] >> i & 1) << int(x[1:])
    ok = got == 0xC0FFEE42
    # the status display (D100): a write to 4D shows on the two TIL311s, the high digit and the low one,
    # and stays there through other writes on the local bus
    hi, lo = ("DBG." + r for r in rd["display"])
    shown = []
    for v in (0xA5, 0x3C):
        do(pico.write(0x4D, v))
        do(pico.write(0x42, v ^ 0xFF))                  # (another register: the display's latches hold)
        s.settle()
        shown.append((s.state[hi]["digit"] << 4 | s.state[lo]["digit"]) if None not in (s.state[hi]["digit"], s.state[lo]["digit"]) else None)
    dok = shown == [0xA5, 0x3C]
    sys.stderr.write("alone: wrote C0FFEE42 at %08X with the card's own clock, read back %08X: %s; "
                     "the status display showed %s: %s; %s\n"
                     % (ADDR, got, "ok" if ok else "WRONG",
                        " then ".join("--" if x is None else "%02X" % x for x in shown), "ok" if dok else "WRONG",
                        "; ".join(notes) if not bad else "WRONG: " + "; ".join(bad)))
    return 0 if ok and dok and not bad else 1

# ---------------------------------------------------------------- xclk: the debug card clocks the CPU card (D76)
def xclk():
    import card_cpu
    cpu, Gc, rc = card_cpu.build()
    dbg, Gd, rd = card_debug.build()
    s = Sim(sim_sys.System([("CPU", cpu), ("DBG", dbg)]), list(Gc.values()) + list(Gd.values()))
    for n in ("CPU.OSC24", "CPU.OSC32", "CPU.STEPSW", "STALL", "FAULT", "PWROK"):
        s._force(n, 0)
    s.pull["-FAST"] = 1
    pico = Pico(s, "DBG.A1")
    s.models["DBG.A1"] = pico.model
    s._force("DBG.DRIVE", 0)                            # (LISTEN: XCLK is the card's own line, D76)
    s._force("DBG.OSC", 0)
    s.settle()
    ph = lambda: sum((s.val.get("CPU.PH%d" % i) or 0) << i for i in range(3))
    def tick(cpu_osc=False):                            # one of the debug card's oscillator periods; the
        s._force("DBG.OSC", 1)                          # CPU card's own oscillator with it if asked
        if cpu_osc:
            s._force("CPU.OSC24", 1)
        s.settle()
        s._force("DBG.OSC", 0); s._force("CPU.OSC24", 0); s.settle()
    def do(gen):
        for _ in gen:
            tick()
    errs = []
    for _ in range(4):
        tick()
    pico.set(GP18=1)                                    # the card out of reset
    tick()
    # 1. XCLKEN off: the CPU card runs on its own oscillator, whatever XCLK does
    do(pico.write(0x41, 0x02))                          # CTRL1: CLKRUN (XCLK would toggle if enabled)
    p0 = ph()
    for _ in range(16):
        tick(cpu_osc=True)
    if s.val.get("-XCLKEN") != 1 or (ph() - p0) % 8 != 0:
        errs.append("XCLKEN off: -XCLKEN %s, the CPU's phase %d -> %d after 16 of its own edges"
                    % (s.val.get("-XCLKEN"), p0, ph()))
    # 2. XCLKEN on, stepping: one phase per STEP, none between
    do(pico.write(0x41, 0x80))                          # CTRL1: XCLKEN, CLKRUN off
    for _ in range(4):
        tick()
    p0 = ph()
    for k in range(1, 11):
        do(pico.write(0x46, 0))                         # STEP
        for _ in range(6):
            tick(cpu_osc=True)                          # (its own oscillator runs on: ignored)
        if ph() != (p0 + k) % 8:
            errs.append("STEP %d: the CPU's phase %d, expected %d" % (k, ph(), (p0 + k) % 8))
            break
    # 3. CLKRUN: XCLK at half the debug card's oscillator
    p0 = ph()
    do(pico.write(0x41, 0x82))                          # CTRL1: XCLKEN, CLKRUN
    p1 = ph()
    for _ in range(32):
        tick()
    if (ph() - p1) % 8 != 0:                            # 16 edges in 32 periods: twice round the eight phases
        errs.append("CLKRUN: the CPU's phase %d -> %d in 32 periods (16 edges expected)" % (p1, ph()))
    # 4. the panel's own stepping: XCLKEN off, the CLOCK switch at STEP, one phase per CLOCK STEP press
    do(pico.write(0x41, 0))                             # CTRL1: XCLKEN off
    s._force("CPU.STEPSW", 1); s.settle()
    p0 = ph()
    for k in range(1, 6):
        s._force("CPU.CSTEPBTN_RAW", 0); s.settle()     # pressed (the RC has settled: one clean edge)
        for _ in range(3):
            tick(cpu_osc=True)                          # (the oscillators run on: ignored)
        s._force("CPU.CSTEPBTN_RAW", 1); s.settle()     # released
        tick(cpu_osc=True)
        if ph() != (p0 + k) % 8:
            errs.append("CLOCK STEP %d: the CPU's phase %d, expected %d" % (k, ph(), (p0 + k) % 8))
            break
    if errs:
        sys.stderr.write("xclk: " + "; ".join(errs) + "\n")
        return 1
    sys.stderr.write("xclk: the CPU card on its own oscillator with XCLKEN off; on XCLK, one phase per STEP "
                     "(10 steps), none between; with CLKRUN, a phase every two periods; CLOCK STEP, one a press (5)\n")
    return 0

if __name__ == "__main__":
    main()
