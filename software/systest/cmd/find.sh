# find: paths, -name, -type, -newer, -print, -exec, operators, errors
mkdir d d/e d/e/f
echo 1 > d/a.c; echo 2 > d/b.h; echo 3 > d/e/c.c; echo 4 > d/e/f/x
find d | sort; echo "status $?"
echo "-- several paths, a trailing /"
find d/e d/a.c | sort
find d/e/ | sort
echo "-- no path: ."
cd d/e; find | sort; cd ../..
echo "-- -name"
find d -name '*.c' | sort
find d -name '?.h'
find d -name '[ab].*' | sort
find d -name '[!a]*' | sort
find d -name e
find d/e -name e
find d -name 'nomatch*'; echo "status $?"
echo "-- -type"
find d -type d | sort
find d -type f | sort
find /dev -type c -name null
find /dev -type b -name ram
find /dev -type f -name null; echo "status $?"
echo "-- operators"
find d ! -type d | sort
find d -name '*.c' -o -name '*.h' | sort
find d -type f -a ! -name '*.c' | sort
find d \( -name a.c -o -name x \) -print | sort
echo "-- -print"
find d -name a.c -print -print
find d/e -type d -print | sort
echo "-- -exec"
find d -name '*.c' -exec cat {} \; | sort
find d -type f -exec echo found {} here \; | sort
find d -name x -exec rm {} \; ; echo "status $?"
test -f d/e/f/x || echo "x removed"
find d -type f -exec test -s {} \; -print | sort
find d -type f -exec false \; -print; echo "-exec false: status $?"
echo "-- -newer"
mkdir n
echo old > n/old
sleep 1
echo new > n/new
find n -newer n/old | sort
find n -newer n/new; echo "status $?"
echo "-- errors"
find nosuch; echo "status $?"
find nosuch d/e/f; echo "status $?"
find d -bogus; echo "status $?"
find d -name; echo "status $?"
find d -type z; echo "status $?"
find d -exec echo {}; echo "status $?"
find d -newer nosuch; echo "status $?"
find d \( -name x; echo "status $?"
find d -name x \); echo "status $?"
find d -o -name x; echo "status $?"
find d -type d d/e; echo "status $?"
