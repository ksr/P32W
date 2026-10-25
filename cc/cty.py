"""cty.py -- C types for p32cc (M2, D22).

P32W: char 8 (signed), short 16, int 32, long 32, long long 64 (aligned to 4,
kept in memory: D30), pointers 32; little-endian; alignment = size up to 4.
"""

class Member:
    __slots__ = ("name", "ty", "offset", "bit", "width")
    def __init__(self, name, ty, offset, bit=0, width=None):
        self.name, self.ty, self.offset = name, ty, offset
        self.bit, self.width = bit, width   # a bit-field: bits bit.. of the ty-sized unit at offset

class Type:
    def __init__(self, kind, size=0, align=1, unsigned=False, base=None):
        self.kind = kind            # void char short int long ptr array func struct union enum
        self.size, self.align, self.unsigned = size, align, unsigned
        self.base = base            # ptr / array element / func return
        self.length = None          # array length (None: incomplete)
        self.params = None          # func: list of param types, or None (unprototyped)
        self.variadic = False
        self.members = None         # struct / union: list of Member (None: incomplete)
        self.tag = None
        self.const = False
        self.is_bool = False        # _Bool: an unsigned char that only ever holds 0 or 1 (D35)

    def __repr__(self):
        return type_str(self)

def is_integer(t):
    return t.kind in ("char", "short", "int", "long", "enum", "llong")
def is_arith(t):
    return is_integer(t) or t.kind == "double"
def is_ptr(t):
    return t.kind == "ptr"
def is_scalar(t):
    return is_arith(t) or t.kind == "ptr"
def is_aggregate(t):
    return t.kind in ("struct", "union", "array")
def is_void(t):
    return t.kind == "void"

VOID = Type("void", 1, 1)
CHAR = Type("char", 1, 1)
UCHAR = Type("char", 1, 1, True)
BOOL = Type("char", 1, 1, True)        # _Bool (D35): any value converted to it is 0 or 1
BOOL.is_bool = True
SHORT = Type("short", 2, 2)
USHORT = Type("short", 2, 2, True)
INT = Type("int", 4, 4)
UINT = Type("int", 4, 4, True)
LONG = Type("long", 4, 4)
ULONG = Type("long", 4, 4, True)
DOUBLE = Type("double", 8, 4)          # (float is the same type: D30)
LLONG = Type("llong", 8, 4)
ULLONG = Type("llong", 8, 4, True)

def ptr_to(t):
    return Type("ptr", 4, 4, True, t)

def array_of(t, n):
    a = Type("array", (t.size * n) if n is not None else 0, t.align, False, t)
    a.length = n
    return a

def func_type(ret, params, variadic=False):
    f = Type("func", 1, 1, False, ret)
    f.params, f.variadic = params, variadic
    return f

def enum_type():
    return Type("enum", 4, 4)

def struct_type(kind, tag):
    s = Type(kind, 0, 1)
    s.tag = tag
    return s

def layout(st, members):
    """Members [(name, type, bit-field width or None)] -> offsets, size,
    alignment (struct or union). Bit-fields as System V lays them out: from
    the lowest bit of a unit the size of their type, never across a unit's
    boundary (a field that does not fit starts the next unit); :0 ends the
    unit; an unnamed one does not add to the alignment."""
    bits, align, out = 0, 1, []                 # (bits: the struct's size so far, in bits)
    for m in members:
        name, t = m[0], m[1]
        width = m[2] if len(m) > 2 else None
        a = t.align
        if width is not None:
            unit = t.size * 8
            if name is not None:
                align = max(align, a)
            if st.kind == "union":
                out.append(Member(name, t, 0, 0, width))
                bits = max(bits, t.size * 8 if name is not None else 0)
                continue
            if width == 0:
                bits = (bits + unit - 1) // unit * unit
                continue
            if bits // unit != (bits + width - 1) // unit:
                bits = (bits + unit - 1) // unit * unit
            out.append(Member(name, t, bits // unit * t.size, bits % unit, width))
            bits += width
            continue
        align = max(align, a)
        if st.kind == "struct":
            off = ((bits + 7) // 8 + a - 1) // a * a
            out.append(Member(name, t, off))
            bits = (off + t.size) * 8
        else:
            out.append(Member(name, t, 0))
            bits = max(bits, t.size * 8)
    off = (bits + 7) // 8
    st.members = out
    st.align = align
    st.size = (off + align - 1) // align * align

def find_member(st, name):
    for m in st.members or []:
        if m.name == name:
            return m
        if m.name is None and m.ty.kind in ("struct", "union"):   # anonymous member
            sub = find_member(m.ty, name)
            if sub:
                return Member(sub.name, sub.ty, m.offset + sub.offset, sub.bit, sub.width)
    return None

def is_memval(t):
    """A value kept in memory, not in a register (handled by its address)."""
    return t.kind in ("struct", "union", "llong", "double")

def words(t):
    """The argument words a value of type t takes."""
    return (t.size + 3) // 4 if is_memval(t) else 1

def stack_args(ft):
    """Does a call pass every argument on the stack? A variadic function, or one
    with a struct parameter (each struct copied into words of its own); else
    arguments 1-4 go in r1-r4. A function returning a struct gets a hidden
    first argument either way: where to put the result (its value in r1)."""
    return ft.variadic or any(is_memval(p) for p in ft.params or [])

def decay(t):
    if t.kind == "array":
        return ptr_to(t.base)
    if t.kind == "func":
        return ptr_to(t)
    return t

def promote(t):
    """Integer promotions: char, short, enum -> int."""
    if t.kind in ("char", "short", "enum"):
        return INT
    return t

def usual(a, b):
    """The usual arithmetic conversions (32-bit ints, and long long, which holds
    every 32-bit value)."""
    a, b = promote(a), promote(b)
    if a.kind == "double" or b.kind == "double":
        return DOUBLE
    if a.kind == "llong" or b.kind == "llong":
        return ULLONG if (a.kind == "llong" and a.unsigned) or (b.kind == "llong" and b.unsigned) else LLONG
    if a.unsigned or b.unsigned:
        return UINT if a.kind == "int" and b.kind == "int" else ULONG
    if a.kind == "long" or b.kind == "long":
        return LONG
    return INT

def compatible(a, b):
    if a is b:
        return True
    if a.kind != b.kind:
        if is_integer(a) and is_integer(b) and a.size == b.size and a.unsigned == b.unsigned:
            return True
        return False
    if a.kind == "ptr":
        return compatible(a.base, b.base)
    if a.kind == "array":
        return compatible(a.base, b.base)
    if a.kind in ("struct", "union"):
        return a is b
    if a.kind == "func":
        return compatible(a.base, b.base)
    return a.unsigned == b.unsigned

def type_str(t):
    if t.kind == "ptr":
        return type_str(t.base) + " *"
    if t.kind == "array":
        return "%s[%s]" % (type_str(t.base), "" if t.length is None else t.length)
    if t.kind == "func":
        return "%s(%s)" % (type_str(t.base), "..." if t.params is None else
                           ", ".join(type_str(p) for p in t.params) + (", ..." if t.variadic else ""))
    if t.kind in ("struct", "union"):
        return "%s %s" % (t.kind, t.tag or "<anonymous>")
    return ("unsigned " if t.unsigned else "") + ("long long" if t.kind == "llong" else t.kind)
