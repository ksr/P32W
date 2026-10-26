"""cgen.py -- the P32W code generator of p32cc (M2, D22).

Walks the typed tree (cparse.py) and writes P32W assembly for p32asm -c.

The ABI (os-design.md 3.3, D22): arguments 1-4 in r1-r4, more on the
stack (argument 5 at the lowest address); the result in r1; r1-r7
caller-saved, r8-r13 callee-saved, r14 the frame pointer, r15 the stack
pointer; CALL pushes the return address. A variadic function, or one with
a struct parameter, gets ALL its arguments on the stack (a struct copied
into as many words as it takes), so va_list is a plain pointer walking
them. A function returning a struct gets a hidden first argument, where
to put it, and returns that address in r1 (D30).

Frame:     fp+8...  stack arguments (all of them for a variadic function)
           fp+4     return address
           fp       the caller's fp
           fp-...   locals, then saved r8-r13

Expressions are evaluated into a stack of temporaries: the lowest free of
r1-r13 (r8-r13 are saved in the prologue if a function uses them). Across
a call only the live temporaries in r1-r7 are saved.
"""
import os
from clex import err
import cty
from cty import INT, is_integer, is_scalar

REGNAMES = {"r%d" % i for i in range(16)} | {"zero", "sp"}

def sym(name):
    """A C name as an assembly symbol (register names would read as registers)."""
    return "_C_" + name if name in REGNAMES else name

def fits16(v):
    return -32768 <= v <= 32767

def s32(v):
    return ((v & 0xFFFFFFFF) ^ 0x80000000) - 0x80000000

# a switch becomes a jump table when it has at least SWITCH_MIN cases and
# the table (one word per value from the lowest case to the highest) is at
# most SWITCH_DENSITY times as long as the case list
SWITCH_MIN, SWITCH_DENSITY = 4, 3

def table_range(cases):
    vals = [v for v, _ in cases]
    return min(vals), max(vals)

def dense(cases):
    if len(cases) < SWITCH_MIN:
        return False
    lo, hi = table_range(cases)
    return hi - lo + 1 <= SWITCH_DENSITY * len(cases)

def const_addr(e):
    """A constant pointer that reaches as an offset from r0 (the I/O page at
    the top of the address space, low memory): its offset, else None."""
    if e.kind == "cast" and e.ty.kind == "ptr":
        e = e.expr
    if e.kind == "num" and fits16(s32(e.val)):
        return s32(e.val)
    return None

REGVARS = (13, 12, 11, 10)      # callee-saved registers for locals (the busiest first)
if os.environ.get("P32CC_NOREGVARS"):
    REGVARS = ()                # (all locals in memory: to measure what registers gain)

class OutOfRegs(Exception):
    pass

_src_cache = {}

def source_line(path, line):
    """A source line for -S's comments: without its indent or end, at most
    100 bytes (read as bytes: the C compiler shows exactly the same)."""
    if path not in _src_cache:
        try:
            _src_cache[path] = open(path, "rb").read().split(b"\n")
        except OSError:
            _src_cache[path] = []
    lines = _src_cache[path]
    if not 1 <= line <= len(lines):
        return ""
    t = lines[line - 1].lstrip(b" \t").rstrip(b" \t\r")
    return t[:100].decode("latin-1")

