# unzip: extract, -l -t -q -d -o -n -a, names, replacing files, errors
# (the standard input is /dev/null where a file may be replaced: no questions)
mkdir d d/sub
echo one > d/a
echo two two > d/sub/b
for i in 1 2 3 4 5 6 7 8 9 10
do
    echo "line $i of a file that compresses well, line $i" >> d/lines
done
chmod 640 d/sub/b
zip -rq a d
mkdir __MACOSX
echo fork > __MACOSX/._a
zip -rq m d/a __MACOSX
mkdir x
cd x
echo "== extract"
unzip ../a
echo "status $?"
cmp d/a ../d/a && cmp d/sub/b ../d/sub/b && cmp d/lines ../d/lines && echo same
ls -l d/sub/b | grep -c '^-rw-r----- '
echo "== an existing file is not replaced without -o"
echo changed > d/a
unzip ../a.zip d/a < /dev/null
echo "status $?"
cat d/a
unzip -n ../a d/a < /dev/null
echo "status $?"
cat d/a
unzip -o ../a d/a < /dev/null
echo "status $?"
cat d/a
cd ..
echo "== some names, -q, -d"
unzip -q -d y a d/sub
echo "status $?"
ls y
ls y/d
ls y/d/sub
unzip -d y/z a d/a d/sub/b
echo "status $?"
unzip -d y a nosuch
echo "status $?"
echo "== -l"
unzip -l a | grep -v ' 20[0-9][0-9]-[01][0-9]-[0-3][0-9] [0-2][0-9]:[0-5][0-9]   '
unzip -l a | grep -c '^        4  20[0-9][0-9]-[01][0-9]-[0-3][0-9] [0-2][0-9]:[0-5][0-9]   d/a$'
unzip -l a d/sub | grep -c d/sub/b
echo "== -t"
unzip -t a
echo "status $?"
unzip -tq a.zip
echo "status $?"
echo "== __MACOSX is left out without -a"
mkdir m1 m2
unzip -q -d m1 m
ls -a m1
unzip -q -a -d m2 m
ls -a m2
ls -a m2/__MACOSX
echo "== .. is not extracted"
cd x
zip -q ../up ../d/a
cd ..
mkdir u
cd u
unzip -t ../up
unzip ../up
echo "status $?"
cd ..
echo "== errors"
unzip nosuch
echo "status $?"
unzip d
echo "status $?"
unzip d/a
echo "status $?"
unzip -d
echo "status $?"
unzip -x a
echo "status $?"
unzip
echo "status $?"
