# cp: copies, into a directory, the same file, directories, -r, modes
echo hello > a
cp a b; echo "status $?"; cat b
echo second line >> a
cp a b; echo "copy over existing: status $?"; cat b
echo "-- same file"
cp a a; echo "status $?"; cat a
cp a ./a; echo "status $?"
mkdir d
cp a d/../a; echo "status $?"; cat a
echo "-- into a directory"
echo x > x; echo y > y
cp a x y d; echo "status $?"; ls d; cat d/y
cp x d/; echo "trailing slash: status $?"; ls d
cp x .; echo "x into .: status $?"
echo "-- errors"
cp nosuch b; echo "status $?"
cp a x nodir; echo "status $?"
cp a; echo "status $?"
cp; echo "status $?"
cp -z a b; echo "status $?"
cp a nodir/b; echo "status $?"
echo "-- a directory needs -r"
cp d e; echo "status $?"; ls
echo "-- modes of new files"
chmod 755 x; cp x xx; echo "status $?"
for m in `ls -l xx`; do echo $m; break; done
chmod 600 y; cp y yy
for m in `ls -l yy`; do echo $m; break; done
echo "existing target keeps its mode:"
cp y xx
for m in `ls -l xx`; do echo $m; break; done
echo "-- binary and empty files"
cp /bin/cat c2; cmp /bin/cat c2 && echo same
: > empty; cp empty e2; echo "status $?"; test -f e2 && test ! -s e2 && echo "empty copy"
cp /dev/null n2; echo "from /dev/null: status $?"; test -s n2 || echo "n2 is empty"
echo "-- -r"
mkdir -p t/u/v
echo 1 > t/one; echo 2 > t/u/two; echo 3 > t/u/v/three
cp -r t t2; echo "status $?"
find t2 | sort
cat t2/u/v/three
cp -r t t2; echo "again, into t2: status $?"
find t2/t | sort
cp -r t t/in; echo "into itself: status $?"
find t | sort
cp -R t/u x t3; echo "-R, last not a directory: status $?"
mkdir t4; cp -r t/u x t4; echo "several into a dir: status $?"
find t4 | sort
cp -r -- t/one t5; echo "-- : status $?"; cat t5
