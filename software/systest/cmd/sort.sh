# sort: text and numeric order, -r -n -u together, several files, long lines
echo pear > f
echo Apple >> f
echo banana >> f
echo apple >> f
echo pear >> f
echo "== plain"
sort f
echo "status $?"
echo "== -r"
sort -r f
echo "== -u, -ru"
sort -u f
sort -ru f
echo "== -n"
echo 10 > n
echo 9 >> n
echo -3 >> n
echo '  7 blanks first' >> n
echo +5 >> n
echo x >> n
echo 007 >> n
echo -0 >> n
echo 3000000000 >> n
echo -3000000000 >> n
echo 2147483647 >> n
echo -2147483648 >> n
echo 99999999999999999999 >> n
sort -n n
echo "== -rn and -n -r"
sort -rn n
sort -n -r n > nr
sort -r -n n | cmp - nr
echo "status $?"
echo "== -nu: equal numbers, different text, are both kept"
echo 7 > d
echo 07 >> d
echo 7 >> d
sort -nu d
echo "== several files, - for the standard input, no last newline"
echo -n last > g
echo first | sort f g -
echo "== a long line"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7
do
    cat a a > b
    mv b a
done
echo >> a
echo 1 > long
cat a >> long
echo 0 >> long
wc long
sort long | wc
sort long | head -1
sort -r long | tail -1
echo "== empty input"
sort < /dev/null
echo "status $?"
echo "== errors"
sort -k f
echo "status $?"
sort nosuch f
echo "status $?"
