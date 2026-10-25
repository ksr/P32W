#!/usr/bin/env python3
"""P32W two-pass assembler (ISA v0, docs/isa-v0.md).

Mnemonics, opcodes and formats come from microcode/p32isa.py -- the single
source of truth (D7) -- so the assembler and the emulator cannot disagree.

    python3 p32asm.py src.asm [-o out.bin] [-l listing.txt]
    python3 p32asm.py -c src.s [-o src.o]       relocatable object (M1, D22)

Output is a flat little-endian binary image starting at address 0 (the ROM),
or with -c a relocatable object for p32ld (assembler/p32obj.py describes the
format): sections .text .data .bss, symbols, relocations.

Syntax
  ; comment                     label:   (may share a line with an instruction)
  NAME = expr                   .equ NAME, expr
  .org expr     .align n (bytes, power of 2)     .space n
  .word e,...   .half e,...     .byte e|"str",...  .ascii "s"   .asciz "s"
  .include "file"   (path relative to the including file)
  Control registers (MFCR rd,cr / MTCR cr,rs): STATUS EPC CAUSE BADADDR VBR USP,
  or a number 0-15.
  Registers: r0-r15, zero (= r0), sp (= r15). Mnemonics are case-insensitive.
  Memory operands: imm(rs), (rs). An offset written as a full address in
  0xFFFF8000-0xFFFFFFFF is taken as its sign-extended 16-bit form, so the I/O
  page is reachable as e.g. 0xFFFFFF00(r0).
  Expressions: decimal, 0x.., 0b.., 'c'; + - * / % << >> & | ^ ~ ( );
  hi(x) = bits 31:16, lo(x) = bits 15:0; '.' = address of the current line.

Relocatable mode (-c) adds
  .text  .data  .bss            switch section (each has its own location counter)
  .global name,...              export a symbol; .extern name,... (undefined
                                symbols are taken as external anyway)
  .Lname: labels                file-local, never exported
  A relocatable value (a label, an external, plus or minus a constant) may
  appear in .word, in LA / LI, in hi()/lo() for LUI / ORI, and as the
  target of JMP / CALL; branches must stay within their section. .org is
  not allowed.

Pseudo-instructions
  NOP              ADD r0,r0,r0
  MOV rd,rs        ADD rd,rs,r0
  NEG rd,rs        SUB rd,r0,rs
  NOT rd,rs        NOR rd,rs,r0
  LI rd,expr       1 instruction if the value fits (ADDI/ORI/LUI), else LUI+ORI
  LA rd,expr       always LUI+ORI (address that may change between passes)
  PUSH rs          ADDI sp,sp,-4 ; SW rs,0(sp)
  POP rd           LW rd,0(sp) ; ADDI sp,sp,4
  BGT/BLE/BGTU/BLEU a,b,l   BLT/BGE/BLTU/BGEU with the operands swapped
  BEQZ/BNEZ a,l    BEQ/BNE a,r0,l
"""
import os, sys, re, argparse

def _find_isa():
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [os.environ.get("P32_ISA_DIR", ""), os.path.join(here, "..", "microcode"),
             os.path.join(os.getcwd(), "microcode")]
    for d in cands:
        if d and os.path.isfile(os.path.join(d, "p32isa.py")):
            return d
    sys.exit("cannot find p32isa.py (looked in: %s)" % ", ".join(c for c in cands if c))
sys.path.insert(0, _find_isa())
from p32isa import OPC, REGS, CREGS  # noqa: E402

class AsmError(Exception):
    pass

# ---------------------------------------------------------------- expressions
TOKEN = re.compile(r"\s*(0x[0-9a-fA-F_]+|0b[01_]+|\d+|'(?:\\.|[^'\\])'|[A-Za-z_.][\w.]*|<<|>>|[-+*/%&|^~()])")
ESC = {"n": 10, "r": 13, "t": 9, "0": 0, "\\": 92, "'": 39, '"': 34}

def tokenize(s):
    toks, pos, s = [], 0, s.strip()
    while pos < len(s):
        m = TOKEN.match(s, pos)
        if not m:
            raise AsmError("bad expression near '%s'" % s[pos:])
        toks.append(m.group(1))
        pos = m.end()
    return toks

