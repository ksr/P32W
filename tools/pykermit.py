#!/usr/bin/env python3
"""pykermit.py -- a small Kermit for the host side (D25): the automated
tests drive P32W's kermit with it, over the emulator's TCP terminal (-T) or a
serial device.

    pykermit.py [--tcp PORT | --line DEV] send FILE ...      (P32W runs kermit -r or -x)
    pykermit.py ... get NAME ...                             (P32W runs kermit -x)
    pykermit.py ... receive                                  (P32W runs kermit -s)
    pykermit.py ... dir [DIR] | cd DIR | finish

Long packets, sliding windows, CRC-16, attribute packets (binary / text,
size, time in UTC). Everyday use from the Mac is C-Kermit; this exists so the
tests need nothing installed.
"""
import argparse, os, socket, sys, time

SOH = 1
DEBUG = bool(os.environ.get("PYKERMIT_DEBUG"))
def tochar(x): return x + 32
def unchar(x): return x - 32
def ctl(x): return x ^ 64

def crc16(data):
    crc = 0
    for c in data:
        q = (crc ^ c) & 15
        crc = (crc >> 4) ^ (q * 0o10201)
        q = (crc ^ (c >> 4)) & 15
        crc = (crc >> 4) ^ (q * 0o10201)
    return crc

def check(data, t):
    if t == 3:
        c = crc16(data)
        return bytes([tochar((c >> 12) & 15), tochar((c >> 6) & 63), tochar(c & 63)])
    s = sum(data)
    if t == 2:
        return bytes([tochar((s >> 6) & 63), tochar(s & 63)])
    return bytes([tochar((s + ((s & 192) >> 6)) & 63)])

class KermitError(Exception):
    pass

class Line:
    """bytes to and from the other Kermit"""
    def __init__(self, tcp=None, dev=None):
        if tcp:
            self.sock = socket.create_connection(("127.0.0.1", tcp))
            self.fd = None
        else:
            import termios, tty
            self.sock = None
            self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
            tty.setraw(self.fd)
        self.buf = b""
        self.loss, self.rng = 0.0, None      # (tests: drop this share of packets, both ways)
    def lossy(self, share, seed=1):
        import random
        self.loss, self.rng = share, random.Random(seed)
    def drop(self):
        return self.loss and self.rng.random() < self.loss
    def write(self, b):
        if self.sock: self.sock.sendall(b)
        else: os.write(self.fd, b)
    def read(self, timeout):
        import select
        end = time.time() + timeout
        while not self.buf:
            left = end - time.time()
            if left <= 0: return None
            src = self.sock if self.sock else self.fd
            r, _, _ = select.select([src], [], [], left)
            if not r: return None
            chunk = self.sock.recv(65536) if self.sock else os.read(self.fd, 65536)
            if not chunk: raise KermitError("the line closed")
            self.buf += chunk
        c, self.buf = self.buf[0], self.buf[1:]
        return c
    def expect(self, text, timeout=60):
        """terminal use (logging in): read until text appears; what was read"""
        got = b""
        end = time.time() + timeout
        while not got.endswith(text.encode()):
            c = self.read(max(0.01, end - time.time()))
            if c is None: raise KermitError("timed out waiting for %r; got %r" % (text, got[-200:]))
            got += bytes([c])
        return got.decode("latin-1")

