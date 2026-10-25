"""cparse.py -- the C parser of p32cc (M2, D22).

Recursive descent over the preprocessed tokens, producing a typed syntax
tree: Node for expressions and statements, Obj for variables and
functions. Types are worked out as the tree is built (C89 rules, 32-bit
integers), and constant expressions are folded.
"""
from clex import CError, err, parse_int, Tok
import copy
import re
import struct
import cty
from cty import (Type, VOID, CHAR, UCHAR, BOOL, SHORT, USHORT, INT, UINT, LONG, ULONG, LLONG, ULLONG, DOUBLE, is_memval,
                 ptr_to, array_of, func_type, decay, promote, usual, is_integer, is_arith,
                 is_ptr, is_scalar, find_member, layout)

# the C library's variadic functions and their headers (an undeclared call
# to one is an error, not a warning)
VARIADIC_LIB = {"printf": "stdio.h", "fprintf": "stdio.h", "sprintf": "stdio.h", "snprintf": "stdio.h",
                "scanf": "stdio.h", "fscanf": "stdio.h", "sscanf": "stdio.h", "open": "fcntl.h",
                "execl": "unistd.h", "execlp": "unistd.h", "execle": "unistd.h"}

class Node:
    def __init__(self, kind, tok=None, ty=None, **kw):
        self.kind, self.tok, self.ty = kind, tok, ty
        self.__dict__.update(kw)
    def __repr__(self):
        return "Node(%s)" % self.kind

class Obj:
    def __init__(self, name, ty, local=False):
        self.name, self.ty, self.local = name, ty, local
        self.static = False
        self.is_def = False          # a definition (function body / global storage)
        self.label = name            # assembly symbol (globals, functions, static locals)
        self.offset = 0              # locals: frame offset (set by the code generator)
        self.init = None             # globals: (bytes, [(offset, label, addend)])
        self.body = None
        self.params = []
        self.locals = []
        self.extern = False
        self.used = False
        self.uses = 0                # references (register allocation picks the busiest)
        self.addr_taken = False      # &x anywhere: it must live in memory

FLOAT_RE = re.compile(r"^((?:[0-9]+\.[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?|[0-9]+[eE][+-]?[0-9]+)[fFlL]?$")
# C99's hexadecimal floating constants: 0x1.8p3 (the p exponent is required)
HEXFLOAT_RE = re.compile(r"^(0[xX](?:[0-9a-fA-F]+\.?[0-9a-fA-F]*|\.[0-9a-fA-F]+)[pP][+-]?[0-9]+)[fFlL]?$")

WRAP = 1 << 32
def wrap(v, t):
    """A constant's value as type t holds it (ints are 32 bits, long long 64)."""
    if t.is_bool:
        return 1 if v else 0
    if t.kind in ("char", "short", "int", "long", "enum", "ptr", "llong"):
        bits = t.size * 8
        v &= (1 << bits) - 1
        if not t.unsigned and t.kind != "ptr" and v >> (bits - 1):
            v -= 1 << bits
    return v

class NotConst(Exception):
    pass

TYPE_KW = {"void", "char", "short", "int", "long", "signed", "unsigned", "struct", "union", "enum",
           "const", "volatile", "typedef", "extern", "static", "auto", "register", "float", "double",
           "_Bool", "inline", "restrict"}

