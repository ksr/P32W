#!/usr/bin/env python3
"""The CPU card and the MMU / data card together, at gate level.

Both netlists (card_cpu.py, card_mmu.py) joined as the backplane joins them:
their J1-J3 nets are one, everything else stays on its card (CPU.name,
MMU.name). Only memory and the devices are a model: p32uemu's, as bus slaves
on J1 (emulator/p32busmodel.c's bm_phys / bm_iack), answering each bus
cycle the MMU / data card runs (-MRD, -MWR, -IACK; A2-A31, BE0-BE3, D0-D31;
-ACK). Everything the CPU sees of memory -- MAR, translation, the TLB and
its faults, steering, byte enables, BADADDR, the bus lock -- is now the
MMU / data card as wired. The state trace and the HALT line are
sim_cpu.py's, to compare with p32emu and p32uemu.

    python3 sim_sys.py [-s TRACE] [-l N] image.bin
"""
import os, sys, ctypes, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_cpu, card_mmu, card_mem                     # noqa: E402
import sim_cpu                                          # noqa: E402
from p32net import CONNECTORS                           # noqa: E402
from p32bus import SIGNALS                              # noqa: E402
from p32sim import Sim                                  # noqa: E402
import p32ucode as UC                                   # noqa: E402

SHARED = set(SIGNALS) | {"VCC", "GND"}

class System:
    """Two cards as one netlist (what p32sim.Sim needs: parts, nets, nc)."""
    def __init__(self, cards):
        self.parts, self.nets, self.nc = {}, {}, set()
        for tag, card in cards:
            for ref, part in card.parts.items():
                if ref in CONNECTORS:
                    continue                            # (the backplane joins them: no part)
                self.parts["%s.%s" % (tag, ref)] = part
            for net, pins in card.nets.items():
                name = net if net in SHARED else "%s.%s" % (tag, net)
                self.nets.setdefault(name, []).extend(("%s.%s" % (tag, r), p) for r, p in pins if r not in CONNECTORS)

MEMTOP = 0x04000000                                     # the memory cards' windows (D52)

