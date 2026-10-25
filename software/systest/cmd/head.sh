# head: -count, -n count, several files with headers, errors
for i in 1 2 3 4 5 6 7 8 9 10 11 12
do
    echo line $i >> f
done
echo one > g
echo -n two >> g
echo "== default 10"
head f
echo "status $?"
echo "== -3, -n 2, -n4, -0"
head -3 f
head -n 2 f
head -n4 f
head -0 f
echo "status $?"
echo "== the standard input"
cat f | head -2
head -n 1 < g
echo "== short files, no last newline"
head -5 g
echo
echo "== several files"
head -2 f g
echo
head -n 1 g - f < f
echo "== errors"
head -n f
echo "status $?"
head -x f
echo "status $?"
head -n
echo "status $?"
head -1 nosuch g
echo "status $?"
