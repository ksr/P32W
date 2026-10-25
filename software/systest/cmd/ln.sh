# ln: links, into a directory, -f, errors
echo one > a
ln a b; echo "status $?"; cat b
echo more >> b; cat a
for w in `ls -l a`; do echo $w; break; done
for w in `ls -l a`; do if test -z "$n"; then n=x; else echo "links $w"; break; fi; done
echo "-- errors"
echo c > c
ln a c; echo "exists: status $?"; cat c
ln nosuch c; echo "no old: status $?"; ls
ln a a; echo "itself: status $?"
ln a nodir/c; echo "status $?"
mkdir d
ln d e; echo "a directory: status $?"; ls
ln a; echo "status $?"
ln; echo "status $?"
ln -s a c; echo "status $?"
ln a b c; echo "last not a directory: status $?"
echo "-- into a directory"
echo x > x
ln a x d; echo "status $?"; ls d; cat d/x
ln a d/; echo "again: status $?"
echo "-- -f"
echo other > o
ln -f a o; echo "status $?"; cat o
ln -f a o; echo "the same file: status $?"; cat a
ln -f a d; echo "into d, d/a is a: status $?"
ln -f x d/a; echo "replace d/a: status $?"; cat d/a; cat a
ln -f nosuch o; echo "no old: status $?"; cat o
ln -- x y; echo "-- : status $?"; cat y
