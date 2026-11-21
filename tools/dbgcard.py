#!/usr/bin/env python3
"""dbgcard.py -- the Mac's side of the debug card (docs/debug-card.md; D135): its Pico's firmware
(hardware/debug-card/firmware) on USB.

    python3 tools/dbgcard.py [-p PORT] COMMAND ...

      raw LINE ...           firmware commands as they are, each reply printed (raw HELP lists them)
      status                 the card's status, CTRL0 / CTRL1, which cards are in
      read ADDR [N]          N words read by DMA (the switch at DRIVE)
      write ADDR WORD ...    words written by DMA
      load FILE ADDR         a binary file written by DMA from ADDR (a program, a kernel)
      save ADDR N FILE       N words read by DMA into a binary file
      capture [-t ADDR[/MASK]] [-r FROM-TO] [-n POST] [--fast] [--timeout S] [--vcd FILE] [--trace FILE]
                             arm a capture (the switch at LISTEN): a trigger on any pattern given (up to eight;
                             none: Ctrl-C or the time-out stops it), POST samples kept after it; then read
                             back, oldest first, into a VCD file (GTKWave) and/or a bus trace (one line a cycle)
      step [N]               the machine on the card's XCLK: N of the CPU's eight phases (XCLK ON first)
      reset [--hold]         the machine reset from the card (the switch at DRIVE; D136): PWROK low for 10 ms,
                             or, --hold, until `reset --release`

The port is the Pico's USB serial port (/dev/cu.usbmodem*), found by itself when there is one. Numbers are hex,
as the firmware's are.
"""
import argparse, glob, os, re, select, sys, termios, time, tty

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "generators"))
import card_debug                                       # noqa: E402  (the slices: which line is which bit)

NSAMPLE = 32768

class CardError(Exception):
    def __init__(self, reply, lines=()):
        super().__init__(reply)
        self.lines = list(lines)

class Serial:
    """The Pico's USB serial port: a command a line, its reply to the line beginning OK or ERR."""
    def __init__(self, port=None, timeout=30.0):
        if not port:
            found = sorted(glob.glob("/dev/cu.usbmodem*"))
            if not found:
                raise SystemExit("dbgcard: no /dev/cu.usbmodem* (is the Pico plugged in?); -p PORT")
            port = found[0]
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        attr = termios.tcgetattr(self.fd)
        attr[2] |= termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW, attr)
        self.timeout, self.buf = timeout, b""
    def command(self, line):
        os.write(self.fd, (line + "\n").encode())
        out, t0 = [], time.time()
        while True:
            while b"\n" not in self.buf:
                r, _, _ = select.select([self.fd], [], [], 0.5)
                if r:
                    self.buf += os.read(self.fd, 65536)
                elif time.time() - t0 > self.timeout:
                    raise CardError("dbgcard: no reply to %r" % line, out)
            ln, self.buf = self.buf.split(b"\n", 1)
            ln = ln.decode(errors="replace").rstrip("\r")
            out.append(ln)
            if ln.startswith(("OK", "ERR")):
                return out

class Sim:
    """For the tests (generators/sim_dbgfw.py): the firmware as the simulator runs it, fw(line) -> reply."""
    def __init__(self, fw):
        self.fw = fw
    def command(self, line):
        return self.fw(line).strip().splitlines()

