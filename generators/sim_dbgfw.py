#!/usr/bin/env python3
"""The debug card's firmware (hardware/debug-card/firmware, D135) against the card's gate-level netlist.

core.c is compiled for the Mac with host.c; its local-bus operations are sim_debug.py's Pico model's, its
waits the card's oscillator periods, and each test sends it commands as the host would.

    python3 sim_dbgfw.py alone          the debug and memory cards: the probe, the reset, which cards are in,
        the display, the interrupt lines; ALONE, the clock stepped and running; words, bytes and halfwords
        written and read by GO alone; every address and data line of a memory bank; an address nobody
        answers; -RES, -FAST, -ACK, -WAIT and -LOCK from the firmware's commands
    python3 sim_dbgfw.py strobe         the local bus's limits, the firmware's reasons (D135): a STEP strobe
        held for a whole CLK period gives two periods; an ARM strobe held across an early trigger at 12 MHz
        arms a second capture over the first, which the firmware's SETUP prevents
    python3 sim_dbgfw.py dma IMAGE      the whole machine running dma_test: a DMA write the program sees and
        halts with, the word read back; a block written and read beside the CPU; a read a slave holds with
        -WAIT
    python3 sim_dbgfw.py listen IMAGE   the whole machine running hello: a trigger pattern on the console's
        address, n samples kept after it, each compared with the cycles the bus carried; a range; the 12 MHz
        mode's CLK
    python3 sim_dbgfw.py wrap           the debug card alone, at 12 MHz: the buffer goes round
    python3 sim_dbgfw.py xclk           the debug and CPU cards: XCLK refused with -RES high; on, n phases a
        XSTEP; XRUN
    python3 sim_dbgfw.py mmu            the debug and MMU / data cards: the CPU card's side of J3 and LR played
        to that card, a microcycle at a time; the guard against driving R over it, every RSRC and CRSEL
    python3 sim_dbgfw.py powercycle     the debug and CPU cards, then the debug and memory cards (D151): the machine
        switched off and on with no command between, the debug card's registers coming up at anything
    python3 sim_dbgfw.py power          the debug and CPU cards (D136): the card's 5 V seen before anything is
        driven; the machine held in reset through PWROK (MRES), on the CPU card's own clock and on XCLK

Exit status 0 when every check passes.
"""
import ctypes, math, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_debug, card_mem                             # noqa: E402
import sim_sys                                          # noqa: E402
from p32sim import Sim                                  # noqa: E402
from sim_debug import Pico                              # noqa: E402
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import dbgcard                                          # noqa: E402  (the Mac's tool, against the simulated card)

FW = os.path.join(HERE, "..", "hardware", "debug-card", "firmware", "dbgcard")
PERIOD_NS = 1000.0 / 24                                 # the card's oscillator: 24 MHz
SL = {name: (k, i) for k, (g, lines) in enumerate(card_debug.SLICES) for i, name in enumerate(lines)}

def build_lib():
    d = tempfile.mkdtemp(prefix="dbgfw-")
    lib = os.path.join(d, "libdbgcard.so")
    r = subprocess.run(["cc", "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC", "-o", lib,
                        os.path.join(FW, "core.c"), os.path.join(FW, "host.c")], capture_output=True, text=True)
    if r.returncode:
        sys.exit("sim_dbgfw: the firmware does not compile:\n" + r.stderr)
    return lib

class Firmware:
    """core.c, its hal.h bound to a simulated card: pico (sim_debug's Pico model) and tick (one oscillator
    period of the whole simulation)."""
    def __init__(self, pico, tick):
        self.pico, self.tick, self.periods, self.ops = pico, tick, 0, 0
        self.lib = ctypes.CDLL(build_lib())
        self.failure = None
        U8, U32 = ctypes.c_uint8, ctypes.c_uint32
        def guard(f):
            def g(*a):
                try:
                    return f(*a)
                except Exception as e:                  # (an exception cannot cross back into C)
                    self.failure = self.failure or e
                    return 0
            return g
        T = ctypes.CFUNCTYPE
        self.cbs = [T(None, U8, U8)(guard(self.wr)), T(U8, U8)(guard(self.rd)), T(None, ctypes.c_int)(guard(self.cresn)),
                    T(None)(guard(self.release)), T(None)(guard(self.take)), T(None, U32)(guard(self.delay)),
                    T(U32)(guard(self.us))]
        self.lib.dbg_host_bind(*self.cbs)
        self.lib.dbg_host_line.restype = ctypes.c_char_p
        self.lib.dbg_host_line.argtypes = [ctypes.c_char_p]
        self.log = []
    def step(self, n=1):
        for _ in range(n):
            self.tick()
            self.periods += 1
    def wr(self, a, v):
        self.ops += 1
        for _ in self.pico.write(a, v):
            self.step()
    def rd(self, a):
        self.ops += 1
        out = []
        for _ in self.pico.read(a, out):
            self.step()
        return out[0] & 0xFF
    def cresn(self, level):
        self.pico.set(GP18=1 if level else 0)
        self.step()
    def release(self):
        self.pico.set(GP15=1, GP16=1, GP17=0, GP18=0, **{"GP%d" % i: None for i in range(8)})
        self.step()
    def take(self):
        self.pico.set(GP15=1, GP16=1)
        self.step()
    def delay(self, ns):
        self.step(max(1, int(math.ceil(ns / PERIOD_NS))))
    def us(self):
        return int(self.periods * PERIOD_NS / 1000) & 0xFFFFFFFF
    def init(self):
        self.lib.dbg_host_init()
        self.check()
    def vcc(self, mv):
        """the card's 5 V as the Pico's ADC sees it (D136), in millivolts; -1: not known (the default)"""
        self.lib.dbg_host_vcc(ctypes.c_int(mv))
    def idle(self, n=1):
        """the firmware waiting for a command: n times round pico.c's loop (D151)"""
        for _ in range(n):
            self.lib.dbg_host_idle()
            self.step()
        self.check()
    def check(self):
        if self.failure:
            raise self.failure
    def __call__(self, line):
        r = self.lib.dbg_host_line(line.encode()).decode()
        self.check()
        self.log.append((line, r))
        return r

class Checks:
    def __init__(self, name):
        self.name, self.bad, self.notes = name, [], []
    def want(self, ok, what):
        if not ok:
            self.bad.append(what)
        return ok
    def reply(self, fw, line, pattern, what=None):
        """a command; its reply's last line must match pattern"""
        r = fw(line)
        last = r.strip().splitlines()[-1] if r.strip() else ""
        if not re.search(pattern, last):
            self.bad.append("%s: %r gave %r" % (what or line, line, r.strip()[-300:]))
        return r
    def done(self):
        if self.bad:
            sys.stderr.write("%s: WRONG: %s\n" % (self.name, "; ".join(self.bad[:6])))
            return 1
        sys.stderr.write("%s: %s\n" % (self.name, "; ".join(self.notes)))
        return 0

def words(reply):
    """the words of an RD reply, in order (None for one with no -ACK)"""
    out = []
    for ln in reply.splitlines():
        m = re.match(r"([0-9A-F]{8}):((?: (?:[0-9A-F]{8}|-{8}))+)$", ln)
        if m:
            out += [None if w.startswith("-") else int(w, 16) for w in m.group(2).split()]
    return out

