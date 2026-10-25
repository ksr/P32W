# bc: arithmetic at several scales, big numbers, / and %, ^ (negative
# exponents too), sqrt, length, ibase and obase, functions (recursion, arrays,
# autos), control flow, strings, -l (s c a l e j at scale 20), the files and
# then the standard input, errors
cat > arith <<'EOF'
1 + 2 * 3
(1 + 2) * 3
10 - 20
7 / 2
-7 / 2
7 % 3
-7 % 3
scale = 4
7 / 3
1 / 3 * 3
2 / 3 + 1 / 3
3.14159 * 2
1.25 * 1.25
.1 + .2
1 - .0001
-.5 * 3
scale = 2
7 % 3
5.5 % 2
scale = 30
1 / 7
22 / 7
-1 / 3
scale
EOF
bc arith < /dev/null
echo "status $?"
echo "== big numbers"
cat > big <<'EOF'
2 ^ 200
3 ^ 100 - 1
12345678901234567890123456789 * 98765432109876543210987654321
(2 ^ 128 + 1) / 3
(2 ^ 128) % 1000007
x = 2 ^ 256; x / 2 ^ 255
2 ^ 1000 - 2 ^ 999 * 2
scale = 120
1 / 3
EOF
bc big < /dev/null
echo "== ^ and sqrt"
cat > pow <<'EOF'
scale = 10
2 ^ -1
2 ^ -10
(-2) ^ 3
-2 ^ 2
2 ^ 3 ^ 2
1.5 ^ 3
.1 ^ 20
10 ^ -3
0 ^ 0
scale = 20
sqrt(2)
sqrt(16)
sqrt(1000000)
sqrt(.0001)
scale = 0
sqrt(99)
sqrt(100)
sqrt(2.00)
length(12345)
length(1.50)
scale(1.50)
length(.0012)
EOF
bc pow < /dev/null
echo "== ibase and obase"
cat > base <<'EOF'
obase = 16
255
4096
-10
obase = 2
10
scale = 2
.75
scale = 0
obase = 8
64
obase = 100
123456
obase = 10
ibase = 16
FF
1A
A
ibase = 2
1010
ibase = A
ibase = 8
777
ibase = A
obase
ibase
EOF
bc base < /dev/null
echo "== functions, arrays, autos"
cat > func <<'EOF'
define f(n) {
    if (n <= 1) return (1)
    return (n * f(n - 1))
}
f(50)
define fib(n) {
    auto a, b, t, i
    a = 0
    b = 1
    for (i = 0; i < n; i++) { t = a + b; a = b; b = t }
    return (a)
}
fib(100)
define sum(x[], n) {
    auto i, s
    for (i = 0; i < n; i++) s = s + x[i]
    x[0] = 1000
    return (s)
}
for (i = 0; i < 10; i++) a[i] = i * i
sum(a[], 10)
a[0]
a[9]
a[100]
define inc(x) { x = x + 1; return (x) }
x = 5
inc(x)
x
define get() { return (v) }
define dyn(v) { return (get()) }
v = 1
dyn(7)
v
define none() { }
none()
define long_name_2(abc_d) { return (abc_d * 2) }
long_name_2(21)
EOF
bc func < /dev/null
echo "== control flow, strings, assignment operators"
cat > flow <<'EOF'
i = 0
while (i < 5) { i = i + 1; if (i == 2) continue; if (i == 4) break; i }
for (i = 0; i < 3; i++) print i, " "
print "\n"
if (1 > 2) print "no\n" else print "yes\n"
if (2 > 1) {
    print "a block\n"
}
i = 10; i++; i; --i; i
x = 3; x += 4; x *= 2; x -= 1; x /= 2; x ^= 2; x %= 10; x
"a plain string, no \n escapes
"
print "tab\there\n", 6 * 7, "\n"
1 == 1; 1 != 1; 2 <= 1; 2 >= 1; !0; 1 && 0; 0 || 3
x = 1 < 2
x
(y = 4)
y
last + 1
/* a comment
   of two lines */ 5 # and one to the end of the line
1 + \
2
quit
6
EOF
bc flow
echo "status $?"
echo "== the files, then the standard input"
echo "z = 7" > f1
echo "z = z * 3" > f2
echo "z * 2" | bc f1 f2
echo "== -l"
echo "s(1); c(1); a(1); l(2); e(1); scale; 4 * a(1); j(0, 1)" | bc -l
echo "== errors"
cat > err <<'EOF'
1 / 0
1 % 0
1 +
2 * * 3
sqrt(-4)
f(1)
define f(x) { return (x) }
f(1, 2)
a[-1] = 1
2 ^ 1.5
ibase = 20
obase = 1
scale = -1
break
return (1)
3 = 4
x = "s"
y = (1
@
print "after the errors\n"
EOF
bc err < /dev/null
echo "status $?"
bc nosuch < /dev/null
echo "status $?"
bc -x
echo "status $?"
