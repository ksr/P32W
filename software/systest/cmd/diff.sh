# diff: normal, -u, -c and -e output (an ed script that ed applies), changes at
# the start, middle and end, empty files, no newline at the end, -b -w -i -q,
# -r and -N on directories, - for the standard input, a file of 2000 lines,
# the exit status
cat > a <<'EOF'
one
two
three
four
five
six
seven
eight
nine
ten
EOF
cat > b <<'EOF'
ONE
two
three
four
four and a half
five
six
seven
nine
ten
eleven
EOF
cp a same
echo "== the same"
diff a same
echo "status $?"
echo "== normal: a change, an insertion, a deletion, an append"
diff a b
echo "status $?"
diff b a
echo "status $?"
echo "== -u (the two header lines, with dates, dropped)"
diff -u a b | head -2 | wc -l
diff -u a b | tail -n +3
diff -U 1 a b | tail -n +3
diff -U0 a b | tail -n +3
echo "== -c"
diff -c a b | tail -n +3
diff -C 1 b a | tail -n +3
echo "== -e: applied by ed, it makes the second file"
diff -e a b
echo "status $?"
cp a a2
(diff -e a b; echo w; echo q) | ed - a2
cmp a2 b
echo "cmp $?"
cat > dots <<'EOF'
one
.
two
EOF
cp a a3
diff -e a dots
(diff -e a dots; echo w; echo q) | ed - a3
cmp a3 dots
echo "cmp $?"
echo "== at the very start and end"
echo first > s1
cat a >> s1
cp a s2
echo last >> s2
diff s1 s2
diff -u s1 s2 | tail -n +3
echo "== empty files"
echo -n > e1
echo -n > e2
diff e1 e2
echo "status $?"
diff e1 a | head -3
diff a e1 | tail -2
diff -u e1 s1 | tail -n +3 | head -3
diff -c a e1 | tail -n +3 | head -4
echo "== no newline at the end"
echo -n ten > t1
echo ten > t2
diff t1 t2
echo "status $?"
cp a n1
echo -n eleven >> n1
cp a n2
echo eleven >> n2
diff n1 n2
diff -u n2 n1 | tail -n +3
echo "== -b -w -i"
cat > w1 <<'EOF'
a b  c
Hello World
	tabbed
trailing
x y
EOF
cat > w2 <<'EOF'
a  b c
hello world
        tabbed
trailing
xy
EOF
diff w1 w2 | wc -l
diff -b w1 w2
echo "status $?"
diff -w w1 w2
echo "status $?"
diff -i w1 w2 | wc -l
diff -b -i w1 w2
echo "== -q"
diff -q a b
echo "status $?"
diff -q a same
echo "status $?"
echo "== the standard input"
cat b | diff a - | head -2
diff a - < b > o
echo "status $?"
wc -l o
diff - same < a
echo "status $?"
echo "== a file and a directory: the file of that name in it"
mkdir d
cp b d/a
diff a d | head -1
echo "== directories, -r, -N"
mkdir d1 d2 d1/sub d2/sub d1/only1
echo x > d1/same
echo x > d2/same
echo old > d1/changed
echo new > d2/changed
echo gone > d1/gone
echo new > d2/added
echo 1 > d1/sub/f
echo 2 > d2/sub/f
diff d1 d2
echo "status $?"
diff -r d1 d2
echo "status $?"
diff -rN d1 d2
echo "status $?"
diff -rq d1 d2
echo "status $?"
mkdir d3 d3/sub d3/only1
cp d1/same d1/changed d1/gone d3
cp d1/sub/f d3/sub
diff -r d1 d3
echo "status $?"
echo "== 2000 lines, a few changed"
echo line > f
for i in 1 2 3 4 5
do
    cat f f f f > g
    mv g f
done
cat f f | grep -n '' > big
wc -l big
head -700 big > big2
echo inserted >> big2
tail -n +701 big | grep -v '^1500:' | grep -v '^204[0-8]:' >> big2
echo the end >> big2
diff big big2
echo "status $?"
diff -u big big2 | tail -n +3 | wc -l
cp big big3
(diff -e big big2; echo w; echo q) | ed - big3
cmp big2 big3
echo "cmp $?"
echo "== errors"
diff a
echo "status $?"
diff a nosuch
echo "status $?"
diff nosuch a
echo "status $?"
diff -x a b
echo "status $?"
diff -U a b
echo "status $?"
diff -N a nosuch | head -2