class Card:
    def __init__(self, transport):
        self.t = transport
    def cmd(self, line):
        out = self.t.command(line)
        if not out or out[-1].startswith("ERR"):
            raise CardError(out[-1] if out else "no reply", out)
        return out

    # ---- memory, by DMA
    def read(self, addr, n=1):
        words = []
        for k in range(0, n, 0x1000):
            m = min(0x1000, n - k)
            for ln in self.cmd("RD %08X %X" % (addr + 4 * k, m))[:-1]:
                words += [None if w.startswith("-") else int(w, 16) for w in ln.split(":")[1].split()]
        return words
    def write(self, addr, words, per_line=32):
        for k in range(0, len(words), per_line):
            self.cmd("WR %08X %s" % (addr + 4 * k, " ".join("%08X" % w for w in words[k:k + per_line])))
    def load(self, data, addr):
        data = bytes(data) + b"\0" * (-len(data) % 4)
        self.write(addr, [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)])
    def save(self, addr, n):
        return b"".join((w or 0).to_bytes(4, "little") for w in self.read(addr, n))

    # ---- the capture
    def capture(self, triggers=(), ranges=(), post=1, fast=False, timeout=10.0, wait=None):
        """Arm, wait for the end, read back. Returns (samples oldest first: 22 bytes each, fast)."""
        wait = wait or (lambda: time.sleep(0.05))
        self.cmd("TRIG CLR")
        pats = [("ADDR %08X %08X" % (a, m)) for a, m in triggers] + [("RANGE %08X %08X" % r) for r in ranges]
        if len(pats) > 8:
            raise CardError("dbgcard: eight trigger patterns at the most")
        for i, p in enumerate(pats):
            self.cmd("TRIG %d %s" % (i, p))
        self.cmd("ARM %s%X" % ("6M " if fast else "", post))
        t0 = time.time()
        try:
            while True:
                if "done=1" in self.cmd("CAP")[-1]:
                    break
                if not pats or time.time() - t0 > timeout:
                    raise KeyboardInterrupt
                wait()
        except KeyboardInterrupt:
            self.cmd("STOP")
        return self.dump(), fast
    def dump(self):
        out = self.cmd("DUMP")
        head = next(ln for ln in out if ln.startswith("CAP "))
        end = int(re.search(r"end=([0-9A-F]{4})", head).group(1), 16)
        wrapped = re.search(r"wrapped=1", head) is not None
        smp = {}
        for ln in out:
            m = re.match(r"([0-9A-F]{4}):([0-9A-F]+)$", ln)
            if m:
                smp[int(m.group(1), 16)] = bytes.fromhex(m.group(2))
        order = [(end + i) % NSAMPLE for i in range(NSAMPLE)] if wrapped else list(range(end))
        return [smp[a] for a in order if a in smp]

# ---------------------------------------------------------------- the samples as lines
SL = {name: (k, i) for k, (g, lines) in enumerate(card_debug.SLICES) for i, name in enumerate(lines)
      if name != card_debug.GND_}

def line(sample, name):
    k, i = SL[name]
    return sample[k] >> i & 1

def bus(sample, prefix, lo, hi):
    """the lines prefix<lo> to prefix<hi> as a number, each at its own bit (A2 at bit 2)"""
    return sum(line(sample, "%s%d" % (prefix, i)) << i for i in range(lo, hi + 1))

def trace(samples, out):
    """one line a sample with a bus cycle on it: the sample, R / W / IACK, the address, the byte enables,
    the data, whether a card answered"""
    out.write("sample  cycle  address   BE  data      -ACK\n")
    for n, s in enumerate(samples):
        kind = "R" if not line(s, "-MRD") else "W" if not line(s, "-MWR") else "IACK" if not line(s, "-IACK") else None
        if kind:
            out.write("%6d  %-5s  %08X  %X   %08X  %s\n" % (n, kind, bus(s, "A", 2, 31), bus(s, "BE", 0, 3),
                                                       bus(s, "D", 0, 31), "low" if not line(s, "-ACK") else "high"))

VECTORS = [("D", 0, 31), ("A", 2, 31), ("BE", 0, 3), ("LR", 0, 31), ("-IRQ", 0, 7), ("-BREQ", 3, 10),
           ("-BGNT", 3, 10), ("FC", 0, 3), ("RSRC", 0, 3), ("RDST", 0, 2), ("MEM", 0, 2), ("SIZE", 0, 1),
           ("CRSEL", 0, 2)]

