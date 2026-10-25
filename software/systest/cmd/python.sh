# python: MicroPython 1.29 (software/micropython): the language (numbers,
# big integers, floats and their formatting, strings, containers, classes,
# exceptions, generators, closures), the library (math, json, re, struct,
# collections, random, time, hashlib, binascii, deflate, io), files and
# the os module, then the python command's -c, arguments, standard input
# and exit statuses. The scripts' output is also what the host's build of
# the same MicroPython prints (software/micropython/ports/p32/build.sh).
cat > lang.py <<'PYEOF'
# lang.py: the language
print("== numbers")
print(7 // 2, -7 // 2, 7 % -3, -7 % 3, 7 / 2, 2 ** 10, 2 ** -1, divmod(17, 5), divmod(-17, 5), abs(-3))
print(2 ** 100, -(2 ** 70) // 3, (2 ** 64) % 1000007, 10 ** 30 // 7 ** 20, 1 << 40, -1 >> 3, 0xff ^ 0x0f)
print(int("123"), int("-0x1f", 16), int("777", 8), int("zz", 36), int(" 42 "), int(3.99), int(-3.99), round(2.5), round(3.5))
print(hex(255), oct(8), bin(5), hex(-(2 ** 40)), (255).to_bytes(2, "big"), int.from_bytes(b"\x01\x00", "little"))
f = 1
for i in range(1, 31):
    f *= i
print(f, len(str(2 ** 1000)), str(3 ** 50)[-10:], pow(3, 200, 1000), pow(2, 100) == 1 << 100)
print(0.1 + 0.2, 1 / 3, -1 / 3, 2 / 3, 1e100, 1e-100, 123456789012345.0, 1e16, 2.0 ** 53, 2.0 ** -1074 > 0)
print(1e308, 1e308 * 10, -1e308 * 10, float("nan"), float("-inf"), 0.1 * 3, 100.0, -0.0)
print(repr(0.1), str(1 / 7), round(3.14159, 2), round(2.675, 2), round(-0.5), round(1234.5, -2))
print("%.3f %e %g %10.4f|%-8.2f|%+d %05d %x %o %c %s %r" % (3.14159, 12345.678, 0.0001, 2.5, 1.5, 5, 42, 255, 8, 65, "s", "r"))
print("{:>8}|{:<6}|{:^7}|{:08.3f}|{:,}|{:x}|{:b}|{:.2e}|{!r}".format("right", "left", "mid", 3.14159, 1234567, 255, 10, 1234.5, "q"))
print(float("1e-30"), float("  -2.5e3 "), 1e23, 8.5e-1, 0.3 == 0.1 * 3)
print(int(1e20), float(2 ** 80), 2 ** 80 == float(2 ** 80), 10 ** 20 / 3, (10 ** 20) // 3, complex(1, 2) * complex(3, -1))
print(max(3, 9, 2), min([4, 1, 7]), sum([1, 2, 3], 10), sorted([3, 1, 2], reverse=True), any([0, 0, 1]), all([]))

print("== strings")
s = "Hello, P32W World"
print(len(s), s.upper(), s.lower(), s[7:10], s[-5:], "".join(reversed(s)), s.find("P32W"), s.find("x"), s.count("o"))
print(s.split(), s.split(", "), "a,b,,c".split(","), "  pad  ".strip() + "|", "xxhixx".strip("x"), s.replace("o", "0"))
print(s.startswith("Hell"), s.endswith("ld"), "abc".isalpha(), "123".isdigit(), " ".isspace(), "Ab".isupper(), s.index("W"), "x=1".partition("="))
print("-".join(["a", "b", "c"]), "ab" * 3, "a" in "cat", "%s=%d" % ("x", 5), "{0}{1}{0}".format("a", "b"), "x".center(5))
print("tab\there", "quote\"s", 'single\'s', "unié €", len("€"), "€".encode(), b"\xe2\x82\xac".decode())
print(ord("A"), chr(97), "abc" < "abd", "Z" < "a", str(12), repr("a\nb"))
print("%5s|%-5s|" % ("ab", "cd"), "%05d" % 42, "{:.<4}|".format("ab"), "a b".rsplit(" ", 1), "Ab".isupper())
b = bytearray(b"abc")
b[0] = 65
b.append(100)
print(b, bytes(b), b[1:], len(b), b"x" * 3, bytes([1, 2, 3]), b"a,b".split(b","), b.decode())
print(f"{1 + 2} {s[:5]!r} {3.14159:.2f} {'nested'} {42:>5}")

print("== containers")
l = [5, 2, 8, 1, 9, 3]
l.sort()
print(l, l[::-1], l[1:4], len(l), l.index(8), l.count(2), 9 in l, l + [0], l * 2 == l + l)
l.append(10); l.insert(0, -1); l.extend([11, 12]); print(l.pop(), l.pop(0), l)
del l[1:3]; l.remove(9); l.reverse(); print(l)
print(sorted(["pear", "Apple", "fig"]), sorted(["pear", "Apple", "fig"], key=len), sorted([(2, "b"), (1, "z"), (2, "a")]))
d = {"a": 1, "b": 2}
d["c"] = 3; d.update(e=5); d.setdefault("f", 6); del d["b"]
print(sorted(d.items()), sorted(d), d.get("x", 0), d.pop("e"), "a" in d, len(d), sorted(d.values()))
print({k: v * 2 for k, v in sorted(d.items())}, sorted(dict(zip("xy", (1, 2))).items()), sorted({1: 2, 3: 4}.keys()))
t = (1, "two", 3.0)
a, b, c = t
x, *rest = [1, 2, 3, 4]
print(t, a, b, c, rest, t[1:], len(t), t + (4,), (1,) * 3, tuple("ab"))
st = {3, 1, 2}
st.add(5); st.discard(1)
print(sorted(st), sorted(st | {9}), sorted(st & {2, 3, 7}), sorted(st - {2}), sorted(st ^ {3, 4}), {1, 2} <= {1, 2, 3}, frozenset([1]) == {1})
print([x * x for x in range(10) if x % 2], list(range(10, 0, -3)), list(enumerate("ab", 1)), list(zip([1, 2], "ab")))
print(list(map(lambda x: x + 1, [1, 2])), list(filter(None, [0, 1, "", "a"])), list(reversed([1, 2, 3])), range(5)[2])
m = [[0] * 3 for _ in range(3)]
for i in range(3):
    m[i][i] = 1
print(m, [row[0] for row in m], sum(sum(r) for r in m))

print("== functions and classes")
def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)
def kw(a, b=2, *args, c=3, **kwargs):
    return a, b, args, c, sorted(kwargs.items())
print(fib(15), kw(1), kw(1, 5, 6, 7, c=8, z=9, y=0), (lambda *a: a)(1, 2))
def depth(n):
    return 0 if n == 0 else 1 + depth(n - 1)
print("recursion", depth(3000))
def counter():
    n = 0
    def inc():
        nonlocal n
        n += 1
        return n
    return inc
c1 = counter(); c1(); print(c1(), counter()())
def deco(f):
    def w(*a):
        return "<" + str(f(*a)) + ">"
    return w
@deco
def sq(x):
    return x * x
print(sq(7), sq.__name__ if hasattr(sq, "__name__") else "w")
class Vec:
    count = 0
    def __init__(self, x, y):
        self.x, self.y = x, y
        Vec.count += 1
    def __add__(self, o):
        return Vec(self.x + o.x, self.y + o.y)
    def __eq__(self, o):
        return self.x == o.x and self.y == o.y
    def __repr__(self):
        return "Vec(%d, %d)" % (self.x, self.y)
    def __len__(self):
        return 2
    def __getitem__(self, i):
        return (self.x, self.y)[i]
    @property
    def norm2(self):
        return self.x * self.x + self.y * self.y
    @staticmethod
    def zero():
        return Vec(0, 0)
    @classmethod
    def unit(cls):
        return cls(1, 0)
class Vec3(Vec):
    def __init__(self, x, y, z):
        super().__init__(x, y)
        self.z = z
    def __repr__(self):
        return "Vec3(%d, %d, %d)" % (self.x, self.y, self.z)
v = Vec(1, 2) + Vec(3, 4)
print(v, v == Vec(4, 6), len(v), list(v), v.norm2, Vec.zero(), Vec.unit(), Vec3(1, 2, 3), Vec.count)
print(isinstance(Vec3(0, 0, 0), Vec), issubclass(Vec3, Vec), type(v).__name__, hasattr(v, "x"), getattr(v, "q", "none"))

print("== exceptions")
def risky(k):
    if k == 0:
        raise ValueError("zero")
    if k == 1:
        return 1 // 0
    if k == 2:
        return [][1]
    if k == 3:
        return {}["k"]
    if k == 4:
        return int("x")
    if k == 5:
        return None + 1
    return "ok"
for k in range(7):
    try:
        print(k, risky(k))
    except (ValueError, ZeroDivisionError) as e:
        print(k, type(e).__name__, e)
    except LookupError as e:
        print(k, "lookup", type(e).__name__)
    except Exception as e:
        print(k, "other", type(e).__name__)
    finally:
        pass
class MyErr(Exception):
    pass
try:
    try:
        raise MyErr("inner")
    finally:
        print("finally ran")
except MyErr as e:
    print("caught", e.args)
def gen_cleanup():
    try:
        yield 1
        yield 2
    finally:
        print("generator closed")
g = gen_cleanup(); print(next(g)); g.close()
with open("ctx.txt", "w") as fh:
    fh.write("x")
print(fh.closed if hasattr(fh, "closed") else "closed")
try:
    assert 1 == 2, "assertion text"
except AssertionError as e:
    print("assert", e)

print("== generators")
def squares(n):
    for i in range(n):
        yield i * i
print(list(squares(6)), sum(x for x in range(101)), next(iter([7])), list(zip(squares(3), "abc")))
def chain(*its):
    for it in its:
        yield from it
print(list(chain([1, 2], (3,), squares(3))))
def acc():
    total = 0
    while True:
        v = yield total
        total += v
a = acc(); next(a); a.send(5); print(a.send(10))
it = iter(range(3))
print(next(it), next(it), next(it), next(it, "done"))
PYEOF
python lang.py
echo "status $?"
cat > lib.py <<'PYEOF'
# lib.py: the library
import math, json, re, struct, collections, random, time, hashlib, binascii, io, sys, errno, heapq, array
print("== math")
g = lambda *v: " ".join("%.12g" % x for x in v)   # (12 digits: libms differ in the last bit)
print(math.sqrt(2), g(math.sin(1), math.cos(1), math.tan(1), math.exp(1), math.log(10)), math.log10(1000), math.log2(8))
print(g(math.atan2(1, -1), math.asin(0.5), math.acos(0.5), math.atan(1)), math.pi, math.e, math.inf, math.isnan(math.nan))
print(math.floor(-2.5), math.ceil(-2.5), math.trunc(-2.5), math.fabs(-3), math.fmod(7, -3), math.copysign(1, -0.0), math.sqrt(3 * 3 + 4 * 4))
print(math.factorial(20), math.pow(2, 0.5), math.ldexp(1, -3), math.frexp(8), math.modf(-3.25), math.isclose(0.1 + 0.2, 0.3))
print(g(math.sinh(1), math.cosh(1), math.tanh(1), math.asinh(1), math.acosh(2), math.atanh(0.5)), math.degrees(math.pi), math.radians(180))
print(g(math.erf(0.5), math.erfc(0.5), math.gamma(5.5), math.lgamma(10), math.expm1(1e-10)))
print(abs(3 + 4j), (1 + 2j) * (1 + 2j), (1 + 2j).real, (1 + 2j).imag, 1j * 1j)
import cmath
r12 = lambda z: complex(round(z.real, 12), round(z.imag, 12))
print(r12(cmath.sqrt(-4)), r12(cmath.exp(1j * math.pi)), round(cmath.phase(-1), 12), r12(cmath.log(-1)), [round(v, 12) for v in cmath.polar(3 + 4j)])
print("== json")
d = json.loads('{"a": [1, 2.5, "x", null, true, false], "b": {"c": "\\u00e9"}, "n": -1e3}')
print(sorted(d), d["a"], d["b"]["c"], d["n"], json.dumps([1, "two", None, True, 1.5, {"k": [1]}]))
print(json.dumps("q\"\n"), json.loads("[]"), json.loads(" 3 "))
try:
    json.loads("{bad")
except ValueError as e:
    print("json error", type(e).__name__)
print("== re")
m = re.match(r"(\w+)@(\w+)\.com", "ken@example.com")
print(m.group(0), m.group(1), m.group(2), m.groups(), m.start(2), m.end(2), m.span(1))
print(re.sub(r"\d+", "#", "a1b22c333"), re.sub(r"(\w)(\d)", r"\2\1", "a1b2"), re.compile(r",\s*").split("a, b,c"))
print(re.search(r"o+", "foooo bar").group(0), re.match("x", "abc"), re.compile("[a-c]+").match("abcd").group(0))
print("== struct")
p = struct.pack("<HIb", 1, 0x01020304, -1)
print(p, struct.unpack("<HIb", p), struct.calcsize("<HIb"), struct.unpack(">d", struct.pack(">d", 1.5)), struct.pack("4s", b"ab"))
print(struct.pack("<f", 1.5), struct.unpack("<f", struct.pack("<f", 0.1)), struct.pack(">e", -2.5), struct.unpack("<e", b"\x00\x3c"), struct.calcsize("ffe"))
fa = array.array("f", [0.5, 1e-45, 3.4e38]); print(fa, len(bytes(fa)), memoryview(fa).itemsize, list(memoryview(bytes(fa)).cast("f")) if hasattr(memoryview, "cast") else fa[2])
print("== collections")
od = collections.OrderedDict(); od["z"] = 1; od["a"] = 2
P = collections.namedtuple("P", "x y")
pt = P(1, 2)
dq = collections.deque((), 5); dq.append(1); dq.append(2); dq.appendleft(0)
print(list(od.items()), pt, pt.x + pt.y, pt[1], len(dq))
groups = {}
for w in ["apple", "avocado", "banana"]:
    groups.setdefault(w[0], []).append(w)
print(sorted(groups.items()))
print("== random (seeded)")
random.seed(42)
r = [random.randint(1, 6) for _ in range(5)]
print(len(r), all(1 <= x <= 6 for x in r), 0 <= random.random() < 1, random.choice([7]), random.getrandbits(1) in (0, 1))
print("== time")
print(time.gmtime(0), time.gmtime(1791936000)[:6], time.mktime(time.localtime(1000000)) == 1000000, type(time.time()).__name__)
t0 = time.ticks_ms(); time.sleep(0.1); print("slept", time.ticks_diff(time.ticks_ms(), t0) >= 80)
print("== hashlib, binascii, io, heapq, array")
print(binascii.hexlify(hashlib.sha256(b"abc").digest()))
print(binascii.hexlify(b"\x01\xff"), binascii.unhexlify("4142"), binascii.b2a_base64(b"hello"), binascii.a2b_base64("aGVsbG8="), binascii.crc32(b"abc"))
sio = io.StringIO(); sio.write("line1\nline2"); print(repr(sio.getvalue()), io.BytesIO(b"xy").read())
h = [5, 1, 8, 3]; heapq.heapify(h); heapq.heappush(h, 0); print([heapq.heappop(h) for _ in range(5)])
ar = array.array("i", [1, 2, 3]); ar.append(4); print(ar, sum(ar), array.array("d", [0.5]))
print("== sys")
print(sys.implementation.name, sys.version_info[:2], sys.byteorder, sys.maxsize >= 2 ** 31 - 1, type(sys.argv), sys.path[0] == "" or True)
print(errno.ENOENT, errno.EEXIST, errno.EINVAL, errno.errorcode[errno.ENOENT])
PYEOF
python lib.py
echo "status $?"
cat > files.py <<'PYEOF'
# files.py: files and the os module
import os
with open("data.txt", "w") as f:
    f.write("first line\nsecond line\n")
    f.write("%d %s\n" % (3, "third"))
with open("data.txt") as f:
    print(repr(f.readline()), f.readlines())
with open("data.txt", "a") as f:
    print(f.write("appended\n"))
print(open("data.txt").read().splitlines(), [l.rstrip() for l in open("data.txt")])
with open("data.txt", "rb") as f:
    print(f.read(5), f.seek(-9, 2), f.read(), f.tell())
with open("bin.dat", "wb") as f:
    f.write(bytes(range(256)))
print(os.stat("bin.dat")[6], len(open("bin.dat", "rb").read()), open("bin.dat", "rb").read()[250:])
os.mkdir("sub")
open("sub/a.txt", "w").write("a")
os.rename("sub/a.txt", "sub/b.txt")
print(sorted(os.listdir()), os.listdir("sub"), os.getcwd() != "", os.stat("sub")[0] & 0o170000 == 0o040000)
os.chdir("sub"); print(os.listdir("."), open("b.txt").read()); os.chdir("..")
os.remove("sub/b.txt"); os.rmdir("sub"); os.remove("bin.dat")
print(sorted(os.listdir()))
for bad in ("no/such/file", "data.txt/x"):
    try:
        open(bad)
    except OSError as e:
        print("OSError", e.errno)
try:
    os.mkdir("data.txt")
except OSError as e:
    print("mkdir", e.errno)
print(os.getenv("HOME") is not None, os.getenv("NO_SUCH_VARIABLE"), os.getenv("NO_SUCH_VARIABLE", "default"))
os.putenv("PYTEST_VAR", "set from python")
print(os.getenv("PYTEST_VAR"), os.system("echo $PYTEST_VAR; exit 3") >> 8)
os.unsetenv("PYTEST_VAR"); print(os.getenv("PYTEST_VAR"))
PYEOF
python files.py
echo "status $?"
echo "== the python command"
python -c 'import sys; print("-c", sys.argv, 6 * 7)' one two
echo 'import sys; print("args", sys.argv)' > args.py
python args.py x "y z"
echo 'print("from stdin", sum(range(10)))' | python
python -c 'import sys; sys.exit(3)'; echo "exit $?"
python -c 'raise SystemExit("bye")'; echo "exit $?"
python -c '1 / 0'; echo "exit $?"
python -c 'def f(:'; echo "exit $?"
python no_such_script.py; echo "exit $?"
python -x; echo "exit $?"
echo 'def helper(n): return n * 2' > mymod.py
python -c 'import mymod; print("import", mymod.helper(21))'
ls /usr/lib/micropython
cmp /usr/bin/python /usr/bin/micropython && echo "micropython is python"
