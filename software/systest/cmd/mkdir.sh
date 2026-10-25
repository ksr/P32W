# mkdir: directories, -p, -m, errors
mkdir a; echo "status $?"; test -d a && echo "a is a directory"
ls -a a
mkdir b c/ d; echo "several: status $?"; ls
echo "-- errors"
mkdir a; echo "exists: status $?"
echo f > f
mkdir f; echo "a file: status $?"
mkdir x/y; echo "no parent: status $?"
mkdir f/y; echo "parent a file: status $?"
mkdir abcdefghijklmnopqrstuvwxyz0123; echo "30 characters: status $?"
mkdir abcdefghijklmnopqrstuvwxyz01; echo "28 characters: status $?"; ls
mkdir; echo "status $?"
mkdir -q z; echo "status $?"
mkdir a e; echo "one fails, the next is made: status $?"; test -d e && echo e made
echo "-- -p"
mkdir -p p/q/r; echo "status $?"; find p
mkdir -p p/q/r; echo "again: status $?"
mkdir -p p//s/t/; echo "doubled and trailing /: status $?"; find p/s
mkdir -p f/g; echo "a file on the way: status $?"
mkdir -p f; echo "the file itself: status $?"
mkdir -p ./u/../v; echo "with ..: status $?"; ls
echo "-- -m"
mkdir -m 700 m1; echo "status $?"
for w in `ls -ld m1`; do echo $w; break; done
mkdir -m755 m2
for w in `ls -ld m2`; do echo $w; break; done
mkdir -p -m 711 m3/m4
for w in `ls -ld m3/m4`; do echo $w; break; done
mkdir -m 9 m5; echo "bad mode: status $?"
mkdir -m; echo "no mode: status $?"
mkdir -- -x; echo "-- : status $?"; test -d ./-x && echo "-x made"