class Undefined(Exception):
    pass

class RelVal:
    """A relocatable value: symbol (a section name for local labels, or an
    external / global name) + addend; kind None, "hi" or "lo" (from hi()/lo())."""
    def __init__(self, sym, add=0, kind=None):
        self.sym, self.add, self.kind = sym, add, kind
    def _plain(self):
        if self.kind:
            raise AsmError("hi()/lo() of a relocatable value must be the whole operand")
    def __add__(self, o):
        self._plain()
        if isinstance(o, int):
            return RelVal(self.sym, self.add + o)
        raise AsmError("cannot add two relocatable values")
    __radd__ = __add__
    def __sub__(self, o):
        self._plain()
        if isinstance(o, int):
            return RelVal(self.sym, self.add - o)
        if isinstance(o, RelVal) and o.sym == self.sym and not o.kind:
            return self.add - o.add                      # distance within a section
        raise AsmError("cannot subtract values from different sections")
    def __rsub__(self, o):
        raise AsmError("cannot subtract a relocatable value from a number")
    def _bad(self, *a):
        raise AsmError("only + and - work on a relocatable value")
    __mul__ = __rmul__ = __and__ = __rand__ = __or__ = __ror__ = __xor__ = __rxor__ = _bad
    __lshift__ = __rlshift__ = __rshift__ = __rrshift__ = __neg__ = __invert__ = _bad
    def __repr__(self):
        return "%s%s(%s%+d)" % (self.kind or "", "", self.sym, self.add)