def vcd(samples, fast, out):
    """the samples as a VCD file: each line a wire, the buses vectors (A as A[31:2]); one sample a CLK period
    (333 ns at 3 MHz) or, at 6 MHz, 166.7 ns (D142)"""
    period = 166667 if fast else 333333
    taken = set()
    sigs = []                                           # (vcd name, width, value of a sample)
    for prefix, lo, hi in VECTORS:
        names = ["%s%d" % (prefix, i) for i in range(lo, hi + 1)]
        if all(n in SL for n in names):
            taken.update(names)
            sigs.append(("%s[%d:%d]" % (prefix.lstrip("-") + ("_N" if prefix.startswith("-") else ""), hi, lo),
                         hi - lo + 1, lambda s, p=prefix, a=lo, b=hi: bus(s, p, a, b) >> a))
    for name in sorted(SL):
        if name not in taken:
            sigs.append((name.lstrip("-") + ("_N" if name.startswith("-") else ""), 1, lambda s, n=name: line(s, n)))
    ids = ["".join(chr(33 + (k // 94 ** j) % 94) for j in range(2)) for k in range(len(sigs))]
    out.write("$date %s $end\n$version P32W tools/dbgcard.py $end\n$timescale 1ps $end\n" % time.ctime())
    out.write("$scope module p32w $end\n")
    for (name, width, f), i in zip(sigs, ids):
        out.write("$var wire %d %s %s $end\n" % (width, i, name.replace("[", " [")))
    out.write("$upscope $end\n$enddefinitions $end\n")
    last = {}
    for n, s in enumerate(samples):
        out.write("#%d\n" % (n * period))
        for (name, width, f), i in zip(sigs, ids):
            v = f(s)
            if last.get(i) != v:
                last[i] = v
                out.write(("%d%s\n" % (v, i)) if width == 1 else ("b%s %s\n" % (format(v, "b"), i)))
    out.write("#%d\n" % (len(samples) * period))

# ---------------------------------------------------------------- the command line
def hexint(t):
    return int(t, 16)

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="\n".join(__doc__.split("\n")[1:]))
    ap.add_argument("-p", dest="port")
    sub = ap.add_subparsers(dest="what", required=True)
    p = sub.add_parser("raw"); p.add_argument("line", nargs="+")
    sub.add_parser("status")
    p = sub.add_parser("read"); p.add_argument("addr", type=hexint); p.add_argument("n", type=hexint, nargs="?", default=1)
    p = sub.add_parser("write"); p.add_argument("addr", type=hexint); p.add_argument("words", type=hexint, nargs="+")
    p = sub.add_parser("load"); p.add_argument("file"); p.add_argument("addr", type=hexint)
    p = sub.add_parser("save"); p.add_argument("addr", type=hexint); p.add_argument("n", type=hexint); p.add_argument("file")
    p = sub.add_parser("capture")
    p.add_argument("-t", dest="trig", action="append", default=[], help="ADDR or ADDR/MASK (hex)")
    p.add_argument("-r", dest="range", action="append", default=[], help="FROM-TO (hex)")
    p.add_argument("-n", dest="post", type=hexint, default=1)
    p.add_argument("--fast", action="store_true", help="6 MHz samples, not one a CLK")
    p.add_argument("--timeout", type=float, default=10.0)
    p.add_argument("--vcd"); p.add_argument("--trace")
    p = sub.add_parser("step"); p.add_argument("n", type=hexint, nargs="?", default=1)
    p = sub.add_parser("reset"); p.add_argument("--hold", action="store_true"); p.add_argument("--release", action="store_true")
    a = ap.parse_args()
    card = Card(Serial(a.port))
    try:
        if a.what == "raw":
            for ln in [" ".join(a.line)] if len(a.line) > 1 or " " in a.line[0] else a.line:
                print("\n".join(card.t.command(ln)))
        elif a.what == "status":
            print("\n".join(card.cmd("ST") + card.cmd("CARDS")))
        elif a.what == "reset":                         # (PWROK held low: the CPU card holds RESET, D136)
            if a.release:
                print("\n".join(card.cmd("MRES OFF")))
            else:
                print("\n".join(card.cmd("MRES ON")))
                if not a.hold:
                    time.sleep(0.01)
                    print("\n".join(card.cmd("MRES OFF")))
        elif a.what == "read":
            for k, w in enumerate(card.read(a.addr, a.n)):
                print("%08X: %s" % (a.addr + 4 * k, "--------" if w is None else "%08X" % w))
        elif a.what == "write":
            card.write(a.addr, a.words)
        elif a.what == "load":
            data = open(a.file, "rb").read()
            t0 = time.time()
            card.load(data, a.addr)
            print("%d bytes at %08X in %.1f s" % (len(data), a.addr, time.time() - t0))
        elif a.what == "save":
            open(a.file, "wb").write(card.save(a.addr, a.n))
        elif a.what == "capture":
            trigs = [(int(t.split("/")[0], 16), int(t.split("/")[1], 16) if "/" in t else 0xFFFFFFFC) for t in a.trig]
            rngs = [tuple(int(x, 16) for x in r.split("-")) for r in a.range]
            samples, fast = card.capture(trigs, rngs, a.post, a.fast, a.timeout)
            print("%d samples" % len(samples))
            if a.vcd:
                with open(a.vcd, "w") as fh:
                    vcd(samples, fast, fh)
            if a.trace:
                with open(a.trace, "w") as fh:
                    trace(samples, fh)
            if not a.vcd and not a.trace:
                trace(samples, sys.stdout)
        elif a.what == "step":
            card.cmd("XSTEP %X" % a.n)
    except CardError as e:
        sys.exit("dbgcard: %s" % e)

if __name__ == "__main__":
    main()
