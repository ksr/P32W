# mv: rename, replace, into a directory, several, errors
echo one > a
mv a b; echo "status $?"; ls; cat b
echo two > c
mv b c; echo "replace: status $?"; ls; cat c
echo "-- the same file"
mv c c; echo "status $?"
mv c ./c; echo "status $?"; cat c
echo "-- into a directory"
mkdir d
mv c d; echo "status $?"; ls; ls d
mv d/c .; echo "back: status $?"; ls
echo 1 > x; echo 2 > y; echo 3 > z
mv x y z d/; echo "several: status $?"; ls; ls d
mkdir e
mv d/x d/y e; echo "status $?"; ls e
echo "-- errors"
mv nosuch b; echo "status $?"
echo q > q
mv q x nodir; echo "status $?"; ls
mv q; echo "status $?"
mv; echo "status $?"
mv d f; echo "a directory: status $?"; ls
mv q nodir/q; echo "status $?"; ls
mv e/x e/y d/z; echo "onto a file in d: status $?"; ls e; cat d/z
mv -- q r; echo "-- : status $?"; ls
mv -f r s; echo "unknown option: status $?"; ls
