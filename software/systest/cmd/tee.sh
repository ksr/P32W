# tee: copies to the output and files, -a, errors reported with status 1
echo hello | tee f1 f2
echo "status $?"
cat f1 f2
echo "== -a appends"
echo again | tee -a f1 > /dev/null
cat f1
echo "== without -a the file is truncated"
echo new | tee f1
cat f1
echo "== no files"
echo plain | tee
echo "== a file that cannot be created is reported; the others are written"
echo some | tee g1 /nodir/x g2
echo "status $?"
cat g1 g2
echo "== 16 files"
echo many | tee m1 m2 m3 m4 m5 m6 m7 m8 m9 m10 m11 m12 m13 m14 m15 m16 > /dev/null
echo "status $?"
cat m1 m16
echo "== more files than a process may have open (20)"
echo lots | tee n1 n2 n3 n4 n5 n6 n7 n8 n9 n10 n11 n12 n13 n14 n15 n16 n17 n18 n19 n20 > /dev/null 2> err
echo "status $?"
if grep 'n20: too many open files' err > /dev/null; then echo n20 reported; fi
cat n1
echo "== larger data, in pieces"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7 8 9
do
    cat a a > b
    mv b a
done
cat a | tee big | wc -c
cmp a big
echo "status $?"
echo "== errors"
echo x | tee -x f1
echo "status $?"
