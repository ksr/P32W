#!/usr/bin/env python3
"""The CPU card in the gate-level simulator (docs/cpu-card.md, "Verification").

The card AS WIRED (card_cpu.py: every chip, the real GAL equations, the
real control store and decode tables in its flash chips) runs a program;
the MMU / data card, memory and the devices are p32uemu's own model of them
(emulator/p32busmodel.c, through ctypes) on the J3 local link. It writes the
architectural state at every instruction start in p32emu's -s format, so a
run compares line for line with the reference emulator, and stops at HALT
as p32uemu does (the same message, cycles included, and exit status).

    python3 sim_cpu.py [-s TRACE] [-l N] image.bin
"""
import os, sys, ctypes, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "microcode"))
import card_cpu                                         # noqa: E402
from card_cpu import fnet                               # noqa: E402
import p32sim                                           # noqa: E402
from p32sim import Sim                                  # noqa: E402
import p32ucode as UC                                   # noqa: E402

EMU = os.path.join(ROOT, "emulator")
LIB = os.path.join(EMU, "libp32bus.so")

def library():
    src = [os.path.join(EMU, f) for f in ("p32busmodel.c", "p32uemu.c", "p32io.h", "p32acrtc.h", "p32uc.h", "p32opc.h")]
    if not os.path.exists(LIB) or any(os.path.getmtime(s) > os.path.getmtime(LIB) for s in src):
        subprocess.run(["make", "-s", "-C", EMU, "p32uemu"], check=True)        # (p32uc.h, p32opc.h)
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-o", LIB, os.path.join(EMU, "p32busmodel.c")],
                       check=True, stderr=subprocess.DEVNULL)
    lib = ctypes.CDLL(LIB)
    U32, P32 = ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)
    lib.bm_init.argtypes = [ctypes.c_char_p]
    lib.bm_alu.restype = U32
    lib.bm_alu.argtypes = [ctypes.c_uint, U32, U32]
    lib.bm_ipend.restype = U32
    lib.bm_wait.argtypes = [U32]
    lib.bm_cycle.argtypes = [ctypes.c_uint] * 5 + [U32, P32, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint)]
    lib.bm_edge.argtypes = [ctypes.c_uint, U32] + [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_int]
    return lib

