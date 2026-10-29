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
import card_cpu, card_mmu, card_mem, card_io, card_disk, card_video  # noqa: E402
import sim_video                                        # noqa: E402
import sim_cpu                                          # noqa: E402
from p32net import CONNECTORS                           # noqa: E402
from p32bus import SIGNALS                              # noqa: E402
from p32sim import Sim                                  # noqa: E402
import p32ucode as UC                                   # noqa: E402

SHARED = set(SIGNALS) | {"VCC", "GND"}

# a card's own radial pair, as the backplane names it in that card's slot
SLOT_NETS = {"DBG": {"-BREQ": "-BREQ0", "-BGNT": "-BGNT0"},     # the debug card: slot 0 (D64)
             "DSK": {"-BREQ": "-BREQ3", "-BGNT": "-BGNT3"}}     # the disk card: slot 3 (its DMA engine, D75)

class System:
    """Cards as one netlist (what p32sim.Sim needs: parts, nets, nc)."""
    def __init__(self, cards):
        self.parts, self.nets, self.nc = {}, {}, set()
        for tag, card in cards:
            for ref, part in card.parts.items():
                if ref in CONNECTORS:
                    continue                            # (the backplane joins them: no part)
                self.parts["%s.%s" % (tag, ref)] = part
            for net, pins in card.nets.items():
                net = SLOT_NETS.get(tag, {}).get(net, net)
                name = net if net in SHARED else "%s.%s" % (tag, net)
                self.nets.setdefault(name, []).extend(("%s.%s" % (tag, r), p) for r, p in pins if r not in CONNECTORS)

MEMTOP = 0x04000000                                     # the memory cards' windows (D52)
IOWIN = 0xFFFFF000                                      # the I/O card's window, 7 (D52)
DISKWIN = 0xFFFFE000                                    # the disk card's, 6
VIDWIN = 0xFFFFD000                                     # the video card's, 5 ...
VIDMEM = 0xFE050000                                     # ... and its 64 KiB of RAM