# ---------------------------------------------------------------- the debug and memory cards
def alone_system(cards=("MEM",)):
    dbg, Gd, rd = card_debug.build()
    parts, gals = [("DBG", dbg)], list(Gd.values())
    if "MEM" in cards:
        mem, Gm, rm = card_mem.build()
        parts.append(("MEM", mem))
        gals += list(Gm.values())
    s = Sim(sim_sys.System(parts), gals)
    for n in ["-ACK", "-WAIT", "-LOCK", "-FAST", "-MRD", "-MWR", "-IACK"] + ["-IRQ%d" % i for i in range(8)]:   # (the backplane's pull-ups)
        s.pull[n] = 1
    if "MEM" in cards:
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
    return s, pico, tick, rd

def alone():
    s, pico, tick, rd = alone_system()
    fw = Firmware(pico, tick)
    c = Checks("alone")
    fw.step(4)
    fw.init()
    c.reply(fw, "ID", r"^OK P32W debug card firmware")
    c.reply(fw, "PROBE", r"^OK the card answers")
    c.reply(fw, "CARDS", r"^OK cpu=out mmu=out")
    st = c.reply(fw, "ST", r"^OK status=")
    c.want(" drive=1 " in st and "ctrl0=00 ctrl1=00" in st, "ST after the reset: %s" % st.strip())
    # the display: the firmware's own code while the host has not written it, then the host's byte
    hi, lo = ("DBG." + r for r in rd["display"])
    digits = lambda: (s.state[hi]["digit"] << 4 | s.state[lo]["digit"]) if None not in (s.state[hi]["digit"], s.state[lo]["digit"]) else None
    c.want(digits() == 0x00, "the display after ST: %s, not 00 (idle, no error)" % digits())
    c.reply(fw, "DISP A5", r"^OK")
    c.reply(fw, "ST", r"^OK")
    c.want(digits() == 0xA5, "DISP A5 shows %s" % digits())
    c.reply(fw, "DISP AUTO", r"^OK")
    c.notes.append("the display: the firmware's code, then A5 from the host")
    # the interrupt lines
    irqs = lambda: sum((s.val.get("-IRQ%d" % i) or 0) << i for i in range(8))
    c.reply(fw, "IRQ 05", r"^OK$")
    a = irqs()
    c.reply(fw, "IRQ 00", r"^OK")
    c.want((a, irqs()) == (0xFA, 0xFF), "IRQ 05 / 00: the lines %02X then %02X" % (a, irqs()))
    # alone: the card's clock, GO with no grant
    c.reply(fw, "RD 00100040", r"^ERR no MMU / data card: nothing grants the bus", "a read before ALONE")
    c.reply(fw, "ALONE ON", r"^OK alone: the card makes CLK; LR and J3 driven, idle$")
    c.want(s.val.get("-RES") == 1 and s.val.get("CLK") is not None, "ALONE ON: -RES %s, CLK %s" % (s.val.get("-RES"), s.val.get("CLK")))
    # with both the CPU and MMU / data cards out, the local link driven idle too (the review, item 4)
    st = fw("ST")
    lk = ([s.val.get("LR%d" % i) for i in range(32)], s.val.get("-WSTB"), s.val.get("CCLK"))
    c.want("ctrl0=3C" in st and lk == ([0] * 32, 1, 0), "ALONE ON, both cards out: %s; LR %s, -WSTB %s, CCLK %s"
           % (st.strip()[-60:], set(lk[0]), lk[1], lk[2]))
    c.reply(fw, "WR 00100040 C0FFEE42", r"^OK 1$")
    r = c.reply(fw, "RD 00100040", r"^OK 1$")
    c.want(words(r) == [0xC0FFEE42], "RD 00100040 after WR C0FFEE42: %s" % r.strip())
    blk = [0x11111111, 0x22222222, 0x33333333, 0x44444444, 0xDEADBEEF, 0x01234567, 0x89ABCDEF, 0xFFFFFFFF, 0]
    c.reply(fw, "WR 00100100 " + " ".join("%08X" % w for w in blk), r"^OK %X$" % len(blk))
    r = c.reply(fw, "RD 00100100 %X" % len(blk), r"^OK %X$" % len(blk))
    c.want(words(r) == blk, "a block of %d words written and read: %s" % (len(blk), words(r)))
    c.notes.append("words written and read by GO alone, the clock stepped by the firmware (one, then a block of %d)" % len(blk))
    # bytes and halfwords: each byte lane by itself
    c.reply(fw, "WRB 00100101 AB", r"^OK 1$")
    c.reply(fw, "WRB 00100106 CD", r"^OK 1$")
    c.reply(fw, "WRH 0010010A 5A5A", r"^OK 1$")
    c.reply(fw, "WRH 0010010C 1234", r"^OK 1$")
    r = c.reply(fw, "RD 00100100 4", r"^OK 4$")
    c.want(words(r) == [0x1111AB11, 0x22CD2222, 0x5A5A3333, 0x44441234],
           "bytes and halfwords: %s" % ["%08X" % w if w is not None else None for w in words(r)])
    c.notes.append("bytes on lanes 1 and 2, halfwords on 2-3 and 0-1, the rest of each word kept")
    # every address line of a bank (A2-A20) and every data line, by a walk
    addrs = [0x00100000] + [0x00100000 | 1 << k for k in range(2, 20)]
    vals = [(0x01010101 * (k + 1)) ^ (1 << (k + 7) % 32) for k in range(len(addrs))]
    for a_, v in zip(addrs, vals):
        fw("WR %08X %08X" % (a_, v))
    got = [words(fw("RD %08X" % a_))[0] for a_ in addrs]
    c.want(got == vals, "the address walk (A2-A19): %s" % [("%08X" % a_, "%08X" % v, None if g is None else "%08X" % g)
                                                           for a_, v, g in zip(addrs, vals, got) if v != g][:3])
    walk = [1 << b for b in range(32)]
    c.reply(fw, "WR 00100200 " + " ".join("%08X" % w for w in walk[:16]), r"^OK 10$")
    c.reply(fw, "WR 00100240 " + " ".join("%08X" % w for w in walk[16:]), r"^OK 10$")
    r = c.reply(fw, "RD 00100200 20", r"^OK 20$")
    c.want(words(r) == walk, "the data walk (D0-D31): %s" % words(r)[:4])
    c.notes.append("A2-A19 and D0-D31 walked through the memory card")
    # an address nobody answers: -ACK stays high
    r = c.reply(fw, "RD 04000000", r"^ERR 1 of 1 words had no -ACK")
    c.want(words(r) == [None], "RD 04000000 (no card there): %s" % r.strip())
    c.reply(fw, "WR 04000000 1", r"^ERR 1 of 1 writes had no -ACK")
    c.notes.append("an address with no card: no -ACK, said so")
    # the clock running (CLKRUN alone: not exercised before)
    c.reply(fw, "CLK RUN", r"^OK CLK runs")
    edges = 0
    last = s.val.get("CLK")
    for _ in range(64):
        fw.step()
        if last == 0 and s.val.get("CLK") == 1:
            edges += 1
        last = s.val.get("CLK")
    c.want(edges == 8, "CLK RUN: %d rising edges in 64 oscillator periods, not 8" % edges)
    c.reply(fw, "WR 00100300 600DF00D CAFEBABE", r"^OK 2$")
    r = c.reply(fw, "RD 00100300 2", r"^OK 2$")
    c.want(words(r) == [0x600DF00D, 0xCAFEBABE], "with CLK running: %s" % r.strip())
    c.reply(fw, "CLK STEP", r"^ERR CLK runs")
    c.reply(fw, "CLK STOP", r"^OK")
    c.reply(fw, "CLK STEP 3", r"^OK 3$")
    c.notes.append("CLK RUN: 3 MHz, and words written and read on it")
    # -RES and -FAST alone
    c.reply(fw, "RES ON", r"^OK")
    a = s.val.get("-RES")
    c.reply(fw, "RES OFF", r"^OK")
    c.reply(fw, "FAST ON", r"^OK")
    b = s.val.get("-FAST")
    c.reply(fw, "FAST OFF", r"^OK")
    c.want((a, s.val.get("-RES"), b, s.val.get("-FAST")) == (0, 1, 0, 1),
           "RES ON/OFF, FAST ON/OFF: -RES %s then %s, -FAST %s then %s" % (a, s.val.get("-RES"), b, s.val.get("-FAST")))
    # the open-drain lines (not exercised before): ODACK, ODWAIT, ODLOCK
    got = []
    for args in ("1 0 0", "0 1 0", "0 0 1", "0 0 0"):
        fw("OD " + args)
        got.append((s.val.get("-ACK"), s.val.get("-WAIT"), s.val.get("-LOCK")))
    c.want(got == [(0, 1, 1), (1, 0, 1), (1, 1, 0), (1, 1, 1)], "OD: -ACK, -WAIT, -LOCK %s" % got)
    c.notes.append("-RES, -FAST, -ACK, -WAIT and -LOCK each pulled and let go")
    # LIVE: the lines through the latches, with no CLK at all (the clock stopped)
    r = c.reply(fw, "LIVE", r"^OK( [0-9A-F]{2}){22}$")
    v = [int(x, 16) for x in r.split()[1:]]
    k, i = SL["-CPUIN"]
    k2, i2 = SL["-MMUIN"]
    c.want(len(v) == 22 and v[k] >> i & 1 and v[k2] >> i2 & 1, "LIVE: slice 21 %s, -CPUIN and -MMUIN not both high" % (v[21:] if v else v))
    # the interlock (D102), from the firmware's side: an MMU / data card in, then a CPU card
    s._force("-MMUIN", 0); s.settle()
    c.reply(fw, "CARDS", r"^OK cpu=out mmu=in")
    c.reply(fw, "ALONE ON", r"^OK alone: the card makes CLK; LR and J3 driven, idle; the MMU / data card is in")
    c.want("ctrl0=3C" in fw("ST"), "ALONE ON with an MMU / data card in: SYS, LR and J3 driven (ctrl0 3C)")
    c.reply(fw, "RD 00100040", r"^ERR the MMU / data card is in")
    s._force("-CPUIN", 0); s.settle()
    c.reply(fw, "ALONE ON", r"^ERR the CPU card is in")
    s._force("-CPUIN", None); s._force("-MMUIN", None); s.settle()
    c.reply(fw, "RD 00100040", r"^OK 1$")
    c.notes.append("with an MMU / data card in, GO alone refused; with a CPU card, ALONE refused; both out, back")
    c.reply(fw, "ALONE OFF", r"^OK")
    c.reply(fw, "RESET", r"^OK")
    c.reply(fw, "FOO", r"^ERR what\?")
    c.notes.append("%d commands, %d oscillator periods (%.1f ms of the card)" % (len(fw.log), fw.periods, fw.periods / 24000.0))
    return c.done()

