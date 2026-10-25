"""cpp.py -- the C preprocessor of p32cc (M2, D22).

Works on tokens (clex.py). Macro expansion follows the classic hideset
algorithm (Prosser's; as in chibicc): every token carries the set of macro
names it came from, and a macro is never expanded inside its own
expansion.

Directives: #include "f" / <f> (-I paths, then the current file's
directory for "f"), #define (object- and function-like, # and ##), #undef,
#if / #ifdef / #ifndef / #elif / #else / #endif (defined X, constant
expressions), #error, #pragma and #line (ignored). Predefined: __P32__,
__STDC__, __FILE__, __LINE__.
"""
import os
from clex import Tok, tokenize, err, parse_int, CError

class Macro:
    def __init__(self, name, body, params=None, variadic=False):
        self.name, self.body, self.params, self.variadic = name, body, params, variadic

class Preprocessor:
    def __init__(self, include_dirs=(), defines=()):
        self.dirs = list(include_dirs)
        self.macros = {}
        self.once = set()
        for d in defines:
            name, eq, val = d.partition("=")
            self.define_text(name, val if eq else "1")     # (-DX=: X empty)
        self.define_text("__P32__", "1")
        self.define_text("__STDC__", "1")

    def define_text(self, name, text):
        body = [t for t in tokenize(text, "<command line>") if t.kind != "eof"]
        self.macros[name] = Macro(name, body)

    # ---------------------------------------------------------------- files
    def run(self, path):
        with open(path, encoding="latin-1") as fh:
            src = fh.read()
        out = self.process(tokenize(src, path))
        return out

    def find_include(self, name, cur, quoted, tok):
        cands = ([os.path.dirname(cur)] if quoted else []) + self.dirs
        for d in cands:
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return p
        err(tok, "cannot find include file %s" % name)

    # ---------------------------------------------------------------- the main loop
    def process(self, toks):
        """Directives and expansion over one file's tokens; returns tokens
        (without the final eof)."""
        out = []
        i = 0
        cond = []                  # stack of [taking now, taken before, saw else]
        def line_of(i):            # the tokens of the directive starting at i (after '#')
            j = i
            while not toks[j].bol or j == i:
                if toks[j].kind == "eof":
                    break
                j += 1
            return toks[i:j], j
        pending = []               # tokens awaiting macro expansion (a run of text lines)
        def flush():
            if pending:
                out.extend(self.expand_all(pending))
                del pending[:]
        while toks[i].kind != "eof":
            t = toks[i]
            if t.bol and t.text == "#" and t.kind == "punct":
                flush()
                line, nxt = line_of(i + 1)
                skipping = cond and not cond[-1][0]
                self.directive(line, t, cond, skipping, out)
                i = nxt
                continue
            if cond and not cond[-1][0]:
                i += 1
                continue
            pending.append(t)
            i += 1
        flush()
        if cond:
            err(toks[-1], "#if without #endif")
        return out

    def directive(self, line, hash_tok, cond, skipping, out):
        if not line:
            return                                          # the null directive
        d = line[0].text
        rest = line[1:]
        if d in ("if", "ifdef", "ifndef"):
            if skipping:
                cond.append([False, True, False])           # whole group skipped
                return
            if d == "if":
                v = self.eval_if(rest, line[0])
            else:
                if not rest or rest[0].kind != "id":
                    err(line[0], "#%s needs a name" % d)
                v = (rest[0].text in self.macros) == (d == "ifdef")
            cond.append([v, v, False])
            return
        if d in ("elif", "else", "endif"):
            if not cond:
                err(line[0], "#%s without #if" % d)
            c = cond[-1]
            outer_skip = len(cond) > 1 and not cond[-2][0]
            if d == "endif":
                cond.pop()
            elif d == "else":
                if c[2]:
                    err(line[0], "#else after #else")
                c[2] = True
                c[0] = not c[1] and not outer_skip
                c[1] = True
            else:
                if c[2]:
                    err(line[0], "#elif after #else")
                if c[1] or outer_skip:
                    c[0] = False
                else:
                    c[0] = self.eval_if(rest, line[0])
                    c[1] = c[0]
            return
        if skipping:
            return
        if d == "define":
            self.do_define(rest, line[0])
        elif d == "undef":
            if rest:
                self.macros.pop(rest[0].text, None)
        elif d == "include":
            self.do_include(rest, line[0], out)
        elif d == "error":
            err(line[0], "#error " + " ".join(t.text for t in rest))
        elif d in ("pragma", "line", "ident"):
            if d == "pragma" and rest and rest[0].text == "once":
                self.once.add(line[0].file)
        else:
            err(line[0], "unknown directive #%s" % d)

    def do_define(self, rest, tok):
        if not rest or rest[0].kind != "id":
            err(tok, "#define needs a name")
        name = rest[0].text
        if len(rest) > 1 and rest[1].text == "(" and not rest[1].space:      # function-like
            params, j, variadic = [], 2, False
            while True:
                if j >= len(rest):
                    err(tok, "bad macro parameters")
                t = rest[j]
                if t.text == ")":
                    j += 1
                    break
                if t.text == "...":
                    variadic = True
                    params.append("__VA_ARGS__")
                elif t.kind == "id":
                    params.append(t.text)
                else:
                    err(t, "bad macro parameter %s" % t.text)
                j += 1
                if rest[j].text == ",":
                    j += 1
            self.macros[name] = Macro(name, rest[j:], params, variadic)
        else:
            self.macros[name] = Macro(name, rest[1:])

    def do_include(self, rest, tok, out):
        if not rest:
            err(tok, "#include needs a file")
        if rest[0].kind == "str":
            name, quoted = rest[0].text[1:-1], True
        elif rest[0].text == "<":
            parts = []
            for t in rest[1:]:
                if t.text == ">":
                    break
                parts.append(t.text)
            name, quoted = "".join(parts), False
        else:
            exp = self.expand_all(rest)
            return self.do_include(exp, tok, out)
        path = self.find_include(name, tok.file, quoted, tok)
        if path in self.once:
            return
        with open(path, encoding="latin-1") as fh:
            src = fh.read()
        out.extend(self.process(tokenize(src, path)))

    # ---------------------------------------------------------------- #if
    def eval_if(self, toks, tok):
        # defined X / defined(X) first, then expand, then identifiers -> 0
        pre = []
        i = 0
        while i < len(toks):
            t = toks[i]
            if t.kind == "id" and t.text == "defined":
                if i + 1 < len(toks) and toks[i + 1].text == "(":
                    name = toks[i + 2].text
                    i += 4
                else:
                    name = toks[i + 1].text
                    i += 2
                n = Tok("num", "1" if name in self.macros else "0", t.file, t.line)
                pre.append(n)
                continue
            pre.append(t)
            i += 1
        exp = self.expand_all(pre)
        vals = []
        for t in exp:
            if t.kind == "id":
                vals.append(Tok("num", "0", t.file, t.line))
            else:
                vals.append(t)
        ev = IfEval(vals, tok)
        v = ev.value()
        if ev.i != len(vals):
            err(tok, "bad #if expression")
        return v != 0

    # ---------------------------------------------------------------- expansion
    def expand_all(self, toks):
        toks = list(toks)
        out = []
        i = 0
        while i < len(toks):
            t = toks[i]
            if t.kind != "id":
                out.append(t)
                i += 1
                continue
            if t.text == "__LINE__":
                n = Tok("num", str(t.line), t.file, t.line, t.bol, t.space)
                out.append(n); i += 1; continue
            if t.text == "__FILE__":
                n = Tok("str", '"%s"' % t.file, t.file, t.line, t.bol, t.space, t.file.encode())
                out.append(n); i += 1; continue
            m = self.macros.get(t.text)
            if not m or t.text in t.hide:
                out.append(t)
                i += 1
                continue
            if m.params is None:
                body = self.subst(m, [], t.hide | {m.name}, t)
                toks[i:i + 1] = body
                continue                                    # rescan
            # function-like: needs '(' next
            if i + 1 >= len(toks) or toks[i + 1].text != "(":
                out.append(t)
                i += 1
                continue
            args, j = self.collect_args(toks, i + 2, t)
            if m.params == [] and args == [[]]:
                args = []
            if m.variadic:
                fixed = len(m.params) - 1
                if len(args) < fixed:
                    err(t, "too few arguments to macro %s" % m.name)
                va = []
                for k, a in enumerate(args[fixed:]):
                    if k:
                        va.append(Tok("punct", ",", t.file, t.line))
                    va.extend(a)
                args = args[:fixed] + [va]
            if len(args) != len(m.params):
                err(t, "macro %s takes %d argument(s), %d given" % (m.name, len(m.params), len(args)))
            rparen = toks[j - 1]
            hide = (t.hide & rparen.hide) | {m.name}
            body = self.subst(m, args, hide, t)
            toks[i:j] = body
        return out

    def collect_args(self, toks, i, mtok):
        args, cur, depth = [], [], 0
        while True:
            if i >= len(toks):
                err(mtok, "unterminated macro call %s" % mtok.text)
            t = toks[i]
            if t.text == "(":
                depth += 1
            elif t.text == ")":
                if depth == 0:
                    args.append(cur)
                    return args, i + 1
                depth -= 1
            elif t.text == "," and depth == 0:
                args.append(cur)
                cur = []
                i += 1
                continue
            cur.append(t)
            i += 1

    def subst(self, m, args, hide, at):
        params = m.params or []
        body = m.body
        out = []
        i = 0
        def arg_of(t):
            if t.kind == "id" and t.text in params:
                return args[params.index(t.text)]
            return None
        while i < len(body):
            t = body[i]
            if t.text == "#" and t.kind == "punct" and m.params is not None and i + 1 < len(body) and arg_of(body[i + 1]) is not None:
                a = arg_of(body[i + 1])
                s = ""
                for k, x in enumerate(a):
                    if k and x.space:
                        s += " "
                    s += x.text
                text = '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
                n = Tok("str", text, at.file, at.line, False, t.space, s.encode("latin-1"))
                out.append(n)
                i += 2
                continue
            if i + 1 < len(body) and body[i + 1].text == "##":
                # paste: lhs ## rhs (raw arguments)
                lhs = arg_of(t)
                lhs = [x.copy() for x in lhs] if lhs is not None else [t.copy()]
                i += 2
                rt = body[i]
                rhs = arg_of(rt)
                rhs = [x.copy() for x in rhs] if rhs is not None else [rt.copy()]
                i += 1
                if lhs and rhs:
                    pasted = self.paste(lhs[-1], rhs[0], at)
                    seq = lhs[:-1] + [pasted] + rhs[1:]
                else:
                    seq = lhs + rhs
                # chained ##
                while i < len(body) and body[i].text == "##":
                    rt = body[i + 1]
                    rhs = arg_of(rt)
                    rhs = [x.copy() for x in rhs] if rhs is not None else [rt.copy()]
                    i += 2
                    if seq and rhs:
                        seq = seq[:-1] + [self.paste(seq[-1], rhs[0], at)] + rhs[1:]
                    else:
                        seq = seq + rhs
                out.extend(seq)
                continue
            a = arg_of(t)
            if a is not None:
                exp = self.expand_all([x.copy() for x in a])
                for k, x in enumerate(exp):
                    x = x.copy()
                    if k == 0:
                        x.space = t.space
                    out.append(x)
                i += 1
                continue
            out.append(t.copy())
            i += 1
        res = []
        for k, x in enumerate(out):
            x = x.copy()
            x.hide = x.hide | hide
            x.file, x.line = at.file, at.line
            x.bol = False
            if k == 0:
                x.space = at.space
            res.append(x)
        return res

    def paste(self, a, b, at):
        text = a.text + b.text
        ts = [t for t in tokenize(text, at.file) if t.kind != "eof"]
        if len(ts) != 1:
            err(at, "## makes %r, not one token" % text)
        return ts[0]