class CPU:
    def __init__(self, image, trace=None, limit=0):
        self.lib = library()
        if self.lib.bm_init(image.encode()):
            sys.exit("sim_cpu: no memory")
        self.card, self.G, self.role = card_cpu.build()
        s = self.sim = Sim(self.card, self.G.values())
        s.alu = p32sim.alu_guard(self.lib.bm_alu, UC.ALU.values())
        mc = os.path.join(ROOT, "microcode")
        for k in range(UC.WORD_BYTES):
            s.rom[self.role["ucode%d" % k]] = open(os.path.join(mc, "u%d.bin" % k), "rb").read()
        s.rom[self.role["decop"]] = open(os.path.join(mc, "dec_op.bin"), "rb").read()
        s.rom[self.role["deccr"]] = open(os.path.join(mc, "dec_cr.bin"), "rb").read()
        self.trace = open(trace, "w") if trace else None
        self.limit = limit
        self.icount = self.cycles = 0
        self.fault = 0
        self.pending_trace = False
        self.osc = "OSC24"                               # (through the clock jumper's mux, D74: 3 MHz)
        s._force(self.osc, 0)
        s._force("OSC32", 0)
        s.pull["-FAST"] = 1
        s._force("STEPSW", 0)                            # the CLOCK switch at RUN (D76)
        for n, v in (("STALL", 0), ("FAULT", 0), ("PWROK", 0)):
            s._force(n, v)
        for i in range(4):
            s._force("FC%d" % i, 0)
        # the MMU / data card's drivers onto R: a model in the simulator,
        # enabled from RSRC (and CRSEL) as the card's own decoders do it
        self.mmu_out = 0
        mmupins = {("MMU", "LR%d" % i): "LR%d" % i for i in range(32)}
        mmupins.update({("MMU", "RSRC%d" % i): "RSRC%d" % i for i in range(4)})
        mmupins.update({("MMU", "CRSEL%d" % i): "CRSEL%d" % i for i in range(3)})
        s.pinnet.update(mmupins)
        s.models["MMU"] = self._mmu
        for (ref, pin), net in mmupins.items():
            if not pin.startswith("LR"):
                s.listen.setdefault(net, set()).add("MMU")
        self.irq(0)
        s.settle()

    def _mmu(self, ref, edge):
        if edge:
            return {}
        g = lambda p: self.sim.get("MMU", p) or 0
        rsrc = sum(g("RSRC%d" % i) << i for i in range(4))
        crsel = sum(g("CRSEL%d" % i) << i for i in range(3))
        R = UC.RSRC
        drive = rsrc in (R["MEM"], R["MAR"], R["BADADDR"]) or (
            rsrc == R["CRHW"] and crsel in (UC.HWCR["ASID"], UC.HWCR["TLBHI"], UC.HWCR["TLBLO"]))
        return {"LR%d" % i: (self.mmu_out >> i & 1) if drive else None for i in range(32)}

    # ---- nets
    def v(self, n):
        return self.sim.val.get(n) or 0
    def bits(self, names):
        return sum(self.v(n) << i for i, n in enumerate(names))
    def field(self, name):
        width = dict(UC.FIELDS)[name]
        return self.bits([fnet(name, i) for i in range(width)])
    def word(self, prefix, n=32, start=0):
        return self.bits(["%s%d" % (prefix, start + i) for i in range(n)])
    def phase(self):
        return self.bits(["PH0", "PH1", "PH2"])
    def irq(self, ipend):
        for i in range(8):
            self.sim._force("-IRQ%d" % i, 0 if ipend >> i & 1 else 1)

    # ---- the register file (copy A; copy B must agree) and the state trace
    def reg(self, a):
        vals = []
        for cp in "AB":
            x = 0
            for k in range(4):
                bitsl = self.sim.state.get(self.role["rf%s%d" % (cp, k)], {"mem": {}})["mem"].get(a, [0] * 8)
                x |= sum((b or 0) << j for j, b in enumerate(bitsl)) << (8 * k)
            vals.append(x)
        if vals[0] != vals[1]:
            raise SystemExit("sim_cpu: register %d differs between the copies: %08X %08X" % (a, vals[0], vals[1]))
        return vals[0]
    def pc(self):
        return self.word("PC", 30, 2) << 2
    def status(self):
        return self.bits(["SUPER", "IE", "PS", "PIE"])
    def write_trace(self):
        sup = self.v("SUPER")
        cur, other = (31, 15) if sup else (15, 31)
        line = "%08X %c" % (self.pc(), "S" if sup else "U")
        line += "".join(" %08X" % self.reg(i) for i in range(1, 15))
        line += " %08X o=%08X st=%X epc=%08X cause=%08X bad=%08X\n" % (
            self.reg(cur), self.reg(other), self.status(), self.reg(16), self.reg(17), self.reg(18))
        self.trace.write(line)

    def stop(self, msg, status):
        sys.stdout.flush()
        sys.stderr.write(msg + "\n")
        if self.trace:
            self.trace.close()
        sys.exit(status)

    # ---- one oscillator period
    def tick(self):
        s, lib = self.sim, self.lib
        pre = self.phase()
        commit = pre == 7 and not self.v("STALL") and not self.v("HOLD")
        if commit:
            running = not self.v("RESET")
            if running and self.v("HALTMODE"):
                self.cycles += 1
                self.stop("HALT at %08X, r1=%08X (%d instructions, %d cycles)" % (
                    self.pc() - 4, self.reg(1), self.icount, self.cycles), self.reg(1) & 0xFF)
            if running and (self.v("GOTRAP") or self.v("GOINTR")) and self.reg(19) == 0:
                self.stop("EXCEPTION: no handler (VBR = 0) at pc %08X" % (self.pc() - 4), 255)
            edge = (self.word("RDST", 3), self.word("LR"), self.v("PCINC"), self.v("MARPC"), self.v("MARINC"),
                    self.v("TLBW"), self.word("CRSEL", 3), self.fault)
        s._force(self.osc, 1)
        s.settle()
        s._force(self.osc, 0)
        s.settle()
        if commit:
            lib.bm_edge(*edge)
            if running:
                self.cycles += 1
            self.after_commit(running)
        if self.pending_trace and self.phase() == 3:            # (the last write is done by now)
            self.pending_trace = False
            if self.trace:
                self.write_trace()

    def after_commit(self, running):
        s, lib = self.sim, self.lib
        lib.bm_super(self.v("SUPER"))
        if running and not self.v("USEQ") and self.word("STEP", 4) == 0:     # an instruction starts
            if self.limit and self.icount >= self.limit:
                self.stop("STOP: instruction limit reached", 255)
            self.icount += 1
            self.pending_trace = True
            lib.bm_tick()
        if running and self.field("MODE") == UC.MODE["WAIT"]:
            imask = (~self.word("IMN", 8)) & 0xFF
            if lib.bm_wait(imask):
                self.stop("STOP: WAIT with no possible wake-up", 255)
        self.irq(lib.bm_ipend())
        s.settle()
        # the MMU / data card's memory cycle and R source for this word
        mem, rsrc = self.field("MEM"), self.field("RSRC")
        size = {0: 1, 1: 2}.get(self.bits(["SIZE0", "SIZE1"]), 4)
        out, drive, fc = ctypes.c_uint32(), ctypes.c_int(), ctypes.c_uint()
        lr_in = self.word("LR")
        self.fault = lib.bm_cycle(mem, rsrc, size, self.v("SIGNED"), self.word("CRSEL", 3), lr_in,
                                  ctypes.byref(out), ctypes.byref(drive), ctypes.byref(fc)) if running else 0
        if self.mmu_out != out.value:
            self.mmu_out = out.value
            s._poke.add("MMU")
        s._force("FAULT", self.fault)
        for i in range(4):
            s._force("FC%d" % i, fc.value >> i & 1 if self.fault else 0)
        self.irq(lib.bm_ipend())
        s.settle()

    def run(self):
        for _ in range(3 * 8):                                  # power-on: PWROK low for three microcycles
            self.tick()
        self.sim._force("PWROK", 1)
        while True:
            self.tick()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", dest="trace")
    ap.add_argument("-l", dest="limit", type=int, default=0)
    ap.add_argument("image")
    a = ap.parse_args()
    CPU(a.image, a.trace, a.limit).run()

if __name__ == "__main__":
    main()