class SystemSim(sim_cpu.CPU):
    def __init__(self, image, trace=None, limit=0, memcard=False, iocard=False, ioargs=(), diskcard=False, extra=(),
                 videocard=False, frame=None, fast=False):
        self.lib = sim_cpu.library()
        lib = self.lib
        U32 = ctypes.c_uint32
        lib.bm_phys.argtypes = [ctypes.c_int, U32, ctypes.c_uint, U32, ctypes.POINTER(U32)]
        lib.bm_iack.argtypes = [ctypes.c_uint, ctypes.POINTER(U32)]
        lib.bm_iack_other.argtypes = [ctypes.c_uint, ctypes.POINTER(U32)]
        lib.bm_ipend_other.restype = U32
        lib.bm_option.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
        for f in ("bm_uart_read", "bm_usb_read", "bm_rtc_read", "bm_net_get"):
            getattr(lib, f).restype = ctypes.c_uint
        if lib.bm_init(image.encode()):
            sys.exit("sim_sys: no memory")
        iocard = iocard or diskcard or videocard        # (the disk and video cards: the I/O card's timer at -C timing)
        self.iocard = iocard
        self.diskcard = diskcard
        self.videocard = videocard
        self.frame_out = frame
        self.devs = []
        self.osc_ticks = 0
        self.dots = None
        self.ps2 = []
        if ioargs:
            argv = (ctypes.c_char_p * len(ioargs))(*[a.encode() for a in ioargs])
            if lib.bm_option(len(ioargs), argv):
                sys.exit("sim_sys: bad I/O option in %s" % " ".join(ioargs))
        cpu, Gc, rc = card_cpu.build()
        mmu, Gm, rm = card_mmu.build()
        cards = [("CPU", cpu), ("MMU", mmu)]
        self.G = dict(Gc, **Gm)
        self.memcard = memcard
        if memcard:                                     # the memory card at gate level, window 0 with the ROM
            mem, Gr, rr = card_mem.build()
            cards.append(("MEM", mem))
            self.G.update(Gr)
        if iocard:                                      # the I/O card at gate level, its default jumpers (D52)
            io, Gi, ri = card_io.build()
            cards.append(("IO", io))
            self.G.update(Gi)
        if diskcard:                                    # the disk card at gate level, window 6, level 1 (D52)
            dk, Gd, rd = card_disk.build()
            cards.append(("DSK", dk))
            self.G.update(Gd)
        if videocard:                                   # the video card at gate level, window 5, level 4 (D52)
            vd, Gv, rv = card_video.build()
            cards.append(("VID", vd))
            self.G.update(Gv)
        for tag, card, G in extra:                      # (more cards at gate level: the debug card's harness)
            cards.append((tag, card))
            self.G.update(G)
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
        for n in ("-MRD", "-MWR", "-IACK"):             # three-state: its 10k holds, which matter while a
            s.pull[n] = 1                               # device is master (the disk card's DMA drives no -IACK)
        if memcard:
            img = open(image, "rb").read()
            for lane in range(4):                       # the ROM's byte lanes
                s.rom["MEM." + rr["rom%d" % lane]] = img[lane::4]
            for j in ("WIN0", "WIN1"):                  # window 0: both window jumpers fitted
                s._force("MEM." + j, 0)
        if self.iocard:
            self.irq_setup()
        if iocard:
            self.io_setup(ri, ioargs)
        if diskcard:
            self.disk_setup(rd)
        if videocard:
            self.video_setup(rv)
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
        # the oscillators are on the CPU card; the clock jumper (D74) picks one
        # through its 74ACT157 onto OSC, and -FAST tells the I/O card's timer
        self.osc = "CPU.OSC32" if fast else "CPU.OSC24"
        s._force(self.osc, 0)
        s._force("CPU.OSC24" if fast else "CPU.OSC32", 0)
        s.pull["-FAST"] = 1                             # (the backplane's pull-up: open = 3 MHz)
        s._force("CPU.STEPSW", 0)                       # the CLOCK switch at RUN (D76)
        if fast:
            s._force("-FAST", 0)
        s._force("PWROK", 0)
        self.irq(0)
        s.settle()

    # ---- the I/O card's chips whose insides are p32io.h's models
    def add_dev(self, ref, model, clocks=()):
        """A chip whose insides are p32io.h's model: it reads its pins and is
        evaluated when they change; clocks: the pins whose rising edge it acts on."""
        s = self.sim
        from p32net import DEV
        s.models[ref] = model
        for p in DEV[self.card.parts[ref][0]]["pins"]:
            n = s.pinnet.get((ref, p))
            if n is not None:
                s.listen.setdefault(n, set()).add(ref)
        for c in clocks:
            s.clocked.setdefault(s.pinnet.get((ref, c)), set()).add(ref)
        self.devs.append(ref)

    def irq_setup(self):
        """-IRQ0-7 from the cards at gate level (open drain) and, for the cards
        still modelled, an open-drain driver from the bus model."""
        s = self.sim
        for n in range(8):
            s.pull["-IRQ%d" % n] = 1                    # (the backplane's pull-ups)
        self.other_irq = 0
        pins = {("OTHERIRQ", "-IRQ%d" % n): "-IRQ%d" % n for n in range(8)}
        s.pinnet.update(pins)
        s.models["OTHERIRQ"] = lambda ref, edge: {} if edge else \
            {"-IRQ%d" % n: (0 if self.other_irq >> n & 1 else None) for n in range(8)}
        self.timctl = None

    def io_setup(self, ri, ioargs):
        s = self.sim
        self.io_role = {k: "IO." + v for k, v in ri.items()}
        for net, v in card_io.defaults().items():
            if v == 0:
                s._force("IO." + net, 0)                # a fitted jumper; open ones read their pull-up
        port = 0
        for i, a in enumerate(ioargs):
            if a == "-p" and i + 1 < len(ioargs):
                port = int(ioargs[i + 1], 0)
        for i in range(8):
            s._force("IO.PI%d" % i, port >> i & 1)
        for k in range(4):
            self.add_dev(self.io_role["uart%d" % k], self._uart(k), ["-WR1"])
        self.add_dev(self.io_role["rtc"], self._rtc, ["-WR"])
        for k in range(2):
            self.add_dev(self.io_role["usb%d" % k], self._usb(k), ["16"])
        self.add_dev(self.io_role["pico"], self._pico, [])
        s.clocked.setdefault("IO.BCLK", set()).add(self.io_role["pico"])
        self.timctl = self.io_role["TIMCTL"]

    def disk_setup(self, rd):
        s = self.sim
        self.lib.bm_disk_gate(1)
        self.dk_role = {k: "DSK." + v for k, v in rd.items()}
        for net, v in card_disk.defaults().items():
            if v == 0:
                s._force("DSK." + net, 0)
        for p in range(card_disk.NPORT):
            self.add_dev(self.dk_role["port%d" % p], self._ata(p), [str(card_disk.ATA["-DIOW"])])

    def video_setup(self, rv):
        s = self.sim
        self.lib.bm_video_gate(1)
        self.vid_role = {k: "VID." + v for k, v in rv.items()}
        for net, v in card_video.defaults().items():
            if v == 0:
                s._force("VID." + net, 0)
        import gen_font
        s.rom[self.vid_role["font"]] = gen_font.rom()
        self.dots = sim_video.DotClock(s, "VID.OSC")
        for port, j in ((0, "VID.J5"), (1, "VID.J6")):
            d = sim_video.PS2Device(self, port, j)
            self.ps2.append(d)
            self.add_dev(j, d.model)

    def video_ram(self):
        """The card's 64 KiB as the CPU sees them (the even bytes' RAM, the odd's)."""
        st = self.sim.state
        mem = bytearray(0x10000)
        for k in range(2):
            m = st.get(self.vid_role["ram%d" % k], {}).get("mem", {})
            for a, bits in m.items():
                if None not in bits:
                    mem[2 * a + k] = sum(b << i for i, b in enumerate(bits))
        return bytes(mem)

    def video_regs(self):
        st, r = self.sim.state, self.vid_role
        ctl = sum((st.get(r["VCTL"], {}).get("CTL%d" % i, 0)) << i for i in range(4))
        q = lambda ref: sum(b << i for i, b in enumerate(st.get(ref, {}).get("q", [0] * 8)))
        return ctl, q(r["CROW"]), q(r["CCOL"])

    def frame_check(self):
        """After HALT: the dot clock alone for a frame and a bit; the picture
        against the one the card's RAM and registers should give."""
        f = sim_video.Frame(self.sim)
        self.dots.on_rise = f.sample
        t = self.dots.edge_time(self.dots.k)
        need = 2 * 525 * 800                            # (a VSYNC, then the frame after it)
        while len(f.samples) < need:
            t += 64 * sim_video.DOT_T
            self.dots.until(t)
            if len(f.samples) > 525 * 800 and any(x[3] == 0 for x in f.samples[:525 * 800]):
                v0 = next(i for i, x in enumerate(f.samples) if x[3] == 0)
                if len(f.samples) > v0 + 516 * 800:
                    break
        pic, errs = f.picture()
        ctl, row, col = self.video_regs()
        blink = self.sim.state.get(self.vid_role.get("blink", ""), {}).get(2, 0) & 1
        want = sim_video.expected(self.video_ram(), ctl, row, col, blink)
        if pic is not None:
            bad = sum(1 for y in range(480) for x in range(640) if pic[y][x] != want[y][x])
            if bad:
                y, x = next((y, x) for y in range(480) for x in range(640) if pic[y][x] != want[y][x])
                errs.append("%d dots differ from the RAM's picture (first: line %d, dot %d: %d, want %d)"
                            % (bad, y, x, pic[y][x], want[y][x]))
            sim_video.save_png(pic, self.frame_out)
        for d in self.ps2:
            errs += d.errors
        return errs

    def stop(self, msg, status):
        perrs = [e for d in self.ps2 for e in d.errors]
        if perrs and not self.frame_out:
            msg += "\nPS/2: " + "; ".join(perrs)
            status = status or 253
        if self.frame_out and msg.startswith("HALT"):
            errs = self.frame_check()
            if errs:
                msg += "\nFRAME: " + "; ".join(errs)
                status = status or 254
            else:
                msg += "\nframe: 640 x 480 as the RAM says, syncs 96 dots / 2 lines, 800 x 525"
        super().stop(msg, status)

    def _ata(self, port):
        """A port's cable: its drives as p32io.h models them, reached through
        the header's pins (a register: -CS0 / -CS1 and DA2-0; the data
        register 16 bits wide when the card's access is a halfword)."""
        A = card_disk.ATA
        DD = [str(A["DD%d" % i]) for i in range(16)]
        lib = self.lib
        def m(ref, edge):
            g = lambda name: self.sim.get(ref, str(A[name]))
            cs0, cs1 = g("-CS0") == 0, g("-CS1") == 0
            reg = lambda: (8 if cs1 else 0) | (g("DA0") or 0) | (g("DA1") or 0) << 1 | (g("DA2") or 0) << 2
            # (the data register is 16 bits for the CPU's halfword and every DMA access)
            wide = lambda: reg() == 0 and bool(self.sim.val.get("DSK.WIDE") or self.sim.val.get("DSK.EACC"))
            present = lib.bm_ata_present(port)
            if edge:
                if present and (cs0 or cs1):
                    n = 2 if wide() else 1
                    lib.bm_ata_write(port, reg(), n, self._byte(ref, DD[:8 * n]))
                return {}
            if present:
                pins = DD if (cs0 or cs1) and wide() else DD[:8]
                out = self._reader(ref, cs0 or cs1, g("-DIOR") == 0,
                                   lambda: lib.bm_ata_read(port, reg(), 2 if len(pins) == 16 else 1), pins)
                if len(pins) == 8:
                    out.update({p: None for p in DD[8:]})
            else:
                out = {p: None for p in DD}
            out[str(A["INTRQ"])] = 1 if lib.bm_ata_intrq(port) else None
            return out
        return m

    def irq(self, ipend):
        if not getattr(self, "iocard", False) or not hasattr(self, "other_irq"):
            return super().irq(ipend)
        v = self.lib.bm_ipend_other()
        if v != self.other_irq:
            self.other_irq = v
            self.sim._poke.add("OTHERIRQ")

    def _byte(self, ref, names):
        return sum((self.sim.get(ref, n) or 0) << i for i, n in enumerate(names))

    def _reader(self, ref, selected, reading, read, pins):
        """A read happens once, as the strobe starts; its byte is held on the
        pins until the strobe ends (a register read may have effects)."""
        st = self.sim.state.setdefault(ref, {"rd": False, "val": 0})
        if selected and reading:
            if not st["rd"]:
                st["rd"], st["val"] = True, read()
            return {p: st["val"] >> i & 1 for i, p in enumerate(pins)}
        st["rd"] = False
        return {p: None for p in pins}

    def _uart(self, n):
        D = ["D%d" % i for i in range(8)]
        def m(ref, edge):
            g = lambda p: self.sim.get(ref, p)
            reg = lambda: self._byte(ref, ["A0", "A1", "A2"])
            sel = g("-CS2") == 0
            if edge:
                if sel:
                    self.lib.bm_uart_write(n, reg(), self._byte(ref, D))
                return {}
            out = self._reader(ref, sel, g("-RD1") == 0, lambda: self.lib.bm_uart_read(n, reg()), D)
            out["INTRPT"] = 1 if self.lib.bm_uart_irq(n) else 0
            return out
        return m

    def _rtc(self, ref, edge):
        AD = ["AD%d" % i for i in range(8)]
        g = lambda p: self.sim.get(ref, p)
        st = self.sim.state.setdefault(ref + "#as", {"as": 0})
        if edge:
            if g("-CS") == 0:
                self.lib.bm_rtc_write(self._byte(ref, AD))
            return {}
        a = g("AS") or 0
        if a:
            st["ad"] = self._byte(ref, AD)              # (the address latch follows AD while AS is high ...)
        elif st["as"]:
            self.lib.bm_rtc_index(st["ad"])             # (... and holds it as AS falls)
        st["as"] = a
        out = self._reader(ref, g("-CS") == 0, g("-RD") == 0, self.lib.bm_rtc_read, AD)
        out["-IRQ"] = None                              # (no clock interrupts are modelled)
        return out

    def _usb(self, n):
        D = [str(1 + 2 * i) for i in range(8)]          # the module's header: D0-D7 on the odd pins
        def m(ref, edge):
            g = lambda p: self.sim.get(ref, p)
            sel = g("12") == 0
            if edge:
                if sel:
                    self.lib.bm_usb_write(n, g("10") or 0, self._byte(ref, D))
                return {}
            out = self._reader(ref, sel, g("14") == 0, lambda: self.lib.bm_usb_read(n, g("10") or 0), D)
            out["8"] = 0 if self.lib.bm_usb_int(n) else 1
            return out
        return m

    def _pico(self, ref, edge):
        """The network coprocessor's firmware, as the mailbox sees it: it takes
        each request byte from the request latch (-PRD, until the card says
        TXE) and puts each reply byte into the reply latch (PWR, until RXF),
        at the bus clock's edges; the protocol itself is p32io.h's."""
        lib, g = self.lib, (lambda p: self.sim.get(ref, p))
        st = self.sim.state.setdefault(ref, {"s": "idle", "prd": 1, "pwr": 0, "pd": None, "reset": 0})
        PD = ["GP%d" % i for i in range(8)]
        if edge:
            run = g("RUN")
            if run == 0:
                if not st["reset"]:
                    lib.bm_net_reset()
                st.update(s="idle", prd=1, pwr=0, pd=None, reset=1)
                return {}
            st["reset"] = 0
            if not lib.bm_net_ready():
                return {}
            s_ = st["s"]
            if s_ == "idle":
                if g("GP10") == 0:                      # TXE low: a request byte is waiting
                    st.update(s="take", prd=0)
                elif lib.bm_net_avail() and g("GP11") == 0:     # RXF low: the reply latch is empty
                    st.update(s="put", pd=None)
            elif s_ == "take":
                if st["pd"] is None:
                    st["pd"] = -1
                    lib.bm_net_put(self._byte(ref, PD))         # (PD driven by the request buffer now)
                elif g("GP10") == 1:                    # the card saw it: let go
                    st.update(s="idle", prd=1, pd=None)
            elif s_ == "put":
                if st["pd"] is None:
                    st["pd"] = lib.bm_net_get()          # the byte on PD first ...
                elif not st["pwr"]:
                    st["pwr"] = 1                       # ... then PWR clocks it into the reply latch
                elif g("GP11") == 1:                    # RXF: the card has it
                    st.update(s="idle", pwr=0, pd=None)
            return {}
        out = {"GP8": st["prd"], "GP9": st["pwr"], "GP12": 1 if (lib.bm_net_ready() and g("RUN") != 0) else 0,
               "GP13": 1 if lib.bm_net_attn() else 0}
        drive = st["s"] == "put" and st["pd"] not in (None, -1)
        out.update({p: (st["pd"] >> i & 1 if drive else None) for i, p in enumerate(PD)})
        return out

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
        if self.iocard and pre == 7 and self.v("STALL") and not self.v("RESET"):
            self.cycles += 1                            # a wait state (-WAIT: STALL holds the microcycle)
        if self.iocard and pre == 7 and not self.v("STALL") and self.v("HOLD") and self.v("WAITHOLD") \
                and not self.v("RESET"):
            self.cycles += 1                            # WAIT holds its microcycle (p32uemu -C)
            if self.timctl and not s.state.get(self.timctl, {}).get("EN"):
                self.stop("STOP: WAIT with no possible wake-up", 255)
        if self.dots:
            self.dots.until(self.osc_ticks * sim_video.OSC_T)       # the video card's dot clock, up to this tick
            if self.osc_ticks % 8 == 0:
                for d in self.ps2:
                    d.step()
        self.osc_ticks += 1
        self.hook(pre)
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

    def hook(self, phase):
        """Each oscillator period, before its edge (a harness's own work)."""

    def after_commit(self, running):
        s, lib = self.sim, self.lib
        if running and not self.v("USEQ") and self.word("STEP", 4) == 0:     # an instruction starts
            if self.limit and self.icount >= self.limit:
                self.stop("STOP: instruction limit reached", 255)
            self.icount += 1
            self.pending_trace = True
            if not self.iocard:
                lib.bm_tick()
        if running and self.field("MODE") == UC.MODE["WAIT"] and not self.iocard:
            if lib.bm_wait((~self.word("IMN", 8)) & 0xFF):
                self.stop("STOP: WAIT with no possible wake-up", 255)
        if self.iocard:
            s._poke.update(self.devs)                   # (the devices' state may have moved: input, the network)
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
        elif getattr(self, "io_role", None) and a >= IOWIN and (g("-MRD") == 0 or g("-MWR") == 0):
            pass                                        # (the I/O card's window: it answers itself)
        elif self.diskcard and DISKWIN <= a < IOWIN and (g("-MRD") == 0 or g("-MWR") == 0):
            pass                                        # (the disk card's window: it answers itself)
        elif self.videocard and (VIDWIN <= a < DISKWIN or VIDMEM <= a < VIDMEM + 0x10000) \
                and (g("-MRD") == 0 or g("-MWR") == 0):
            pass                                        # (the video card's windows: it answers itself)
        elif self.iocard and g("-IACK") == 0:
            self.bus_ack = lib.bm_iack_other(a >> 2 & 7, ctypes.byref(out))
            self.bus_d = out.value if self.bus_ack else None
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
    ap.add_argument("--io", action="store_true", help="the I/O card at gate level too (its timer: compare with p32uemu -C)")
    ap.add_argument("--disk", action="store_true", help="the disk card at gate level too (two wait states: compare with p32uemu -C)")
    ap.add_argument("--video", action="store_true", help="the video card at gate level too (compare with p32uemu -C)")
    ap.add_argument("--frame", help="with --video: after HALT, a frame from the card's VGA outputs, checked and written as PNG")
    ap.add_argument("--fast", action="store_true", help="the CPU card's clock jumper fitted (D74): compare with p32uemu -C -F")
    ap.add_argument("--opt", action="append", default=[], help="an emulator I/O option for the devices, e.g. --opt=-p --opt=0x5A")
    ap.add_argument("image")
    a = ap.parse_args()
    SystemSim(a.image, a.trace, a.limit, a.mem, a.io, a.opt, a.disk, videocard=a.video, frame=a.frame, fast=a.fast).run()

if __name__ == "__main__":
    main()
