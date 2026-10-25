# redirection: > >> < 2> 2>&1 and pipes
T=/tmp/rd$$
echo first > $T
echo second >> $T
wc -l < $T
ls /no/such 2> $T.err
wc -l < $T.err
ls /no/such > $T.both 2>&1
wc -l < $T.both
cat $T | sort | head -1
rm -f $T $T.err $T.both