class Kermit:
    def __init__(self, line, maxlen=1000, window=3, log=None):
        self.line = line
        self.want_len, self.want_win = maxlen, window
        self.chk, self.spsiz, self.window = 1, 80, 1
        self.qctl, self.qbin, self.rept = ord("#"), 0, 0
        self.attrs = False
        self.seq = 0
        self.timeout = 10
        self.log = log or (lambda *a: None)
        self.stats = {"packets": 0, "retries": 0}

    # -------------------------------------------------- packets
    def spack(self, typ, n, data=b""):
        t = 1 if typ in "SI" or (typ == "Y" and self.force1) else self.chk
        cl = 3 if t == 3 else t
        if len(data) + 2 + cl <= 94:
            body = bytes([tochar(len(data) + 2 + cl), tochar(n & 63), ord(typ)])
        else:
            total = len(data) + cl
            h = bytes([tochar(0), tochar(n & 63), ord(typ), tochar(total // 95), tochar(total % 95)])
            s = sum(h)
            body = h + bytes([tochar((s + ((s & 192) >> 6)) & 63)])
        body += data
        if not self.line.drop():
            self.line.write(bytes([SOH]) + body + check(body, t) + b"\r")
        self.stats["packets"] += 1
        if DEBUG: print(">", typ, n & 63, data[:40], file=sys.stderr)
    force1 = False

    def rpack(self, timeout=None):
        """(type, seq, data); type 'T' on timeout, 'Q' if damaged"""
        timeout = timeout or self.timeout
        rd = lambda: self.line.read(timeout)
        while True:
            c = rd()
            if c is None: return "T", 0, b""
            if c != SOH: continue
            while True:
                hdr = []
                for _ in range(3):
                    c = rd()
                    if c is None: return "T", 0, b""
                    if c == SOH: break
                    hdr.append(c)
                if len(hdr) < 3: continue           # a new SOH: start over
                ln, n, typ = unchar(hdr[0]), unchar(hdr[1]), chr(hdr[2])
                buf = bytes(hdr)
                if ln == 0:
                    ext = bytes(rd() for _ in range(3))
                    s = sum(buf + ext[:2])
                    if ext[2] != tochar((s + ((s & 192) >> 6)) & 63): return "Q", n, b""
                    total = unchar(ext[0]) * 95 + unchar(ext[1])
                    buf += ext
                else:
                    total = ln - 2
                rest = bytearray()
                while len(rest) < total:
                    c = rd()
                    if c is None: return "T", 0, b""
                    rest.append(c)
                for t in ([1] if typ in "SI" else []) + [self.chk, 1, 3, 2]:
                    cl = 3 if t == 3 else t
                    if total >= cl and check(buf + bytes(rest[:total - cl]), t) == bytes(rest[total - cl:]):
                        if DEBUG: print("<", typ, n, bytes(rest[:total - cl])[:40], file=sys.stderr)
                        if self.line.drop(): break           # (lost on the way)
                        return typ, n, bytes(rest[:total - cl])
                if DEBUG: print("< damaged", typ, n, file=sys.stderr)
                return "Q", n, b""

    # -------------------------------------------------- data
    def encode(self, data):
        out = bytearray()
        i = 0
        while i < len(data):
            c = data[i]
            run = 1
            while self.rept and i + run < len(data) and data[i + run] == c and run < 94: run += 1
            pre = b""
            if self.rept and run >= 3:
                pre = bytes([self.rept, tochar(run)])
            else:
                run = 1
            e = bytearray()
            c7 = c & 0x7F
            if self.qbin and c & 0x80:
                e.append(self.qbin); c = c7
            if c7 < 32 or c7 == 127:
                e.append(self.qctl); c = ctl(c)
            elif c7 == self.qctl or (self.qbin and c7 == self.qbin) or (self.rept and c7 == self.rept):
                e.append(self.qctl)
            e.append(c)
            out += pre + e
            i += run
        return bytes(out)

    def chunks(self, data, room):
        """data cut so each piece encodes into at most room bytes"""
        i = 0
        while i < len(data):
            lo, hi = i + 1, min(len(data), i + room)
            while lo < hi:                      # the longest prefix that fits
                mid = (lo + hi + 1) // 2
                if len(self.encode(data[i:mid])) <= room: lo = mid
                else: hi = mid - 1
            yield self.encode(data[i:lo])
            i = lo

    def decode(self, s):
        out = bytearray()
        i = 0
        while i < len(s):
            c = s[i]; i += 1
            r, b8 = 1, 0
            if self.rept and c == self.rept: r = unchar(s[i]); c = s[i + 1]; i += 2
            if self.qbin and c == self.qbin: b8 = 0x80; c = s[i]; i += 1
            if c == self.qctl:
                c = s[i]; i += 1
                if 0x3F <= (c & 0x7F) <= 0x5F: c = ctl(c)
            out += bytes([c | b8]) * r
        return bytes(out)

    # -------------------------------------------------- negotiation
    def init_data(self):
        return bytes([tochar(94), tochar(self.timeout), tochar(0), ctl(0), tochar(13), ord("#"), ord("Y"),
                      ord("3"), ord("~"), tochar(2 | 4 | 8), tochar(self.want_win),
                      tochar(self.want_len // 95), tochar(self.want_len % 95)])

    def take_init(self, d):
        self.spsiz = unchar(d[0]) if d else 80
        self.qctl = d[5] if len(d) > 5 else ord("#")
        q8 = d[6] if len(d) > 6 else ord("N")
        self.qbin = q8 if (32 < q8 < 63 or 95 < q8 < 127) else 0
        self.chk = 3 if len(d) > 7 and d[7] == ord("3") else 1
        self.rept = ord("~") if len(d) > 8 and d[8] == ord("~") else 0
        self.window, self.attrs = 1, False
        if len(d) > 9:
            capas = unchar(d[9]); k = 9
            while k < len(d) and unchar(d[k]) & 1: k += 1
            self.attrs = bool(capas & 8)
            if capas & 4 and k + 1 < len(d): self.window = max(1, min(unchar(d[k + 1]), self.want_win))
            if capas & 2 and k + 3 < len(d):
                self.spsiz = min(unchar(d[k + 2]) * 95 + unchar(d[k + 3]) or 500, self.want_len)

    def room(self):
        cl = 3 if self.chk == 3 else self.chk
        return self.spsiz - (7 if self.spsiz > 94 else 3) - cl

    def exchange(self, typ, data=b"", tries=10):
        for _ in range(tries):
            self.spack(typ, self.seq, data)
            while True:
                t, n, d = self.rpack()
                if t == "Y" and n == self.seq:
                    self.seq = (self.seq + 1) & 63
                    return d
                if t == "N" and n == ((self.seq + 1) & 63):
                    self.seq = (self.seq + 1) & 63
                    return b""
                if t == "E": raise KermitError("the other side: " + self.decode(d).decode("latin-1"))
                if t in "TNQ":
                    self.stats["retries"] += 1
                    break
        raise KermitError("too many retries (%s)" % typ)

    def send_init(self, typ="S"):
        self.seq, self.chk = 0, 1
        for _ in range(10):
            self.spack(typ, 0, self.init_data())
            t, n, d = self.rpack()
            if t == "Y" and n == 0:
                self.take_init(d)
                self.seq = 1
                return
            if t == "E": raise KermitError("the other side: " + self.decode(d).decode("latin-1"))
        raise KermitError("no answer to " + typ)

    # -------------------------------------------------- sending
    def send_data(self, data):
        pieces = list(self.chunks(data, self.room()))
        base, nxt = 0, 0
        acked = [False] * len(pieces)
        tries = [0] * len(pieces)
        seq0 = self.seq
        while base < len(pieces):
            while nxt < len(pieces) and nxt - base < self.window:
                self.spack("D", (seq0 + nxt) & 63, pieces[nxt]); nxt += 1
            t, n, d = self.rpack()
            if t in "YN":
                off = (n - (seq0 + base)) & 63
                if t == "Y" and off < nxt - base:
                    acked[base + off] = True
                    if d[:1] in (b"X", b"Z"): raise KermitError("cancelled by the receiver")
                elif t == "N" and off < nxt - base:
                    i = base + off; tries[i] += 1; self.stats["retries"] += 1
                    if tries[i] > 10: raise KermitError("too many retries")
                    self.spack("D", (seq0 + i) & 63, pieces[i])
                elif t == "N" and off == nxt - base:
                    for i in range(base, nxt): acked[i] = True
                while base < len(pieces) and acked[base]: base += 1
            elif t == "E":
                raise KermitError("the other side: " + self.decode(d).decode("latin-1"))
            else:                               # timeout / damage: the oldest again
                if base < nxt:
                    tries[base] += 1; self.stats["retries"] += 1
                    if tries[base] > 10: raise KermitError("too many retries")
                    self.spack("D", (seq0 + base) & 63, pieces[base])
        self.seq = (seq0 + len(pieces)) & 63

    def send_file(self, path, as_name=None, text=False, mtime=None):
        data = open(path, "rb").read()
        if text: data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        name = as_name or os.path.basename(path)
        self.exchange("F", self.encode(name.encode()))
        if self.attrs:
            st = os.stat(path)
            t = time.gmtime(mtime if mtime is not None else st.st_mtime)
            a = b""
            for tag, v in (("\"", b"AMJ" if text else b"B8"), ("!", str((st.st_size + 1023) // 1024).encode()),
                           ("1", str(st.st_size).encode()), ("#", time.strftime("%Y%m%d %H:%M:%S", t).encode()),
                           (".", b"U1")):
                a += tag.encode() + bytes([tochar(len(v))]) + v
            r = self.exchange("A", a)
            if r[:1] == b"N": raise KermitError("refused: " + name)
        self.send_data(data)
        self.exchange("Z")
        self.log("sent", path)

    def send(self, paths, text=False):
        self.send_init("S")
        for p in paths: self.send_file(p, text=text)
        self.exchange("B")

    # -------------------------------------------------- receiving
    def receive_after_init(self, dest=".", collect=None):
        """after an S was acknowledged: files (or text, into collect) until B"""
        expect, f, name, mtime, text = self.seq, None, None, None, False
        early = {}
        got = []
        while True:
            t, n, d = self.rpack()
            if t in "TQ":
                self.stats["retries"] += 1
                self.spack("N", expect); continue
            if t == "E": raise KermitError("the other side: " + self.decode(d).decode("latin-1"))
            if t != "D" and n != expect:
                self.spack("Y", n); continue
            if t == "F":
                name = self.decode(d).decode("latin-1")
                f, mtime, text = bytearray(), None, False
                self.spack("Y", n, self.encode(name.encode())); expect = (expect + 1) & 63
            elif t == "X":
                f, name = bytearray(), None
                self.spack("Y", n); expect = (expect + 1) & 63
            elif t == "A":
                i = 0
                while i + 2 <= len(d):
                    tag, ln = chr(d[i]), unchar(d[i + 1]); v = d[i + 2:i + 2 + ln]
                    if tag == '"': text = v[:1] == b"A"
                    if tag == "#":                # (P32W sends UTC)
                        try:
                            import calendar
                            mtime = calendar.timegm(time.strptime(v.decode()[:17], "%Y%m%d %H:%M:%S"))
                        except ValueError: mtime = None
                    i += 2 + ln
                self.spack("Y", n, b"Y"); expect = (expect + 1) & 63
            elif t == "D":
                off = (n - expect) & 63
                if off >= 32: self.spack("Y", n); continue
                if off >= self.window: continue
                if off == 0:
                    f += self.decode(d); self.spack("Y", n); expect = (expect + 1) & 63
                    while expect in early:
                        f += self.decode(early.pop(expect)); expect = (expect + 1) & 63
                else:
                    early[n] = d; self.spack("Y", n); self.spack("N", expect)
            elif t == "Z":
                if f is not None and d[:1] != b"D":
                    data = bytes(f)
                    if text: data = data.replace(b"\r\n", b"\n")
                    if name is None:
                        if collect is not None: collect.append(data)
                    else:
                        p = os.path.join(dest, os.path.basename(name))
                        open(p, "wb").write(data)
                        if mtime: os.utime(p, (mtime, mtime))
                        got.append(p); self.log("received", p)
                f = None
                self.spack("Y", n); expect = (expect + 1) & 63
            elif t == "B":
                self.spack("Y", n)
                return got
            else:
                raise KermitError("unexpected packet " + t)

    def receive(self, dest="."):
        self.chk = 1
        for _ in range(30):
            t, n, d = self.rpack()
            if t == "S":
                self.take_init(d)
                ours = bytearray(self.init_data()); ours[10] = tochar(self.window)
                agreed, self.chk = self.chk, 1
                self.spack("Y", 0, bytes(ours)); self.chk = agreed
                self.seq = 1
                return self.receive_after_init(dest)
            if t == "E": raise KermitError("the other side: " + self.decode(d).decode("latin-1"))
            if t == "T": self.spack("N", 0)
        raise KermitError("nothing to receive")

    # -------------------------------------------------- a server's client
    def get(self, name, dest="."):
        self.send_init("I")
        self.seq, self.chk = 0, 1
        for _ in range(10):
            self.spack("R", 0, self.encode(name.encode()))
            t, n, d = self.rpack()
            if t == "S":
                self.take_init(d)
                ours = bytearray(self.init_data()); ours[10] = tochar(self.window)
                agreed, self.chk = self.chk, 1
                self.spack("Y", 0, bytes(ours)); self.chk = agreed
                self.seq = 1
                return self.receive_after_init(dest)
            if t == "E": raise KermitError("the server: " + self.decode(d).decode("latin-1"))
        raise KermitError("no answer to GET")

    def generic(self, cmd, arg=None):
        """a server command; its text reply"""
        self.send_init("I")
        self.seq, self.chk = 0, 1
        data = cmd.encode() + (bytes([tochar(len(arg))]) + arg.encode() if arg is not None else b"")
        for _ in range(10):
            self.spack("G", 0, self.encode(data))
            t, n, d = self.rpack()
            if t == "Y": return self.decode(d).decode("latin-1")
            if t == "S":
                self.take_init(d)
                ours = bytearray(self.init_data()); ours[10] = tochar(self.window)
                agreed, self.chk = self.chk, 1
                self.spack("Y", 0, bytes(ours)); self.chk = agreed
                self.seq = 1
                out = []
                self.receive_after_init(collect=out)
                return b"".join(out).decode("latin-1")
            if t == "E": raise KermitError("the server: " + self.decode(d).decode("latin-1"))
        raise KermitError("no answer to the server command")

def main():
    ap = argparse.ArgumentParser(description="a small Kermit for the host side of P32W tests")
    ap.add_argument("--tcp", type=int); ap.add_argument("--line")
    ap.add_argument("-e", type=int, default=1000); ap.add_argument("-w", type=int, default=3)
    ap.add_argument("--text", action="store_true")
    ap.add_argument("cmd"); ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    k = Kermit(Line(a.tcp, a.line), a.e, a.w, log=lambda *x: print(*x))
    if a.cmd == "send": k.send(a.args, a.text)
    elif a.cmd == "get":
        for n in a.args: k.get(n)
    elif a.cmd == "receive": k.receive()
    elif a.cmd == "dir": print(k.generic("D", a.args[0] if a.args else ""))
    elif a.cmd == "cd": print(k.generic("C", a.args[0]))
    elif a.cmd == "finish": k.generic("F")
    else: ap.error("unknown command")

if __name__ == "__main__":
    main()