# ---------------------------------------------------------------- the local bus's limits (D135)
def write_held(p, a, v, held):
    """a local-bus write whose strobe is held low for `held` oscillator periods (the Pico model's: 2)"""
    p.addr(a)
    p.set(GP17=1, **{"GP%d" % i: v >> i & 1 for i in range(8)})
    yield
    p.set(GP15=0)
    for _ in range(held):
        yield
    p.set(GP15=1)
    yield
    p.set(GP17=0, **{"GP%d" % i: None for i in range(8)})
    yield

def strobe():
    c = Checks("strobe")
    # 1. STEP: its strobe holds STEPQ's preset; held past phase 3 (eight oscillator periods), a second period
    s, pico, tick, rd = alone_system(cards=())
    def do(gen):
        for _ in gen:
            tick()
    for _ in range(4):
        tick()
    pico.set(GP18=1); tick()
    do(pico.write(0x40, 0x20))                          # ALONE
    do(pico.write(0x46, 0))                             # (from power-on: to phase 3)
    for _ in range(16):
        tick()
    edges = []
    for held in (2, 7, 10):
        n, last = 0, s.val.get("CLK")
        gen = write_held(pico, 0x46, 0, held)
        for _ in range(40):
            next(gen, None)
            tick()
            if last == 0 and s.val.get("CLK") == 1:
                n += 1
            last = s.val.get("CLK")
        edges.append(n)
    c.want(edges == [1, 1, 2], "STEP with its strobe held 2, 7 and 10 oscillator periods: %s CLK periods, not 1, 1, 2" % edges)
    c.notes.append("a STEP strobe held 2 or 7 oscillator periods gives one CLK period, held 10 it gives two: "
                   "the strobe must span an oscillator edge and end within eight")
    # 2. ARM at 12 MHz with an early trigger (TNOW): the strobe held across the edges that start the capture
    #    and end it keeps ARMQ set past ARMCLRN, and a second capture follows the first
    def captures(setup):
        s, pico, tick, rd = alone_system(cards=())
        s._force("DBG.DRIVE", 0)                        # (LISTEN)
        s.settle()
        def do(gen):
            for _ in gen:
                tick()
        for _ in range(4):
            tick()
        pico.set(GP18=1); tick()
        do(pico.write(0x41, 0x14 | (0x08 if setup else 0)))   # SCLKSEL, TNOW (and SETUP, as the firmware's ARM)
        do(pico.write(0x42, 0xFF)); do(pico.write(0x43, 0xFF))   # one sample after the trigger
        do(pico.write(0x4C, 0))
        runs, last = 0, s.val.get("DBG.ACT")
        gen = write_held(pico, 0x45, 0, 8)                 # ARM, its strobe over four 12 MHz sample edges
        for k in range(64):
            if next(gen, None) is None and setup and k == 32:
                do(pico.write(0x41, 0x14))              # SETUP off: the trigger may come now
            tick()
            if last == 0 and s.val.get("DBG.ACT") == 1:
                runs += 1
            last = s.val.get("DBG.ACT")
        st = []
        do(pico.read(0x40, st))
        return runs, st[0]
    plain, guarded = captures(False), captures(True)
    c.want(plain[0] == 2 and guarded[0] == 1 and guarded[1] >> 4 & 1,
           "ARM at 12 MHz, the strobe over 8 periods, triggering at once: %d captures (status %02X); with SETUP "
           "through the strobe %d (status %02X); expected 2 and 1, DONE" % (plain + guarded))
    c.notes.append("an ARM strobe held across an early trigger at 12 MHz runs a second capture over the first; "
                   "the firmware's SETUP through the strobe gives one")
    return c.done()