class IfEval:
    """Constant expressions of #if, as C99 says: in 64 bits (intmax_t, or
    uintmax_t when unsigned), each value (v, unsigned?), the usual arithmetic
    conversions between them (cc in C's cpp.c does the same)."""
    BIN = [("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!=", ), ("<", ">", "<=", ">="),
           ("<<", ">>"), ("+", "-"), ("*", "/", "%")]
    M = (1 << 64) - 1
    def __init__(self, toks, tok):
        self.t, self.i, self.tok = toks, 0, tok
    def norm(self, v, u):
        v &= self.M
        return (v - (1 << 64) if not u and v >> 63 else v), u
    def peek(self):
        return self.t[self.i].text if self.i < len(self.t) else None
    def value(self):
        return self.expr()[0]
    def expr(self):
        c = self.binary(0)
        if self.peek() == "?":
            self.i += 1
            a = self.expr()
            if self.peek() != ":":
                err(self.tok, "bad ?: in #if")
            self.i += 1
            b = self.expr()
            u = a[1] or b[1]
            return self.norm((a if c[0] else b)[0], u)
        return c
    def binary(self, lvl):
        if lvl == len(self.BIN):
            return self.unary()
        v, vu = self.binary(lvl + 1)
        while self.peek() in self.BIN[lvl]:
            op = self.peek()
            self.i += 1
            r, ru = self.binary(lvl + 1)
            if op in ("/", "%") and r == 0:
                err(self.tok, "division by zero in #if")
            if op in ("&&", "||"):
                v, vu = int(bool(v) and bool(r)) if op == "&&" else int(bool(v) or bool(r)), False
                continue
            if op in ("<<", ">>"):                  # (the left operand's type)
                n = r & 63
                v, vu = self.norm(v << n if op == "<<" else v >> n, vu)
                continue
            u = vu or ru
            a, b = (v & self.M, r & self.M) if u else (v, r)
            if op in ("==", "!=", "<", ">", "<=", ">="):
                v, vu = int({"==": a == b, "!=": a != b, "<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b}[op]), False
                continue
            if op in ("/", "%"):                    # (toward zero)
                q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
                res = q if op == "/" else a - q * b
            else:
                res = {"|": a | b, "^": a ^ b, "&": a & b, "+": a + b, "-": a - b, "*": a * b}[op]
            v, vu = self.norm(res, u)
        return v, vu
    def unary(self):
        if self.i >= len(self.t):
            err(self.tok, "#if expression ends early")
        t = self.t[self.i]
        self.i += 1
        if t.text in ("-", "+", "!", "~"):
            v, u = self.unary()
            if t.text == "!":
                return int(not v), False
            return self.norm({"-": -v, "+": v, "~": ~v}[t.text], u)
        if t.text == "(":
            v = self.expr()
            if self.peek() != ")":
                err(self.tok, "missing ) in #if")
            self.i += 1
            return v
        if t.kind == "num":
            v, uns = parse_int(t)[0], parse_int(t)[1]
            if v >> 64:
                err(t, "constant too big for #if")
            return self.norm(v, uns or v >> 63 != 0)
        if t.kind == "char":
            return t.val, False
        err(self.tok, "bad token %s in #if" % t.text)
