#!/usr/bin/env python3
"""The video card's bus side at gate level, as wired (docs/video-card-acrtc.md
4; D106): the card's netlist alone in p32sim, its backplane pins driven as a
bus master drives them, half a clock at a time, with stand-ins at the pins
of the two parts that have no model:

  * the chip (the HD63484): it watches CS, RS and R/W as the card gives
    them. In a read it puts a word on its D0-D15 from CS's fall to its rise
    (so a buffer pointing the wrong way is a bus fight); in a write it takes
    D0-D15 as CS rises. IRQ is pulled low when the test says.
  * the ATmega328P: it loads a byte (MD0-7, a low pulse on LDDATA), reads
    the command latch (-RDCMD low), holds INTREQ and SRC.

What it checks is the wiring, the polarities and the five GALs together, at
zero delay: the four-clock cycle of 4.2 (when CS, RS, R/W, the buffers and
-WAIT move, in which half of which clock), every register of 4.1 with its
size, the bus errors, the PS/2 handshake, both interrupts on their levels
and the vector, the jumpers. The chip's timing against 2CLK, the memory
side and the card in a machine are step 4 of the note's plan.

    python3 sim_video.py          exit status 0 when every check passes
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import card_video                                       # noqa: E402
from p32sim import Sim, SimError                        # noqa: E402

PAGE = lambda win: 0xFFFF8F00 + win * 0x1000
HALVES = ["%d%s" % (k, h) for k in range(1, 5) for h in "ab"]   # a cycle's halves: 1a (CLK high) ... 4b

class Bench:
    def __init__(self, jumpers=None):
        self.card, self.G, self.role = card_video.build()
        s = self.sim = Sim(self.card, self.G.values())
        for n in ["-ACK", "-WAIT"] + ["-IRQ%d" % k for k in range(8)]:
            s.pull[n] = 1                               # the backplane's pull-ups
        j = card_video.defaults()
        j.update(jumpers or {})
        for net, v in j.items():
            if v == 0:
                s._force(net, 0)                        # a jumper fitted
        # the two stand-ins work at their parts' pins, not on nets by name: a pin on the wrong net is seen
        chip, mcu = self.card.kept["IC1"], self.role["mcu"]
        self.pin = {"CS": s.net(chip, "-CS"), "RS": s.net(chip, "RS"), "RW": s.net(chip, "R/W"), "IRQ": s.net(chip, "-IRQ"),
                    "LDDATA": s.net(mcu, "PD6"), "RDCMD": s.net(mcu, "PD7"), "FULL": s.net(mcu, "PB2"),
                    "BUSY": s.net(mcu, "PB3"), "INTREQ": s.net(mcu, "PB4"), "SRC": s.net(mcu, "PB5"), "MRST": s.net(mcu, "-RESET")}
        self.hd = [s.net(chip, "D%d" % i) for i in range(16)]
        self.md = [s.net(mcu, "PC%d" % i) for i in range(6)] + [s.net(mcu, "PB0"), s.net(mcu, "PB1")]
        self.errs = []
        self.chip_out = None                            # the word the chip answers a read with
        self.chip_got = []                              # (RS, the word) of each write the chip took
        self.cs_was = 1
        s.set(CLK=0, _CLK=1, _RES=0, _MRD=1, _MWR=1, _IACK=1)
        self.put(LDDATA=1, RDCMD=1, INTREQ=0, SRC=0)
        s.set_word("A", 30, 0, start=2); s.set_word("BE", 4, 0)
        self.settle()
        self.half(1); self.half(0)                      # a clock in reset
        self.check(self.v("MRST") == 0, "reset: the ATmega is not held in reset while -RES is low")
        s.set(_RES=1)
        self.settle()
        self.half(1); self.half(0)

    def check(self, ok, what):
        if not ok:
            self.errs.append(what)

    def v(self, net):
        """A net's level; or a stand-in's pin's, by the pin's name here (CS, RS, RW, FULL, ...)."""
        return self.sim.val.get(self.pin.get(net, net))

    def put(self, **pins):
        """A stand-in drives its pins (None: lets go)."""
        for name, v in pins.items():
            self.sim._force(self.pin[name], v)

    def bits(self, nets, value=False):
        """Drive nets with a word's bits (None: let go), or read them as a word (None if any floats)."""
        if value is not False:
            for i, n in enumerate(nets):
                self.sim._force(n, None if value is None else value >> i & 1)
            return None
        vals = [self.sim.val.get(n) for n in nets]
        return None if None in vals else sum(x << i for i, x in enumerate(vals))

    def settle(self):
        """Settle, with the chip's stand-in in the loop: its D0-D15 follow CS and R/W."""
        s = self.sim
        for _ in range(4):
            s.settle()
            cs, rw = self.v("CS"), self.v("RW")
            drive = cs == 0 and rw == 1 and self.chip_out is not None
            self.bits(self.hd, self.chip_out if drive else None)
            if cs == 1 and self.cs_was == 0 and rw == 0:
                self.chip_got.append((self.v("RS"), self.bits(self.hd)))
            self.cs_was = cs
        s.settle()

    def half(self, clk):
        self.sim.set(CLK=clk, _CLK=1 - clk)
        self.settle()

    def cycle(self, addr, be, rd, data=None, iack=False):
        """One bus cycle. Returns (acked, clocks, the word read, the halves in which each watched net was low or
        high): the master's lines move just after the edge that starts the cycle and are held to the edge that
        ends it; -ACK is looked at as clock 1 ends, -WAIT and D as each clock ends."""
        s = self.sim
        s.set_word("A", 30, addr >> 2, start=2); s.set_word("BE", 4, be)
        s.set(_MRD=0 if rd and not iack else 1, _MWR=0 if not rd and not iack else 1, _IACK=0 if iack else 1)
        if not rd and not iack:
            s.set_word("D", 32, data)
        self.settle()
        seen = {n: [] for n in ("CS", "RS", "RW", "LE0N", "LE1N", "-WAIT")}
        acked, got, n = None, None, 0
        while True:
            n += 1
            for h in (1, 0):
                if (n, h) != (1, 1):
                    self.half(h)
                for net in seen:
                    seen[net].append(self.v(net))
            if n == 1:
                acked = self.v("-ACK") == 0
            wait = self.v("-WAIT") == 0
            if not wait or n >= 8:
                if rd or iack:
                    got = [self.v("D%d" % i) for i in range(32)]
                break
            self.half(1)                                # the edge that ends a wait
        self.half(1)                                    # the edge that ends the cycle: then the master lets go
        s.set(_MRD=1, _MWR=1, _IACK=1)
        s.set_word("D", 32, None)
        self.settle()
        self.half(0)
        return acked, n, got, seen

    def read(self, addr, be):
        acked, n, got, seen = self.cycle(addr, be, True)
        lanes = [k for k in range(4) if be >> k & 1]
        word = None
        if acked and all(got[8 * k + i] is not None for k in lanes for i in range(8)):
            word = sum(got[8 * k + i] << (8 * k + i) for k in lanes for i in range(8)) >> (8 * lanes[0])
        return acked, n, word, got, seen

    def load(self, byte, src=0):
        """The ATmega loads a byte for the CPU: SRC, MD0-7, a low pulse on LDDATA."""
        self.put(SRC=src); self.bits(self.md, byte); self.put(LDDATA=0); self.settle()
        self.put(LDDATA=1); self.settle()
        self.bits(self.md, None); self.settle()

    def command(self):
        """The ATmega reads the command latch."""
        self.put(RDCMD=0); self.settle()
        b = self.bits(self.md)
        self.put(RDCMD=1); self.settle()
        return b