class Parser:
    def __init__(self, toks, warn=None):
        self.t = toks + [Tok("eof", "", toks[-1].file if toks else "?", toks[-1].line if toks else 0)]
        self.i = 0
        self.scopes = [{}]
        self.tags = [{}]
        self.globals = []            # Obj, in order
        self.funcs = []              # the functions defined, in that order
        self.llconsts = {}           # (value, unsigned) -> the long long constant's object
        self.helpers = {}            # the libc helpers called: name -> Obj
        self.dconsts = {}            # a double's bits -> its constant's object
        self.gnames = {}
        self.strings = []            # (label, bytes)
        self.fn = None               # the function being parsed
        self.nlabel = 0
        self.brk = []                # break allowed?
        self.hdr = 0                 # inside a loop's condition or increment
        self.cont = []
        self.switches = []
        self.warn = warn or (lambda m: None)

    # ---------------------------------------------------------------- tokens
    @property
    def tok(self):
        return self.t[self.i]
    def peek(self, k=1):
        return self.t[min(self.i + k, len(self.t) - 1)]
    def at(self, s):
        return self.tok.text == s and self.tok.kind in ("punct", "id")
    def skip(self, s):
        if not self.at(s):
            err(self.tok, "expected '%s', got '%s'" % (s, self.tok.text or "end of file"))
        self.i += 1
    def consume(self, s):
        if self.at(s):
            self.i += 1
            return True
        return False
    def ident(self):
        if self.tok.kind != "id":
            err(self.tok, "expected a name, got '%s'" % self.tok.text)
        t = self.tok
        self.i += 1
        return t

    # ---------------------------------------------------------------- scopes
    def push(self):
        self.scopes.append({}); self.tags.append({})
    def pop(self):
        self.scopes.pop(); self.tags.pop()
    def lookup(self, name):
        for s in reversed(self.scopes):
            if name in s:
                return s[name]
        return None
    def lookup_tag(self, name):
        for s in reversed(self.tags):
            if name in s:
                return s[name]
        return None
    def declare(self, name, entry):
        self.scopes[-1][name] = entry
    def is_typename(self, tok=None):
        tok = tok or self.tok
        if tok.kind != "id":
            return False
        if tok.text in TYPE_KW:
            return True
        e = self.lookup(tok.text)
        return e is not None and e[0] == "typedef"
    def newlabel(self, what="L"):
        self.nlabel += 1
        return ".L%s%d" % (what, self.nlabel)

    # ================================================================ declarations
    def program(self):
        while self.tok.kind != "eof":
            if self.consume(";"):
                continue
            self.external()
        return self

    def declspec(self, allow_storage=True):
        """Specifiers -> (type, storage) where storage is None/typedef/extern/static/register/auto."""
        storage = None
        counts = {}
        ty = None
        tok0 = self.tok
        const = False
        self.saw_volatile = False
        while True:
            t = self.tok
            if t.kind != "id":
                break
            s = t.text
            if s in ("typedef", "extern", "static", "auto", "register"):
                if not allow_storage:
                    err(t, "storage class not allowed here")
                if storage and storage != s:
                    err(t, "two storage classes")
                storage = s
                self.i += 1
                continue
            if s in ("const", "volatile", "inline", "restrict"):     # (restrict: a hint, ignored)
                const = const or s == "const"
                self.saw_volatile = self.saw_volatile or s == "volatile"
                self.i += 1
                continue
            if s in ("struct", "union"):
                if ty or counts:
                    err(t, "two types")
                self.i += 1
                ty = self.struct_decl(s)
                continue
            if s == "enum":
                if ty or counts:
                    err(t, "two types")
                self.i += 1
                ty = self.enum_decl()
                continue
            if s in ("void", "char", "short", "int", "long", "signed", "unsigned", "_Bool", "float", "double"):
                if ty:
                    err(t, "two types")
                counts[s] = counts.get(s, 0) + 1
                self.i += 1
                continue
            e = self.lookup(s)
            if e and e[0] == "typedef" and not ty and not counts:
                ty = e[1]
                self.i += 1
                continue
            break
        if ty is None:
            if not counts:
                if storage is None and not const and self.tok is tok0:
                    err(tok0, "expected a type, got '%s'" % tok0.text)
                ty = INT                                    # C89 implicit int
            else:
                ty = self.basic(counts, tok0)
        return ty, storage

    def basic(self, c, tok):
        u = c.get("unsigned", 0)
        if u and c.get("signed"):
            err(tok, "signed and unsigned")
        if c.get("void"):
            return VOID
        if c.get("float") or c.get("double"):              # (float, double, long double: one type)
            return DOUBLE
        if c.get("_Bool"):
            return BOOL
        if c.get("char"):
            return UCHAR if u else CHAR
        if c.get("short"):
            return USHORT if u else SHORT
        if c.get("long", 0) >= 2:
            return ULLONG if u else LLONG
        if c.get("long"):
            return ULONG if u else LONG
        return UINT if u else INT

    def struct_decl(self, kind):
        tag = None
        if self.tok.kind == "id":
            tag = self.ident().text
        if not self.at("{"):
            if tag is None:
                err(self.tok, "%s needs a tag or a body" % kind)
            st = self.lookup_tag(tag)
            if st is None:
                st = cty.struct_type(kind, tag)
                self.tags[0 if False else -1][tag] = st     # incomplete, in the current scope
            elif st.kind != kind:
                err(self.tok, "%s %s is a %s" % (kind, tag, st.kind))
            return st
        self.skip("{")
        st = None
        if tag:
            st = self.tags[-1].get(tag)
            if st and st.members is not None:
                err(self.tok, "%s %s redefined" % (kind, tag))
        if st is None:
            st = cty.struct_type(kind, tag)
            if tag:
                self.tags[-1][tag] = st
        members = []
        while not self.consume("}"):
            base, _ = self.declspec(False)
            if self.consume(";"):                            # anonymous struct / union member
                if base.kind in ("struct", "union"):
                    members.append((None, base))
                continue
            while True:
                if self.at(":"):                            # an unnamed bit-field
                    name, ty = None, base
                else:
                    name, ty = self.declarator(base)
                width = None
                if self.consume(":"):
                    t = self.tok
                    width = self.const_expr()
                    if not is_integer(ty):
                        err(t, "a bit-field needs an integer type")
                    if ty.kind == "llong":
                        err(t, "bit-fields wider than 32 bits are not supported")
                    if width < 0 or width > ty.size * 8:
                        err(t, "bit-field width %d is out of range" % width)
                    if width == 0 and name:
                        err(t, "a bit-field of width 0 has no name")
                elif ty.kind == "func" or (ty.kind == "array" and ty.length is None) or ty.size == 0 and ty.kind != "array":
                    if not (ty.kind == "array" and ty.length is None):
                        err(name or self.tok, "member %s has an incomplete type" % (name.text if name else ""))
                members.append((name.text if name else None, ty, width))
                if not self.consume(","):
                    break
            self.skip(";")
        layout(st, members)
        return st

    def enum_decl(self):
        tag = None
        if self.tok.kind == "id":
            tag = self.ident().text
        ty = cty.enum_type()
        if not self.at("{"):
            e = self.lookup_tag(tag) if tag else None
            return e or ty
        self.skip("{")
        v = 0
        while not self.consume("}"):
            name = self.ident()
            if self.consume("="):
                v = self.const_expr()
            self.declare(name.text, ("enum", v))
            v += 1
            if not self.consume(","):
                self.skip("}")
                break
        if tag:
            self.tags[-1][tag] = ty
        return ty

    def declarator(self, ty, abstract=False):
        """-> (name token or None, type)."""
        while self.consume("*"):
            ty = ptr_to(ty)
            while self.tok.text in ("const", "volatile", "restrict") and self.tok.kind == "id":
                if self.tok.text == "volatile":  # T *volatile p: p itself is volatile, so never kept
                    self.saw_volatile = True     # in a register (setjmp / longjmp would undo it)
                self.i += 1
        if self.at("(") and not abstract_params(self) and not (abstract and self.is_typename(self.peek())):
            # a nested declarator: parse what follows the parentheses first
            start = self.i
            self.i += 1
            depth = 1
            while depth:
                if self.tok.kind == "eof":
                    err(self.tok, "unbalanced parentheses")
                if self.at("("):
                    depth += 1
                elif self.at(")"):
                    depth -= 1
                self.i += 1
            ty = self.suffixes(ty)
            end = self.i
            self.i = start + 1
            name, ty = self.declarator(ty, abstract)
            self.skip(")")
            self.i = end
            return name, ty
        name = None
        if self.tok.kind == "id" and not self.is_typename():
            name = self.ident()
        elif self.tok.kind == "id":
            name = self.ident()                              # a typedef name redeclared (a parameter too: qstr qstr)
        elif not abstract:
            err(self.tok, "expected a name, got '%s'" % self.tok.text)
        return name, self.suffixes(ty)

    def suffixes(self, ty):
        if self.consume("["):
            n = None
            if not self.at("]"):
                n = self.const_expr()
                if n < 0:
                    err(self.tok, "negative array size")
            self.skip("]")
            inner = self.suffixes(ty)
            if inner.kind == "func":
                err(self.tok, "array of functions")
            return array_of(inner, n)
        if self.at("("):
            self.i += 1
            params, variadic, names, vols = self.params()
            ret = self.suffixes(ty)
            if ret.kind in ("func", "array"):
                err(self.tok, "a function cannot return a %s" % ret.kind)
            f = func_type(ret, params, variadic)
            f.param_names = names
            f.param_vol = vols                              # (which parameters are volatile)
            return f
        return ty

    def params(self):
        if self.consume(")"):
            return None, False, [], []                      # unprototyped: f()
        if self.at("void") and self.peek().text == ")":
            self.i += 2
            return [], False, [], []
        ps, names, vols, variadic = [], [], [], False
        while True:
            if self.consume("..."):
                variadic = True
                self.skip(")")
                break
            if not self.is_typename() and self.tok.kind == "id":
                # K&R identifier list: f(a, b) -- not supported
                err(self.tok, "old-style (K&R) parameter lists are not supported")
            base, _ = self.declspec(True)
            vol = self.saw_volatile
            self.saw_volatile = False
            name, ty = self.declarator(base, abstract=True)
            vol = vol or self.saw_volatile                  # (a volatile parameter: kept in memory)
            if ty.kind == "array":
                ty = ptr_to(ty.base)
            elif ty.kind == "func":
                ty = ptr_to(ty)
            ps.append(ty)
            names.append(name)
            vols.append(vol)
            if self.consume(")"):
                break
            self.skip(",")
        return ps, variadic, names, vols

    def typename(self):
        base, _ = self.declspec(False)
        _, ty = self.declarator(base, abstract=True)
        return ty

    # ---------------------------------------------------------------- top level
    def external(self):
        base, storage = self.declspec()
        if self.consume(";"):
            return                                           # struct / enum declaration only
        first = True
        while True:
            name, ty = self.declarator(base)
            if storage == "typedef":
                self.declare(name.text, ("typedef", ty))
            elif ty.kind == "func":
                fn = self.declare_global(name, ty, storage)
                if first and self.at("{"):
                    self.function(fn, name)
                    return
            else:
                obj = self.declare_global(name, ty, storage)
                if self.consume("="):
                    self.global_init(obj, name)
                elif storage != "extern":
                    if not obj.is_def and obj.init is None:
                        obj.tentative = True
            first = False
            if self.consume(";"):
                return
            self.skip(",")

    def declare_global(self, name, ty, storage):
        if self.fn is not None and ty.kind == "func" and getattr(ty, "param_names", None):
            ty = copy.copy(ty)       # (declared in a body: never a definition, so no names; cc in C
            ty.param_names = None    #  frees a body's tokens once it is generated)
        e = self.scopes[0].get(name.text)
        if e and e[0] == "var":
            obj = e[1]
            if ty.kind == "func" and obj.ty.kind == "func":
                if ty.params is not None:
                    obj.ty = ty
            elif ty.kind == "array" and obj.ty.kind == "array" and obj.ty.length is None:
                obj.ty = ty
            if storage == "static":
                obj.static = True
            if storage != "extern":
                obj.extern = False
            return obj
        obj = Obj(name.text, ty)
        obj.static = storage == "static"
        obj.extern = storage == "extern"
        obj.tentative = False
        self.scopes[0][name.text] = ("var", obj)
        self.globals.append(obj)
        return obj

    def function(self, fn, name):
        if fn.is_def:
            err(name, "function %s defined twice" % name.text)
        fn.is_def = True
        fn.extern = False
        self.fn = fn
        fn.locals, fn.params = [], []
        fn.labels, fn.gotos = {}, []
        self.push()
        ty = fn.ty
        if is_memval(ty.base):                              # where to put the result: argument 0
            fn.retptr = Obj("", ptr_to(ty.base), True)
            fn.params.append(fn.retptr)
        for k, (pt, pn) in enumerate(zip(ty.params or [], getattr(ty, "param_names", []) or [])):
            if pn is None:
                err(name, "parameter %d of %s has no name" % (k + 1, name.text))
            p = Obj(pn.text, pt, True)
            if k < len(getattr(ty, "param_vol", None) or []) and ty.param_vol[k]:
                p.addr_taken = True                          # (volatile: kept in memory, so longjmp cannot undo it)
            fn.params.append(p)
            self.declare(pn.text, ("var", p))
        if ty.params is None:
            pass                                             # f() { ... }: no parameters used
        fn.body = self.compound()
        for g in fn.gotos:
            if g.label not in fn.labels:
                err(g.tok, "label %s is not defined" % g.label)
        self.pop()
        self.fn = None
        self.funcs.append(fn)        # (in the order defined: cc in C generates each as it ends)

    # ---------------------------------------------------------------- initialisers
    def initializer(self, ty):
        """-> an Init tree: ('expr', node), ('list', [Init...]), and in a list
        ('desig', [designator...], Init) for [index] = and .member = items."""
        if self.at("{"):
            self.i += 1
            items = []
            while not self.consume("}"):
                ds = []
                while self.at("[") or self.at("."):
                    t = self.tok
                    if self.consume("["):
                        ds.append(("index", self.const_expr(), t))
                        self.skip("]")
                    else:
                        self.i += 1
                        ds.append(("field", self.ident().text, t))
                if ds:
                    self.skip("=")
                    items.append(("desig", ds, self.initializer(None), ds[0][2]))
                else:
                    items.append(self.initializer(None))
                if not self.consume(","):
                    self.skip("}")
                    break
            return ("list", items, self.tok)
        return ("expr", self.assign(), self.tok)

    def flatten_init(self, ty, init, off, out, tok, bf=None):
        """Walk an Init tree against a type: out gets (offset, type, expr node or
        bytes, bit-field (bit, width) or None) in order (a later one for the same
        place wins). -> the array length used."""
        kind = init[0]
        if ty.kind == "array":
            # a string literal for a char array
            src = init
            if kind == "list" and len(init[1]) == 1 and init[1][0][0] == "expr" and init[1][0][1].kind == "str":
                src = init[1][0]
            if src[0] == "expr" and src[1].kind == "str" and ty.base.kind == "char":
                data = src[1].data
                n = ty.length if ty.length is not None else len(data)
                out.append((off, ty, data[:n].ljust(n, b"\0"), None))
                return n
            if kind != "list":
                err(tok, "array initialiser needs { }")
            n = self.fill(ty, init[1], 0, off, out, tok, True)[1]
            return n if ty.length is None else ty.length
        if ty.kind in ("struct", "union"):
            if kind != "list":
                if ty.kind and init[1].ty is not None and init[1].ty is ty:
                    out.append((off, ty, init[1], None))     # struct = struct expression
                    return 1
                err(tok, "struct initialiser needs { }")
            self.fill(ty, init[1], 0, off, out, tok, True)
            return 1
        # scalar (braces allowed around it)
        while kind == "list":
            if len(init[1]) != 1 or init[1][0][0] == "desig":
                err(tok, "scalar initialiser with %d values" % len(init[1]))
            init = init[1][0]
            kind = init[0]
        out.append((off, ty, init[1], bf))
        return 1

    def whole(self, ty, it):
        """Does the (unbraced) item it initialise all of aggregate ty: a string for a
        char array, or an expression of the struct's type?"""
        e = it[1]
        if ty.kind == "array":
            return e.kind == "str" and ty.base.kind == "char"
        return e.ty is ty

    def inits(self, ty):
        """The members an initialiser fills (unnamed bit-fields are skipped)."""
        return [m for m in ty.members if not (m.width is not None and m.name is None)]

    def subobject(self, ty, pos, off):
        """-> (type, offset, bit-field (bit, width) or None)"""
        if ty.kind == "array":
            return ty.base, off + pos * ty.base.size, None
        m = self.inits(ty)[pos]
        return m.ty, off + m.offset, (m.bit, m.width) if m.width else None

    def anon_path(self, ty, ds):
        """.name for a member of an anonymous struct or union member (C11):
        that member's position first, then .name inside it."""
        kind, v, t = ds[0]
        if kind != "field" or ty.kind not in ("struct", "union"):
            return ds
        mems = self.inits(ty)
        if any(m.name == v for m in mems):
            return ds
        for k, m in enumerate(mems):
            if m.name is None and m.ty.kind in ("struct", "union") and cty.find_member(m.ty, v):
                return [("pos", k, t)] + self.anon_path(m.ty, ds)
        return ds

    def designate(self, ty, d):
        """The position a designator names in aggregate ty."""
        kind, v, t = d
        if kind == "pos":                       # (anon_path's: an anonymous member)
            return v
        if kind == "index":
            if ty.kind != "array":
                err(t, "[%d] = for a non-array" % v)
            if v < 0 or (ty.length is not None and v >= ty.length):
                err(t, "index %d is outside the array" % v)
            return v
        if ty.kind not in ("struct", "union"):
            err(t, ".%s = for a non-struct" % v)
        for k, m in enumerate(self.inits(ty)):
            if m.name == v:
                return k
        err(t, "no member %s" % v)

    def fill(self, ty, items, idx, off, out, tok, braced):
        """Initialise aggregate ty from items[idx:], C's "current object" walk: each
        item takes the next member or element (a designator moves there first); an
        unbraced item for an aggregate member starts filling it from the same list
        (brace elision). braced: the whole list is ty's; else (elision) only as many
        items as ty takes, stopping at a designator, which belongs to the list
        above. -> (the next item, the array length used)."""
        pos = used = 0
        nmem = None if ty.kind == "array" and ty.length is None else \
            ty.length if ty.kind == "array" else len(self.inits(ty)) if ty.kind == "struct" else min(1, len(self.inits(ty)))
        items = list(items)
        done = 0
        while idx < len(items):
            it = items[idx]
            if it[0] == "desig":
                if not braced:
                    break
                ds = self.anon_path(ty, it[1])
                pos = self.designate(ty, ds[0])
                if len(ds) > 1:                     # .a.b = / [i].x =: down to the last one
                    st, so, _ = self.subobject(ty, pos, off)
                    self.fill(st, [("desig", ds[1:], it[2], it[3])], 0, so, out, tok, True)
                    pos += 1
                    used = max(used, pos)
                    idx += 1
                    done += 1
                    continue
                items[idx] = it = it[2]
            elif nmem is not None and (pos >= nmem or (ty.kind == "union" and done)):
                if braced:
                    self.warn("%s:%d: warning: excess initialiser" % (tok.file, tok.line))
                    idx = len(items)
                break
            st, so, bf = self.subobject(ty, pos, off)
            if st.kind in ("array", "struct", "union") and it[0] == "expr" and not self.whole(st, it):
                idx = self.fill(st, items, idx, so, out, tok, False)[0]
            else:
                self.flatten_init(st, it, so, out, tok, bf)
                idx += 1
            pos += 1
            used = max(used, pos)
            done += 1
        return idx, used

    def global_init(self, obj, name):
        init = self.initializer(obj.ty)
        out = []
        n = self.flatten_init(obj.ty, init, 0, out, name)
        if obj.ty.kind == "array" and obj.ty.length is None:
            obj.ty = array_of(obj.ty.base, n)
        obj.init = self.global_bytes(obj.ty, out, name)
        obj.is_def = True

    def global_bytes(self, ty, items, tok):
        size = ty.size
        if ty.kind == "struct" and ty.members and ty.members[-1].ty.kind == "array" and ty.members[-1].ty.length is None:
            # a flexible array member given elements (GCC's extension, static
            # objects only): the object runs on past sizeof to hold them (D35)
            for off, t, v, bf in items:
                end = off + (len(v) if isinstance(v, (bytes, bytearray)) else t.size)
                size = max(size, (end + ty.align - 1) // ty.align * ty.align)
        buf = bytearray(size)
        relocs = []
        for off, t, v, bf in items:
            if isinstance(v, (bytes, bytearray)):
                buf[off:off + len(v)] = v
                continue
            if t.kind == "double":                  # (8 bytes: a double constant, or an integer one)
                f = self.dv(v)
                if f is None:
                    err(tok, "not a constant expression")
                buf[off:off + 8] = struct.pack("<d", f)
                continue
            if t.kind == "llong" and getattr(v, "dval", None) is not None:
                w = int(v.dval) if v.dval == v.dval and abs(v.dval) != float("inf") else None
                if w is None or not ((0 <= w < 1 << 64) if t.unsigned else (-(1 << 63) <= w < 1 << 63)):
                    err(tok, "not a constant expression")
                buf[off:off + 8] = (w & self.M64).to_bytes(8, "little")
                continue
            label, val = self.eval_addr(v)
            if bf:                                  # (into its bits of the unit)
                if label is not None:
                    err(tok, "an address in a bit-field")
                bit, width = bf
                mask = ((1 << width) - 1) << bit
                cur = int.from_bytes(buf[off:off + t.size], "little")
                cur = cur & ~mask | (val << bit) & mask
                buf[off:off + t.size] = cur.to_bytes(t.size, "little")
                continue
            if label is not None:
                if t.size != 4:
                    err(tok, "an address needs a 4-byte object")
                relocs.append((off, label, val))
                continue
            val = wrap(val, t)
            buf[off:off + t.size] = (val & ((1 << (8 * t.size)) - 1)).to_bytes(t.size, "little")
        return bytes(buf), relocs

    def local_init(self, var, tok):
        init = self.initializer(var.ty)
        out = []
        n = self.flatten_init(var.ty, init, 0, out, tok)
        if var.ty.kind == "array" and var.ty.length is None:
            var.ty = array_of(var.ty.base, n)
        stmts = []
        if var.ty.kind in ("array", "struct", "union"):
            stmts.append(Node("memzero", tok, var=var))
        for off, t, v, bf in out:
            if isinstance(v, (bytes, bytearray)):
                for k, b in enumerate(v):
                    if b:
                        lv = Node("varoff", tok, CHAR, var=var, off=off + k)
                        stmts.append(Node("exprstmt", tok, expr=self.new_assign(lv, self.num(b - 256 if b > 127 else b, INT, tok), tok)))
                continue
            lv = Node("varoff", tok, t, var=var, off=off, bf=bf)
            stmts.append(Node("exprstmt", tok, expr=self.new_assign(lv, v, tok)))
        return stmts

    # ================================================================ statements
    def compound(self):
        self.skip("{")
        self.push()
        items = []
        while not self.consume("}"):
            if self.is_typename() and not (self.peek().text == ":" and self.tok.kind == "id" and self.lookup(self.tok.text) and self.lookup(self.tok.text)[0] != "typedef"):
                items.extend(self.local_decl())
            else:
                items.append(self.stmt())
        self.pop()
        return Node("block", None, body=items)

    def local_decl(self):
        tok = self.tok
        base, storage = self.declspec()
        vol = self.saw_volatile
        out = []
        if self.consume(";"):
            return out
        while True:
            self.saw_volatile = False
            name, ty = self.declarator(base)
            this_vol = vol or self.saw_volatile      # (volatile in the specifiers, or T *volatile name)
            if storage == "typedef":
                self.declare(name.text, ("typedef", ty))
            elif ty.kind == "func" or storage == "extern":
                g = self.scopes[0].get(name.text)
                obj = g[1] if g and g[0] == "var" else self.declare_global(name, ty, "extern")
                self.declare(name.text, ("var", obj))
            elif storage == "static":
                obj = Obj(name.text, ty)
                obj.static = True
                obj.label = self.newlabel("s_%s_" % name.text)
                obj.tentative = False
                self.globals.append(obj)
                self.declare(name.text, ("var", obj))
                if self.consume("="):
                    self.global_init(obj, name)
                else:
                    obj.is_def = True
            else:
                var = Obj(name.text, ty, True)
                var.addr_taken = this_vol    # (volatile: kept in memory, so longjmp cannot undo it)
                self.fn.locals.append(var)
                self.declare(name.text, ("var", var))
                if self.consume("="):
                    out.extend(self.local_init(var, name))
                if var.ty.kind != "array" and var.ty.size == 0 or (var.ty.kind == "array" and var.ty.length is None):
                    err(name, "%s has an incomplete type" % name.text)
            if self.consume(";"):
                return out
            self.skip(",")

    def stmt(self):
        t = self.tok
        if t.kind == "id" and self.peek().text == ":" and t.text not in ("default",) and not self.is_typename():
            self.i += 2
            if t.text in self.fn.labels:
                err(t, "label %s defined twice" % t.text)
            self.fn.labels[t.text] = True
            return Node("label", t, label=t.text, body=self.stmt())
        if self.consume("{"):
            self.i -= 1
            return self.compound()
        if self.consume(";"):
            return Node("block", t, body=[])
        if t.kind == "id":
            k = t.text
            if k == "if":
                self.i += 1
                self.skip("(")
                c = self.cond_expr()
                self.skip(")")
                then = self.stmt()
                els = self.stmt() if self.consume("else") else None
                return Node("if", t, cond=c, then=then, els=els)
            if k == "while":
                self.i += 1
                self.skip("(")
                self.hdr += 1
                c = self.cond_expr()
                self.hdr -= 1
                self.skip(")")
                self.brk.append(1); self.cont.append(1)
                body = self.stmt()
                self.brk.pop(); self.cont.pop()
                return Node("for", t, init=None, cond=c, inc=None, body=body)
            if k == "do":
                self.i += 1
                self.brk.append(1); self.cont.append(1)
                body = self.stmt()
                self.brk.pop(); self.cont.pop()
                if not self.consume("while"):
                    err(self.tok, "expected while")
                self.skip("(")
                self.hdr += 1
                c = self.cond_expr()
                self.hdr -= 1
                self.skip(")")
                self.skip(";")
                return Node("do", t, cond=c, body=body)
            if k == "for":
                self.i += 1
                self.skip("(")
                self.push()
                init = None
                if self.is_typename():
                    init = Node("block", t, body=self.local_decl())
                elif not self.consume(";"):
                    init = Node("exprstmt", t, expr=self.expr())
                    self.skip(";")
                self.hdr += 1
                c = None if self.at(";") else self.cond_expr()
                self.skip(";")
                inc = None if self.at(")") else self.expr()
                self.hdr -= 1
                self.skip(")")
                self.brk.append(1); self.cont.append(1)
                body = self.stmt()
                self.brk.pop(); self.cont.pop()
                self.pop()
                return Node("for", t, init=init, cond=c, inc=inc, body=body)
            if k == "switch":
                self.i += 1
                self.skip("(")
                e = self.rvalue(self.expr())
                if not is_integer(e.ty) or e.ty.kind == "llong":
                    err(t, "switch needs an integer (not a long long)")
                self.skip(")")
                sw = Node("switch", t, expr=e, cases=[], default=None)
                self.switches.append(sw)
                self.brk.append(1)
                sw.body = self.stmt()
                self.brk.pop()
                self.switches.pop()
                return sw
            if k == "case":
                self.i += 1
                if not self.switches:
                    err(t, "case outside switch")
                v = self.const_expr()
                self.skip(":")
                sw = self.switches[-1]
                v = wrap(v, promote(sw.expr.ty))
                if any(c[0] == v for c in sw.cases):
                    err(t, "duplicate case %d" % v)
                lab = self.newlabel("case")
                sw.cases.append((v, lab))
                return Node("caselabel", t, label=lab, body=self.stmt())
            if k == "default":
                self.i += 1
                self.skip(":")
                if not self.switches:
                    err(t, "default outside switch")
                sw = self.switches[-1]
                if sw.default:
                    err(t, "two defaults")
                sw.default = self.newlabel("dflt")
                return Node("caselabel", t, label=sw.default, body=self.stmt())
            if k == "break":
                self.i += 1
                self.skip(";")
                if not self.brk:
                    err(t, "break outside a loop or switch")
                return Node("break", t)
            if k == "continue":
                self.i += 1
                self.skip(";")
                if not self.cont:
                    err(t, "continue outside a loop")
                return Node("continue", t)
            if k == "return":
                self.i += 1
                rt = self.fn.ty.base
                if self.consume(";"):
                    return Node("return", t, expr=None)
                e = self.expr()
                self.skip(";")
                if rt.kind == "void":
                    err(t, "return with a value in a void function")
                if is_memval(rt):                            # *ret = e, and ret in r1
                    rp = self.fn.retptr
                    a = self.new_assign(Node("deref", t, rt, expr=Node("var", t, rp.ty, obj=rp)), e, t)
                    return Node("return", t, expr=Node("comma", t, rp.ty, lhs=a, rhs=Node("var", t, rp.ty, obj=rp)))
                return Node("return", t, expr=self.convert(e, rt, t))
            if k == "goto":
                self.i += 1
                name = self.ident()
                self.skip(";")
                g = Node("goto", name, label=name.text)
                self.fn.gotos.append(g)
                return g
        e = self.expr()
        self.skip(";")
        return Node("exprstmt", t, expr=e)

    def cond_expr(self):
        e = self.rvalue(self.expr())
        if not is_scalar(e.ty):
            err(e.tok, "a condition must be a number or a pointer")
        return self.truth(e)

    # ================================================================ expressions
    def num(self, v, ty, tok):
        return Node("num", tok, ty, val=wrap(v, ty))

    def expr(self):
        e = self.assign()
        while self.at(","):
            t = self.tok
            self.i += 1
            r = self.assign()
            e = Node("comma", t, self.rvalue(r).ty, lhs=e, rhs=self.rvalue(r))
        return e

    ASSIGN_OPS = {"=": None, "+=": "+", "-=": "-", "*=": "*", "/=": "/", "%=": "%", "&=": "&",
                  "|=": "|", "^=": "^", "<<=": "<<", ">>=": ">>"}

    def assign(self):
        e = self.conditional()
        t = self.tok
        if t.kind == "punct" and t.text in self.ASSIGN_OPS:
            self.i += 1
            r = self.assign()
            op = self.ASSIGN_OPS[t.text]
            if op is None:
                return self.new_assign(e, r, t)
            return self.new_opassign(op, e, r, t)
        return e

    def conditional(self):
        c = self.binary(0)
        if not self.at("?"):
            return c
        t = self.tok
        self.i += 1
        c = self.truth(self.rvalue(c))
        a = self.rvalue(self.expr())
        self.skip(":")
        b = self.rvalue(self.conditional())
        c = self.rvalue(c)
        if is_arith(a.ty) and is_arith(b.ty):
            ty = usual(a.ty, b.ty)
        elif a.ty.kind == "ptr" and b.ty.kind == "ptr":
            ty = a.ty if a.ty.base.kind != "void" else b.ty
        elif a.ty.kind == "ptr" and self.is_null(b):
            ty = a.ty
        elif b.ty.kind == "ptr" and self.is_null(a):
            ty = b.ty
        elif a.ty.kind == "void" or b.ty.kind == "void":
            ty = VOID
        elif a.ty.kind in ("struct", "union") and a.ty is b.ty:
            ty = a.ty                                       # (by address)
        else:
            err(t, "?: operands of different types")
        if c.kind == "num":
            return self.convert(a if c.val else b, ty, t)
        return Node("cond", t, ty, cond=c, a=self.convert(a, ty, t), b=self.convert(b, ty, t))

    BIN = [("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!="), ("<", ">", "<=", ">="),
           ("<<", ">>"), ("+", "-"), ("*", "/", "%")]

    def binary(self, lvl):
        if lvl == len(self.BIN):
            return self.cast()
        e = self.binary(lvl + 1)
        while self.tok.kind == "punct" and self.tok.text in self.BIN[lvl]:
            t = self.tok
            self.i += 1
            r = self.binary(lvl + 1)
            e = self.new_binary(t.text, e, r, t)
        return e

    def cast(self):
        if self.at("(") and self.is_typename(self.peek()):
            t = self.tok
            self.i += 1
            ty = self.typename()
            self.skip(")")
            if self.at("{"):
                return self.postfix(self.compound_literal(ty, t))
            e = self.rvalue(self.cast())
            if ty.kind != "void" and not is_scalar(ty):
                err(t, "cast to a non-scalar type")
            if ty.kind != "void" and not is_scalar(e.ty):
                err(t, "cast of a non-scalar value")
            return self.new_cast(e, ty, t)
        return self.unary()

    def unary(self):
        t = self.tok
        if t.kind == "punct":
            s = t.text
            if s in ("++", "--"):
                self.i += 1
                e = self.unary()
                return self.new_incdec("pre" + ("inc" if s == "++" else "dec"), e, t)
            if s == "&":
                self.i += 1
                e = self.cast()
                if e.kind == "var" and e.obj.ty.kind == "func":
                    return Node("addr", t, ptr_to(e.ty), expr=e)
                self.need_lvalue(e, t, "&")
                if getattr(e, "bf", None):
                    err(t, "& of a bit-field")
                base = e
                while base.kind == "member":
                    base = base.expr
                if base.kind == "var":
                    base.obj.addr_taken = True
                return Node("addr", t, ptr_to(e.ty), expr=e)
            if s == "*":
                self.i += 1
                e = self.rvalue(self.cast())
                if e.ty.kind != "ptr":
                    err(t, "* needs a pointer")
                if e.ty.base.kind == "func":
                    return e                                     # *f == f
                if e.ty.base.kind == "void":
                    err(t, "dereferencing a void pointer")
                return Node("deref", t, e.ty.base, expr=e)
            if s in ("+", "-", "~", "!"):
                self.i += 1
                e = self.rvalue(self.cast())
                if s == "!":
                    if not is_scalar(e.ty):
                        err(t, "! needs a scalar")
                    e = self.truth(e)
                    if e.kind == "num":
                        return self.num(int(e.val == 0), INT, t)
                    return Node("not", t, INT, expr=e)
                if e.ty.kind == "double" and s in ("+", "-"):
                    return e if s == "+" else self.dop("neg", [e], t)
                if not is_integer(e.ty):
                    err(t, "%s needs an integer" % s)
                ty = promote(e.ty)
                if ty.kind == "llong":
                    if s == "+":
                        return e
                    return self.llop("neg" if s == "-" else "not", [e], ty, t)
                if e.kind == "num":
                    v = {"+": e.val, "-": -e.val, "~": ~e.val}[s]
                    return self.num(v, ty, t)
                if s == "+":
                    return self.convert(e, ty, t)
                return Node("neg" if s == "-" else "bitnot", t, ty, expr=self.convert(e, ty, t))
        if t.kind == "id" and t.text == "sizeof":
            self.i += 1
            if self.at("(") and self.is_typename(self.peek()):
                self.i += 1
                ty = self.typename()
                self.skip(")")
            else:
                e = self.unary()
                if getattr(e, "bf", None):
                    err(t, "sizeof a bit-field")
                ty = e.ty
            if ty.kind == "func" or (ty.kind in ("struct", "union") and ty.members is None) or ty.kind == "void":
                err(t, "sizeof an incomplete type")
            return self.num(ty.size, UINT, t)
        return self.postfix()

    def compound_literal(self, ty, t):
        """(type){ ... }: an object without a name. Outside functions a static one;
        inside, a local one whose initialisation runs where the literal is
        evaluated: *(stmtexpr: the initialising statements, then &object)."""
        if self.fn is None:
            obj = Obj("", ty)
            obj.static = True
            obj.label = self.newlabel("cl")
            obj.tentative = False
            self.globals.append(obj)
            self.global_init(obj, t)
            return Node("var", t, obj.ty, obj=obj)
        var = Obj("", ty, True)
        var.addr_taken = True
        self.fn.locals.append(var)
        stmts = self.local_init(var, t)
        a = Node("addr", t, ptr_to(var.ty), expr=Node("var", t, var.ty, obj=var))
        return Node("deref", t, var.ty, expr=Node("stmtexpr", t, a.ty, body=stmts, expr=a))

    def postfix(self, e=None):
        if e is None:
            e = self.primary()
        while True:
            t = self.tok
            if self.consume("["):
                idx = self.expr()
                self.skip("]")
                s = self.new_binary("+", e, idx, t)
                if s.ty.kind != "ptr":
                    err(t, "subscript of a non-pointer")
                e = Node("deref", t, s.ty.base, expr=s)
            elif self.consume("("):
                e = self.call(e, t)
            elif self.consume("."):
                name = self.ident()
                if e.ty.kind not in ("struct", "union"):
                    err(t, ". needs a struct")
                e = self.member(e, name)
            elif self.consume("->"):
                name = self.ident()
                p = self.rvalue(e)
                if p.ty.kind != "ptr" or p.ty.base.kind not in ("struct", "union"):
                    err(t, "-> needs a struct pointer")
                e = self.member(Node("deref", t, p.ty.base, expr=p), name)
            elif self.at("++") or self.at("--"):
                self.i += 1
                e = self.new_incdec("post" + ("inc" if t.text == "++" else "dec"), e, t)
            else:
                return e

    def member(self, e, name):
        if e.ty.members is None:
            err(name, "use of an incomplete %s" % e.ty.kind)
        m = find_member(e.ty, name.text)
        if m is None:
            err(name, "no member %s in %s" % (name.text, cty.type_str(e.ty)))
        return Node("member", name, m.ty, expr=e, mem=m, bf=(m.bit, m.width) if m.width else None)

    def call(self, fn, t):
        args = []
        if not self.consume(")"):
            while True:
                args.append(self.rvalue(self.assign()))
                if self.consume(")"):
                    break
                self.skip(",")
        return self.build_call(fn, args, t)

    def build_call(self, fn, args, t):
        f = self.rvalue(fn)
        if f.ty.kind != "ptr" or f.ty.base.kind != "func":
            err(t, "call of a non-function")
        ft = f.ty.base
        conv = []
        if ft.params is not None:
            if len(args) < len(ft.params) or (len(args) > len(ft.params) and not ft.variadic):
                err(t, "%d argument(s) to a function taking %d" % (len(args), len(ft.params)))
        for k, a in enumerate(args):
            if is_memval(a.ty) and ft.params is None:
                err(a.tok or t, "a %s passed to a function without a prototype" % cty.type_str(a.ty))
            if ft.params is not None and k < len(ft.params):
                conv.append(self.convert(a, ft.params[k], a.tok or t))
            else:
                conv.append(self.convert(a, promote(a.ty) if is_integer(a.ty) else a.ty, a.tok or t))
        direct = fn.obj if fn.kind == "var" and fn.obj.ty.kind == "func" else None
        if is_memval(ft.base):                              # the result into a temporary: *f(&tmp, ...)
            tmp = Obj("", ft.base, True)
            tmp.addr_taken = True
            self.fn.locals.append(tmp)
            p = ptr_to(ft.base)
            conv.insert(0, Node("addr", t, p, expr=Node("var", t, ft.base, obj=tmp)))
            return Node("deref", t, ft.base, expr=Node("call", t, p, fn=f, args=conv, ftype=ft, direct=direct))
        return Node("call", t, ft.base, fn=f, args=conv, ftype=ft, direct=direct)

    def primary(self):
        t = self.tok
        if self.consume("("):
            e = self.expr()
            self.skip(")")
            return e
        if t.kind == "num":
            self.i += 1
            m = FLOAT_RE.match(t.text)
            if m:                                       # a floating constant (f, l: the same double)
                return self.dconst(float(m.group(1)), t)
            m = HEXFLOAT_RE.match(t.text)
            if m:
                return self.dconst(float.fromhex(m.group(1)), t)
            v, uns, lng, nondec = parse_int(t)
            if v >= 1 << 64:
                err(t, "constant too big for 64 bits")
            if lng == 2 or v >= 1 << 32:               # (a 32-bit one keeps its C89 type)
                return self.llconst(v, ULLONG if uns or v >= 1 << 63 else LLONG, t)
            if uns:
                ty = ULONG if lng else UINT
            elif v >= 1 << 31:
                ty = UINT if (nondec or lng) else UINT
            else:
                ty = LONG if lng else INT
            return self.num(v, ty, t)
        if t.kind == "char":
            self.i += 1
            return self.num(t.val, INT, t)
        if t.kind == "str":
            data = b""
            while self.tok.kind == "str":
                data += self.tok.val
                self.i += 1
            data += b"\0"
            label = self.newlabel("str")
            self.strings.append((label, data))
            return Node("str", t, array_of(CHAR, len(data)), label=label, data=data)
        if t.kind == "id":
            self.i += 1
            e = self.lookup(t.text)
            if e is None and t.text in ("__builtin_nan", "__builtin_inf") and self.at("("):
                # math.h's NAN and INFINITY as constants (the NaN is 0x7FF8000000000000);
                # the argument (nan's tag) is ignored
                self.i += 1
                depth = 1
                while depth:
                    if self.tok.kind == "eof":
                        err(t, "unbalanced parentheses")
                    depth += self.at("(") - self.at(")")
                    self.i += 1
                return self.dconst(float("nan") if t.text == "__builtin_nan" else float("inf"), t)
            if e is None and t.text == "__func__" and self.fn is not None:
                data = self.fn.name.encode() + b"\0"           # (C99: the function's name)
                label = self.newlabel("str")
                self.strings.append((label, data))
                return Node("str", t, array_of(CHAR, len(data)), label=label, data=data)
            if e is None:
                if self.at("("):
                    if t.text in VARIADIC_LIB:
                        # (a variadic function takes every argument on the stack: called
                        # undeclared, it would read the wrong ones and crash)
                        err(t, "%s() takes a variable number of arguments: #include <%s>" % (t.text, VARIADIC_LIB[t.text]))
                    self.warn("%s:%d: warning: implicit declaration of %s()" % (t.file, t.line, t.text))
                    obj = self.declare_global(t, func_type(INT, None), "extern")
                    return Node("var", t, obj.ty, obj=obj)
                err(t, "undeclared %s" % t.text)
            if e[0] == "enum":
                return self.num(e[1], INT, t)
            if e[0] == "typedef":
                err(t, "%s is a type, not a value" % t.text)
            obj = e[1]
            obj.used = True
            obj.uses += 1 + 7 * (len(self.brk) + self.hdr)  # (a use inside a loop or its header counts more)
            return Node("var", t, obj.ty, obj=obj)
        err(t, "expected an expression, got '%s'" % (t.text or "end of file"))

    # ---------------------------------------------------------------- typing helpers
    def rvalue(self, e):
        """An expression used for its value: arrays and functions decay."""
        if e.ty.kind in ("array", "func"):
            return Node("addr", e.tok, decay(e.ty), expr=e)
        return e

    def is_null(self, e):
        return e.kind == "num" and e.val == 0 and is_integer(e.ty) or \
            (e.kind == "cast" and e.ty.kind == "ptr" and e.ty.base.kind == "void" and self.is_null(e.expr))

    def need_lvalue(self, e, t, what):
        if e.kind not in ("var", "deref", "member", "varoff") or (e.kind == "var" and e.obj.ty.kind == "func"):
            err(t, "%s needs an lvalue" % what)
        if e.kind == "member" and not self.lvalue_ok(e.expr):
            err(t, "%s needs an lvalue" % what)

    def lvalue_ok(self, e):
        return e.kind in ("var", "deref", "varoff") or (e.kind == "member" and self.lvalue_ok(e.expr))

    # ---------------------------------------------------------------- long long (D30)
    # A long long lives in memory (by its address, as a struct): constants are
    # objects in the data, operations calls of the library's helpers (libc's
    # llong.c) taking and returning long longs by value. Constants fold here.
    M64 = (1 << 64) - 1

    def llconst(self, v, ty, t):
        """A long long constant: an object in the data (one per value and type)."""
        v = wrap(v, ty)
        key = (v & self.M64, ty.unsigned)
        obj = self.llconsts.get(key)
        if obj is None:
            obj = Obj("", ty)
            obj.static = True
            obj.label = self.newlabel("ll")
            obj.tentative = False
            obj.is_def = True
            obj.init = ((v & self.M64).to_bytes(8, "little"), [])
            self.globals.append(obj)
            self.llconsts[key] = obj
        return Node("var", t, ty, obj=obj, llval=v)

    def llv(self, e):
        """A constant's value (a long long or an int constant), or None."""
        if getattr(e, "llval", None) is not None:
            return e.llval
        return e.val if e.kind == "num" else None

    def helper(self, name, ret, params, t, args):
        """A call of a libc helper: name(args) returning ret."""
        key = (name,)
        obj = self.helpers.get(key)
        if obj is None:
            obj = Obj(name, func_type(ret, params))
            obj.extern = True
            self.helpers[key] = obj
        return self.build_call(Node("var", t, obj.ty, obj=obj), args, t)

    def retype(self, e, ty, t):
        """The same long long bits seen as ty (signed / unsigned)."""
        if e.ty is ty:
            return e
        if self.llv(e) is not None:
            return self.llconst(self.llv(e), ty, t)
        return Node("deref", t, ty, expr=Node("memaddr", t, ptr_to(ty), expr=e))

    def llop(self, op, args, ty, t):
        """A long long operation of type ty (folded when the operands are constants)."""
        vals = [self.llv(a) for a in args]
        if all(v is not None for v in vals):
            M = self.M64
            a = vals[0] & M
            b = vals[1] & M if len(vals) > 1 else 0
            sa, sb = wrap(a, LLONG), wrap(b, LLONG)
            r = None
            if op == "add": r = a + b
            elif op == "sub": r = a - b
            elif op == "mul": r = a * b
            elif op in ("div", "mod") and b:
                if ty.unsigned:
                    r = a // b if op == "div" else a % b
                else:
                    q = abs(sa) // abs(sb) * (1 if (sa < 0) == (sb < 0) else -1)
                    r = q if op == "div" else sa - q * sb
            elif op == "and": r = a & b
            elif op == "or": r = a | b
            elif op == "xor": r = a ^ b
            elif op == "neg": r = -a
            elif op == "not": r = ~a
            elif op == "shl": r = a << (b & 63)
            elif op == "shr": r = (a >> (b & 63)) if ty.unsigned else (sa >> (b & 63))
            if r is not None:
                return self.llconst(r, ty, t)
        name = "__ll" + op
        if ty.unsigned and op in ("div", "mod", "shr"):
            name = "__ull" + op
        params = [LLONG] * len(args)
        if op == "shl" or op == "shr":
            params = [LLONG, INT]
        conv = [self.retype(self.convert(a, ty, t), LLONG, t) if p is LLONG else self.convert(a, INT, t)
                for a, p in zip(args, params)]
        return self.retype(self.helper(name, LLONG, params, t, conv), ty, t)

    def llbinary(self, op, l, r, t):
        """l op r where an operand is a long long."""
        if op in ("<<", ">>"):
            lt = promote(l.ty)
            if lt.kind != "llong":                      # (int << long long: the count's low word)
                return self.new_binary(op, l, self.llcast(r, INT, t), t)
            return self.llop("shl" if op == "<<" else "shr", [l, r], lt, t)
        ty = usual(l.ty, r.ty)
        if op in ("==", "!=", "<", ">", "<=", ">="):
            a, b = self.llv(l), self.llv(r)
            if a is not None and b is not None:
                a, b = wrap(a, ty), wrap(b, ty)
                return self.num(int({"==": a == b, "!=": a != b, "<": a < b, ">": a > b,
                                     "<=": a <= b, ">=": a >= b}[op]), INT, t)
            c = self.helper("__ullcmp" if ty.unsigned else "__llcmp", INT, [LLONG, LLONG], t,
                            [self.retype(self.convert(l, ty, t), LLONG, t), self.retype(self.convert(r, ty, t), LLONG, t)])
            return Node("cmp", t, INT, op=op, lhs=c, rhs=self.num(0, INT, t), unsigned=False)
        names = {"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "mod", "&": "and", "|": "or", "^": "xor"}
        if op not in names:
            err(t, "%s on a long long" % op)
        return self.llop(names[op], [l, r], ty, t)

    def llcast(self, e, ty, t):
        """Conversions to and from long long."""
        e = self.rvalue(e)
        v = self.llv(e)
        if ty.kind == "void":
            return Node("cast", t, VOID, expr=e)
        if ty.kind == "llong":
            if e.ty.kind == "llong":
                return self.retype(e, ty, t)
            if not is_scalar(e.ty):
                err(t, "cast of a non-scalar value")
            if v is not None:
                return self.llconst(v, ty, t)
            src = UINT if e.ty.unsigned or e.ty.kind == "ptr" else INT
            f = self.helper("__llfromuint" if src.unsigned else "__llfromint", LLONG, [src], t, [self.convert(e, src, t)])
            return self.retype(f, ty, t)
        if not is_scalar(ty):
            err(t, "cast to a non-scalar type")
        if v is not None and is_integer(ty):
            return self.num(v, ty, t)
        low = Node("deref", t, UINT, expr=Node("memaddr", t, ptr_to(UINT), expr=e))     # (the low word)
        return self.new_cast(low, ty, t)

    def truth(self, e):
        """e used as a condition: a long long through __lltest (1 if not 0), a
        double through __dtest (1 if not 0: a NaN too)."""
        if e.ty.kind == "double":
            v = self.dv(e)
            if v is not None:
                return self.num(int(v != 0), INT, e.tok)
            return self.helper("__dtest", INT, [DOUBLE], e.tok, [e])
        if e.ty.kind != "llong":
            return e
        v = self.llv(e)
        if v is not None:
            return self.num(int(v != 0), INT, e.tok)
        return self.helper("__lltest", INT, [LLONG], e.tok, [self.retype(e, LLONG, e.tok)])

    # ---------------------------------------------------------------- double (D30)
    # A double (float is the same type) lives in memory, as a long long: its
    # constants are objects in the data, its operations calls of libc's
    # helpers (dbl.c, on softfp). Constants fold, except a division by zero
    # or a NaN result (NaNs' bits differ between machines).
    def dconst(self, v, t):
        bits = struct.unpack("<Q", struct.pack("<d", v))[0]
        obj = self.dconsts.get(bits)
        if obj is None:
            obj = Obj("", DOUBLE)
            obj.static = True
            obj.label = self.newlabel("fl")
            obj.tentative = False
            obj.is_def = True
            obj.init = (bits.to_bytes(8, "little"), [])
            self.globals.append(obj)
            self.dconsts[bits] = obj
        return Node("var", t, DOUBLE, obj=obj, dval=v)

    def dv(self, e):
        """A constant's value as a double, or None."""
        if getattr(e, "dval", None) is not None:
            return e.dval
        v = self.llv(e)
        return None if v is None else float(v)

    def dop(self, op, args, t):
        vals = [self.dv(a) for a in args]
        if all(v is not None for v in vals):
            a = vals[0]
            b = vals[1] if len(vals) > 1 else 0.0
            r = None
            if op == "add": r = a + b
            elif op == "sub": r = a - b
            elif op == "mul": r = a * b
            elif op == "div" and b != 0: r = a / b
            elif op == "neg": r = -a
            if r is not None and r == r:
                return self.dconst(r, t)
        return self.helper("__d" + op, DOUBLE, [DOUBLE] * len(args), t, [self.convert(a, DOUBLE, t) for a in args])

    def dbinary(self, op, l, r, t):
        """l op r where an operand is a double."""
        if op in ("==", "!=", "<", ">", "<=", ">="):
            a, b = self.dv(l), self.dv(r)
            if a is not None and b is not None:
                return self.num(int({"==": a == b, "!=": a != b, "<": a < b, ">": a > b,
                                     "<=": a <= b, ">=": a >= b}[op]), INT, t)
            c = self.helper("__dcmp", INT, [DOUBLE, DOUBLE], t, [self.convert(l, DOUBLE, t), self.convert(r, DOUBLE, t)])
            # (__dcmp: -1, 0, 1, or 2 unordered: every comparison with a NaN is false but !=)
            if op in ("==", "!="):
                return Node("cmp", t, INT, op=op, lhs=c, rhs=self.num(0, INT, t), unsigned=False)
            if op in ("<", ">"):
                return Node("cmp", t, INT, op="==", lhs=c, rhs=self.num(-1 if op == "<" else 1, INT, t), unsigned=False)
            if op == "<=":                              # c is -1 or 0: (unsigned)(c + 1) <= 1
                c = Node("binop", t, INT, op="+", lhs=c, rhs=self.num(1, INT, t))
            return Node("cmp", t, INT, op="<=", lhs=c, rhs=self.num(1, INT, t), unsigned=True)
        names = {"+": "add", "-": "sub", "*": "mul", "/": "div"}
        if op not in names:
            err(t, "%s on a double" % op)
        return self.dop(names[op], [l, r], t)

    def dcast(self, e, ty, t):
        """Conversions to and from double."""
        e = self.rvalue(e)
        if ty.kind == "void":
            return Node("cast", t, VOID, expr=e)
        if ty.kind == "double":
            if e.ty.kind == "double":
                return e
            if not is_integer(e.ty):
                err(t, "%s to double" % cty.type_str(e.ty))
            v = self.dv(e)
            if v is not None:
                return self.dconst(v, t)
            if e.ty.kind == "llong":
                return self.helper("__dfromull" if e.ty.unsigned else "__dfromll", DOUBLE, [e.ty], t, [e])
            src = UINT if e.ty.unsigned else INT
            return self.helper("__dfromuint" if src.unsigned else "__dfromint", DOUBLE, [src], t, [self.convert(e, src, t)])
        if not is_integer(ty):
            err(t, "double to %s" % cty.type_str(ty))
        v = self.dv(e)
        if v is not None and v == v and v not in (float("inf"), float("-inf")):
            w = int(v)                                  # (toward zero; folded only if it fits)
            if ty.kind == "llong":
                if (0 <= w < 1 << 64) if ty.unsigned else (-(1 << 63) <= w < 1 << 63):
                    return self.llconst(w, ty, t)
            elif (0 <= w < 1 << (8 * ty.size)) if ty.unsigned else (-(1 << (8 * ty.size - 1)) <= w < 1 << (8 * ty.size - 1)):
                return self.num(w, ty, t)
        if ty.kind == "llong":
            return self.helper("__dtoull" if ty.unsigned else "__dtoll", ty, [DOUBLE], t, [e])
        f = self.helper("__dtouint" if ty.unsigned else "__dtoint", UINT if ty.unsigned else INT, [DOUBLE], t, [e])
        return self.new_cast(f, ty, t)

    def to_bool(self, e, t):
        """e converted to _Bool: 1 if it is not 0 (or not a null pointer),
        else 0 -- not its low byte (D35)."""
        if e.ty.is_bool:
            return e
        if e.ty.kind in ("double", "llong"):
            return Node("cast", t, BOOL, expr=self.truth(e))
        if e.kind == "num":
            return self.num(e.val, BOOL, t)
        return Node("cast", t, BOOL, expr=self.new_binary("!=", e, self.num(0, INT, t), t))

    def new_cast(self, e, ty, t):
        if ty.is_bool and is_scalar(e.ty):
            return self.to_bool(e, t)
        if ty.kind == "double" or e.ty.kind == "double":
            return self.dcast(e, ty, t)
        if ty.kind == "llong" or e.ty.kind == "llong":
            return self.llcast(e, ty, t)
        if is_integer(ty) and e.ty.kind == "ptr":     # a constant address with no symbol (offsetof's
            try:                                      # &((T *)0)->m): a number, so it folds further
                lab, v = self.eval_addr(e)
                if lab is None:
                    return self.num(v, ty, t)
            except NotConst:
                pass
        if e.kind == "num" and is_scalar(ty):
            return self.num(e.val, ty, t)
        if ty.kind == "void":
            return Node("cast", t, VOID, expr=e)
        return Node("cast", t, ty, expr=e)

    def convert(self, e, ty, t):
        """Implicit conversion (assignment, argument, return)."""
        e = self.rvalue(e)
        if ty.kind in ("struct", "union"):
            if e.ty is not ty:
                err(t, "incompatible struct")
            return e
        if ty.kind == "void":
            return e
        if ty.is_bool and is_scalar(e.ty):
            return self.to_bool(e, t)
        if (ty.kind == "double" or e.ty.kind == "double") and is_arith(ty) and is_arith(e.ty):
            return self.dcast(e, ty, t)
        if (ty.kind == "llong" or e.ty.kind == "llong") and is_arith(ty) and is_arith(e.ty):
            return self.llcast(e, ty, t)
        if is_arith(ty) and is_arith(e.ty):
            if e.ty.size == ty.size and e.ty.unsigned == ty.unsigned:
                return e if e.ty.kind == ty.kind else Node("cast", t, ty, expr=e) if e.kind != "num" else self.num(e.val, ty, t)
            return self.new_cast(e, ty, t)
        if ty.kind == "ptr":
            if e.ty.kind == "ptr":
                if not (ty.base.kind == "void" or e.ty.base.kind == "void" or cty.compatible(ty.base, e.ty.base)):
                    self.warn("%s:%d: warning: incompatible pointer types (%s, %s)" %
                              (t.file, t.line, cty.type_str(e.ty), cty.type_str(ty)))
                return e if e.ty is ty else Node("cast", t, ty, expr=e)
            if is_integer(e.ty):
                if not self.is_null(e):
                    self.warn("%s:%d: warning: integer converted to pointer" % (t.file, t.line))
                return self.new_cast(e, ty, t)
        if is_integer(ty) and e.ty.kind == "ptr":
            self.warn("%s:%d: warning: pointer converted to integer" % (t.file, t.line))
            return Node("cast", t, ty, expr=e)
        err(t, "cannot convert %s to %s" % (cty.type_str(e.ty), cty.type_str(ty)))

    def new_assign(self, lhs, rhs, t):
        self.need_lvalue(lhs, t, "assignment")
        if lhs.ty.kind == "array":
            err(t, "assignment to an array")
        if lhs.ty.const:
            err(t, "assignment to a const")
        return Node("assign", t, lhs.ty, lhs=lhs, rhs=self.convert(rhs, lhs.ty, t))

    def simple(self, e):
        """Can e be evaluated twice (no side effects, cheap)?"""
        if e.kind in ("var", "varoff"):
            return True
        if e.kind == "member":
            return self.simple(e.expr)
        return e.kind == "deref" and e.expr.kind == "var"

    def bf_place(self, lhs, t):
        """An lvalue (a bit-field, or a long long) to use twice: (code to run first
        or None, the lvalue)."""
        if not getattr(lhs, "bf", None):
            if self.simple(lhs):
                return None, lhs
            p = Obj("", ptr_to(lhs.ty), True)            # through a pointer temporary
            self.fn.locals.append(p)
            pv = Node("var", t, p.ty, obj=p)
            pre = Node("assign", t, p.ty, lhs=pv, rhs=Node("addr", t, p.ty, expr=lhs))
            return pre, Node("deref", t, lhs.ty, expr=pv)
        if self.simple(lhs.expr):
            return None, lhs
        base = lhs.expr                                     # the struct, through a pointer temporary
        p = Obj("", ptr_to(base.ty), True)
        self.fn.locals.append(p)
        pv = Node("var", t, p.ty, obj=p)
        pre = Node("assign", t, p.ty, lhs=pv, rhs=Node("addr", t, p.ty, expr=base))
        return pre, Node("member", t, lhs.ty, expr=Node("deref", t, base.ty, expr=pv), mem=lhs.mem, bf=lhs.bf)

    def bf_update(self, op, lhs, rhs, t, post):
        """lhs op= rhs (post: the old value is the result, as x++) on a bit-field."""
        pre, place = self.bf_place(lhs, t)
        if post:
            old = Obj("", promote(lhs.ty), True)
            self.fn.locals.append(old)
            ov = Node("var", t, old.ty, obj=old)
            a1 = Node("assign", t, ov.ty, lhs=ov, rhs=self.convert(place, ov.ty, t))      # old = place,
            a2 = self.new_assign(place, self.new_binary(op, ov, rhs, t), t)                # place = old op rhs,
            res = self.convert(ov, lhs.ty, t)                                               # old
            e = Node("comma", t, res.ty, lhs=a1, rhs=Node("comma", t, res.ty, lhs=a2, rhs=res))
        else:
            e = self.new_assign(place, self.new_binary(op, place, rhs, t), t)
        return Node("comma", t, e.ty, lhs=pre, rhs=e) if pre else e

    def new_opassign(self, op, lhs, rhs, t):
        self.need_lvalue(lhs, t, "%s=" % op)
        if getattr(lhs, "bf", None) or lhs.ty.kind in ("llong", "double") or \
                (rhs.ty.kind in ("llong", "double") and is_arith(lhs.ty)):
            return self.bf_update(op, lhs, self.rvalue(rhs), t, False)
        rhs = self.rvalue(rhs)
        lt = lhs.ty
        if lt.kind == "ptr" and op in "+-":
            if not is_integer(rhs.ty):
                err(t, "pointer %s= needs an integer" % op)
            if lt.base.size == 0:
                err(t, "arithmetic on a pointer to an incomplete type")
            return Node("opassign", t, lt, op=op, lhs=lhs, rhs=self.scale(rhs, lt.base.size, t), opty=lt)
        if not (is_integer(lt) and is_integer(rhs.ty)):
            err(t, "%s= needs integers" % op)
        opty = usual(lt, rhs.ty) if op not in ("<<", ">>") else promote(lt)
        return Node("opassign", t, lt, op=op, lhs=lhs, rhs=self.convert(rhs, opty if op not in ("<<", ">>") else promote(rhs.ty), t), opty=opty)

    def new_incdec(self, kind, e, t):
        self.need_lvalue(e, t, kind[-3:] == "inc" and "++" or "--")
        if getattr(e, "bf", None) or e.ty.kind in ("llong", "double"):
            return self.bf_update("+" if kind.endswith("inc") else "-", e, self.num(1, INT, t), t, kind.startswith("post"))
        if e.ty.kind == "ptr":
            if e.ty.base.size == 0:
                err(t, "++/-- on a pointer to an incomplete type")
            step = e.ty.base.size
        elif is_integer(e.ty):
            step = 1
        else:
            err(t, "++/-- needs a number or a pointer")
        return Node(kind, t, e.ty, expr=e, step=step)

    def scale(self, e, size, t):
        e = self.convert(e, promote(e.ty), t)
        if size == 1:
            return e
        if e.kind == "num":
            return self.num(e.val * size, e.ty, t)
        return Node("binop", t, e.ty, op="*", lhs=e, rhs=self.num(size, INT, t))

    def new_binary(self, op, l, r, t):
        l, r = self.rvalue(l), self.rvalue(r)
        if op in ("&&", "||"):
            l, r = self.truth(l), self.truth(r)
        if op in ("+", "-", "[") and (l.ty.kind == "ptr") != (r.ty.kind == "ptr"):
            if l.ty.kind == "llong":                    # (a long long index: its low word)
                l = self.llcast(l, INT, t)
            if r.ty.kind == "llong":
                r = self.llcast(r, INT, t)
        lt, rt = l.ty, r.ty
        if is_arith(lt) and is_arith(rt) and (lt.kind == "double" or rt.kind == "double") and op not in ("&&", "||"):
            return self.dbinary(op, l, r, t)
        if is_integer(lt) and is_integer(rt) and (lt.kind == "llong" or rt.kind == "llong") and op not in ("&&", "||"):
            return self.llbinary(op, l, r, t)
        if op in ("&&", "||"):
            if not (is_scalar(lt) and is_scalar(rt)):
                err(t, "%s needs scalars" % op)
            if l.kind == "num" and r.kind == "num":
                v = (l.val != 0 and r.val != 0) if op == "&&" else (l.val != 0 or r.val != 0)
                return self.num(int(v), INT, t)
            return Node("logand" if op == "&&" else "logor", t, INT, lhs=l, rhs=r)
        if op == "+":
            if lt.kind == "ptr" and is_integer(rt):
                return self.ptr_add(l, r, t, 1)
            if rt.kind == "ptr" and is_integer(lt):
                return self.ptr_add(r, l, t, 1)
        if op == "-":
            if lt.kind == "ptr" and is_integer(rt):
                return self.ptr_add(l, r, t, -1)
            if lt.kind == "ptr" and rt.kind == "ptr":
                size = lt.base.size
                if size == 0:
                    err(t, "subtracting pointers to incomplete types")
                d = Node("binop", t, INT, op="-", lhs=Node("cast", t, INT, expr=l), rhs=Node("cast", t, INT, expr=r))
                if size == 1:
                    return d
                return Node("binop", t, INT, op="/", lhs=d, rhs=self.num(size, INT, t))
        if op in ("==", "!=", "<", ">", "<=", ">="):
            if is_arith(lt) and is_arith(rt):
                ct = usual(lt, rt)
            elif lt.kind == "ptr" and rt.kind == "ptr":
                ct = UINT
            elif lt.kind == "ptr" and is_integer(rt):
                if not self.is_null(r):
                    self.warn("%s:%d: warning: comparing a pointer with an integer" % (t.file, t.line))
                ct = UINT
            elif rt.kind == "ptr" and is_integer(lt):
                if not self.is_null(l):
                    self.warn("%s:%d: warning: comparing a pointer with an integer" % (t.file, t.line))
                ct = UINT
            else:
                err(t, "cannot compare %s with %s" % (cty.type_str(lt), cty.type_str(rt)))
            if l.kind == "num" and r.kind == "num":
                a, b = wrap(l.val, ct), wrap(r.val, ct)
                return self.num(int({"==": a == b, "!=": a != b, "<": a < b, ">": a > b,
                                     "<=": a <= b, ">=": a >= b}[op]), INT, t)
            return Node("cmp", t, INT, op=op, lhs=l, rhs=r, unsigned=ct.unsigned)
        if not (is_integer(lt) and is_integer(rt)):
            err(t, "%s needs integers (got %s, %s)" % (op, cty.type_str(lt), cty.type_str(rt)))
        if op in ("<<", ">>"):
            ty = promote(lt)
            r = self.convert(r, promote(rt), t)
        else:
            ty = usual(lt, rt)
            r = self.convert(r, ty, t)
        l = self.convert(l, ty, t)
        if l.kind == "num" and r.kind == "num":
            a, b = wrap(l.val, ty), wrap(r.val, promote(rt) if op in ("<<", ">>") else ty)
            if op in ("/", "%") and b == 0:
                err(t, "division by zero")
            v = {"+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b,
                 "/": lambda: abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1),
                 "%": lambda: a - b * (abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)),
                 "&": lambda: a & b, "|": lambda: a | b, "^": lambda: a ^ b,
                 "<<": lambda: a << (b & 31), ">>": lambda: a >> (b & 31)}[op]()
            return self.num(v, ty, t)
        return Node("binop", t, ty, op=op, lhs=l, rhs=r)

    def ptr_add(self, p, i, t, sign):
        if p.ty.base.size == 0:
            err(t, "arithmetic on a pointer to an incomplete type")
        off = self.scale(i, p.ty.base.size, t)
        if off.kind == "num" and off.val == 0:
            return p
        return Node("binop", t, p.ty, op="+" if sign > 0 else "-", lhs=p, rhs=off)

    # ---------------------------------------------------------------- constants
    def const_expr(self):
        t = self.tok
        e = self.conditional()
        try:
            label, v = self.eval_addr(e)
        except NotConst:
            err(t, "not a constant expression")
        if label is not None:
            err(t, "not an integer constant")
        return v

    def eval_addr(self, e):
        """A constant initialiser: (label or None, value)."""
        if getattr(e, "llval", None) is not None:
            return None, e.llval
        if getattr(e, "dval", None) is not None:   # (a double for an integer: truncated, if it fits 32 bits)
            v = e.dval
            if v == v and -2147483648.0 <= v < 4294967296.0:
                return None, int(v)
            raise NotConst()
        k = e.kind
        if k == "num":
            return None, e.val
        if k == "str":
            return e.label, 0
        if k == "addr":
            return self.eval_lvalue(e.expr)
        if k == "cast":
            lab, v = self.eval_addr(e.expr)
            return lab, (wrap(v, e.ty) if lab is None else v)
        if k == "binop" and e.op in "+-":
            l1, a = self.eval_addr(e.lhs)
            l2, b = self.eval_addr(e.rhs)
            if l1 is not None and l2 is not None:
                raise NotConst()
            if l2 is not None:
                if e.op == "-":
                    raise NotConst()
                return l2, a + b
            return l1, (a + b if e.op == "+" else a - b) if l1 is not None else wrap(a + b if e.op == "+" else a - b, e.ty)
        if k == "var" and e.ty.kind in ("array", "func"):
            return self.eval_lvalue(e)
        if k == "cond":
            lab, c = self.eval_addr(e.cond)
            if lab is not None:
                raise NotConst()
            return self.eval_addr(e.a if c else e.b)
        raise NotConst()

    def eval_lvalue(self, e):
        if e.kind == "var" and not e.obj.local:
            return e.obj.label, 0
        if e.kind == "str":
            return e.label, 0
        if e.kind == "deref":
            return self.eval_addr(e.expr)
        if e.kind == "member":
            lab, v = self.eval_lvalue(e.expr)
            return lab, v + e.mem.offset
        raise NotConst()

def abstract_params(p):
    """At '(' in a declarator: does it start a parameter list (not a nested declarator)?"""
    nxt = p.peek()
    return nxt.text == ")" or p.is_typename(nxt) or nxt.text == "..."
