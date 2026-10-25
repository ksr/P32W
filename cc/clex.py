"""clex.py -- the C lexer of p32cc (M2, D22).

Turns source text into tokens. Each token knows its file and line, whether
it begins a line (`bol`, for preprocessor directives) and whether white
space precedes it (`space`, for macro definitions and stringizing).
Backslash-newline is removed first; comments become white space.
"""
import re

class CError(Exception):
    pass

class Tok:
    __slots__ = ("kind", "text", "val", "file", "line", "bol", "space", "hide")
    def __init__(self, kind, text, file="?", line=0, bol=False, space=False, val=None):
        self.kind, self.text, self.val = kind, text, val      # kind: id num char str punct eof
        self.file, self.line, self.bol, self.space = file, line, bol, space
        self.hide = frozenset()                                 # macro hideset
    def copy(self):
        t = Tok(self.kind, self.text, self.file, self.line, self.bol, self.space, self.val)
        t.hide = self.hide
        return t
    def __repr__(self):
        return "Tok(%s %r %s:%d)" % (self.kind, self.text, self.file, self.line)

def err(tok, msg):
    raise CError("%s:%d: error: %s" % (tok.file, tok.line, msg) if tok else "error: " + msg)

PUNCT = ["...", "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
         "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##"] + list("[](){}.&*+-~!/%<>^|?:;=,#")
ESC = {"n": 10, "t": 9, "r": 13, "0": 0, "a": 7, "b": 8, "f": 12, "v": 11, "\\": 92, "'": 39,
       '"': 34, "?": 63, "e": 27}

def unescape(body, tok):
    """The bytes of a char / string literal body (between the quotes)."""
    out, i = bytearray(), 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out += c.encode("latin-1")
            i += 1
            continue
        i += 1
        if i >= len(body):
            err(tok, "bad escape")
        c = body[i]
        if c == "x":
            m = re.match(r"[0-9a-fA-F]+", body[i + 1:])
            if not m:
                err(tok, "\\x needs hex digits")
            out.append(int(m.group(0), 16) & 0xFF)
            i += 1 + len(m.group(0))
        elif c in "01234567":
            m = re.match(r"[0-7]{1,3}", body[i:])
            out.append(int(m.group(0), 8) & 0xFF)
            i += len(m.group(0))
        elif c in ESC:
            out.append(ESC[c])
            i += 1
        else:
            err(tok, "unknown escape \\%s" % c)
    return bytes(out)

RX = re.compile(r"""
   (?P<ws>[ \t\f\v\r]+)
 | (?P<nl>\n)
 | (?P<lc>//[^\n]*)
 | (?P<bc>/\*.*?\*/)
 | (?P<num>\.?[0-9](?:[eEpP][+-]|[0-9a-zA-Z_.])*)
 | (?P<id>[A-Za-z_][A-Za-z_0-9]*)
 | (?P<chr>'(?:\\.|[^'\\\n])+')
 | (?P<str>"(?:\\.|[^"\\\n])*")
""", re.X | re.S)

def tokenize(src, fname):
    src = src.replace("\\\n", "")          # (line numbers after a continuation drift; fine)
    toks, pos, line, bol, space = [], 0, 1, True, False
    n = len(src)
    while pos < n:
        m = RX.match(src, pos)
        if m:
            k = m.lastgroup
            text = m.group(0)
            if k == "ws":
                space = True
            elif k == "nl":
                line += 1; bol = True; space = False
            elif k in ("lc", "bc"):
                line += text.count("\n")
                if "\n" in text:
                    bol = True
                space = True
            else:
                kind = {"num": "num", "id": "id", "chr": "char", "str": "str"}[k]
                t = Tok(kind, text, fname, line, bol, space)
                if kind == "str":
                    t.val = unescape(text[1:-1], t)
                elif kind == "char":
                    b = unescape(text[1:-1], t)
                    if len(b) != 1:
                        err(t, "character constant must be one character")
                    t.val = b[0] - 256 if b[0] > 127 else b[0]   # char is signed
                toks.append(t)
                bol = space = False
            pos = m.end()
            continue
        if src.startswith("/*", pos):
            err(Tok("eof", "", fname, line), "unterminated comment")
        for p in PUNCT:
            if src.startswith(p, pos):
                toks.append(Tok("punct", p, fname, line, bol, space))
                bol = space = False
                pos += len(p)
                break
        else:
            err(Tok("eof", "", fname, line), "stray character %r" % src[pos])
    toks.append(Tok("eof", "", fname, line, True, False))
    return toks

def parse_int(tok):
    """An integer constant: (value, unsigned?, longs (0, 1, or 2: ll), not decimal?)."""
    t = tok.text.lower()
    m = re.match(r"^(0x[0-9a-f]+|0[0-7]*|[1-9][0-9]*)(u|l|ul|lu|ll|ull|llu)?$", t)
    if not m:
        err(tok, "bad number %s (no floating point)" % tok.text)
    s = m.group(1)
    v = int(s, 16) if s.startswith("0x") else int(s, 8) if s.startswith("0") and len(s) > 1 else int(s)
    suf = m.group(2) or ""
    return v, "u" in suf, suf.count("l"), s.startswith("0x") or (s.startswith("0") and len(s) > 1)
