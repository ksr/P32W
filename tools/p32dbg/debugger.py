"""What the debugger's front ends share: symbols, addresses, the disassembler
(from microcode/p32isa.py, the ISA's single source of truth, as the assembler
is), the microword's fields (from microcode/p32ucode.py, as the control store
is made) and the video card's screen as text.
"""
import bisect, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "microcode"))
sys.path.insert(0, os.path.join(ROOT, "assembler"))
import p32isa                                   # noqa: E402
import p32ucode                                 # noqa: E402

# ---------------------------------------------------------------- symbols
class Symbols:
    """names <-> addresses, from the kernel's map and the monitor's listing"""
    def __init__(self):
        self.by_name, self.addrs, self.names, self.sources = {}, [], [], []

    def add(self, addr, name):
        if name in self.by_name:
            return
        self.by_name[name] = addr
        k = bisect.bisect_right(self.addrs, addr)
        self.addrs.insert(k, addr)
        self.names.insert(k, name)

    def lookup(self, name):
        return self.by_name.get(name, self.by_name.get(name.lower()))

    def near(self, addr, within=0x10000):
        """'name' or 'name+off' for addr, the nearest symbol at or below it; '' if none near"""
        k = bisect.bisect_right(self.addrs, addr) - 1
        if k < 0 or addr - self.addrs[k] > within:
            return ""
        off = addr - self.addrs[k]
        return self.names[k] + ("+%X" % off if off else "")

    def at(self, addr):
        k = bisect.bisect_left(self.addrs, addr)
        return self.names[k] if k < len(self.addrs) and self.addrs[k] == addr else ""

    def load_kernel(self, mapf):
        """kernel/unix.map: its global symbols, and each object's own (statics too), as tools/uprof.py"""
        kdir = os.path.dirname(os.path.abspath(mapf))
        n = 0
        try:
            import p32obj
        except ImportError:
            p32obj = None
        for line in open(mapf):
            m = re.match(r"^([0-9A-F]{8})\s+(\S+)\s*$", line)
            if m:
                self.add(int(m.group(1), 16), m.group(2))
                n += 1
                continue
            m = re.match(r"^(\S+?)(?:\((\S+)\))?\s+text ([0-9A-F]{8}) data", line)
            if m and p32obj and not m.group(2):
                path = os.path.join(kdir, m.group(1))
                if os.path.exists(path):
                    try:
                        for name, s in p32obj.read(path)["symbols"].items():
                            if s["section"] == ".text":
                                self.add(int(m.group(3), 16) + s["value"], name)
                                n += 1
                    except Exception:           # (an object of another format: its globals are enough)
                        pass
        self.sources.append("%s (%d)" % (os.path.relpath(mapf, ROOT), n))

    def load_listing(self, listing, what):
        """an assembler listing (p32asm.py -l): each label at the next address"""
        n, pending = 0, []
        for line in open(listing, errors="replace"):
            src = line[29:] if len(line) > 29 else ""
            lm = re.match(r"\s*([A-Za-z_.][A-Za-z_0-9.]*):", src)
            if lm:                              # (a label: this line's address, or the next line's)
                pending.append(lm.group(1))
            m = re.match(r"^([0-9A-F]{8}) ", line)
            if m:
                for name in pending:
                    self.add(int(m.group(1), 16), name)
                    n += 1
                pending = []
        self.sources.append("%s (%d)" % (what, n))

    def load_monitor(self, asm):
        """the monitor's labels: its source assembled again, with a listing"""
        with tempfile.TemporaryDirectory(prefix="p32dbg") as t:
            lst = os.path.join(t, "monitor.lst")
            r = subprocess.run([sys.executable, os.path.join(ROOT, "assembler", "p32asm.py"), asm,
                                "-o", os.path.join(t, "m.bin"), "-l", lst], capture_output=True)
            if r.returncode == 0 and os.path.exists(lst):
                self.load_listing(lst, os.path.relpath(asm, ROOT))

# ---------------------------------------------------------------- addresses
REGNAMES = {"r%d" % i: i for i in range(16)}
REGNAMES.update({"zero": 0, "sp": 15})