class SystemSim(sim_cpu.CPU):
    def __init__(self, image, trace=None, limit=0, memcard=False):
        self.lib = sim_cpu.library()
        lib = self.lib
        U32 = ctypes.c_uint32
        lib.bm_phys.argtypes = [ctypes.c_int, U32, ctypes.c_uint, U32, ctypes.POINTER(U32)]
        lib.bm_iack.argtypes = [ctypes.c_uint, ctypes.POINTER(U32)]
        if lib.bm_init(image.encode()):
            sys.exit("sim_sys: no memory")
        cpu, Gc, rc = card_cpu.build()
        mmu, Gm, rm = card_mmu.build()
        cards = [("CPU", cpu), ("MMU", mmu)]
        self.G = dict(Gc, **Gm)
        self.memcard = memcard
        if memcard:                                     # the memory card at gate level, window 0 with the ROM
            mem, Gr, rr = card_mem.build()
            cards.append(("MEM", mem))
            self.G.update(Gr)
        self.card = System(cards)
        self.role = {k: "CPU." + v for k, v in rc.items()}
        s = self.sim = Sim(self.card, self.G.values())
        s.alu = lambda f, a, b: lib.bm_alu(f, a, b)
        mc = os.path.join(sim_cpu.ROOT, "microcode")
        for k in range(UC.WORD_BYTES):
            s.rom[self.role["ucode%d" % k]] = open(os.path.join(mc, "u%d.bin" % k), "rb").read()
        s.rom[self.role["decop"]] = open(os.path.join(mc, "dec_op.bin"), "rb").read()
        s.rom[self.role["deccr"]] = open(os.path.join(mc, "dec_cr.bin"), "rb").read()
        for n in ("-ACK", "-WAIT", "-LOCK"):            # open-drain: the backplane's pull-ups
            s.pull[n] = 1
        if memcard:
            img = open(image, "rb").read()
            for lane in range(4):                       # the ROM's byte lanes
                s.rom["MEM." + rr["rom%d" % lane]] = img[lane::4]
            for j in ("WIN0", "WIN1"):                  # window 0: both window jumpers fitted
                s._force("MEM." + j, 0)
        # memory and the devices: a bus slave model in the simulator
        self.bus_d, self.bus_ack = None, 0
        pins = {("BUS", "D%d" % i): "D%d" % i for i in range(32)}
        pins[("BUS", "-ACK")] = "-ACK"
        for st in ["-MRD", "-MWR", "-IACK"] + ["A%d" % i for i in range(2, 32)]:
            pins[("BUS", st)] = st                      # (a slave answers only while it is asked, at its address)
            s.listen.setdefault(st, set()).add("BUS")
        self.bus_a = None
        s.pinnet.update(pins)
        s.models["BUS"] = self._bus
        self.trace = open(trace, "w") if trace else None
        self.limit = limit
        self.icount = self.cycles = 0
        self.fault = 0
        self.pending_trace = False
        self.osc = "CPU.OSC"                            # (the oscillator is on the CPU card)
        s._force(self.osc, 0)
        s._force("PWROK", 0)
        self.irq(0)
        s.settle()

    def _bus(self, ref, edge):
        if edge:
            return {}
        g = lambda p: self.sim.get("BUS", p)
        here = sum((g("A%d" % i) or 0) << i for i in range(2, 32)) == self.bus_a
        reading = here and (g("-MRD") == 0 or g("-IACK") == 0)
        asked = reading or (here and g("-MWR") == 0)
        d = self.bus_d if reading else None
        out = {"D%d" % i: (None if d is None else d >> i & 1) for i in range(32)}
        out["-ACK"] = 0 if (self.bus_ack and asked) else None
        return out

    def v(self, n):
        local = "CPU." + n
        return self.sim.val.get(local if local in self.card.nets else n) or 0

    def tick(self):
        s = self.sim
        pre = self.phase()
        commit = pre == 7 and not self.v("STALL") and not self.v("HOLD")
        running = commit and not self.v("RESET")
        if running and self.v("HALTMODE"):
            self.cycles += 1
            self.stop("HALT at %08X, r1=%08X (%d instructions, %d cycles)" % (
                self.pc() - 4, self.reg(1), self.icount, self.cycles), self.reg(1) & 0xFF)
        if running and (self.v("GOTRAP") or self.v("GOINTR")) and self.reg(19) == 0:
            self.stop("EXCEPTION: no handler (VBR = 0) at pc %08X" % (self.pc() - 4), 255)
        s._force(self.osc, 1)
        s.settle()
        s._force(self.osc, 0)
        s.settle()
        if commit:
            if running:
                self.cycles += 1
            self.after_commit(running)
        if self.pending_trace and self.phase() == 3:
            self.pending_trace = False
            if self.trace:
                self.write_trace()

    def after_commit(self, running):
        s, lib = self.sim, self.lib
        if running and not self.v("USEQ") and self.word("STEP", 4) == 0:     # an instruction starts
            if self.limit and self.icount >= self.limit:
                self.stop("STOP: instruction limit reached", 255)
            self.icount += 1
            self.pending_trace = True
            lib.bm_tick()
        if running and self.field("MODE") == UC.MODE["WAIT"]:
            if lib.bm_wait((~self.word("IMN", 8)) & 0xFF):
                self.stop("STOP: WAIT with no possible wake-up", 255)
        self.irq(lib.bm_ipend())
        self.bus_d, self.bus_ack, self.bus_a = None, 0, None
        s._poke.add("BUS")
        s.settle()
        # the bus cycle the MMU / data card runs in this microcycle, answered once
        g = self.sim.val.get
        a = sum((g("A%d" % i) or 0) << i for i in range(2, 32))
        be = sum((g("BE%d" % i) or 0) << i for i in range(4))
        out = ctypes.c_uint32()
        self.bus_a = a
        if self.memcard and a < MEMTOP and (g("-MRD") == 0 or g("-MWR") == 0):
            pass                                        # (the memory card's: it answers itself)
        elif g("-MRD") == 0:
            self.bus_ack = lib.bm_phys(0, a, be, 0, ctypes.byref(out))
            self.bus_d = out.value if self.bus_ack else None
        elif g("-MWR") == 0:
            data = sum((g("D%d" % i) or 0) << i for i in range(32))
            self.bus_ack = lib.bm_phys(1, a, be, data, ctypes.byref(out))
        elif g("-IACK") == 0:
            self.bus_ack = lib.bm_iack(a >> 2 & 7, ctypes.byref(out))
            self.bus_d = out.value if self.bus_ack else None
        s._poke.add("BUS")
        self.irq(lib.bm_ipend())
        s.settle()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", dest="trace")
    ap.add_argument("-l", dest="limit", type=int, default=0)
    ap.add_argument("--mem", action="store_true", help="the memory card at gate level too")
    ap.add_argument("image")
    a = ap.parse_args()
    SystemSim(a.image, a.trace, a.limit, a.mem).run()

if __name__ == "__main__":
    main()