def tdiv(a, b):
    """Integer division truncating toward zero (as C and the DIV instruction do)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

class Expr:
    """Recursive-descent evaluator. Precedence (low->high): | ^ & << >> + - * / %."""
    LEVELS = [("|",), ("^",), ("&",), ("<<", ">>"), ("+", "-"), ("*", "/", "%")]

    def __init__(self, text, syms, here, externs=None):
        self.t, self.i, self.syms, self.here = tokenize(text), 0, syms, here
        self.externs = externs
        if not self.t:
            raise AsmError("missing expression")

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def take(self):
        tok = self.peek()
        self.i += 1
        return tok

    def value(self):
        v = self.binary(0)
        if self.peek() is not None:
            raise AsmError("unexpected '%s' in expression" % self.peek())
        return v

    def binary(self, lvl):
        if lvl == len(self.LEVELS):
            return self.unary()
        v = self.binary(lvl + 1)
        while self.peek() in self.LEVELS[lvl]:
            op, r = self.take(), self.binary(lvl + 1)
            if op in "/%" and (isinstance(v, RelVal) or isinstance(r, RelVal)):
                raise AsmError("only + and - work on a relocatable value")
            if op in "/%" and r == 0:
                raise AsmError("division by zero in expression")
            v = {"|": lambda: v | r, "^": lambda: v ^ r, "&": lambda: v & r,
                 "<<": lambda: v << r, ">>": lambda: v >> r, "+": lambda: v + r,
                 "-": lambda: v - r, "*": lambda: v * r,
                 "/": lambda: tdiv(v, r), "%": lambda: v - r * tdiv(v, r)}[op]()
        return v

    def unary(self):
        tok = self.take()
        if tok is None:
            raise AsmError("expression ends early")
        if tok == "-":
            return -self.unary()
        if tok == "+":
            return self.unary()
        if tok == "~":
            return ~self.unary()
        if tok == "(":
            v = self.binary(0)
            if self.take() != ")":
                raise AsmError("missing ')'")
            return v
        if tok.lower() in ("hi", "lo") and self.peek() == "(":
            self.take()
            v = self.binary(0)
            if self.take() != ")":
                raise AsmError("missing ')'")
            if isinstance(v, RelVal):
                v._plain()
                return RelVal(v.sym, v.add, tok.lower())
            return (v >> 16) & 0xFFFF if tok.lower() == "hi" else v & 0xFFFF
        if tok[0].isdigit():
            return int(tok.replace("_", ""), 0)
        if tok[0] == "'":
            body = tok[1:-1]
            return ESC[body[1]] if body[0] == "\\" else ord(body)
        if tok == ".":
            return self.here
        if tok in self.syms:
            return self.syms[tok]
        if self.externs is not None and not tok.startswith(".L"):
            self.externs.add(tok)                       # relocatable mode: an external
            return RelVal(tok)
        raise Undefined(tok)

# ---------------------------------------------------------------- parsing
def split_operands(s):
    """Split on commas that are outside quotes and parentheses."""
    out, cur, depth, q = [], "", 0, None
    for ch in s:
        if q:
            cur += ch
            if ch == q and not cur.endswith("\\" + q):
                q = None
        elif ch in "\"'":
            q, cur = ch, cur + ch
        elif ch == "(":
            depth, cur = depth + 1, cur + ch
        elif ch == ")":
            depth, cur = depth - 1, cur + ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out

def strip_comment(line):
    q = None
    for i, ch in enumerate(line):
        if q:
            if ch == q and line[i - 1] != "\\":
                q = None
        elif ch in "\"'":
            q = ch
        elif ch == ";":
            return line[:i]
    return line

def parse_string(s):
    if len(s) < 2 or s[0] != '"' or s[-1] != '"':
        raise AsmError("expected a \"string\"")
    out, i, body = bytearray(), 0, s[1:-1]
    while i < len(body):
        if body[i] == "\\":
            i += 1
            if body[i] not in ESC:
                raise AsmError("unknown escape \\%s" % body[i])
            out.append(ESC[body[i]])
        else:
            out += body[i].encode("latin-1")
        i += 1
    return bytes(out)

def reg(s):
    r = REGS.get(s.strip().lower())
    if r is None:
        raise AsmError("expected a register, got '%s'" % s)
    return r

MEM = re.compile(r"^(.*)\(\s*([A-Za-z]\w*)\s*\)$")

def memop(s):
    m = MEM.match(s.strip())
    if not m:
        raise AsmError("expected imm(reg), got '%s'" % s)
    return (m.group(1).strip() or "0"), reg(m.group(2))

PSEUDO_SIZE = {"NOP": 4, "MOV": 4, "NEG": 4, "NOT": 4, "PUSH": 8, "POP": 8, "LA": 8,
               "BGT": 4, "BLE": 4, "BGTU": 4, "BLEU": 4, "BEQZ": 4, "BNEZ": 4}

class Line:
    def __init__(self, fname, num, text):
        self.fname, self.num, self.text = fname, num, text
        self.label = self.op = None
        self.args = []
        self.addr = 0
        self.size = 0
        self.sect = ".text"

SECTIONS = (".text", ".data", ".bss")

class Assembler:
    def __init__(self, reloc=False):
        self.syms = {}
        self.errors = []
        self.reloc = reloc
        self.sect = ".text"
        self.pcs = {s: 0 for s in SECTIONS}
        self.globals, self.externs = set(), set()
        self.relocs = []                                # (section, offset, kind, symbol, addend)
        self.passno = 1

    @property
    def pc(self):
        return self.pcs[self.sect]
    @pc.setter
    def pc(self, v):
        self.pcs[self.sect] = v

    def err(self, ln, msg):
        self.errors.append("%s:%d: error: %s" % (ln.fname, ln.num, msg))

    def ev(self, text, ln, need=True):
        here = RelVal(ln.sect, ln.addr) if self.reloc else ln.addr
        ext = self.externs if (self.reloc and self.passno == 2) else None
        try:
            return Expr(text, self.syms, here, ext).value()
        except Undefined as u:
            if need:
                raise AsmError("undefined symbol '%s'" % u.args[0])
            return None
        except TypeError:
            raise AsmError("only + and - work on a relocatable value")

    # ---- pass 1: parse, size, assign addresses, define labels
    def pass1(self, fname, src, depth=0):
        """Parse, size and place every line; .include is expanded in place.
        The location counter self.pc runs on across included files."""
        lines = []
        for num, raw in enumerate(src.splitlines(), 1):
            pc = self.pc
            ln = Line(fname, num, raw)
            text = strip_comment(raw).strip()
            m = re.match(r"^(\.?[A-Za-z_][\w.]*)\s*:(.*)$", text)
            if m:
                ln.label, text = m.group(1), m.group(2).strip()
            ln.addr = pc
            ln.sect = self.sect
            try:
                if ln.label:
                    self.define(ln.label, RelVal(self.sect, pc) if self.reloc else pc)
                m = re.match(r"^([A-Za-z_][\w.]*)\s*=\s*(.+)$", text)
                if m:
                    self.define(m.group(1), self.ev(m.group(2), ln))
                    text = ""
                if text:
                    parts = text.split(None, 1)
                    ln.op = parts[0].upper() if not parts[0].startswith(".") else parts[0].lower()
                    ln.args = split_operands(parts[1]) if len(parts) > 1 else []
                    if ln.op == ".include":
                        ln.op = None        # the .include line itself emits nothing
                        inc = self.include(ln, fname, depth)
                        lines.append(ln)
                        lines += inc
                        continue
                    if ln.op in SECTIONS or ln.op in (".global", ".globl", ".extern"):
                        self.directive(ln)
                        continue
                    self.pc = self.size_line(ln, pc)
            except AsmError as e:
                self.err(ln, str(e))
            lines.append(ln)
        return lines

    def include(self, ln, fname, depth):
        if len(ln.args) != 1:
            raise AsmError('.include needs one "file"')
        if depth >= 8:
            raise AsmError(".include nested too deeply")
        path = os.path.join(os.path.dirname(fname), parse_string(ln.args[0]).decode("latin-1"))
        try:
            with open(path) as fh:
                src = fh.read()
        except OSError as e:
            raise AsmError("cannot include %s: %s" % (path, e.strerror))
        return self.pass1(path, src, depth + 1)

    def directive(self, ln):
        if not self.reloc:
            raise AsmError("%s needs relocatable mode (-c)" % ln.op)
        if ln.op in SECTIONS:
            if ln.args:
                raise AsmError("%s takes no operands" % ln.op)
            self.sect = ln.op
        else:
            for a in ln.args:
                if not re.match(r"^[A-Za-z_][\w.]*$", a):
                    raise AsmError("bad symbol name '%s'" % a)
                (self.globals if ln.op != ".extern" else self.externs).add(a)

    def define(self, name, value):
        if name in REGS or (not self.reloc and (name.upper() in OPC or name.upper() in CREGS)):
            raise AsmError("'%s' is a reserved name" % name)   # (-c: C names like div or call are fine)
        if name in self.syms:
            raise AsmError("'%s' defined twice" % name)
        self.syms[name] = value

    def size_line(self, ln, pc):
        op, a = ln.op, ln.args
        if op == ".equ":
            if len(a) != 2:
                raise AsmError(".equ needs NAME, expr")
            self.define(a[0], self.ev(a[1], ln))
            return pc
        if op == ".org":
            if self.reloc:
                raise AsmError(".org is not allowed in relocatable mode")
            new = self.ev(a[0], ln)
            if new < pc:
                raise AsmError(".org 0x%X moves backwards (now at 0x%X)" % (new, pc))
            ln.addr = pc = new
            return pc
        if op == ".align":
            n = self.ev(a[0], ln)
            if n <= 0 or n & (n - 1):
                raise AsmError(".align needs a power of 2")
            ln.size = (-pc) % n
        elif op == ".space":
            ln.size = self.ev(a[0], ln)
        elif op == ".word":
            ln.size = 4 * len(a)
        elif op == ".half":
            ln.size = 2 * len(a)
        elif op == ".byte":
            ln.size = sum(len(parse_string(x)) if x.startswith('"') else 1 for x in a)
        elif op in (".ascii", ".asciz"):
            ln.size = len(parse_string(a[0])) + (op == ".asciz")
        elif op.startswith("."):
            raise AsmError("unknown directive %s" % op)
        else:
            if pc % 4:
                raise AsmError("instruction at unaligned address 0x%X (use .align 4)" % pc)
            if op in OPC:
                ln.size = 4
            elif op in PSEUDO_SIZE:
                ln.size = PSEUDO_SIZE[op]
            elif op == "LI":
                if len(a) != 2:
                    raise AsmError("LI needs rd, expr")
                v = self.ev(a[1], ln, need=False)
                ln.size = 8 if v is None or isinstance(v, RelVal) else 4 * len(li_parts(v))
            else:
                raise AsmError("unknown instruction %s" % op)
        return pc + ln.size

    # ---- pass 2: encode
    def pass2(self, lines):
        self.passno = 2
        self.images = {s: bytearray() for s in SECTIONS}
        for ln in lines:
            if not ln.op or ln.op in (".equ", ".org"):
                continue
            try:
                if ln.sect == ".bss" and ln.op not in (".space", ".align"):
                    raise AsmError(".bss holds only .space and .align")
                data = self.encode(ln)
            except AsmError as e:
                self.err(ln, str(e))
                continue
            if len(data) != ln.size:
                self.err(ln, "internal: size changed between passes")
                continue
            image = self.images[ln.sect]
            end = ln.addr + len(data)
            if end > len(image):
                image.extend(bytes(end - len(image)))
            image[ln.addr:end] = data
            ln.data = data
        return self.images[".text"]

    def add_reloc(self, ln, off, kind, v):
        self.relocs.append((ln.sect, off, kind, v.sym, v.add))

    def encode(self, ln):
        op, a = ln.op, ln.args
        if op == ".align" or op == ".space":
            return bytes(ln.size)
        if op in (".word", ".half", ".byte"):
            n = {".word": 4, ".half": 2, ".byte": 1}[op]
            out = bytearray()
            for x in a:
                if n == 1 and x.startswith('"'):
                    out += parse_string(x)
                    continue
                v = self.ev(x, ln)
                if isinstance(v, RelVal):
                    if n != 4 or v.kind:
                        raise AsmError("a relocatable value needs .word")
                    self.add_reloc(ln, ln.addr + len(out), "ABS32", v)
                    out += bytes(4)
                    continue
                if not -(1 << (8 * n - 1)) <= v < (1 << (8 * n)):
                    raise AsmError("value %d does not fit in %s" % (v, op))
                out += (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")
            return bytes(out)
        if op in (".ascii", ".asciz"):
            return parse_string(a[0]) + (b"\0" if op == ".asciz" else b"")
        words = []
        for i, (m, args) in enumerate(self.expand(ln)):
            self.woff = ln.addr + 4 * i
            words.append(self.ins(ln, m, args))
        return b"".join(w.to_bytes(4, "little") for w in words)

    def expand(self, ln):
        """Turn a (pseudo-)instruction into a list of (mnemonic, operand list)."""
        op, a = ln.op, ln.args
        if op in OPC:
            return [(op, a)]
        def need(n):
            if len(a) != n:
                raise AsmError("%s takes %d operand(s)" % (op, n))
        if op == "NOP":
            need(0); return [("ADD", ["r0", "r0", "r0"])]
        if op == "MOV":
            need(2); return [("ADD", [a[0], a[1], "r0"])]
        if op == "NEG":
            need(2); return [("SUB", [a[0], "r0", a[1]])]
        if op == "NOT":
            need(2); return [("NOR", [a[0], a[1], "r0"])]
        if op == "PUSH":
            need(1); return [("ADDI", ["sp", "sp", "-4"]), ("SW", [a[0], "0(sp)"])]
        if op == "POP":
            need(1); return [("LW", [a[0], "0(sp)"]), ("ADDI", ["sp", "sp", "4"])]
        if op in ("BGT", "BLE", "BGTU", "BLEU"):
            need(3)
            real = {"BGT": "BLT", "BLE": "BGE", "BGTU": "BLTU", "BLEU": "BGEU"}[op]
            return [(real, [a[1], a[0], a[2]])]
        if op in ("BEQZ", "BNEZ"):
            need(2); return [(op[:3], [a[0], "r0", a[1]])]
        if op in ("LI", "LA"):
            need(2)
            if isinstance(self.ev(a[1], ln), RelVal):
                return [("LUI", [a[0], "hi(%s)" % a[1]]), ("ORI", [a[0], a[0], "lo(%s)" % a[1]])]
            v = self.ev(a[1], ln) & 0xFFFFFFFF
            if not -(1 << 31) <= self.ev(a[1], ln) < (1 << 32):
                raise AsmError("%s value does not fit in 32 bits" % op)
            parts = li_parts(v) if ln.size == 4 else [("LUI", v >> 16), ("ORI", v & 0xFFFF)]
            out = []
            for m, imm in parts:
                if m == "LUI":
                    out.append(("LUI", [a[0], str(imm)]))
                elif m == "ORI" and len(parts) == 2:
                    out.append(("ORI", [a[0], a[0], str(imm)]))
                else:
                    out.append((m, [a[0], "r0", str(imm)]))
            return out
        raise AsmError("unknown instruction %s" % op)

    def ins(self, ln, m, a):
        opcode, fmt = OPC[m]
        w = opcode << 24
        counts = {"R": 3, "I": 3, "IU": 3, "SH": 3, "LUI": 2, "LD": 2, "ST": 2,
                  "BR": 3, "J": 1, "JR": 1, "N": 0, "CR": 2}
        if len(a) != counts[fmt]:
            raise AsmError("%s takes %d operand(s)" % (m, counts[fmt]))
        if fmt == "R":
            return w | reg(a[0]) << 20 | reg(a[1]) << 16 | reg(a[2]) << 12
        if fmt in ("I", "IU", "SH"):
            v = self.ev(a[2], ln)
            if isinstance(v, RelVal):
                if m != "ORI" or v.kind != "lo":
                    raise AsmError("a relocatable immediate works only as lo() in ORI")
                self.add_reloc(ln, self.woff, "LO16", v)
                v = 0
            lo, hi = {"I": (-32768, 32767), "IU": (0, 0xFFFF), "SH": (0, 31)}[fmt]
            if not lo <= v <= hi:
                raise AsmError("%s immediate %d out of range %d..%d" % (m, v, lo, hi))
            return w | reg(a[0]) << 20 | reg(a[1]) << 16 | (v & 0xFFFF)
        if fmt == "LUI":
            v = self.ev(a[1], ln)
            if isinstance(v, RelVal):
                if v.kind != "hi":
                    raise AsmError("a relocatable LUI operand must be hi()")
                self.add_reloc(ln, self.woff, "HI16", v)
                v = 0
            if not 0 <= v <= 0xFFFF:
                raise AsmError("LUI immediate %d out of range 0..65535" % v)
            return w | reg(a[0]) << 20 | v
        if fmt in ("LD", "ST"):
            off, base = memop(a[1])
            v = self.ev(off, ln)
            if isinstance(v, RelVal):
                raise AsmError("a load/store offset cannot be relocatable (LA the address first)")
            if 0xFFFF8000 <= v <= 0xFFFFFFFF:   # top-32K address, e.g. CON(r0)
                v -= 1 << 32
            if not -32768 <= v <= 32767:
                raise AsmError("offset %d out of range -32768..32767" % v)
            return w | reg(a[0]) << 20 | base << 16 | (v & 0xFFFF)
        if fmt == "BR":
            off = self.rel(ln, a[2], 16)
            return w | reg(a[0]) << 20 | reg(a[1]) << 16 | (off & 0xFFFF)
        if fmt == "J":
            t = self.ev(a[0], ln)
            if isinstance(t, RelVal) and (t.sym != ln.sect or t.kind):
                if t.kind:
                    raise AsmError("a jump target cannot be hi()/lo()")
                self.add_reloc(ln, self.woff, "J24", t)
                return w
            return w | (self.rel(ln, a[0], 24) & 0xFFFFFF)
        if fmt == "JR":
            return w | reg(a[0]) << 16
        if fmt == "CR":                 # MFCR rd, cr  /  MTCR cr, rs
            gpr, cr = (a[0], a[1]) if m == "MFCR" else (a[1], a[0])
            return w | reg(gpr) << 20 | self.creg(cr, ln) << 16
        return w  # N

    def creg(self, text, ln):
        n = CREGS.get(text.strip().upper())
        if n is None:
            n = self.ev(text, ln)
            if not 0 <= n <= 15:
                raise AsmError("control register %d out of range 0..15" % n)
        return n

    def rel(self, ln, text, bits):
        # pseudo-instructions that expand to one word keep ln.addr as their PC
        target = self.ev(text, ln)
        if isinstance(target, RelVal):
            if target.sym != ln.sect or target.kind:
                raise AsmError("branch to '%s' leaves the section (use JMP)" % text)
            target = target.add
        delta = target - (getattr(self, "woff", ln.addr) + 4)
        if delta % 4:
            raise AsmError("branch target 0x%X is not word-aligned" % target)
        off = delta // 4
        if not -(1 << (bits - 1)) <= off < (1 << (bits - 1)):
            raise AsmError("branch target out of range (%d words)" % off)
        return off

def li_parts(v):
    """Shortest sequence loading the 32-bit value v (two's complement)."""
    v &= 0xFFFFFFFF
    s = v - (1 << 32) if v & 0x80000000 else v
    if -32768 <= s <= 32767:
        return [("ADDI", s)]
    if v <= 0xFFFF:
        return [("ORI", v)]
    if v & 0xFFFF == 0:
        return [("LUI", v >> 16)]
    return [("LUI", v >> 16), ("ORI", v & 0xFFFF)]

def assemble(fname, src, reloc=False):
    asm = Assembler(reloc)
    lines = asm.pass1(fname, src)
    image = asm.pass2(lines) if not asm.errors else bytearray()
    return asm, lines, image

def obj_of(asm, fname):
    """The relocatable object (p32obj.py) of an assembled file."""
    import p32obj
    syms = {}
    for name, v in asm.syms.items():
        if isinstance(v, RelVal) and v.sym in SECTIONS and not name.startswith(".L"):
            syms[name] = {"section": v.sym, "value": v.add, "global": name in asm.globals}
        elif isinstance(v, int) and name in asm.globals:
            syms[name] = {"section": "*ABS*", "value": v, "global": True}
    for g in asm.globals:
        if g not in syms:
            asm.errors.append("%s: error: .global %s is not defined" % (fname, g))
    imgs = dict(asm.images)
    imgs[".text"] = imgs[".text"].ljust(asm.pcs[".text"], b"\0")
    imgs[".data"] = imgs[".data"].ljust(asm.pcs[".data"], b"\0")
    return p32obj.make(os.path.basename(fname), imgs, asm.pcs[".bss"], {}, syms, asm.relocs)

def listing(lines):
    out = []
    for ln in lines:
        data = getattr(ln, "data", b"")
        if ln.op and ln.op not in (".equ", ".org") and ln.op[0] != "." and data:
            words = ["%08X" % int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
            out.append("%08X  %-17s  %s" % (ln.addr, " ".join(words), ln.text))
        elif data:
            out.append("%08X  %-17s  %s" % (ln.addr, data[:8].hex(" ").upper(), ln.text))
        else:
            out.append("%8s  %-17s  %s" % ("", "", ln.text))
    return "\n".join(out) + "\n"

def main():
    ap = argparse.ArgumentParser(description="P32W two-pass assembler (ISA v0)")
    ap.add_argument("src")
    ap.add_argument("-o", "--out")
    ap.add_argument("-l", "--listing")
    ap.add_argument("-c", action="store_true", help="relocatable object for p32ld")
    args = ap.parse_args()
    with open(args.src) as fh:
        asm, lines, image = assemble(args.src, fh.read(), args.c)
    if args.c and not asm.errors:
        obj = obj_of(asm, args.src)
    if asm.errors:
        sys.stderr.write("\n".join(asm.errors) + "\n")
        sys.exit(1)
    if args.c:
        import p32obj
        p32obj.write(args.out or os.path.splitext(args.src)[0] + ".o", obj)
        if args.listing:
            with open(args.listing, "w") as fh:
                fh.write(listing(lines))
        return
    out = args.out or os.path.splitext(args.src)[0] + ".bin"
    with open(out, "wb") as fh:
        fh.write(image)
    if args.listing:
        with open(args.listing, "w") as fh:
            fh.write(listing(lines))

if __name__ == "__main__":
    main()