def parse_addr(text, regs=None, syms=None, pc=None):
    """an address: hex (F06A, $F06A, 0xF06A), a symbol, r0..r15, sp, pc, or one of
    these plus or minus a hex offset (cmdloop+10)"""
    text = text.strip()
    m = re.match(r"^(.+?)\s*([+-])\s*(?:0x|\$)?([0-9A-Fa-f]+)$", text)
    if m and not re.match(r"^(0x|\$)?[0-9A-Fa-f]+$", text):
        base = parse_addr(m.group(1), regs, syms, pc)
        off = int(m.group(3), 16)
        return (base + off if m.group(2) == "+" else base - off) & 0xFFFFFFFF
    low = text.lower()
    if low == "pc" and pc is not None:
        return pc
    if low in REGNAMES and regs is not None:
        return regs["r"][REGNAMES[low]]
    if syms is not None and syms.lookup(text) is not None:
        return syms.lookup(text)
    h = text[1:] if text.startswith("$") else text[2:] if low.startswith("0x") else text
    if re.match(r"^[0-9A-Fa-f]+$", h):
        return int(h, 16) & 0xFFFFFFFF
    raise ValueError("not an address: %s" % text)

# ---------------------------------------------------------------- the disassembler
OPS = {op: (name.lower(), fmt) for name, (op, fmt) in p32isa.OPC.items()}
CRNAME = {v: k.lower() for k, v in p32isa.CREGS.items()}

def rn(n):
    return "sp" if n == 15 else "r%d" % n

def s8(x):
    return x - 0x100 if x & 0x80 else x

def s16(x):
    return x - 0x10000 if x & 0x8000 else x

def disasm(pc, ins, nxt=None, syms=None):
    """(text, size, target): the instruction word ins at pc; nxt the next word (LI32's
    value); target: a branch's or jump's destination (else None)"""
    op = ins >> 24
    A, B, C = (ins >> 20) & 15, (ins >> 16) & 15, (ins >> 12) & 15
    imm = ins & 0xFFFF
    if op not in OPS or (ins & p32isa.FMT_ZERO[OPS[op][1]]):
        return (".word 0x%08X" % ins, 4, None)
    name, fmt = OPS[op]

    def lab(t):
        s = syms.at(t) if syms else ""
        return s or "0x%08X" % t
    if fmt == "R":
        return ("%s %s, %s, %s" % (name, rn(A), rn(B), rn(C)), 4, None)
    if fmt == "I":
        return ("%s %s, %s, %d" % (name, rn(A), rn(B), s16(imm)), 4, None)
    if fmt == "IU":
        return ("%s %s, %s, 0x%X" % (name, rn(A), rn(B), imm), 4, None)
    if fmt == "SH":
        return ("%s %s, %s, %d" % (name, rn(A), rn(B), imm & 31), 4, None)
    if fmt == "LUI":
        return ("%s %s, 0x%X" % (name, rn(A), imm), 4, None)
    if fmt in ("LD", "ST"):
        return ("%s %s, %d(%s)" % (name, rn(A), s16(imm), rn(B)), 4, None)
    if fmt == "BR":
        t = (pc + 4 + s16(imm) * 4) & 0xFFFFFFFF
        return ("%s %s, %s, %s" % (name, rn(A), rn(B), lab(t)), 4, t)
    if fmt == "J":
        off = ins & 0xFFFFFF
        off = off - 0x1000000 if off & 0x800000 else off
        t = (pc + 4 + off * 4) & 0xFFFFFFFF
        return ("%s %s" % (name, lab(t)), 4, t)
    if fmt == "JR":
        return ("%s %s" % (name, rn(B)), 4, None)
    if fmt == "N":
        return (name, 4, None)
    if fmt == "CR":
        cr = CRNAME.get(B, "cr%d" % B)
        return (("mfcr %s, %s" % (rn(A), cr)) if name == "mfcr" else ("mtcr %s, %s" % (cr, rn(A))), 4, None)
    if fmt == "X":
        sc = 1 << (ins & 3)
        return ("%s %s, %s, %s%s" % (name, rn(A), rn(B), rn(C), ", %d" % sc if sc > 1 else ""), 4, None)
    if fmt == "P":
        return ("%s %s, %d(%s), %d" % (name, rn(A), s8(imm >> 8), rn(B), s8(imm & 0xFF)), 4, None)
    if fmt == "L32":
        v = nxt if nxt is not None else 0
        s = syms.at(v) if syms and nxt is not None else ""
        return ("%s %s, %s" % (name, rn(B), s or ("0x%X" % v if nxt is not None else "?")), 8, None)
    return (".word 0x%08X" % ins, 4, None)

