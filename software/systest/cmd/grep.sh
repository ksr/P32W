# grep: options -v -n -c -i, patterns, several files, long lines, errors
echo apple > f1
echo Banana >> f1
echo cherry pie >> f1
echo a.b >> f1
echo ']x' >> f1
echo '-dash' >> f1
echo banana split > f2
echo kiwi >> f2
echo "== plain"
grep an f1
echo "status $?"
echo "== no match"
grep zzz f1
echo "status $?"
echo "== -v -n -c -i"
grep -v a f1
grep -n an f1
grep -c a f1
grep -i banana f1
grep -in BANANA f1
grep -vc a f1
echo "== stdin"
cat f2 | grep -n kiwi
echo "== several files"
grep an f1 f2
grep -c an f1 f2
grep -n i f1 f2
echo "== patterns"
grep '^c' f1
grep 'e$' f1
grep 'a.b' f1
grep 'a\.b' f1
grep 'ch*e' f1
grep '^[A-C]' f1
grep -i '^[a-c]' f1
grep '[^a-z]' f1
grep 'p*le' f1
echo "== leading ] in a class"
grep '[]]' f1
grep '[]q]x' f1
grep -c '[^]a-z.-]' f1
echo "== -- and a pattern starting with -"
grep -- -dash f1
grep '\-dash' f1
echo "== a long line"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7 8
do
    cat a a > b
    mv b a
done
echo >> a
echo first > long
cat a >> long
echo third >> long
wc long
grep -n third long
grep -c 0123 long
grep -n '89$' long | wc -c
grep -v 0 long
echo "== errors"
grep -x an f1
echo "status $?"
grep -vq an f1
echo "status $?"
grep
echo "status $?"
grep -n
echo "status $?"
grep an nosuch f1
echo "status $?"
grep zzz f1 nosuch
echo "status $?"
grep kiwi f2 f1
echo "status $?"
