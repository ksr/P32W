# tail: -count, -n count, +count, several files with headers, long lines
for i in 1 2 3 4 5 6 7 8 9 10 11 12
do
    echo line $i >> f
done
echo one > g
echo -n two >> g
echo "== default 10"
tail f
echo "status $?"
echo "== -3, -n 2, -n4, -0, -n 0"
tail -3 f
tail -n 2 f
tail -n4 f
tail -0 f
tail -n 0 f
echo "status $?"
echo "== +11, -n +12, +0, +100"
tail +11 f
tail -n +12 f
tail +0 g
echo
tail +100 f
echo "status $?"
echo "== more lines asked for than there are"
tail -100 g
echo
echo "== the standard input"
cat f | tail -2
tail -n 1 < f
echo "== several files"
tail -1 f g
echo
tail -n 1 g - f < f
echo "== long lines"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7
do
    cat a a > b
    mv b a
done
echo >> a
cat a a a > long
echo end >> long
tail -2 long | wc
tail -n 3 long | wc
echo "== errors"
tail -n f
echo "status $?"
tail -x f
echo "status $?"
tail -n
echo "status $?"
tail -1 nosuch f
echo "status $?"