def words(hexs, addr):
    """{address: word} of the whole, readable words in a mem reply's hex"""
    out = {}
    for k in range(0, len(hexs) // 8):
        h = hexs[8 * k:8 * k + 8]
        if "?" not in h:
            b = bytes.fromhex(h)
            out[addr + 4 * k] = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24
    return out

def code(emu, at, n, syms, before=0):
    """[(addr, word, text, symbol)] of n instructions from at (before: instructions shown
    above it, when they decode as such: a guess, as code before a point is unknown)"""
    start = max(0, at - 4 * before) & ~3
    r = emu.ok("mem %X %d" % (start, 8 * (n + before) + 8))    # (LI32 takes two words)
    w = words(r["hex"], start)
    out, a = [], start
    while len(out) < n + before and a < start + 8 * (n + before):
        if a in w:
            text, size, _ = disasm(a, w[a], w.get(a + 4), syms)
            out.append((a, w[a], text, syms.at(a) if syms else ""))
            a += size
        else:
            out.append((a, None, "??", ""))
            a += 4
    return out

# ---------------------------------------------------------------- the microword
_RSEL = {v: k for k, v in p32ucode.RSEL.items()}
UNAMES = {"ASRC": p32ucode.ASRC, "BSRC": p32ucode.BSRC, "RSRC": p32ucode.RSRC, "RDST": p32ucode.RDST,
          "ALU": p32ucode.ALU, "IMM": p32ucode.IMM, "MEM": p32ucode.MEM, "SEQ": p32ucode.SEQ,
          "COND": p32ucode.COND, "MODE": p32ucode.MODE, "AOFF": p32ucode.AOFF,
          "RSA": p32ucode.RSEL, "RSB": p32ucode.RSEL, "RSW": p32ucode.RSEL}
UNAMES = {k: {v: n for n, v in d.items()} for k, d in UNAMES.items()}
UNAMES["RAISE"] = {v: k for k, v in p32isa.CAUSES.items()}
ISEQ = {v: k for k, v in p32ucode.ISEQ.items()}

def ufields(word):
    """[(field, value, name)] of every field of a microword, in the word's order"""
    w = int(word, 16) if isinstance(word, str) else word
    out = []
    for name, width in p32ucode.FIELDS:
        v = p32ucode.field(w, name)
        out.append((name, v, UNAMES.get(name, {}).get(v, "%d" % v if width > 1 else ("1" if v else "0"))))
    return out

def usummary(word):
    """the fields that do something this cycle, as one line"""
    f = {n: (v, s) for n, v, s in ufields(word)}
    parts = []
    if f["ASRC"][0]: parts.append("A=" + f["ASRC"][1] + ("(%s)" % f["RSA"][1] if f["ASRC"][1] == "REG" else ""))
    if f["BSRC"][0]: parts.append("B=" + f["BSRC"][1] + ("(%s)" % f["RSB"][1] if f["BSRC"][1] == "REG" else
                                                        "(%s)" % f["IMM"][1] if f["BSRC"][1] == "IMM" else ""))
    if f["RSRC"][0]: parts.append("R=" + f["RSRC"][1])
    if f["RDST"][0]: parts.append("->" + f["RDST"][1] + ("(%s)" % f["RSW"][1] if f["RDST"][1] == "REG" else ""))
    if f["ALUGO"][0]: parts.append("ALU " + f["ALU"][1])
    if f["MEM"][0]: parts.append("MEM " + f["MEM"][1])
    for b in ("MDRB", "MARPC", "PCINC", "MARINC", "LOCK"):
        if f[b][0]: parts.append(b)
    if f["MODE"][0]: parts.append("MODE " + f["MODE"][1])
    parts.append("SEQ " + f["SEQ"][1] + ("" if f["COND"][1] == "ZERO" else " if " + f["COND"][1]))
    if f["SEQ"][1] == "TRAP": parts.append("RAISE " + f["RAISE"][1])
    return "  ".join(parts)

def uaddr_name(ua):
    """[SEQ, OPCODE, STEP, COND] as words: 'ADD step 2', 'FETCH', 'TRAP step 1'"""
    seq, op, step, cond = ua
    if seq == 0 and step == 0:
        return "FETCH"
    what = ISEQ.get(op, "?") if seq else OPS.get(op, ("op %02X" % op,))[0].upper()
    return "%s step %d%s" % (what, step, " (cond 1)" if cond else "")

# ---------------------------------------------------------------- the screen
def screen_text(text):
    """the video card's console as lines: the 25 rows of 80 characters the debug server reads from the
    picture's dots (each cell matched against the font; '?' where nothing matches)"""
    lines = [s.rstrip() for s in text.split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    return lines


def mouse_packets(dx, dy):
    """a movement of the PS/2 mouse (steps to the right, steps up) as the packets it takes: 255 steps each way
    at most in one; no movement is one packet too (a button's change)"""
    out = []
    while True:
        x, y = max(-255, min(255, dx)), max(-255, min(255, dy))
        out.append((x, y))
        dx, dy = dx - x, dy - y
        if not dx and not dy:
            return out
