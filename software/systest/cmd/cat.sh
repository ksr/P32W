# cat: files, standard input, -, -n, -u, errors
echo one > a; echo two > b
cat a; echo "status $?"
cat a b a
echo "-- standard input"
echo in | cat
echo in | cat a - b
echo in | cat -u a - b
echo "-- -n"
cat -n a b
echo "x y" | cat -n a - b
: > empty
cat -n empty a
cat -n /dev/null; echo "status $?"
echo "-- binary"
cat /bin/echo > e2; cmp /bin/echo e2 && echo "the same bytes"
echo "-- errors"
cat nosuch; echo "status $?"
cat a nosuch b; echo "status $?"
mkdir d
cat d; echo "status $?"
cat -z a; echo "status $?"
echo m > -m
cat -- -m; echo "-- : status $?"
echo in | cat ./-m -; echo "status $?"