# ---------------------------------------------------------------- the whole machine
class Machine(sim_sys.SystemSim):
    """the CPU, MMU / data and memory cards running a program, the debug card in slot 0, its Pico the
    firmware's"""
    def __init__(self, image, drive, hold=False):
        dbg, G, role = card_debug.build()
        super().__init__(image, None, 0, memcard=True, extra=[("DBG", dbg, G)])
        s = self.sim
        self.pico = Pico(s, "DBG.A1")
        s.models["DBG.A1"] = self.pico.model
        s._force("DBG.DRIVE", drive)
        s._force("DBG.OSC", 0)
        self.halted, self.bus_log, self.wait_left, self.cyc_clocks = None, [], 0, 0
        self.fw = Firmware(self.pico, self.step1)
        for _ in range(3 * 8):                          # power-on: PWROK low for three microcycles (sim_cpu.run)
            self.step1()
        if not hold:                                    # (held: the CPU in reset until start(), CLK running)
            self.start()
    def start(self):
        self.sim._force("PWROK", 1)
    def step1(self):
        self.tick()
        self.sim._force("DBG.OSC", 0)
        self.sim.settle()
    def hook(self, phase):
        s = self.sim
        if phase == 7:                                  # (CLK rises at this edge: the cycle's end)
            g = s.val.get
            if g("-MRD") == 0 or g("-MWR") == 0:
                a = sum((g("A%d" % i) or 0) << i for i in range(2, 32))
                be = sum((g("BE%d" % i) or 0) << i for i in range(4))
                d = sum((g("D%d" % i) or 0) << i for i in range(32))
                self.bus_log.append((a, be, "R" if g("-MRD") == 0 else "W", d))
            if g("DBG.BUSEN") == 1:                     # the card's own cycle: a slave's -WAIT, if one is asked for
                self.cyc_clocks += 1
                if self.wait_left > 0:
                    s._force("-WAIT", 0)
                    self.wait_left -= 1
                else:
                    s._force("-WAIT", None)
        s._force("DBG.OSC", 1)
    def stop(self, msg, status):
        if self.halted is None:
            self.halted = (msg, status)
    def run_until(self, cond, limit):
        for _ in range(limit):
            if cond():
                return True
            self.step1()
        return cond()

def dma(image):
    m = Machine(image, 1)
    fw, s, c = m.fw, m.sim, Checks("dma")
    fw.init()
    m.run_until(lambda: False, 24 * 80)                 # (the program starts polling)
    c.reply(fw, "CARDS", r"^OK cpu=in mmu=in pwrok=1")
    c.reply(fw, "WR 00180000 5A5A1234", r"^OK 1$")
    m.run_until(lambda: m.halted, 24 * 400)
    c.want(m.halted and m.halted[1] == 0x34, "the program after the DMA write: %s, not HALT with 34" % (m.halted,))
    r = c.reply(fw, "RD 00180000", r"^OK 1$")
    c.want(words(r) == [0x5A5A1234], "the word read back by DMA: %s" % r.strip())
    c.notes.append("a DMA write the polling program saw and halted with (34); the word read back")
    blk = [0x1000 * k + k for k in range(1, 17)]
    c.reply(fw, "WR 00180100 " + " ".join("%08X" % w for w in blk), r"^OK 10$")
    r = c.reply(fw, "RD 00180100 10", r"^OK 10$")
    c.want(words(r) == blk, "a block of 16 written and read by DMA: %s" % words(r))
    c.notes.append("a block of 16 words written and read by DMA, each cycle granted by the arbiter")
    # -WAIT in a DMA cycle (not exercised before): a slave holds it two clocks
    m.cyc_clocks, m.wait_left = 0, 2
    r = c.reply(fw, "RD 00180104", r"^OK 1$")
    c.want(words(r) == [blk[1]] and m.cyc_clocks == 3, "a read held by -WAIT for 2 clocks: %s, the cycle %d clocks (3 expected)"
           % (r.strip(), m.cyc_clocks))
    c.notes.append("a DMA read held two clocks by a slave's -WAIT: three clocks, the word right")
    # the Mac's tool: a file's bytes loaded by DMA and saved back
    tool = dbgcard.Card(dbgcard.Sim(fw))
    data = bytes(range(0x30, 0x30 + 38))
    tool.load(data, 0x00180200)
    back = tool.save(0x00180200, 10)
    c.want(back[:len(data)] == data and back[len(data):] == b"\0\0", "the tool's load and save: %r" % back)
    c.notes.append("through the Mac's tool, 38 bytes loaded and saved back")
    c.reply(fw, "ALONE ON", r"^ERR the CPU card is in")
    c.notes.append("ALONE refused with the CPU card in")
    # (the interrupt lines are the alone test's: this simulator's interrupt sources force -IRQ0-7 from outside)
    return c.done()

def samples(dump):
    """the samples of a DUMP: {address: bytes}"""
    out = {}
    for ln in dump.splitlines():
        m = re.match(r"([0-9A-F]{4}):([0-9A-F]*)$", ln)
        if m:
            out[int(m.group(1), 16)] = bytes.fromhex(m.group(2))
    return out

def cycle_of(slices, b):
    """a sample's bus cycle (A, BE, R/W, D) from the bytes of the slices asked for, or None: no strobe"""
    v = {k: x for k, x in zip(slices, b)}
    bit = lambda name: v[SL[name][0]] >> SL[name][1] & 1
    if bit("-MRD") and bit("-MWR"):
        return None
    return (sum(bit("A%d" % i) << i for i in range(2, 32)), sum(bit("BE%d" % i) << i for i in range(4)),
            "R" if bit("-MRD") == 0 else "W", sum(bit("D%d" % i) << i for i in range(32)))

