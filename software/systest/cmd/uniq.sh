# uniq: -c -d -u, input and output files, long lines
echo a > f
echo a >> f
echo b >> f
echo c >> f
echo c >> f
echo c >> f
echo a >> f
echo -n d >> f
echo "== plain, from the standard input"
uniq < f
echo "status $?"
echo "== -c -d -u"
uniq -c f
uniq -d f
uniq -u f
uniq -cd f
uniq -c -u f
uniq -du f
echo "status $?"
echo "== an input and an output file"
uniq f out
echo "status $?"
cat out
uniq -c - out2 < f
cat out2
echo "== lines that differ only at the end"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6
do
    cat a a > b
    mv b a
done
cat a > long
echo x >> long
cat a >> long
echo x >> long
cat a >> long
echo y >> long
uniq -c long | wc
uniq -c long | grep -c '^   2 0123'
uniq -c long | grep -c '^   1 0123.*y$'
uniq -u long | wc -c
echo "== errors"
uniq -x f
echo "status $?"
uniq nosuch
echo "status $?"
uniq f out extra
echo "status $?"
uniq f /nodir/out
echo "status $?"
