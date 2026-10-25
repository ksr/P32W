# awk: patterns and ranges, fields (FS, NF, $0 rebuilt), arrays (SUBSEP,
# delete, in), functions (recursion, arrays by reference, locals), control
# flow, printf and number output (integers, softfp fractions, %e %f %g),
# strnum comparisons, string functions, regular expressions (ERE, dynamic),
# getline in every form, > >> | redirections, close and system, RS modes,
# -F -v -f and operand assignments, ENVIRON and ARGV, exit, errors
cat > emp <<'EOF'
Beth	4.00	0
Dan	3.75	0
Kathy	4.00	10
Mark	5.00	20
Mary	5.50	22
Susie	4.25	18
EOF
cat > text <<'EOF'
The quick brown fox jumps over the lazy dog.
Now is the time for all good men
to come to the aid of their party.

  leading blanks   and   trailing
EOF
echo "== patterns, fields, totals"
cat > p1 <<'EOF'
$3 > 0 { print $1, $2 * $3 }
$3 == 0 { print $1, "none" }
/^M/, /^S/ { print "range:", $1 }
NR == 2, NR == 3 { print "lines:", NR }
{ pay += $2 * $3; if ($2 > maxrate) { maxrate = $2; who = $1 } }
END {
    print NR, "employees, total pay", pay, "average", pay / NR
    print "highest rate:", maxrate, "for", who; print "last:", $0, NF
}
EOF
awk -f p1 emp
echo "status $?"
echo "== printf"
cat > p2 <<'EOF'
{ printf("%-8s $%6.2f|%5d|%-5s|%05.1f|%x\n", $1, $2 * $3, $3, $1, $2, $3 + 250) }
END {
    printf "%d %i %o %X %#x %u ", 42.9, -42, 8, 255, 255, 7
    printf "%c%c %5.1f%% [%10.3s] [%-6s]\n", 65, "zebra", 45.67, "abcdef", "ab"
    printf "%e %E %.2e %g %G ", 1234.5678, 0.000123, 1e100, 100000, 1e-10
    printf "%.3g %#g %g %g\n", 3.14159, 2, 1e6, 0.0001
    printf "%f %.0f %.0f %.0f %.1f ", 3.14159265, 0.5, 1.5, 2.5, 0.05
    printf "%.10f %10.4f|%-10.2f|\n", 1/3, -2.5, 1e6
    printf "%*d|%-*d|%.*f|%+d|% d|%.3d\n", 5, 1, 4, 2, 2, 3.14159, 5, 5, 7
    printf "%.17g %.20f %d %d\n", 0.1, 0.1, 2^53, -1e18
}
EOF
awk -f p2 emp
cat > p3 <<'EOF'
BEGIN {
    print "== numbers"
    print 1e6, 1e16, 2^31, 2^53, 2^64, 1e300 * 1e300, -1/3, 100/3, 0.1 + 0.2
    print 7 % 3, -7 % 3, 7.5 % 2, 2^-1, 2**10, -2^2, 2^3^2, 100000 * 100000, 2147483647 + 1
    print 3 / 2, int(3.9), int(-3.9), sqrt(2), exp(1), log(10), sin(1), cos(1), atan2(1, 1) * 4
    OFMT = "%.2f"; x = 3.14159; print x, x ""; CONVFMT = "%.3f"; print x "", 17 ""
    print length(12345), length(1/4), "3x" + 2, -"4", +".5e1", "abc" + 0, "1e3" + 0
    s = 0; for (i = 1; i <= 100; i++) s += i / 10; print s, s * s, s / 7
}
EOF
echo "== comparisons"
cat > p4 <<'EOF'
{ printf "%s %d%d%d%d\n", $1, ($2 == 4), ($2 == "4.00"), ($3 < 10), ($1 < "M") }
END {
    x = "10"; y = 9
    print (x < y), (x + 0 < y), ("10" < "9"), (10 < 9), (u == 0), (u == "")
}
EOF
awk -f p4 emp
echo "3.0 3 abc 1e1" | awk '{ print ($1 == $2), ($1 == 3), ($1 == "3"), ($3 > $4), ($4 == 10) }'
echo "== fields"
cat > p5 <<'EOF'
BEGIN { FS = ":"; OFS = "-" }
{ print NF, $2; $2 = "X"; print; $5 = "e"; print; print NF; NF = 2; print }
{ $0 = "p:q"; print $2, NF }
EOF
echo a:b:c | awk -f p5
cat > p6 <<'EOF'
BEGIN {
    print "== fields in BEGIN"
    OFMT = CONVFMT = "%.6g"
    $0 = "a b c"; $1 = $1; print; OFS = ","; $1 = $1; print; FS = ","; $0 = $0; print NF
    FS = ""; $0 = "xyz"; print NF, $2; FS = "[0-9]+"; $0 = "a12b345c"; print NF, $1 $2 $3
}
EOF
awk '{ print NF ": [" $1 "] [" $NF "]" }' text
echo "== arrays and functions"
cat > p7 <<'EOF'
{ for (i = 1; i <= NF; i++) n[tolower($i)]++ }
END {
    for (w in n) if (n[w] > 1) print w, n[w] | "sort"
    close("sort")
    print length(n), ("the" in n), ("zzz" in n), length(n)
}
EOF
awk -f p7 text
cat > p8 <<'EOF'
function fact(n) { return n <= 1 ? 1 : n * fact(n - 1) }
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2) }
function fill(a, n,   i) { for (i = 1; i <= n; i++) a[i] = i * i; return n }
function sum(a,   k, s) { for (k in a) s += a[k]; return s }
function sortarr(a, n,   i, j, t) {
    for (i = 2; i <= n; i++) {
        t = a[i]
        for (j = i - 1; j > 0 && a[j] > t; j--) a[j+1] = a[j]
        a[j+1] = t
    }
}
function local(x,   y) { y = x * 2; x = 0; return y }
function depth(n) { return n == 0 ? 0 : 1 + depth(n - 1) }
BEGIN {
    print fact(10), fact(20), fib(15), depth(300)
    fill(sq, 10); print sum(sq), length(sq)
    delete sq[1]; print length(sq), (1 in sq), (2 in sq); delete sq; print length(sq)
    n = split("33 7 12 99 1 45 8", A, " "); sortarr(A, n)
    for (i = 1; i <= n; i++) printf "%s ", A[i]
    print ""
    v = 5; print local(v), v, y
    a[1, 2] = "x"; for (k in a) { split(k, p, SUBSEP); print p[1], p[2] }
    print ((1, 2) in a), ((2, 1) in a)
    m[0.1 + 0.2] = 1; for (k in m) print k
}
EOF
awk -f p8
cat > p9 <<'EOF'
BEGIN {
    print "== control flow"
    FS = OFS = " "; x = 0
    while (1) { x++; if (x > 6) break; if (x % 2) continue; printf "%d ", x }
    print ""
    do x--; while (x > 0); print x
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            if (j == 1) continue
            if (i == 2) break
            printf "%d%d ", i, j
        }
    print ""
    if (x) print "no"; else if (!x) print "else if"; else print "no"
}
EOF
cat > in5 <<'EOF'
1
2
3
4
5
EOF
awk 'NR == 2 { next } NR == 4 { exit 3 } { print } END { print "end", NR }' in5
echo "status $?"
cat > p10 <<'EOF'
BEGIN {
    print "== strings"
    s = "hello, world"
    print length(s), substr(s, 1, 5), substr(s, 8), substr(s, 0, 2), substr(s, -1), substr(s, 20) "|"
    print index(s, "world"), index(s, "x"), toupper(s), tolower("MiXeD"), sprintf("%3d:%s", 7, "x")
    n = split("a:b::d", p, ":"); print n, p[2], p[3] "|" p[4]
    n = split("  one  two  ", w); print n, w[1] w[2]
    n = split("a1b22c333", q, /[0-9]+/); print n, q[1] q[2] q[3] "|" q[4]
    t = "aaa bbb aaa"; print gsub(/aaa/, "X", t), t
    t = "hello"; print gsub(/l/, "[&]", t), t; t = "hello"; sub(/l/, "\\&", t); print t
    t = "abc"; print gsub(/x*/, "-", t), t; t = "banana"; print gsub(/ana/, "<&>", t), t
    print match("the quick brown", /q[a-z]+/), RSTART, RLENGTH, match("abc", /z/), RSTART, RLENGTH
}
EOF
cat > p11 <<'EOF'
BEGIN {
    print "== regular expressions"
    print ("foo.bar" ~ /o\.b/), ("fooxbar" ~ "o\\.b"), ("hello" ~ "^[a-z]+$"), ("Hello" ~ /^[a-z]+$/)
    print ("aaa" ~ /^a{3}$/), ("abab" ~ /^(ab)+$/), ("dog" ~ /cat|dog/), ("a+b" ~ /a\+b/)
    print ("A1" ~ /^[[:alpha:]][[:digit:]]$/), ("x" ~ /[^abc]/), ("]" ~ /[]a]/), ("a/b" ~ /a\/b/)
    print ("" ~ /^$/), ("tab	x" ~ /\t/), match("xxabcabcyy", /(abc)+/), RLENGTH
}
EOF
awk -f p3 -f p6 -f p9 -f p10 -f p11
awk '/fox|men/ { print NR ": " $1 } $0 !~ /e/ { print "no e:", NR }' text
echo "== getline and redirection"
cat > p12 <<'EOF'
BEGIN {
    while ((getline line < "emp") > 0) n++
    print n, line
    close("emp"); getline < "emp"; print $1, NF, NR
    "echo a b c" | getline; print $2, NF; "echo x" | getline v; print v
    print (getline z < "nonexistent")
    print "one" > "out1"; print "two" > "out1"; close("out1")
    print "three" >> "out1"; close("out1")
    while ((getline l < "out1") > 0) print "read:", l
    print "c\nb\na" | "sort"; close("sort")
    print "status", system("exit 3"), close("nothing")
}
EOF
awk -f p12
awk 'NR == 1 { getline; print "after:", $1, NR } NR == 3 { getline x; print "var:", x, $1, NR }' emp
cat > p13 <<'EOF'
{ print $1 > ($3 > 10 ? "big" : "small") }
END { close("big"); close("small"); while ((getline l < "big") > 0) print "big:", l }
EOF
awk -f p13 emp
cat small
echo "== RS"
cat > para <<'EOF'


