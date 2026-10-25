# rm: files, -f, -r, -i, --, errors
echo a > a; echo b > b; echo c > c
rm a; echo "status $?"; ls
rm b c; echo "status $?"; ls
echo "-- missing files"
rm nosuch; echo "status $?"
rm -f nosuch; echo "-f: status $?"
rm -f; echo "-f alone: status $?"
rm; echo "no names: status $?"
echo "-- options"
echo x > x
rm -x x; echo "unknown option: status $?"; ls
rm -fz x; echo "status $?"; ls
echo "-- names that start with -"
echo m > -m
rm -m; echo "status $?"; ls
rm -- -m; echo "--: status $?"; ls
echo m > -m
rm ./-m; echo "status $?"; ls
echo "-- directories"
mkdir d; echo 1 > d/one; mkdir d/e; echo 2 > d/e/two
rm d; echo "no -r: status $?"; ls
rm -r d; echo "-r: status $?"; ls
mkdir d; mkdir d/e
rm -rf d/ nosuch; echo "-rf, a trailing /: status $?"; ls
mkdir d
rm -R d; echo "-R: status $?"; ls
rm -r .; echo ". : status $?"
rm -r ..; echo ".. : status $?"
rm -r d/..; echo "d/.. : status $?"
echo "-- -i"
echo k > k; echo l > l
echo y | rm -i k 2>/dev/null; echo "yes: status $?"
echo n | rm -i l 2>/dev/null; echo "no: status $?"
ls
echo n | rm -i l; echo
mkdir d; echo 1 > d/one
echo y | rm -ri d 2>/dev/null; echo "-ri yes to d, no to d/one: status $?"
ls; ls d
rm -fi l 2>/dev/null </dev/null; echo "-fi (the last wins): status $?"; ls
rm -if l; echo "-if: status $?"; ls
echo "-- long paths"
p=d
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16
do
    p=$p/abcdefghijklmn
done
mkdir -p $p; echo "mkdir -p: status $?"
here=`pwd`
cd $p
mkdir -p x/abcdefghijklmn/abcdefghijklmn/abcdefghijklmn/abcdefghijklmn
cd $here
rm -r d 2> ../err; echo "too long: status $?"
grep -c "name too long" ../err
test -d $p/x && echo "the long part is still there"
cd $p
rm -r x; echo "from nearer: status $?"
cd $here
rm -r d; echo "rm -r: status $?"; ls