def listen(image):
    m = Machine(image, 0, hold=True)                    # (armed with the CPU in reset: its first write is caught)
    fw, s, c = m.fw, m.sim, Checks("listen")
    fw.init()
    SLICES = list(range(9)) + [20]                      # D, A, BE, the strobes; -ACK
    mask = "%06X" % sum(1 << k for k in SLICES)
    c.reply(fw, "TRIG ZERO", r"^OK")
    # the first capture through the Mac's tool (tools/dbgcard.py): its trigger, its wait, its read-back in order
    tool = dbgcard.Card(dbgcard.Sim(fw))
    n = 8
    log0 = len(m.bus_log)
    started = []
    def wait():
        if not started:
            m.start()                                   # (the CPU out of reset once the capture is armed)
            started.append(1)
        m.run_until(lambda: False, 24 * 4)
    smp, fast = tool.capture(triggers=[(0xFFFFFF00, 0xFFFFFFFC)], post=n, timeout=1e9, wait=wait)
    cyc = lambda x: None if dbgcard.line(x, "-MRD") and dbgcard.line(x, "-MWR") else (
        dbgcard.bus(x, "A", 2, 31), dbgcard.bus(x, "BE", 0, 3), "R" if not dbgcard.line(x, "-MRD") else "W",
        dbgcard.bus(x, "D", 0, 31))
    tc = cyc(smp[-n - 1]) if len(smp) > n else None
    c.want(tc is not None and tc[0] == 0xFFFFFF00 and tc[2] == "W" and tc[1] == 1 and tc[3] & 0xFF == 0x50,
           "the sample n + 1 from the end (%d samples): %s, not the console write of 'P'" % (len(smp), tc))
    got = [x for x in (cyc(y) for y in smp) if x]
    ref = m.bus_log[log0:]
    start = next((o for o in range(len(ref)) if ref[o:o + len(got)] == got), None)
    c.want(start is not None, "the captured cycles (%d) are not a run of the bus's (%d)" % (len(got), len(ref)))
    import io
    tr, vc = io.StringIO(), io.StringIO()
    dbgcard.trace(smp, tr)
    dbgcard.vcd(smp, fast, vc)
    c.want(re.search(r"\n +%d  W      FFFFFF00  1   50505050  low\n" % (len(smp) - n - 1), tr.getvalue()) is not None,
           "the tool's trace has no line for the write of 'P': %s" % tr.getvalue()[:300])
    c.want(vc.getvalue().count("\n#") == len(smp) + 1 and "$var wire 30 " in vc.getvalue() and "A [31:2]" in vc.getvalue(),
           "the tool's VCD: %d time marks for %d samples" % (vc.getvalue().count("\n#"), len(smp)))
    c.notes.append("through the Mac's tool, a trigger on the console's address: n + 1 from the end the write of "
                   "'P', the %d cycles captured the bus's, in order; its trace and VCD made" % len(got))
    # a range: the first instruction fetch from 0x40-0x5F
    c.reply(fw, "TRIG 0 OFF", r"^OK")
    c.reply(fw, "TRIG 1 RANGE 40 5F", r"^OK")
    c.reply(fw, "ARM 2", r"^OK armed")
    for _ in range(400):
        m.run_until(lambda: False, 24 * 4)
        if "done=1" in fw("CAP"):
            break
    cap = c.reply(fw, "CAP", r"^OK armed=0 running=0 done=1")
    end = int(re.search(r"ca=([0-9A-F]{4})", cap).group(1), 16)
    smp = samples(c.reply(fw, "DUMP %s %X 1" % (mask, end - 3), r"^OK 1$"))
    tc = cycle_of(SLICES, smp.get(end - 3, b"\0" * len(SLICES)))
    c.want(tc is not None and 0x40 <= tc[0] <= 0x5F, "TRIG RANGE 40 5F: the trigger's sample %s" % (tc,))
    c.notes.append("TRIG RANGE: the trigger's sample a cycle at %08X" % (tc[0] if tc else 0))
    # the 12 MHz mode (not exercised before): four samples a bus clock, CLK high in two
    c.reply(fw, "TRIG CLR", r"^OK")
    c.reply(fw, "ARM 12M", r"^OK armed: 1 samples after the trigger, sampling at 12 MHz")
    m.run_until(lambda: False, 24 * 20)
    cap = c.reply(fw, "STOP", r"^OK done: ca=")
    end = int(re.search(r"ca=([0-9A-F]{4})", cap).group(1), 16)
    smp = samples(c.reply(fw, "DUMP 400 %X 20" % (end - 0x20), r"^OK 20$"))   # (slice 10: CLK bit 1, -CLK bit 2)
    k = SL["CLK"][1]
    clk = "".join(str(smp[a][0] >> k & 1) for a in sorted(smp))
    nclk = "".join(str(smp[a][0] >> SL["-CLK"][1] & 1) for a in sorted(smp))
    ok = clk in ("1100" * 8, "0110" * 8, "0011" * 8, "1001" * 8) and all(x != y for x, y in zip(clk, nclk))
    c.want(ok, "at 12 MHz, CLK over 32 samples: %s, -CLK %s" % (clk, nclk))
    c.notes.append("12 MHz: CLK two samples high, two low, -CLK its opposite")
    return c.done()

def wrap():
    s, pico, tick, rd = alone_system(cards=())
    s._force("DBG.DRIVE", 0)
    s.settle()
    fw, c = Firmware(pico, tick), Checks("wrap")
    fw.step(4)
    fw.init()
    c.reply(fw, "TRIG ZERO", r"^OK")
    c.reply(fw, "ARM 12M", r"^OK armed")
    fw.step(2 * 32768 + 400)
    c.reply(fw, "CAP", r"^OK armed=0 running=1 done=0 trig=0 ca=.... wrapped=1")
    c.reply(fw, "DUMP", r"^ERR a capture is armed or running")
    cap = c.reply(fw, "STOP", r"^OK done: ca=.... wrapped=1")
    d = c.reply(fw, "DUMP 1 0 4", r"^OK 4$")
    c.want(len(samples(d)) == 4 and "count=4" in d, "DUMP after the wrap: %s" % d.strip()[:200])
    c.notes.append("at 12 MHz, past 32 K samples: the wrap bit set, the capture stopped, read back (%s)" % cap.strip()[3:])
    return c.done()

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
    s._force("DBG.DRIVE", 0)
    s._force("DBG.OSC", 0)
    s.settle()
    def tick():
        s._force("DBG.OSC", 1); s.settle()
        s._force("DBG.OSC", 0); s.settle()
    fw, c = Firmware(pico, tick), Checks("xclk")
    fw.step(4)
    fw.init()
    ph = lambda: sum((s.val.get("CPU.PH%d" % i) or 0) << i for i in range(3))
    c.reply(fw, "XCLK ON", r"^ERR -RES is high")         # (the CPU card's own clock stopped: RESET not yet latched)
    for _ in range(4):                                  # its oscillator: GAL PANEL's RESET takes PWROK low
        s._force("CPU.OSC24", 1); s.settle()
        s._force("CPU.OSC24", 0); s.settle()
    c.want(s.val.get("-RES") == 0, "-RES with PWROK low, the CPU card's clock running: %s" % s.val.get("-RES"))
    c.reply(fw, "XCLK ON", r"^OK the CPU card on the debug card's XCLK")
    fw.step(8)
    p0 = ph()
    c.reply(fw, "XSTEP A", r"^OK")
    c.want(ph() == (p0 + 10) % 8, "XSTEP A: the CPU's phase %d -> %d, not +10" % (p0, ph()))
    # a capture at CLK with the machine stepped: STOP gives it phases until DONE; the last write, left open by
    # a clock stopped high, is ended before the capture address is read (D135)
    c.reply(fw, "TRIG ZERO", r"^OK")
    c.reply(fw, "ARM 2", r"^OK armed")
    c.reply(fw, "XSTEP 40", r"^OK")
    c.reply(fw, "CAP", r"^OK armed=0 running=1")
    cap = c.reply(fw, "STOP", r"^OK done: ca=")
    end = int(re.search(r"ca=([0-9A-F]{4})", cap).group(1), 16)
    d = c.reply(fw, "DUMP 400", r"^OK %X$" % end)
    raw = int(fw("R 41").split()[1], 16)
    c.want(len(samples(d)) == end and end >= 8 and raw == end & 0xFF,
           "the stepped capture: %d samples read for CA %X (the counter %X after the read-back's CAPCLR? no: %X)"
           % (len(samples(d)), end, raw, end))
    c.notes.append("a capture with the machine stepped on XCLK: STOP stepped it to DONE, %d samples read back" % end)
    p1 = ph()
    c.reply(fw, "XRUN", r"^OK")
    p2 = ph()
    fw.step(32)
    c.want((ph() - p2) % 8 == 0, "XRUN: the CPU's phase %d -> %d in 32 periods (16 edges)" % (p2, ph()))
    c.reply(fw, "XSTEP", r"^ERR XCLK runs")
    c.reply(fw, "XSTOP", r"^OK")
    c.reply(fw, "XCLK OFF", r"^OK the CPU card on its own oscillator")
    c.reply(fw, "XSTEP", r"^ERR XCLK ON first")
    c.notes.append("XCLK refused while -RES is high; on with it low; XSTEP A: ten phases; XRUN: a phase per two "
                   "periods; off again")
    return c.done()