class Gen:
    def __init__(self, parser, annotate=False):
        self.p = parser
        self.annotate = annotate        # (-S: each statement's line as a comment)
        self.noted = None
        self.out = []
        self.nlab = 0
        self.regvar, self.reserved = {}, set()
        self.after = None               # (addr_parts: the increment after a *p++ access)
        self.no_value = False

    def emit(self, s):
        self.out.append("        " + s)
    def label(self, l):
        self.out.append("%s:" % l)
    def newlab(self):
        self.nlab += 1
        return ".L%s_%d" % (self.fname, self.nlab)

    # ---------------------------------------------------------------- registers
    def alloc(self):
        for r in range(1, 14):
            if r not in self.busy and r not in self.reserved:
                self.busy.add(r)
                if r >= 8:
                    self.callee.add(r)
                return r
        if self.reserved:
            raise OutOfRegs()               # (function() retries with fewer register variables)
        raise_err(self.cur_tok, "expression too complex (out of registers)")

    def regof(self, n):
        """The register a variable lives in, or None."""
        if n.kind == "var":
            return self.regvar.get(id(n.obj))
        if n.kind == "varoff" and n.off == 0:
            return self.regvar.get(id(n.var))
        return None

    def src(self, n):
        """A register holding n's value, only to be read: a register
        variable itself (borrowed), else a new temporary."""
        return self.regof(n) or self.expr(n)

    def own(self, r):
        """r, or a copy of it if r is borrowed (a register variable)."""
        if r in self.reserved:
            t = self.alloc()
            self.emit("mov   r%d, r%d" % (t, r))
            return t
        return r

    def dest(self, a):
        """Where an operation reading a may put its result."""
        return self.alloc() if a in self.reserved else a

    def effect(self, n):
        """n for its side effects only: an assignment's value is not copied out."""
        if n.kind in ("assign", "opassign", "preinc", "predec", "postinc", "postdec"):
            self.no_value = True
        self.free(self.expr(n))
        self.no_value = False

    def take_no_value(self):
        v, self.no_value = self.no_value, False
        return v

    def free(self, r):
        if r is not None and r not in self.reserved and r != 14:
            self.busy.discard(r)

    # ---------------------------------------------------------------- program
    def program(self):
        P = self.p
        self.printf_fp = False      # (a double passed to a variadic function: see below)
        self.out.append("; generated by p32cc")
        self.out.append("        .text")
        for obj in P.funcs:          # (in the order defined)
            self.function(obj)
        data, bss = [], []
        for obj in P.globals:
            if obj.ty.kind == "func" or obj.extern and not obj.is_def:
                continue
            if obj.init is not None:
                data.append(obj)
            elif obj.is_def or getattr(obj, "tentative", False) or obj.static:
                bss.append(obj)
        if data or P.strings:
            self.out.append("        .data")
            for obj in data:
                self.global_data(obj)
            for label, b in P.strings:
                self.label(label)
                self.bytes_(b)
        if self.printf_fp:          # (libc's printf formats doubles only in programs that ask: fphook.c)
            self.out.append("        .data")
            self.emit(".align 4")
            self.emit(".word __printf_fp")
        if bss:
            self.out.append("        .bss")
            for obj in bss:
                if obj.ty.size == 0:
                    raise_err(None, "%s has an incomplete type" % obj.name)
                self.emit(".align %d" % max(obj.ty.align, 1))
                if not obj.static:
                    self.emit(".global %s" % sym(obj.label))
                self.label(sym(obj.label))
                self.emit(".space %d" % obj.ty.size)
        return "\n".join(self.out) + "\n"

    def bytes_(self, b):
        for k in range(0, len(b), 16):
            self.emit(".byte " + ",".join(str(x) for x in b[k:k + 16]))

    def global_data(self, obj):
        data, relocs = obj.init
        self.emit(".align %d" % max(obj.ty.align, 1))
        if not obj.static:
            self.emit(".global %s" % sym(obj.label))
        self.label(sym(obj.label))
        rel = {off: (lab, add) for off, lab, add in relocs}
        k = 0
        run = bytearray()
        while k < len(data):
            if k in rel:
                if run:
                    self.bytes_(run); run = bytearray()
                lab, add = rel[k]
                self.emit(".word %s%s" % (sym(lab), "%+d" % add if add else ""))
                k += 4
            else:
                run.append(data[k]); k += 1
        if run:
            self.bytes_(run)

    # ---------------------------------------------------------------- functions
    def function(self, fn):
        """Scalar locals and parameters whose address is never taken may live
        in callee-saved registers (r13 down to r10), the most used first; if a
        function then runs out of temporaries, it is generated again with
        fewer."""
        cands = [v for v in list(fn.params) + list(fn.locals)
                 if not v.addr_taken and v.uses >= 3 and v.ty.kind in ("char", "short", "int", "long", "enum", "ptr")]
        cands.sort(key=lambda v: -v.uses)
        n = min(len(REGVARS), len(cands))
        while True:
            try:
                return self.function_with(fn, cands[:n])
            except OutOfRegs:
                n -= 1

    def function_with(self, fn, regvars):
        self.noted = None
        self.fn = fn
        self.fname = fn.label.lstrip(".")
        self.busy, self.callee = set(), set()
        self.after = None
        self.regvar = {id(v): REGVARS[k] for k, v in enumerate(regvars)}
        self.reserved = set(self.regvar.values())
        self.callee |= self.reserved
        self.cur_tok = None
        self.brk, self.cont = [], []
        self.nlab_start = self.nlab
        stack = cty.stack_args(fn.ty)                   # (every argument on the stack: variadic, or a struct)
        pos, w = [], 0
        for p in fn.params:
            pos.append(w)
            w += cty.words(p.ty)
        # parameters and locals
        off = 0
        stores, moves = [], []
        for k, p in enumerate(fn.params):
            reg = self.regvar.get(id(p))
            if stack or k >= 4:
                p.offset = 8 + 4 * (pos[k] if stack else k - 4)
                if reg:
                    moves.append("lw    r%d, %d(r14)" % (reg, p.offset))
            elif reg:
                moves.append("mov   r%d, r%d" % (reg, k + 1))
            else:
                off += 4
                p.offset = -off
                stores.append((k + 1, p.offset))
        for v in fn.locals:
            if id(v) in self.regvar:
                continue
            a = max(v.ty.align, 4)                       # word-aligned: memzero uses sw
            off = (off + v.ty.size + a - 1) // a * a
            v.offset = -off
        locals_size = (off + 3) // 4 * 4
        self.ret = ".L%s_ret" % self.fname
        body_out, self.out = self.out, []
        try:
            self.stmt(fn.body)
        except OutOfRegs:
            self.out = body_out
            self.nlab = self.nlab_start
            raise
        body, self.out = self.out, body_out
        saved = sorted(self.callee)
        frame = locals_size + 4 * len(saved)
        # prologue
        if not fn.static:
            self.emit(".global %s" % sym(fn.label))
        self.label(sym(fn.label))
        self.emit("addi  sp, sp, -4")
        self.emit("sw    r14, 0(sp)")
        self.emit("mov   r14, sp")
        if frame:
            if fits16(-frame):
                self.emit("addi  sp, sp, %d" % -frame)
            else:
                self.emit("li    r6, %d" % frame)
                self.emit("sub   sp, sp, r6")
        for k, r in enumerate(saved):
            self.emit("sw    r%d, %d(r14)" % (r, -(locals_size + 4 * (k + 1))))
        for reg, o in stores:
            self.emit("sw    r%d, %d(r14)" % (reg, o))
        for m in moves:
            self.emit(m)
        self.out.extend(body)
        # epilogue
        self.label(self.ret)
        for k, r in enumerate(saved):
            self.emit("lw    r%d, %d(r14)" % (r, -(locals_size + 4 * (k + 1))))
        self.emit("mov   sp, r14")
        self.emit("lw    r14, 0(sp)")
        self.emit("addi  sp, sp, 4")
        self.emit("ret")

    # ---------------------------------------------------------------- statements
    def stmt(self, n):
        if n is None:
            return
        self.cur_tok = n.tok or self.cur_tok
        k = n.kind
        if self.annotate and k != "block" and n.tok and n.tok.file:
            key = (n.tok.file, n.tok.line)
            if key != self.noted:
                self.noted = key
                self.out.append("; %s:%d: %s" % (os.path.basename(n.tok.file), n.tok.line,
                                                  source_line(n.tok.file, n.tok.line)))
        if k == "block":
            for s in n.body:
                self.stmt(s)
        elif k == "exprstmt":
            self.effect(n.expr)
            assert not self.busy, "registers leaked"
        elif k == "if":
            els, end = self.newlab(), self.newlab()
            self.branch(n.cond, els, False)
            self.stmt(n.then)
            if n.els:
                self.emit("jmp   %s" % end)
            self.label(els)
            if n.els:
                self.stmt(n.els)
                self.label(end)
        elif k == "for":
            top, cont, brk = self.newlab(), self.newlab(), self.newlab()
            if n.init:
                self.stmt(n.init)
            test = self.newlab()
            self.emit("jmp   %s" % test)
            self.label(top)
            self.brk.append(brk); self.cont.append(cont)
            self.stmt(n.body)
            self.brk.pop(); self.cont.pop()
            self.label(cont)
            if n.inc is not None:
                self.effect(n.inc)
            self.label(test)
            if n.cond is not None:
                self.branch(n.cond, top, True)
            else:
                self.emit("jmp   %s" % top)
            self.label(brk)
        elif k == "do":
            top, cont, brk = self.newlab(), self.newlab(), self.newlab()
            self.label(top)
            self.brk.append(brk); self.cont.append(cont)
            self.stmt(n.body)
            self.brk.pop(); self.cont.pop()
            self.label(cont)
            self.branch(n.cond, top, True)
            self.label(brk)
        elif k == "switch":
            brk = self.newlab()
            v = self.expr(n.expr)
            t = self.alloc()
            if dense(n.cases):
                self.jump_table(n, v, t, brk)
            else:
                self.compare_chain(n, v, t, brk)
            self.free(t); self.free(v)
            self.brk.append(brk)
            self.stmt(n.body)
            self.brk.pop()
            self.label(brk)
        elif k == "caselabel":
            self.label(n.label)
            self.stmt(n.body)
        elif k == "break":
            self.emit("jmp   %s" % self.brk[-1])
        elif k == "continue":
            self.emit("jmp   %s" % self.cont[-1])
        elif k == "return":
            if n.expr is not None:
                r = self.expr(n.expr)
                if r != 1:
                    self.emit("mov   r1, r%d" % r)
                self.free(r)
            self.emit("jmp   %s" % self.ret)
        elif k == "goto":
            self.emit("jmp   .L%s_u_%s" % (self.fname, n.label))
        elif k == "label":
            self.label(".L%s_u_%s" % (self.fname, n.label))
            self.stmt(n.body)
        elif k == "memzero":
            self.zero(n.var)
        else:
            raise_err(n.tok, "internal: statement %s" % k)

    def zero(self, var):
        size = (var.ty.size + 3) // 4 * 4
        if size <= 64:
            for o in range(0, size, 4):
                self.emit("sw    r0, %d(r14)" % (var.offset + o))
            return
        a, e = self.alloc(), self.alloc()
        lp = self.newlab()
        self.emit("addi  r%d, r14, %d" % (a, var.offset))
        self.emit("addi  r%d, r%d, %d" % (e, a, size))
        self.label(lp)
        self.emit("sw    r0, 0(r%d)" % a)
        self.emit("addi  r%d, r%d, 4" % (a, a))
        self.emit("bne   r%d, r%d, %s" % (a, e, lp))
        self.free(a); self.free(e)

    # ---------------------------------------------------------------- conditions
    BR = {"==": "beq", "!=": "bne", "<": "blt", ">=": "bge"}
    INV = {"==": "!=", "!=": "==", "<": ">=", ">=": "<", ">": "<=", "<=": ">"}

    def branch(self, n, target, when):
        """Jump to target if n's truth equals `when`."""
        k = n.kind
        if k == "num":
            if bool(n.val) == when:
                self.emit("jmp   %s" % target)
            return
        if k == "not":
            return self.branch(n.expr, target, not when)
        if k == "logand":
            if not when:
                self.branch(n.lhs, target, False)
                self.branch(n.rhs, target, False)
            else:
                skip = self.newlab()
                self.branch(n.lhs, skip, False)
                self.branch(n.rhs, target, True)
                self.label(skip)
            return
        if k == "logor":
            if when:
                self.branch(n.lhs, target, True)
                self.branch(n.rhs, target, True)
            else:
                skip = self.newlab()
                self.branch(n.lhs, skip, True)
                self.branch(n.rhs, target, False)
                self.label(skip)
            return
        if k == "cmp":
            op = n.op if when else self.INV[n.op]
            a = self.src(n.lhs)
            b = 0 if (n.rhs.kind == "num" and n.rhs.val == 0) else self.src(n.rhs)
            if op in (">", "<="):
                a, b, op = b, a, {">": "<", "<=": ">="}[op]
            ins = self.BR[op] + ("u" if n.unsigned and op in ("<", ">=") else "")
            self.emit("%-5s r%d, r%d, %s" % (ins, a, b, target))
            self.free(a if a else None); self.free(b if b else None)
            return
        r = self.src(n)
        self.emit("%-5s r%d, r0, %s" % ("bne" if when else "beq", r, target))
        self.free(r)

    # ---------------------------------------------------------------- expressions
    def load(self, dst, base, off, ty, fold=False):
        """off: a constant, or ("x", index register, scale) from addr_parts
        (an indexed load, D59). fold: a pending *p++ step on this base goes
        into the instruction (post-increment, D59)."""
        if isinstance(off, tuple):
            ins = {1: "lbux" if ty.unsigned else "lbx", 2: "lhux" if ty.unsigned else "lhx", 4: "lwx"}[ty.size]
            self.emit("%-5s r%d, r%d, r%d, %d" % (ins, dst, base, off[1], off[2]))
            self.free(off[1])
            return
        if ty.kind in ("array", "func") or cty.is_memval(ty):            # (by address)
            if dst != base or off:
                self.emit("addi  r%d, r%d, %d" % (dst, base, off))
            return
        ins = {1: "lbu" if ty.unsigned else "lb", 2: "lhu" if ty.unsigned else "lh", 4: "lw"}[ty.size]
        step = self.post_step(base, off) if fold and dst != base else None
        if step is not None:
            self.emit("%-5s r%d, (r%d), %d" % (ins + "p", dst, base, step))
        else:
            self.emit("%-5s r%d, %d(r%d)" % (ins, dst, off, base))

    def store(self, val, base, off, ty, fold=False):
        if isinstance(off, tuple):
            self.emit("%-5s r%d, r%d, r%d, %d" % ({1: "sbx", 2: "shx", 4: "swx"}[ty.size], val, base, off[1], off[2]))
            self.free(off[1])
            return
        ins = {1: "sb", 2: "sh", 4: "sw"}[ty.size]
        step = self.post_step(base, off) if fold else None
        if step is not None:
            self.emit("%-5s r%d, (r%d), %d" % (ins + "p", val, base, step))
        else:
            self.emit("%-5s r%d, %d(r%d)" % (ins, val, off, base))

    def post_step(self, base, off):
        """The step of the pending *p++ on base, taken over by the access (it
        is then no longer pending), or None."""
        if not self.after or off != 0 or self.after[0] != base or not -128 <= self.after[1] <= 127:
            return None
        step, self.after = self.after[1], None
        return step

    def narrow(self, r, ty):
        """Make register r hold a value of type ty (sign- or zero-extended)."""
        if ty.kind == "void" or ty.size == 4 or ty.kind in ("ptr", "array", "func") or cty.is_memval(ty):
            return
        bits = ty.size * 8
        if ty.unsigned:
            self.emit("andi  r%d, r%d, %d" % (r, r, (1 << bits) - 1))
        else:
            self.emit("slli  r%d, r%d, %d" % (r, r, 32 - bits))
            self.emit("srai  r%d, r%d, %d" % (r, r, 32 - bits))

    def local_mem(self, n):
        """(base reg 14, offset) for a local scalar lvalue, or None."""
        if n.kind == "var" and n.obj.local and fits16(n.obj.offset):
            return n.obj.offset
        if n.kind == "varoff" and fits16(n.var.offset + n.off):
            return n.var.offset + n.off
        return None

    def flush_after(self):
        """The pointer increment a *p++ access left to follow it."""
        if self.after:
            self.emit("addi  r%d, r%d, %d" % (self.after[0], self.after[0], self.after[1]))
            self.after = None

    def addr_parts(self, n, index_ok=False):
        """(base register, offset) of an lvalue; the base may be borrowed
        (a register variable, r14 for a local, r0 for a constant address).
        A constant added to a pointer (p[2], p + 3) goes into the offset.
        With index_ok, a scalar p[i] (p + i * 1, 2, 4 or 8) gives the offset
        ("x", the index's register, the scale), for an indexed access (D59)."""
        if n.kind == "deref":
            e = n.expr
            if index_ok and e.kind == "binop" and e.op == "+" and e.ty.kind == "ptr" \
                    and e.lhs.ty.kind == "ptr" and e.rhs.kind != "num" and cty.is_scalar(n.ty) \
                    and n.ty.kind not in ("array", "func") and not cty.is_memval(n.ty) and n.ty.size in (1, 2, 4):
                i, scale = e.rhs, 1
                if i.kind == "binop" and i.op == "*" and i.rhs.kind == "num" and i.rhs.val in (2, 4, 8):
                    i, scale = i.lhs, i.rhs.val
                if is_integer(i.ty) and i.ty.size == 4:
                    b = self.src(e.lhs)
                    return b, ("x", self.src(i), scale)
            rv = self.regof(e)
            if rv:
                return rv, 0
            c = const_addr(e)
            if c is not None:
                return 0, c
            if e.kind in ("postinc", "postdec") and e.expr.ty.kind == "ptr" and self.regof(e.expr):
                # *p++ on a register pointer: the access through p, then p moves
                rv = self.regof(e.expr)
                self.after = (rv, e.step if e.kind == "postinc" else -e.step)
                return rv, 0
            if e.kind == "binop" and e.op in ("+", "-") and e.ty.kind == "ptr" and e.rhs.kind == "num":
                d = s32(e.rhs.val) if e.op == "+" else -s32(e.rhs.val)
                if fits16(d):
                    return self.src(e.lhs), d
        if n.kind == "member":
            b, off = self.addr_parts(n.expr)
            if fits16(off + n.mem.offset):
                return b, off + n.mem.offset
            r = self.dest(b) if b not in (0, 14) else self.alloc()
            self.emit("addi  r%d, r%d, %d" % (r, b, off + n.mem.offset))
            return r, 0
        if n.kind == "var" and n.obj.local and fits16(n.obj.offset) and not self.regof(n):
            return 14, n.obj.offset
        return self.addr(n), 0

    def retarget(self, v, rv):
        """Move temporary v's value into register variable rv (and free v):
        rewrite the last instruction to write rv directly when it wrote v."""
        last = self.out[-1] if self.out else ""
        parts = last.split(None, 1)
        if len(parts) == 2 and parts[0] not in ("sw", "sh", "sb", "call", "callr", "jr", "jmp") \
                and not parts[0].startswith("b") and parts[1].startswith("r%d," % v):
            self.out[-1] = "%s%s" % (last[:last.index(parts[1])], "r%d" % rv + parts[1][len("r%d" % v):])
        else:
            self.emit("mov   r%d, r%d" % (rv, v))
        self.free(v)

    def addr(self, n):
        k = n.kind
        if self.regof(n):
            raise_err(n.tok, "internal: the address of a register variable")
        if k == "var":
            r = self.alloc()
            o = n.obj
            if o.local:
                self.emit("addi  r%d, r14, %d" % (r, o.offset))
            else:
                self.emit("la    r%d, %s" % (r, sym(o.label)))
            return r
        if k == "varoff":
            r = self.alloc()
            self.emit("addi  r%d, r14, %d" % (r, n.var.offset + n.off))
            return r
        if k == "str":
            r = self.alloc()
            self.emit("la    r%d, %s" % (r, n.label))
            return r
        if k == "deref":
            return self.expr(n.expr)
        if k == "member":
            r = self.addr(n.expr)
            if n.mem.offset:
                self.emit("addi  r%d, r%d, %d" % (r, r, n.mem.offset))
            return r
        raise_err(n.tok, "not an lvalue")

    def expr(self, n):
        self.cur_tok = n.tok or self.cur_tok
        k = n.kind
        if k == "num":
            r = self.alloc()
            self.emit("li    r%d, %d" % (r, n.val))
            return r
        if k == "str":
            return self.addr(n)
        if k in ("var", "varoff"):
            rv = self.regof(n)
            if rv:
                return self.own(rv)
            o = self.local_mem(n)
            if o is not None and n.ty.kind not in ("array", "func") and not cty.is_memval(n.ty):
                r = self.alloc()
                self.load(r, 14, o, n.ty)
                return r
            if k == "var" and n.ty.kind == "func":
                r = self.alloc()
                self.emit("la    r%d, %s" % (r, sym(n.obj.label)))
                return r
            r = self.addr(n)
            self.load(r, r, 0, n.ty)
            return r
        if k == "addr":
            e = n.expr
            if e.kind == "var" and e.ty.kind == "func":
                r = self.alloc()
                self.emit("la    r%d, %s" % (r, sym(e.obj.label)))
                return r
            return self.addr(e)
        if k in ("deref", "member"):
            if k == "member" and n.expr.kind not in ("deref", "var", "member", "varoff"):
                raise_err(n.tok, "member of a non-lvalue struct is not supported")
            b, off = self.addr_parts(n, index_ok=not getattr(n, "bf", None))
            r = self.dest(b) if b not in (0, 14) else self.alloc()
            self.load(r, b, off, n.ty, fold=not getattr(n, "bf", None))
            self.flush_after()
            if getattr(n, "bf", None):
                self.extract(r, n.bf, n.ty)
            return r
        if k == "cast":
            r = self.expr(n.expr)
            if n.ty.kind == "void":
                self.free(r)
                return None
            src = n.expr.ty
            if is_integer(n.ty) and (n.ty.size < 4 or (n.ty.size < src.size)):
                if not (src.size <= n.ty.size and src.unsigned == n.ty.unsigned and is_integer(src)):
                    self.narrow(r, n.ty)
            return r
        if k == "neg":
            r = self.expr(n.expr)
            self.emit("sub   r%d, r0, r%d" % (r, r))
            return r
        if k == "bitnot":
            r = self.expr(n.expr)
            self.emit("nor   r%d, r%d, r0" % (r, r))
            return r
        if k == "not":
            r = self.expr(n.expr)
            self.emit("sltiu r%d, r%d, 1" % (r, r))
            return r
        if k == "binop":
            return self.binop(n)
        if k == "cmp":
            return self.cmp(n)
        if k in ("logand", "logor"):
            r = self.alloc()
            end = self.newlab()
            if k == "logand":
                self.emit("li    r%d, 0" % r)
                self.branch(n.lhs, end, False)
                self.branch(n.rhs, end, False)
                self.emit("li    r%d, 1" % r)
            else:
                self.emit("li    r%d, 1" % r)
                self.branch(n.lhs, end, True)
                self.branch(n.rhs, end, True)
                self.emit("li    r%d, 0" % r)
            self.label(end)
            return r
        if k == "cond":
            els, end = self.newlab(), self.newlab()
            self.branch(n.cond, els, False)
            if n.ty.kind == "void":
                self.free(self.expr(n.a))
                self.emit("jmp   %s" % end)
                self.label(els)
                self.free(self.expr(n.b))
                self.label(end)
                return None
            r = self.expr(n.a)
            self.emit("jmp   %s" % end)
            self.label(els)
            r2 = self.expr(n.b)
            if r2 != r:
                self.emit("mov   r%d, r%d" % (r, r2))
                self.free(r2)
            self.label(end)
            return r
        if k == "comma":
            self.free(self.expr(n.lhs))
            return self.expr(n.rhs)
        if k == "memaddr":                  # (a long long's address, as a pointer)
            return self.expr(n.expr)
        if k == "stmtexpr":                 # (a compound literal: its initialisation, then its address;
            for s in n.body:                #  only these two kinds, run among live temporaries)
                if s.kind == "memzero":
                    self.zero(s.var)
                else:
                    self.free(self.expr(s.expr))
            return self.expr(n.expr)
        if k == "assign":
            return self.assign(n)
        if k == "opassign":
            return self.opassign(n)
        if k in ("preinc", "predec", "postinc", "postdec"):
            return self.incdec(n)
        if k == "call":
            return self.call(n)
        raise_err(n.tok, "internal: expression %s" % k)

    def imm_op(self, op, unsigned, v):
        """An immediate instruction for `reg op v`, or None."""
        if op == "+" and fits16(v):
            return "addi", v
        if op == "-" and fits16(-v):
            return "addi", -v
        if op in "&|^" and 0 <= v <= 0xFFFF:
            return {"&": "andi", "|": "ori", "^": "xori"}[op], v
        if op == "<<" and 0 <= v <= 31:
            return "slli", v
        if op == ">>" and 0 <= v <= 31:
            return ("srli" if unsigned else "srai"), v
        if op == "*" and v > 0 and v & (v - 1) == 0:
            return "slli", v.bit_length() - 1
        if op == "/" and unsigned and v > 0 and v & (v - 1) == 0:
            return "srli", v.bit_length() - 1
        if op == "%" and unsigned and v > 0 and v & (v - 1) == 0 and v - 1 <= 0xFFFF:
            return "andi", v - 1
        return None

    RR = {"+": "add", "-": "sub", "*": "mul", "&": "and", "|": "or", "^": "xor", "<<": "sll"}

    def arith(self, op, unsigned, a, b, d=None):
        if op in self.RR:
            ins = self.RR[op]
        elif op == ">>":
            ins = "srl" if unsigned else "sra"
        elif op == "/":
            ins = "divu" if unsigned else "div"
        elif op == "%":
            ins = "remu" if unsigned else "rem"
        else:
            raise_err(None, "internal: op %s" % op)
        self.emit("%-5s r%d, r%d, r%d" % (ins, a if d is None else d, a, b))

    def binop(self, n):
        a = self.src(n.lhs)
        uns = n.lhs.ty.unsigned if n.op == ">>" else n.ty.unsigned
        if n.rhs.kind == "num":
            im = self.imm_op(n.op, uns, n.rhs.val)
            if im:
                d = self.dest(a)
                self.emit("%-5s r%d, r%d, %d" % (im[0], d, a, im[1]))
                return d
        b = self.src(n.rhs)
        d = self.dest(a)
        self.arith(n.op, uns, a, b, d)
        self.free(b)
        return d

    def cmp(self, n):
        a = self.src(n.lhs)
        b = self.src(n.rhs)
        if a in self.reserved:
            d = self.alloc()
            self.emit("mov   r%d, r%d" % (d, a))
            a = d
        u = "u" if n.unsigned else ""
        op = n.op
        if op == "==":
            self.emit("xor   r%d, r%d, r%d" % (a, a, b))
            self.emit("sltiu r%d, r%d, 1" % (a, a))
        elif op == "!=":
            self.emit("xor   r%d, r%d, r%d" % (a, a, b))
            self.emit("sltu  r%d, r0, r%d" % (a, a))
        elif op == "<":
            self.emit("slt%s  r%d, r%d, r%d" % (u, a, a, b))
        elif op == ">":
            self.emit("slt%s  r%d, r%d, r%d" % (u, a, b, a))
        elif op == "<=":
            self.emit("slt%s  r%d, r%d, r%d" % (u, a, b, a))
            self.emit("xori  r%d, r%d, 1" % (a, a))
        else:
            self.emit("slt%s  r%d, r%d, r%d" % (u, a, a, b))
            self.emit("xori  r%d, r%d, 1" % (a, a))
        self.free(b)
        return a

    def copy_struct(self, dst, src, ty):
        """Copy a struct of type ty, in the widest pieces its size and its
        alignment allow (a 2-aligned struct may lie at an odd halfword)."""
        t = self.alloc()
        size = ty.size
        unit = 4 if size % 4 == 0 and ty.align >= 4 else 2 if size % 2 == 0 and ty.align >= 2 else 1
        ld, st = {4: ("lw   ", "sw   "), 2: ("lhu  ", "sh   "), 1: ("lbu  ", "sb   ")}[unit]
        if size > 64:                       # (a loop, not a long run of copies)
            a, b, e = self.alloc(), self.alloc(), self.alloc()
            lp = self.newlab()
            self.emit("mov   r%d, r%d" % (a, src))
            self.emit("mov   r%d, r%d" % (b, dst))
            self.emit("addi  r%d, r%d, %d" % (e, dst, size))
            self.label(lp)
            self.emit("%s r%d, 0(r%d)" % (ld, t, a))
            self.emit("%s r%d, 0(r%d)" % (st, t, b))
            self.emit("addi  r%d, r%d, %d" % (a, a, unit))
            self.emit("addi  r%d, r%d, %d" % (b, b, unit))
            self.emit("bne   r%d, r%d, %s" % (b, e, lp))
            self.free(a); self.free(b); self.free(e)
        else:
            for o in range(0, size, unit):
                self.emit("%s r%d, %d(r%d)" % (ld, t, o, src))
                self.emit("%s r%d, %d(r%d)" % (st, t, o, dst))
        self.free(t)

    def extract(self, r, bf, ty):
        """r holds a bit-field's unit: leave the field's value, sign- or zero-extended."""
        bit, width = bf
        if 32 - bit - width:
            self.emit("slli  r%d, r%d, %d" % (r, r, 32 - bit - width))
        if 32 - width:
            self.emit("%s  r%d, r%d, %d" % ("srli" if ty.unsigned else "srai", r, r, 32 - width))

    def assign_bits(self, n):
        """A bit-field's unit = (unit & ~mask) | (value << bit & mask); the value
        the field now holds is the result."""
        lhs, ty = n.lhs, n.ty
        bit, width = lhs.bf
        novalue = self.take_no_value()
        v = self.own(self.src(n.rhs))
        o = self.local_mem(lhs) if lhs.kind == "varoff" else None
        if o is not None:
            b, off = 14, o
        else:
            b, off = self.addr_parts(lhs)
        u, m = self.alloc(), self.alloc()
        self.load(u, b, off, ty)
        self.emit("li    r%d, %d" % (m, ((((1 << width) - 1) << bit) ^ 0x80000000) - 0x80000000))
        self.emit("nor   r%d, r%d, r0" % (m, m))
        self.emit("and   r%d, r%d, r%d" % (u, u, m))
        self.emit("slli  r%d, r%d, %d" % (m, v, 32 - width))
        if 32 - width - bit:
            self.emit("srli  r%d, r%d, %d" % (m, m, 32 - width - bit))
        self.emit("or    r%d, r%d, r%d" % (u, u, m))
        self.store(u, b, off, ty)
        self.free(m)
        self.free(u)
        self.flush_after()
        self.free(b)
        if novalue:
            self.free(v)
            return None
        self.extract(v, (0, width), ty)
        return v

    def assign(self, n):
        lhs, ty = n.lhs, n.ty
        if getattr(lhs, "bf", None):
            return self.assign_bits(n)
        if cty.is_memval(ty):
            self.take_no_value()        # (else it reaches the operands: m[k++] = x lost k's value)
            src = self.addr(n.rhs) if n.rhs.kind in ("var", "deref", "member", "varoff") else self.expr(n.rhs)
            dst = self.addr(lhs)
            self.copy_struct(dst, src, ty)
            self.free(src)
            return dst
        novalue = self.take_no_value()
        rv = self.regof(lhs)
        if rv:
            if rv != self.regof(n.rhs) or not novalue:
                # (the value straight into the variable when it is computed in a temporary)
                v = self.src(n.rhs)
                if v in self.reserved:
                    self.emit("mov   r%d, r%d" % (rv, v))
                else:
                    self.retarget(v, rv)
            return None if novalue else self.own(rv)
        v = self.src(n.rhs)
        o = self.local_mem(lhs)
        if o is not None:
            self.store(v, 14, o, ty)
        else:
            b, off = self.addr_parts(lhs, index_ok=True)
            self.store(v, b, off, ty, fold=True)
            self.flush_after()
            self.free(b)
        if novalue:
            self.free(v)
            return None
        return self.own(v)

    def opassign(self, n):
        lhs, ty = n.lhs, n.ty
        rv = self.regof(lhs)
        novalue = self.take_no_value()
        if rv:
            uns = (ty.unsigned if n.op == ">>" else n.opty.unsigned) if ty.kind != "ptr" else True
            im = self.imm_op(n.op, uns, n.rhs.val) if n.rhs.kind == "num" else None
            if im:
                self.emit("%-5s r%d, r%d, %d" % (im[0], rv, rv, im[1]))
            else:
                b = self.expr(n.rhs)
                self.arith(n.op, uns, rv, b)
                self.free(b)
            self.narrow(rv, ty)
            return None if novalue else self.own(rv)
        o = self.local_mem(lhs)
        if o is not None:
            base, off, a = 14, o, None
        else:
            a = self.addr(lhs)
            base, off = a, 0
        v = self.alloc()
        self.load(v, base, off, ty)
        uns = (ty.unsigned if n.op == ">>" else n.opty.unsigned) if ty.kind != "ptr" else True
        im = self.imm_op(n.op, uns, n.rhs.val) if n.rhs.kind == "num" else None
        if im:
            self.emit("%-5s r%d, r%d, %d" % (im[0], v, v, im[1]))
        else:
            b = self.expr(n.rhs)
            self.arith(n.op, uns, v, b)
            self.free(b)
        self.narrow(v, ty)
        self.store(v, base, off, ty)
        self.free(a)
        return v

    def incdec(self, n):
        e, ty = n.expr, n.ty
        rv = self.regof(e)
        novalue = self.take_no_value()
        if rv:
            step = n.step if n.kind.endswith("inc") else -n.step
            v = None
            if n.kind.startswith("post") and not novalue:
                v = self.alloc()
                self.emit("mov   r%d, r%d" % (v, rv))
            self.emit("addi  r%d, r%d, %d" % (rv, rv, step))
            self.narrow(rv, ty)
            if n.kind.startswith("pre") and not novalue:
                v = self.own(rv)
            return v
        o = self.local_mem(e)
        if o is not None:
            base, off, a = 14, o, None
        else:
            a = self.addr(e)
            base, off = a, 0
        v = self.alloc()
        self.load(v, base, off, ty)
        step = n.step if n.kind.endswith("inc") else -n.step
        if n.kind.startswith("pre"):
            self.emit("addi  r%d, r%d, %d" % (v, v, step))
            self.narrow(v, ty)
            self.store(v, base, off, ty)
            self.free(a)
            return v
        t = self.alloc()
        self.emit("addi  r%d, r%d, %d" % (t, v, step))
        self.narrow(t, ty)
        self.store(t, base, off, ty)
        self.free(t)
        self.free(a)
        return v

    def push(self, r):
        self.emit("addi  sp, sp, -4")
        self.emit("sw    r%d, 0(sp)" % r)

    def compare_chain(self, n, v, t, brk):
        """A sparse switch: compare with each case in turn."""
        for val, lab in n.cases:
            if val == 0:
                self.emit("beq   r%d, r0, %s" % (v, lab))
            else:
                self.emit("li    r%d, %d" % (t, val))
                self.emit("beq   r%d, r%d, %s" % (v, t, lab))
        self.emit("jmp   %s" % (n.default or brk))

    def jump_table(self, n, v, t, brk):
        """A dense switch: one unsigned compare against the table's size
        (which also catches values below the lowest case), then a jump
        through a table of case addresses that follows the JR in .text."""
        lo, hi = table_range(n.cases)
        other = n.default or brk
        d = self.alloc()
        tab = self.newlab()
        x = v
        if lo:
            if fits16(-lo):
                self.emit("addi  r%d, r%d, %d" % (d, v, -lo))
            else:
                self.emit("li    r%d, %d" % (d, s32(lo)))
                self.emit("sub   r%d, r%d, r%d" % (d, v, d))
            x = d
        self.emit("li    r%d, %d" % (t, hi - lo + 1))
        self.emit("bgeu  r%d, r%d, %s" % (x, t, other))
        self.emit("slli  r%d, r%d, 2" % (d, x))
        self.emit("la    r%d, %s" % (t, tab))
        self.emit("add   r%d, r%d, r%d" % (d, d, t))
        self.emit("lw    r%d, 0(r%d)" % (d, d))
        self.emit("jr    r%d" % d)
        self.free(d)
        labs = dict(n.cases)
        self.label(tab)
        for k in range(lo, hi + 1):
            self.emit(".word %s" % labs.get(k, other))

    def call(self, n):
        live = sorted(r for r in self.busy if r <= 7)
        if live:
            self.emit("addi  sp, sp, %d" % (-4 * len(live)))
            for k, r in enumerate(live):
                self.emit("sw    r%d, %d(sp)" % (r, 4 * k))
            for r in live:
                self.busy.discard(r)
        nargs = len(n.args)
        if n.ftype.variadic and any(a.ty.kind == "double" for a in n.args):
            self.printf_fp = True
        nreg = 0 if cty.stack_args(n.ftype) else min(4, nargs)
        # the outgoing arguments, left to right, into slots reserved first
        # (a word each, a struct as many as it needs; nested calls leave sp
        # as they found it)
        slots, nwords = [], 0
        for a in n.args:
            slots.append(nwords)
            nwords += cty.words(a.ty)
        if nwords:
            self.emit("addi  sp, sp, %d" % (-4 * nwords))
        for k, a in enumerate(n.args):
            r = self.expr(a)
            if cty.is_memval(a.ty):                     # (r: its address; a copy of its own)
                d = self.alloc()
                self.emit("addi  r%d, sp, %d" % (d, 4 * slots[k]))
                self.copy_struct(d, r, a.ty)
                self.free(d)
            else:
                self.emit("sw    r%d, %d(sp)" % (r, 4 * slots[k]))
            self.free(r)
        f = None
        if n.direct is None:
            f = self.expr(n.fn)
            if f <= 4:
                self.emit("mov   r5, r%d" % f)
                self.free(f)
                f = 5
        for k in range(nreg):
            self.emit("lw    r%d, %d(sp)" % (k + 1, 4 * k))
        if nreg:
            self.emit("addi  sp, sp, %d" % (4 * nreg))
        if n.direct is not None:
            self.emit("call  %s" % sym(n.direct.label))
        else:
            self.emit("callr r%d" % f)
            self.free(f)
        if nwords - nreg:
            self.emit("addi  sp, sp, %d" % (4 * (nwords - nreg)))
        res = None
        if n.ty.kind != "void":
            for r in range(1, 14):
                if r not in self.busy and r not in live:
                    res = r
                    break
            self.busy.add(res)
            if res >= 8:
                self.callee.add(res)
            if res != 1:
                self.emit("mov   r%d, r1" % res)
            self.narrow(res, n.ty)
        if live:
            for k, r in enumerate(live):
                self.emit("lw    r%d, %d(sp)" % (r, 4 * k))
                self.busy.add(r)
            self.emit("addi  sp, sp, %d" % (4 * len(live)))
        return res

def raise_err(tok, msg):
    err(tok, msg)