a b
c



d e f

EOF
awk 'BEGIN { RS = "" } { print NR ": " NF, $NF }' para
awk 'BEGIN { printf "x;y;z" > "semi"; printf "a12b345c" > "digits" }'
awk 'BEGIN { RS = ";" } { print NR, $0 }' semi
awk 'BEGIN { RS = "[0-9]+" } { print FILENAME, NR, $0 }' digits
echo "== options and operands"
echo a:b:c | awk -F: -v 'msg=hi\tthere' '{ print msg, $2 }'
awk -F '\t' '{ print $3 }' emp | sed -n 3p
echo 'BEGIN { print "from -f", v }' > prog.awk
awk -v v=1 -f prog.awk
echo stdin | awk '{ print v, $1 }' v=1 emp v=2 -
cat > p14 <<'EOF'
BEGIN { print ARGC, ARGV[0], ARGV[1], ARGV[2]; ARGV[1] = ""; ARGV[2] = "emp" }
{ n++ }
END { print n, FILENAME, (ENVIRON["HOME"] != ""), (length(ENVIRON) > 0) }
EOF
awk -f p14 nonesuch other
echo "== errors"
awk 'BEGIN { x = ; }'
echo "status $?"
awk 'BEGIN { print 1 / 0 }'
echo "status $?"
awk '{ print $1 / $3 }' emp
echo "status $?"
awk '{ print }' nonesuch
echo "status $?"
awk 'BEGIN { f(1) }'
echo "status $?"
awk 'BEGIN { printf "%d %s\n", 1 }'
echo "status $?"