# ---------------------------------------------------------------- the card's 5 V; the machine's reset (D136)
def power():
    """The CPU and debug cards, PWROK on the backplane's pull-up. The card's 5 V seen before anything is driven,
    and lost under the firmware; the machine held in reset from the card (TSAH's bit 7 through the spare NAND,
    inverter and 7406 onto PWROK), the CPU card's -RES following it, on its own clock and on XCLK; DMA refused
    meanwhile; CRESN letting it go; at LISTEN nothing pulled, by wiring."""
    import card_cpu
    cpu, Gc, rc = card_cpu.build()
    dbg, Gd, rd = card_debug.build()
    s = Sim(sim_sys.System([("CPU", cpu), ("DBG", dbg)]), list(Gc.values()) + list(Gd.values()))
    for n in ("CPU.OSC24", "CPU.OSC32", "CPU.STEPSW", "STALL", "FAULT"):
        s._force(n, 0)
    s.pull["-FAST"] = 1
    s.pull["PWROK"] = 1                                 # (the backplane's 10k; its supervisor's output off: D78)
    pico = Pico(s, "DBG.A1")
    s.models["DBG.A1"] = pico.model
    s._force("DBG.DRIVE", 1)
    s._force("DBG.OSC", 0)
    s.settle()
    def tick():
        s._force("DBG.OSC", 1); s.settle()
        s._force("DBG.OSC", 0); s.settle()
    def cpu_clock(n=4):                                 # the CPU card's own oscillator
        for _ in range(n):
            s._force("CPU.OSC24", 1); s.settle()
            s._force("CPU.OSC24", 0); s.settle()
    v = lambda n: s.val.get(n)
    fw, c = Firmware(pico, tick), Checks("power")
    fw.step(4)
    fw.init()
    # the machine off, the Pico on USB: nothing driven, and said so
    fw.vcc(0)
    ops = fw.ops
    c.reply(fw, "PROBE", r"^ERR the card's 5 V is 0\.00 V: is the machine's power on\?")
    c.want(fw.ops == ops, "with no 5 V on the card the firmware still used the local bus (%d operations)" % (fw.ops - ops))
    fw.vcc(4980)
    c.reply(fw, "PROBE", r"^OK the card answers")
    c.reply(fw, "CARDS", r"^OK cpu=in mmu=out pwrok=1 vcc=4\.98V$")
    fw.vcc(120)                                         # (switched off under the firmware)
    c.reply(fw, "ST", r"^ERR the card's 5 V is 0\.12 V")
    c.want(v("DBG.CRESN") == 0 and v("DBG.XDIR") == 0,
           "the card let go of when its 5 V went: CRESN %s, XDIR %s" % (v("DBG.CRESN"), v("DBG.XDIR")))
    fw.vcc(5010)
    c.reply(fw, "ST", r"^OK status=.* mres=0$")         # (probed and reset again)
    # the machine's reset, on the CPU card's own clock
    cpu_clock()
    c.want(v("-RES") == 1, "-RES with PWROK high, the CPU card's clock running: %s" % v("-RES"))
    c.reply(fw, "MRES ON", r"^OK PWROK low, -RES high \(the CPU card takes PWROK at its clock's next edge")
    cpu_clock()
    c.want(v("PWROK") == 0 and v("-RES") == 0, "after MRES ON and the CPU card's clock: PWROK %s, -RES %s"
           % (v("PWROK"), v("-RES")))
    c.reply(fw, "ST", r"^OK status=.* mres=1$")
    c.reply(fw, "RD 0", r"^ERR the machine is held in reset \(MRES ON\): its arbiter grants nothing")
    # ... and on XCLK, which needs it (D76): -RES follows PWROK at the next phase
    c.reply(fw, "XCLK ON", r"^OK the CPU card on the debug card's XCLK")
    c.reply(fw, "MRES OFF", r"^OK PWROK high, -RES low$")
    c.reply(fw, "XSTEP 8", r"^OK")
    c.want(v("-RES") == 1, "-RES after MRES OFF and eight phases on XCLK: %s" % v("-RES"))
    c.reply(fw, "XCLK OFF", r"^ERR -RES is high: hold the CPU card's RESET \(MRES ON, or its button\)")
    c.reply(fw, "MRES ON", r"^OK PWROK low, -RES high")
    c.reply(fw, "XSTEP 8", r"^OK")
    c.want(v("-RES") == 0, "-RES after MRES ON and eight phases on XCLK: %s" % v("-RES"))
    c.reply(fw, "XCLK OFF", r"^OK the CPU card on its own oscillator")
    # CRESN lets it go (TSAH cleared)
    c.reply(fw, "RESET", r"^OK")
    c.want(v("PWROK") == 1, "PWROK after RESET (CRESN clears TSAH): %s" % v("PWROK"))
    # LISTEN: the firmware refuses, and the mode switch gates the bit by wiring
    s._force("DBG.DRIVE", 0); s.settle()
    c.reply(fw, "MRES ON", r"^ERR the mode switch is at LISTEN")
    c.reply(fw, "W 49 80", r"^OK")                      # (TSAH's bit 7 written as it is)
    lsn = v("PWROK")
    s._force("DBG.DRIVE", 1); s.settle()
    drv = v("PWROK")
    c.reply(fw, "W 49 00", r"^OK")
    c.want((lsn, drv, v("PWROK")) == (1, 0, 1), "PWROK with TSAH's bit 7 set at LISTEN, then at DRIVE, then the bit "
           "clear: %s, %s, %s (expected 1, 0, 1)" % (lsn, drv, v("PWROK")))
    c.notes.append("the card's 5 V: off, the probe refused with no local-bus operation; lost under the firmware, the "
                   "bus let go; back, probed again. MRES: PWROK low through TSAH's bit 7, the CPU card's -RES "
                   "following at its clock's next edge, on its own oscillator and on XCLK (XCLK switched both ways "
                   "under it); DMA refused meanwhile; CRESN lets it go; at LISTEN refused, and the bit pulls nothing")
    return c.done()

# ---------------------------------------------------------------- off and on with no command between (D151)
def wake(s, card, prefix, fill, rnd):
    """Every register of one card as it might come up: all 0, all 1 or at random (fill 0, 1, None). The clears
    act as the cards evaluate."""
    bit = lambda: rnd.getrandbits(1) if fill is None else fill
    for ref, (dev, f, v) in card.parts.items():
        x = prefix + ref
        if dev in ("74HCT273", "74HCT574", "74ACT574"):
            s.state[x] = {"q": [bit() for _ in range(8)]}
        elif dev in ("74HCT74", "74ACT74"):
            s.state[x] = {1: bit(), 2: bit()}
        elif dev == "74F161":
            s.state[x] = {"q": sum(bit() << i for i in range(4))}
        elif dev == "CY7C199":
            s.state[x] = {"mem": {}}
        elif dev == "TIL311":
            s.state[x] = {"latch": sum(bit() << i for i in range(4)), "digit": 0}
        elif x in s.gal:
            s.state[x] = {o.name: bit() for o in s.gal[x].outs.values() if o.kind == "reg"}
        else:
            continue
        s._poke.add(x)

