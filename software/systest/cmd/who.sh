# who -- one line per login in /etc/utmp: name, terminal, time
who > out; echo "status $?"
test -s out; echo "someone is logged in $?"
grep -c '^[a-z][a-z0-9]* *tty[01] *[A-Z][a-z][a-z] [ 1-3][0-9] [0-2][0-9]:[0-5][0-9]$' out > n
lines=`grep -c '^' out`
test `cat n` -eq $lines; echo "every line is name tty time $?"
grep "^`id -un` " out > /dev/null; echo "I am there $?"
n2=`who x | grep -c '^'`; test $n2 -eq $lines; echo "arguments ignored $?"
