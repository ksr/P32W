# comm: the three columns, -1 -2 -3 in every combination, -, errors
cat > a <<'END'
apple
banana
cherry
fig
grape
END
cat > b <<'END'
banana
date
fig
kiwi
END
echo "== all three columns"
comm a b
echo "status $?"
echo "== leaving columns out"
for o in -1 -2 -3 -12 -13 -23 -123 '-1 -3'
do
    echo "-- $o"
    comm $o a b
done
echo "== the other way round"
comm b a
echo "== - for the standard input; an empty file; no last newline"
comm a b > c
comm - b < a | cmp - c
echo "status $?"
: > e
comm a e
comm -2 e b
echo -n fig > nl
comm a nl
echo "== repeated lines pair one for one"
cat > r1 <<'END'
x
x
x
y
END
cat > r2 <<'END'
x
y
y
END
comm r1 r2
echo "== a long line"
echo -n 0123456789 > l
for i in 1 2 3 4 5 6 7 8 9 10
do
    cat l l > m
    mv m l
done
cat l > l1
echo x >> l1
cat l > l2
echo y >> l2
comm -12 l1 l2 | wc -c
comm -3 l1 l2 | wc -c
comm -12 l1 l1 | wc -c
echo "== errors"
comm a
echo "status $?"
comm a b c
echo "status $?"
comm -4 a b
echo "status $?"
comm a nosuch
echo "status $?"