def powercycle():
    """Once the host has used the card the Pico holds CRESN high. The machine switched off and on with no command
    between, the Pico alive on USB: the firmware sees the 5 V go while it waits (dbg_idle()) and lets go, so the
    card's registers, however they come up, are clear and drive nothing; without that they drive what they like
    (the control: the fault found 2026-11-25). Then the first command resets the card and the drive registers and
    the display are written afresh."""
    import card_cpu, random
    c = Checks("powercycle")
    rnd = random.Random(151)
    cpu, Gc, rc = card_cpu.build()
    dbg, Gd, rd = card_debug.build()
    starts = [0, 1] + [None] * 10
    def machine():
        s = Sim(sim_sys.System([("CPU", cpu), ("DBG", dbg)]), list(Gc.values()) + list(Gd.values()))
        for n in ("CPU.OSC24", "CPU.OSC32", "CPU.STEPSW", "STALL", "FAULT"):
            s._force(n, 0)
        for n in ["-FAST", "PWROK", "-ACK", "-WAIT", "-LOCK"] + ["-IRQ%d" % i for i in range(8)]:   # (the backplane's pull-ups)
            s.pull[n] = 1
        pico = Pico(s, "DBG.A1")
        s.models["DBG.A1"] = pico.model
        s._force("DBG.DRIVE", 1)
        s._force("DBG.OSC", 0)
        s.settle()
        def tick():
            s._force("DBG.OSC", 1); s.settle()
            s._force("DBG.OSC", 0); s.settle()
        def cpu_clock(n=4):
            for _ in range(n):
                s._force("CPU.OSC24", 1); s.settle()
                s._force("CPU.OSC24", 0); s.settle()
        fw = Firmware(pico, tick)
        fw.step(4)
        fw.init()
        fw.vcc(5000)
        c.reply(fw, "PROBE", r"^OK the card answers")
        return s, fw, cpu_clock
    LINES = [n for n, pins in dbg.nets.items() if any(r in ("J1", "J2", "J3") for r, p in pins)
             and n not in ("VCC", "GND", "CLK", "-CLK", "XCLK", "-XCLKEN")]
    def driven(s):
        """the lines the debug card drives; -XCLKEN (always its own) when it is low"""
        out = [n for n in LINES if any(ref.startswith("DBG.") for ref, pin in s.contrib.get(n, {}))]
        return sorted(out + (["-XCLKEN"] if s.val.get("-XCLKEN") == 0 else []))
    def cycle(fw, s, cpu_clock, drive, fill, polls):
        """off and on at the mode switch's position: the firmware polling, or not (the control)"""
        s._force("DBG.DRIVE", drive); s.settle()
        fw.vcc(120)
        fw.idle(polls)
        wake(s, dbg, "DBG.", fill, rnd)
        s._force("PWROK", 0)                            # the backplane's supervisor, 130 ms
        s.settle()
        fw.vcc(5000)
        fw.idle(polls)
        cpu_clock(8)
        s._force("PWROK", None); s.settle()             # let go: the card may still pull it
        cpu_clock(8)
        return driven(s)
    n, bad_driven, dead, seen = 0, [], [], {}
    for drive in (0, 1):
        for k, fill in enumerate(starts):
            s, fw, cpu_clock = machine() if k == 0 else (s, fw, cpu_clock)
            fw("ST")                                    # (the card in use again, CRESN high)
            low = cycle(fw, s, cpu_clock, drive, fill, 2)
            n += 1
            if low or s.val.get("DBG.CRESN") != 0:
                bad_driven.append(("DRIVE" if drive else "LISTEN", k, low[:6], s.val.get("DBG.CRESN")))
            if s.val.get("-RES") != 1:
                dead.append(("DRIVE" if drive else "LISTEN", k, s.val.get("-RES"), s.val.get("PWROK")))
        st = fw("ST")
        c.want("ctrl0=00 ctrl1=00" in st and "mres=0" in st, "ST after the machine came back: %s" % st.strip())
    c.want(not bad_driven, "off and on with no command between, the card drove: %s" % bad_driven[:4])
    c.want(not dead, "off and on with no command between, the CPU card stayed in reset: %s" % dead[:4])
    # the control: the same with the firmware not looking. The fault must show, or this test proves nothing.
    for drive in (0, 1):
        s, fw, cpu_clock = machine()
        for k, fill in enumerate(starts):
            for x in cycle(fw, s, cpu_clock, drive, fill, 0):
                seen.setdefault(("LISTEN", "DRIVE")[drive], set()).add(x)
    c.want(seen.get("LISTEN") == {"-XCLKEN"}, "the control, LISTEN: driven %s, expected -XCLKEN alone" % sorted(seen.get("LISTEN", ())))
    want = {"-XCLKEN", "PWROK", "-WAIT", "-ACK", "-LOCK", "-IRQ0", "-IRQ7"}
    c.want(want <= seen.get("DRIVE", set()), "the control, DRIVE: not driven %s" % sorted(want - seen.get("DRIVE", set())))
    c.notes.append("the machine off and on %d times with no command between, registers at 0, 1 and random, LISTEN and "
                   "DRIVE: CRESN low, no line driven, -RES rising; with the firmware not looking, -XCLKEN low in LISTEN "
                   "and %d lines driven in DRIVE" % (n, len(seen.get("DRIVE", ()))))
    # the drive registers and the display afresh: alone with a memory card, what they hold before and after
    s, pico, tick, rd = alone_system()
    fw = Firmware(pico, tick)
    fw.step(4)
    fw.init()
    fw.vcc(5000)
    link = lambda: ([s.val.get("LR%d" % i) for i in range(32)], s.val.get("-WSTB"), s.val.get("CCLK"))
    digits = lambda: [s.state["DBG." + r]["digit"] for r in rd["display"]]
    c.reply(fw, "ALONE ON", r"^OK alone")
    c.reply(fw, "WR 00100040 C0FFEE42", r"^OK 1$")
    shown = digits()
    fw.vcc(120); fw.idle(2)
    c.want(s.val.get("DBG.CRESN") == 0 and s.val.get("DBG.XDIR") == 0 and s.val.get("DBG.SYSOEN") == 1,
           "off while waiting: CRESN %s, XDIR %s, the system lines' drivers %s" % tuple(s.val.get("DBG." + x) for x in ("CRESN", "XDIR", "SYSOEN")))
    wake(s, dbg, "DBG.", 1, rnd)
    fw.vcc(5000); fw.idle(2)
    c.want(digits() == [15, 15], "(the test: the display woke at %s)" % digits())
    c.reply(fw, "ALONE ON", r"^OK alone")
    c.want(link() == ([0] * 32, 1, 0), "ALONE ON after the machine came back, the drive registers all 1 before: LR %s, "
           "-WSTB %s, CCLK %s" % (set(link()[0]), link()[1], link()[2]))
    c.want(digits() == shown, "the display after the machine came back: %s, not %s" % (digits(), shown))
    c.reply(fw, "WR 00100044 12345678", r"^OK 1$")
    r = c.reply(fw, "RD 00100040 2", r"^OK 2$")
    c.want(words(r) == [0xC0FFEE42, 0x12345678], "after the machine came back: %s" % r.strip())
    c.notes.append("the first command after it: the drive registers and the display written afresh")
    return c.done()

