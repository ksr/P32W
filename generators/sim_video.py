#!/usr/bin/env python3
"""The video card at gate level (docs/video-card.md, D67): what sim_sys.py
--video needs besides the card's netlist.

  * DotClock: the card's 25.175 MHz oscillator against the CPU card's 24 MHz
    one. Time runs in units of 1/(24 x 25.175) us / 1000: the CPU's
    oscillator ticks at j x 25175, the dot clock rises at k x 24000 + 1 and
    falls at k x 24000 + 12001 (the +1: never at the same instant as a
    tick). p32uemu -C uses the same schedule (p32io.h, vid_*), so the two
    agree on the wait states and the status register.
  * PS2Device: a keyboard or mouse on a socket, driving its clock and data
    lines open-drain at a fast pace (a half bit = 12 bus clocks), with
    p32io.h's model behind it (bm_ps2_*): the bytes it sends, its answers
    to commands.
  * expected(): the picture the card should draw from its RAM and registers;
    Frame: the card's VGA outputs sampled every dot, cut into 640 x 480 by
    the syncs, to compare.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_font                                         # noqa: E402

OSC_T, DOT_T = 25175, 24000                             # the periods, in the common unit

class DotClock:
    def __init__(self, sim, net):
        self.s, self.net = sim, net
        self.k = 0                                      # the next edge: 2k rising, 2k+1 falling
        self.level = 0
        self.rises = 0
        self.on_rise = None
        sim._force(net, 0)

    def edge_time(self, e):
        return (e >> 1) * DOT_T + 1 + (e & 1) * (DOT_T // 2)

    def until(self, t):
        """Every dot clock edge before time t."""
        s = self.s
        while self.edge_time(self.k) < t:
            self.level = 1 - (self.k & 1)
            s._force(self.net, self.level)
            s.settle()
            if self.level:
                self.rises += 1
                if self.on_rise:
                    self.on_rise()
            self.k += 1

class PS2Device:
    H = 12                                              # bus clocks a half bit

    def __init__(self, sys_, port, ref):
        self.sys, self.lib, self.port, self.ref = sys_, sys_.lib, port, ref
        self.clk = self.dat = None                      # what it drives: 0, or None (let go)
        self.state, self.t, self.idle = "idle", 0, 0
        self.inh = False
        self.byte = None                                # a byte being sent (kept if the host interrupts)
        self.bits, self.i, self.rx = [], 0, []
        self.errors = []

    def model(self, ref, edge):
        return {} if edge else {"5": self.clk, "1": self.dat}

    def lines(self):
        g = lambda p: self.sys.sim.get(self.ref, p)
        return g("5"), g("1")

    def drive(self, clk, dat):
        if (clk, dat) != (self.clk, self.dat):
            self.clk, self.dat = clk, dat
            self.sys.sim._poke.add(self.ref)

    def step(self):
        """One bus clock."""
        ck, dt = self.lines()
        if self.state == "idle":
            if ck == 0:
                self.inh, self.idle = True, 0
            elif self.inh and dt == 0:                  # the host's request to send
                self.inh = False
                self.state, self.i, self.t, self.rx = "rx", 0, self.H, []
            else:
                self.inh = False
                self.idle += 1
                if self.idle >= 2 * self.H and (self.byte is not None or self.lib.bm_ps2_avail(self.port)):
                    if self.byte is None:
                        self.byte = self.lib.bm_ps2_get(self.port)
                    d = [self.byte >> b & 1 for b in range(8)]
                    self.bits = [0] + d + [1 - (sum(d) & 1)] + [1]
                    self.state, self.i, self.t = "tx", 0, 0
                    self.phase = "set"
            return
        if self.t:
            self.t -= 1
            return
        if self.state == "tx":                          # device to host: data while the clock is high
            if self.phase == "set":
                self.drive(None, 0 if self.bits[self.i] == 0 else None)
                self.phase, self.t = "low", self.H
            elif self.phase == "low":
                if ck == 0:                             # the host holds the clock: it is not listening
                    self.drive(None, None)
                    self.state, self.idle = "idle", 0
                    return
                self.drive(0, self.dat)
                self.phase, self.t = "high", self.H
            else:
                self.drive(None, self.dat)
                self.i += 1
                if self.i == 11:                        # sent (the host took the stop bit at the fall)
                    self.drive(None, None)
                    self.byte = None
                    self.state, self.idle = "idle", 0
                else:
                    self.phase, self.t = "set", 0
        elif self.state == "rx":                        # host to device: the device clocks, reads as the clock rises
            if self.clk is None:
                if self.i == 10:
                    self.drive(None, 0)                 # the acknowledge: data low through the 11th clock
                    self.drive(0, 0)
                else:
                    self.drive(0, None)
                self.t = self.H
            else:
                if self.i < 10:
                    self.rx.append(dt)
                self.drive(None, None if self.i == 10 else self.dat)
                self.i += 1
                self.t = self.H
                if self.i == 11:
                    d = self.rx[:8]
                    v = sum(b << k for k, b in enumerate(d))
                    if (sum(d) + self.rx[8]) & 1 != 1 or self.rx[9] != 1:
                        self.errors.append("port %d: command %02X: parity %d, stop %d" % (self.port, v, self.rx[8], self.rx[9]))
                    self.lib.bm_ps2_command(self.port, v)
                    self.state, self.idle = "idle", 0

# ---------------------------------------------------------------- the picture
def expected(ram, ctl, row, col, blink=0):
    """640 x 480 dots (0 dark, 1 lit, 2 bright) from the card's RAM (64 KiB)
    and registers, as the card draws them (docs/video-card.md)."""
    font = gen_font.rom()
    img = [[0] * 640 for _ in range(480)]
    for y in range(480):
        tl = y - 40
        for cx in range(80):
            t = [0] * 8
            bright = 0
            if ctl & 1 and 0 <= tl < 400:
                r, sc = tl >> 4, tl & 15
                cell = 2 * (r * 80 + cx)
                ch, at = ram[cell], ram[cell + 1]
                g = font[ch * 16 + sc] if not (at & 8 and blink) else 0
                cur = ctl & 8 and r == row and cx == col and sc >= 14 and not blink
                for x in range(8):
                    d = g >> (7 - x) & 1
                    if at & 2 and sc == 15:
                        d = 1
                    d ^= (at & 1) ^ (1 if cur else 0)
                    t[x] = d
                bright = at >> 2 & 1
            ob = ram[0x1000 + 80 * y + cx] if ctl & 2 else 0
            for x in range(8):
                o = ob >> (7 - x) & 1
                p = (t[x] ^ o) if ctl & 4 else (t[x] | o)
                img[y][8 * cx + x] = (2 if bright and p else 1 if p else 0)
    return img

class Frame:
    """The card's outputs, one sample a dot (at each rising edge, after the
    VOUT register has taken it)."""
    def __init__(self, sim, tag="VID"):
        self.s, self.tag = sim, tag
        self.samples = []

    def sample(self):
        v = self.s.val
        t = self.tag
        self.samples.append((v.get(t + ".VID", 0), v.get(t + ".VBR", 0), v.get(t + ".HSO", 1), v.get(t + ".VSO", 1)))

    def picture(self):
        """The 640 x 480 dots: line 0 is 35 lines after the line in which
        VSYNC falls (2 sync, 33 back porch); a line's dot 0 is 144 dots after
        the HSYNC before it falls (96 sync, 48 back porch). Also checks the
        syncs' widths and periods, and that nothing is lit outside."""
        sm = self.samples
        vfall = [i for i in range(1, len(sm)) if sm[i - 1][3] == 1 and sm[i][3] == 0]
        hfall = [i for i in range(1, len(sm)) if sm[i - 1][2] == 1 and sm[i][2] == 0]
        errs = []
        if not vfall:
            return None, ["no VSYNC"]
        per = [b - a for a, b in zip(hfall, hfall[1:])]
        if any(p != 800 for p in per):
            errs.append("HSYNC periods %s" % sorted(set(per)))
        hw = set()
        for i in hfall:
            n = 0
            while i + n < len(sm) and sm[i + n][2] == 0:
                n += 1
            if i + n < len(sm):
                hw.add(n)
        if hw != {96}:
            errs.append("HSYNC widths %s" % sorted(hw))
        if len(vfall) > 1 and vfall[1] - vfall[0] != 525 * 800:
            errs.append("VSYNC period %d" % (vfall[1] - vfall[0]))
        v0 = vfall[0]
        n = 0
        while v0 + n < len(sm) and sm[v0 + n][3] == 0:
            n += 1
        if n != 2 * 800:
            errs.append("VSYNC width %d dots" % n)
        # VSYNC falls as the frame's line 490 begins (VC turns 450 two
        # characters before its dot 0); that line's HSYNC is the next to fall,
        # and each line's dot 0 is 144 dots after the previous line's HSYNC
        h490 = min(h for h in hfall if h > v0)
        start = h490 + 34 * 800 + 144
        if start + 479 * 800 + 640 > len(sm):
            return None, errs + ["frame incomplete"]
        pic = [[(2 if sm[start + 800 * y + x][1] else 1 if sm[start + 800 * y + x][0] else 0) for x in range(640)]
               for y in range(480)]
        # nothing lit outside the 640 x 480
        for y in range(480):
            base = start + 800 * y
            for x in list(range(-144, 0)) + list(range(640, 656)):
                i = base + x
                if 0 <= i < len(sm) and (sm[i][0] or sm[i][1]):
                    errs.append("lit dot outside the picture: line %d, dot %d" % (y, x))
                    break
        return pic, errs

def save_png(img, path):
    try:
        from PIL import Image
    except ImportError:
        return False
    h, w = len(img), len(img[0])
    im = Image.new("L", (w, h))
    im.putdata([(0, 170, 255)[p] for row in img for p in row])
    im.save(path)
    return True