def low(seen, net):
    return [HALVES[i] for i, x in enumerate(seen[net]) if x == 0]

def run():
    b = Bench()
    ck, s = b.check, b.sim
    P = PAGE(card_video.WINDOW)
    # ---- out of reset
    ck(b.v("CS") == 1, "reset: CS is not high")
    ck(b.v("-ACK") == 1 and b.v("-WAIT") == 1, "reset: -ACK or -WAIT is pulled")
    ck(b.v("FULL") == 0 and b.v("MRST") == 1, "reset: FULL is set, or the ATmega is still held in reset")
    ck(all(b.v("-IRQ%d" % k) == 1 for k in range(8)), "reset: an -IRQ line is pulled")

    # ---- the chip's cycle (4.2): a write of the address register, a read of the register addressed
    acked, n, got, seen = b.cycle(P + 0x00, 0b0011, False, 0x1234BE82)
    ck(acked and n == 4, "write +00: acked %s, %d clocks (want 4)" % (acked, n))
    ck(low(seen, "-WAIT") == ["1a", "1b", "2a", "2b", "3a", "3b"], "write +00: -WAIT low in %s" % low(seen, "-WAIT"))
    ck(low(seen, "CS") == ["2b", "3a", "3b", "4a"], "write +00: CS low in %s (want 2b-4a: two clocks)" % low(seen, "CS"))
    ck(seen["RS"][2:] == [0] * 6 and seen["RW"][2:] == [0] * 6, "write +00: RS, R/W in clocks 2-4: %s, %s" % (seen["RS"][2:], seen["RW"][2:]))
    ck(low(seen, "LE0N") == ["2b", "3a", "3b", "4a", "4b"] and low(seen, "LE1N") == low(seen, "LE0N"),
       "write +00: the buffers on in %s / %s" % (low(seen, "LE0N"), low(seen, "LE1N")))
    ck(b.chip_got == [(0, 0xBE82)], "write +00: the chip took %s (want RS 0, BE82)" % b.chip_got)
    ck(b.v("RS") == 0 and b.v("RW") == 0 and b.v("CS") == 1, "write +00: RS, R/W moved, or CS is low, after the cycle")
    b.chip_out, b.chip_got = 0xA55A, []
    acked, n, word, got, seen = b.read(P + 0x04, 0b0011)
    ck(acked and n == 4 and word == 0xA55A, "read +04: acked %s, %d clocks, %s (want A55A)" % (acked, n, word and hex(word)))
    ck(low(seen, "CS") == ["2b", "3a", "3b", "4a", "4b"], "read +04: CS low in %s (want 2b-4b)" % low(seen, "CS"))
    ck(seen["RS"][2:] == [1] * 6 and seen["RW"][2:] == [1] * 6, "read +04: RS, R/W in clocks 2-4: %s, %s" % (seen["RS"][2:], seen["RW"][2:]))
    ck(all(x is None for x in got[16:]), "read +04: D16-D31 are driven")
    ck(b.v("CS") == 1 and b.v("D0") is None and not b.chip_got, "read +04: CS low, D driven or a write seen after the cycle")
    acked, n, word, got, seen = b.read(P + 0x00, 0b0011)
    ck(acked and word == 0xA55A and seen["RS"][2:] == [0] * 6, "read +00 (the status): %s, RS %s" % (word and hex(word), seen["RS"][2:]))
    acked, n, got, seen = b.cycle(P + 0x04, 0b0011, False, 0x0000C088)
    ck(b.chip_got == [(1, 0xC088)], "write +04: the chip took %s (want RS 1, C088)" % b.chip_got)

    # ---- the card's own registers: one clock each
    for what, a, be, want in (("the card ID, +F0", P + 0xF0, 0b0001, card_video.CARD_ID),
                              ("the revision, +F1", P + 0xF0, 0b0010, card_video.CARD_REV),
                              ("the card status, +08", P + 0x08, 0b0001, 0x00),
                              ("the interrupt status, +F4", P + 0xF4, 0b1111, 0)):
        acked, n, word, got, seen = b.read(a, be)
        ck(acked and n == 1 and word == want, "%s: acked %s, %d clock(s), %s (want %X)" % (what, acked, n, word, want))
        ck(not low(seen, "CS"), "%s: CS went low" % what)

    # ---- the PS/2 section: a byte in, the interrupt, the vector, the read that clears FULL
    b.load(0x41, src=1)
    ck(b.v("FULL") == 1, "PS/2: FULL is not set by the load")
    acked, n, word, got, seen = b.read(P + 0x08, 0b0001)
    ck(word == 0x03, "PS/2: the status with a mouse byte waiting is %s (want 03: FULL, SRC)" % word)
    ck(all(b.v("-IRQ%d" % k) == 1 for k in range(8)), "PS/2: an -IRQ line is pulled with INTREQ low")
    b.put(INTREQ=1); b.settle()
    ck([k for k in range(8) if b.v("-IRQ%d" % k) == 0] == [card_video.LEVEL], "PS/2: FULL and INTREQ pull %s (want -IRQ%d)"
       % ([k for k in range(8) if b.v("-IRQ%d" % k) == 0], card_video.LEVEL))
    acked, n, word, got, seen = b.read(P + 0xF4, 0b1111)
    ck(word == 1, "PS/2: the interrupt status is %s (want 1)" % word)
    acked, n, got, seen = b.cycle(card_video.LEVEL << 2, 0, True, iack=True)
    vec = sum((got[i] or 0) << i for i in range(8))
    ck(acked and n == 1 and vec == card_video.WINDOW * 16, "PS/2: the acknowledge cycle: acked %s, vector %02X (want %02X)"
       % (acked, vec, card_video.WINDOW * 16))
    acked, n, got, seen = b.cycle((card_video.LEVEL ^ 1) << 2, 0, True, iack=True)
    ck(not acked, "PS/2: another level's acknowledge cycle is answered")
    acked, n, word, got, seen = b.read(P + 0x0C, 0b0001)
    ck(acked and n == 1 and word == 0x41, "PS/2: the data read: acked %s, %s (want 41)" % (acked, word))
    ck(b.v("FULL") == 0, "PS/2: FULL is not cleared after the data is read")
    ck(all(b.v("-IRQ%d" % k) == 1 for k in range(8)), "PS/2: the -IRQ line is still pulled after the read")
    b.half(1); b.half(0)
    b.load(0x42)
    ck(b.v("FULL") == 1, "PS/2: a load a clock after the read is lost")
    acked, n, word, got, seen = b.read(P + 0x0C, 0b0001)
    ck(word == 0x42, "PS/2: the second byte reads %s (want 42)" % word)
    b.put(INTREQ=0); b.settle()
    # a command out
    busy_at = []
    watch = b.half
    def half(clk):
        watch(clk)
        busy_at.append(b.v("BUSY"))
    b.half = half                                       # (BUSY in each half of the write's two clocks, from 1b)
    acked, n, got, seen = b.cycle(P + 0x08, 0b0001, False, 0xFFFFFF05)
    b.half = watch
    ck(acked and n == 2 and b.v("BUSY") == 1, "PS/2 command: acked %s, %d clock(s) (want 2), BUSY %s" % (acked, n, b.v("BUSY")))
    ck(low(seen, "-WAIT") == ["1a", "1b"], "PS/2 command: -WAIT low in %s (want clock 1)" % low(seen, "-WAIT"))
    ck(busy_at[:3] == [0, 1, 1], "PS/2 command: BUSY in halves 1b, 2a, 2b is %s (want set as clock 2 begins)" % busy_at[:3])
    acked, n, word, got, seen = b.read(P + 0x08, 0b0001)
    ck(word == 0x04, "PS/2 command: the status is %s (want 04: BUSY)" % word)
    cmd = b.command()
    ck(cmd == 0x05 and b.v("BUSY") == 0, "PS/2 command: the ATmega reads %s (want 5), BUSY %s after" % (cmd, b.v("BUSY")))

    # ---- the chip's interrupt: device 1
    b.put(IRQ=0); b.settle()
    ck([k for k in range(8) if b.v("-IRQ%d" % k) == 0] == [card_video.LEVEL], "the chip's IRQ pulls %s" % [k for k in range(8) if b.v("-IRQ%d" % k) == 0])
    acked, n, word, got, seen = b.read(P + 0xF4, 0b1111)
    ck(word == 2, "the chip's IRQ: the interrupt status is %s (want 2)" % word)
    acked, n, word, got, seen = b.read(P + 0x08, 0b0001)
    ck(word == 0x10, "the chip's IRQ: the card status is %s (want 10)" % word)
    acked, n, got, seen = b.cycle(card_video.LEVEL << 2, 0, True, iack=True)
    vec = sum((got[i] or 0) << i for i in range(8))
    ck(acked and vec == card_video.WINDOW * 16 + 1, "the chip's IRQ: vector %02X (want %02X)" % (vec, card_video.WINDOW * 16 + 1))
    b.load(0x43); b.put(INTREQ=1); b.settle()
    acked, n, got, seen = b.cycle(card_video.LEVEL << 2, 0, True, iack=True)
    vec = sum((got[i] or 0) << i for i in range(8))
    ck(vec == card_video.WINDOW * 16, "both devices asking: vector %02X (want the PS/2 section's, %02X)" % (vec, card_video.WINDOW * 16))
    b.put(IRQ=None, INTREQ=0); b.settle()
    b.read(P + 0x0C, 0b0001)

    # ---- no -ACK: a bus error
    for what, a, be, rd in (("a write of +0C", P + 0x0C, 1, False), ("a word at +00", P + 0x00, 0b1111, True),
                            ("a byte at +04", P + 0x04, 0b0001, True), ("a byte on lane 1 at +00", P + 0x00, 0b0010, True),
                            ("a halfword at +08", P + 0x08, 0b0011, True), ("+10", P + 0x10, 0b0001, True),
                            ("a write of +F0", P + 0xF0, 0b0001, False), ("a write of +F4", P + 0xF4, 0b1111, False),
                            ("a halfword at +F4", P + 0xF4, 0b0011, True), ("window 6's page", PAGE(6), 0b0011, True),
                            ("the window below the page", P - 0x100, 0b0011, True),
                            ("where the card's memory was, FE05_0000", 0xFE050000, 0b0011, True)):
        b.chip_got = []
        acked, n, got, seen = b.cycle(a, be, rd, 0x5A5A5A5A)
        ck(not acked and n == 1, "%s is answered (acked %s, %d clocks)" % (what, acked, n))
        ck(not low(seen, "CS") and not b.chip_got and b.v("BUSY") == 0, "%s reaches the chip or the command latch" % what)

    # ---- the jumpers: another window, the two devices on levels of their own, TEXT
    L = card_video.LV
    # (window 3 and levels 3 and 6: numbers that are not their own mirror image, so swapped jumper bits show)
    b2 = Bench({"WIN0": 1, "WIN1": 1, "WIN2": 0, L(0)[0]: 1, L(0)[1]: 1, L(0)[2]: 0, L(1)[0]: 0, L(1)[1]: 1, L(1)[2]: 1, "TEXTN": 0})
    Q = PAGE(3)
    acked, n, word, got, seen = b2.read(Q + 0xF0, 0b0001)
    ck2 = lambda ok, what: b.check(ok, "jumpers: " + what)
    ck2(acked and word == card_video.CARD_ID, "the card is not at window 3")
    acked, n, word, got, seen = b2.read(P + 0xF0, 0b0001)
    ck2(not acked, "the card still answers at window 5")
    acked, n, word, got, seen = b2.read(Q + 0x08, 0b0001)
    ck2(word == 0x08, "the status with TEXT fitted is %s (want 08)" % word)
    b2.load(0x44); b2.put(INTREQ=1, IRQ=0); b2.settle()
    ck2([k for k in range(8) if b2.v("-IRQ%d" % k) == 0] == [3, 6], "the two devices pull %s (want -IRQ3 and -IRQ6)"
        % [k for k in range(8) if b2.v("-IRQ%d" % k) == 0])
    for lvl, want in ((3, 0x30), (6, 0x31)):
        acked, n, got, seen = b2.cycle(lvl << 2, 0, True, iack=True)
        vec = sum((got[i] or 0) << i for i in range(8))
        ck2(acked and vec == want, "level %d's vector is %02X (want %02X)" % (lvl, vec, want))
    acked, n, got, seen = b2.cycle(4 << 2, 0, True, iack=True)
    ck2(not acked, "level 4's acknowledge cycle is answered")
    b.errs = [e for e in b.errs + b2.errs if e]
    return b.errs

def main():
    try:
        errs = run()
    except SimError as e:
        errs = ["the simulation stopped: %s" % e]
    if errs:
        print("\n".join(errs[:40]))
        sys.exit("video card, bus side at gate level: %d checks failed" % len(errs))
    print("video card, bus side at gate level: the chip's four-clock cycle (CS, RS, R/W, the buffers, -WAIT, half by half), "
          "every register and its size, 12 bus errors, the PS/2 handshake, both interrupts and their vectors, the jumpers")

if __name__ == "__main__":
    main()
