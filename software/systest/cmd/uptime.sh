# uptime -- the form of the line (its figures vary)
uptime > out; echo "status $?"
wc -l < out
grep -c '^ [0-2][0-9]:[0-5][0-9]:[0-5][0-9] up ' out
grep -c ' up .*[ 1-2][0-9]:[0-5][0-9], ' out
grep -c ', [0-9][0-9]* users*,  load average: [0-9]*\.[0-9][0-9], [0-9]*\.[0-9][0-9], [0-9]*\.[0-9][0-9]$' out
n=`who | grep -c '^'`
u=users
if test $n -eq 1; then u=user; fi
grep -c ", $n $u, " out
uptime x | wc -l