# ---------------------------------------------------------------- the MMU / data card alone, through the local link
def mmu():
    """The card plays the CPU card's side of J3 and LR to an MMU / data card alone (debug-card.md section 4):
    a commit at a time (CCLK's rise), then R read back as that card drives it."""
    import card_mmu
    dbg, Gd, rd = card_debug.build()
    mcard, Gm, rm = card_mmu.build()
    s = Sim(sim_sys.System([("DBG", dbg), ("MMU", mcard)]), list(Gd.values()) + list(Gm.values()))
    for n in ["-ACK", "-WAIT", "-LOCK", "-FAST", "-MRD", "-MWR", "-IACK"] + ["-IRQ%d" % i for i in range(8)]:
        s.pull[n] = 1                                   # (the backplane's pull-ups)
    pico = Pico(s, "DBG.A1")
    s.models["DBG.A1"] = pico.model
    # With the CPU card out nothing drives J3 or R until the debug card does. The MMU / data card's RN3 holds
    # RSRC, MEM and CCLK low (D136); the rest of J3 floats. (Without RN3 the simulator saw that card's RSRC
    # decode undefined and its R drivers fighting: D135.)
    s._force("DBG.DRIVE", 1)
    s._force("DBG.OSC", 0)
    s.settle()
    lr = ["LR%d" % i for i in range(32)]
    def mmu_on_r():
        return any(ref.startswith("MMU.") for n in lr for (ref, pin) in s.contrib.get(n, {}))
    idle = [s.val.get(n) for n in ("RSRC0", "RSRC1", "RSRC2", "RSRC3", "MEM0", "MEM1", "MEM2", "CCLK")]
    def tick():
        s._force("DBG.OSC", 1); s.settle()
        s._force("DBG.OSC", 0); s.settle()
    fw, c = Firmware(pico, tick), Checks("mmu")
    c.want(idle == [0] * 8 and not mmu_on_r(), "J3 with no CPU card and the debug card not alone: RSRC, MEM, CCLK %s "
           "(RN3: all 0), the MMU / data card %s R" % (idle, "on" if mmu_on_r() else "off"))
    fw.step(4)
    fw.init()
    c.reply(fw, "CARDS", r"^OK cpu=out mmu=in")
    c.reply(fw, "ALONE ON", r"^OK alone: the card makes CLK; LR and J3 driven, idle; the MMU / data card is in")
    c.want(s.val.get("RSRC0") == 0 and s.val.get("-WSTB") == 1 and s.val.get("LR0") == 0,
           "after ALONE ON the local link is the card's: RSRC0 %s, -WSTB %s, LR0 %s"
           % (s.val.get("RSRC0"), s.val.get("-WSTB"), s.val.get("LR0")))
    # J3 (microcode/p32ucode.py): slice 16 RSRC 3:0, RDST 6:4, MEM0; slice 18 CRSEL1-2, -WSTB, LOCKREQ, PCINC,
    # MARPC, MARINC, CCLK. LR (R) is slices 12-15.
    RSRC_MAR, RDST_PC, RDST_MAR = 4, 2, 3
    WSTB, PCINC, MARPC, MARINC, CCLK = 0x04, 0x10, 0x20, 0x40, 0x80
    def drv(k, v):
        c.reply(fw, "DRV %X %02X" % (k, v), r"^OK$")
    def commit(s16, s18, r=None):
        """one microcycle's commit: J3 set (RSRC first, so the MMU / data card is off R before the card drives
        it), R driven if given, CCLK low then high"""
        drv(0x10, s16)
        drv(0x11, 0)
        drv(0x12, s18)
        if r is not None:
            for k in range(4):
                drv(12 + k, r >> 8 * k & 0xFF)
        c.reply(fw, "GRP %X" % (0x04 | 0x10 | (0x08 if r is not None else 0)), r"^OK")   # GSYS, GJ3, and GLR to drive R
        drv(0x12, s18 | CCLK)
        drv(0x12, s18)
    def mar():
        """MAR, as the MMU / data card puts it on R (RSRC = MAR), the card's LR drivers off first"""
        c.reply(fw, "GRP 14", r"^OK")
        drv(0x10, RSRC_MAR)
        r = c.reply(fw, "LIVE F000", r"^OK( [0-9A-F]{2}){4}$")
        v = [int(x, 16) for x in r.split()[1:5]]
        return sum(b << 8 * k for k, b in enumerate(v)) if len(v) == 4 else None
    V, P = 0x1234567B, 0x00ABCDE5
    commit(RDST_MAR << 4, WSTB, V)
    a = mar()
    commit(0, WSTB | MARINC)
    b = mar()
    commit(RDST_PC << 4, WSTB, P)                       # the shadow PC from R, as the CPU card's PC
    commit(0, WSTB | MARPC)                             # MAR from the shadow PC: the next fetch
    d = mar()
    commit(0, WSTB | PCINC)                             # the PC counts, and the shadow with it
    commit(0, WSTB | MARPC)
    e = mar()
    hx = lambda x: None if x is None else "%08X" % x
    c.want((a, b, d, e) == (V, V + 4, P & ~3, (P & ~3) + 4),
           "MAR through the local link: %s, %s, %s, %s; expected %08X, %08X, %08X, %08X"
           % (hx(a), hx(b), hx(d), hx(e), V, V + 4, P & ~3, (P & ~3) + 4))
    c.notes.append("the CPU card's side of J3 and LR played to an MMU / data card alone: MAR loaded from R, counted "
                   "by MARINC, loaded from the shadow PC (bits 1:0 cleared) after RDST = PC and after PCINC, each read "
                   "back on R as that card drives it")
    # The guard (D135). LR let go, J3 set to every RSRC and CRSEL in turn, the netlist says whether the MMU / data
    # card drives R; GLR is then asked for, which the firmware must refuse exactly then (were it to let one
    # through, the simulator would stop on the fight).
    wrong, fights = [], 0
    for rsrc in range(16):
        for crsel in range(8):
            c.reply(fw, "GRP 14", r"^OK")
            drv(0x10, rsrc)
            drv(0x11, (crsel & 1) << 7)
            drv(0x12, crsel >> 1 | WSTB)
            on_r = mmu_on_r()
            fights += on_r
            r = fw("GRP 1C")
            if r.startswith("ERR") != on_r or (on_r and "RSRC %X puts the MMU / data card on R" % rsrc not in r):
                wrong.append("RSRC %X CRSEL %d: the MMU / data card %s R; GRP 1C: %s"
                             % (rsrc, crsel, "drives" if on_r else "is off", r.strip()))
    c.want(not wrong, "the guard against the netlist: " + "; ".join(wrong[:4]))
    c.reply(fw, "GRP 14", r"^OK")
    drv(0x10, 0)
    drv(0x11, 0)
    drv(0x12, WSTB)
    c.reply(fw, "GRP 1C", r"^OK$")
    c.reply(fw, "DRV 10 %X" % RSRC_MAR, r"^ERR RSRC 4 puts the MMU / data card on R")
    c.reply(fw, "GRP 0C", r"^ERR J3 stays driven alone with the MMU / data card in")
    c.reply(fw, "ALONE OFF", r"^OK: J3 and R undriven again")
    idle = [s.val.get(n) for n in ("RSRC0", "RSRC1", "RSRC2", "RSRC3", "MEM0", "MEM1", "MEM2", "CCLK")]
    c.want(idle == [0] * 8 and not mmu_on_r(), "after ALONE OFF: RSRC, MEM, CCLK %s (RN3: all 0), the MMU / data "
           "card %s R" % (idle, "on" if mmu_on_r() else "off"))
    c.notes.append("the guard: of RSRC 0-F with every CRSEL, the %d settings that put the MMU / data card on R "
                   "(per its netlist) refused with LR driven, the rest let through with no fight; J3 kept driven; "
                   "before ALONE ON, and after ALONE OFF, that card's RN3 holding RSRC, MEM and CCLK at 0 and it "
                   "off R (D136)" % fights)
    return c.done()

def main():
    tests = {"alone": alone, "strobe": strobe, "dma": dma, "listen": listen, "wrap": wrap, "xclk": xclk, "mmu": mmu,
             "power": power, "powercycle": powercycle}
    if len(sys.argv) < 2 or sys.argv[1] not in tests:
        sys.exit(__doc__)
    sys.exit(tests[sys.argv[1]](*sys.argv[2:]))

if __name__ == "__main__":
    main()
