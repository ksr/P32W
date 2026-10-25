# ps -- the heading, -f, -u, -t, the states, errors (pids and sizes vary)
ps > out; echo "status $?"
head -1 out
grep -c '^ *1  *0 S  *0  *? .* init$' out
grep -c ' R .* ps$' out
ps -f > out; echo "status $?"
head -1 out
grep -c '^ *1  *0 S root  *? .* init$' out
me=`id -un`
ps -f -u $me > out; echo "status $?"
n=`grep -c " $me " out`; t=`grep -c '^' out`; test $n -eq `expr $t - 1`; echo "only mine $?"
grep -c ' ps$' out
ps -u `id -u` | grep -c ' init$'
ps -u root | grep -c ' init$'
ps -u 0 | grep -c ' init$'
ps -t 7 | wc -l
ps -t 0 | grep -c ' init$'
echo '--- states'
n=`ps -u $me | grep -c ' S .* sh$'`; test $n -ge 1; echo "a sleeping sh $?"
ps -u $me | grep -c ' R .* ps$'
m=`sh -c "ps -u $me" | grep -c ' S .* sh$'`; test $m -gt $n; echo "one more sh $?"
echo '--- errors'
ps -u nosuchuser; echo "status $?"
ps -u 12x; echo "status $?"
ps -t x; echo "status $?"
ps -u; echo "status $?"
ps -t; echo "status $?"
ps -x; echo "status $?"
ps aux; echo "status $?"
