# control flow: if / elif / else, while, for, && and ||, test and expr
n=0
while test $n -lt 5
do
    n=`expr $n + 1`
    if test $n -eq 2; then echo two
    elif test $n -eq 4; then echo four
    else echo "n=$n"
    fi
done
for f in x y; do echo "for $f"; done
true && echo and-ran
false || echo or-ran
false && echo never
test -d /usr/examples && echo examples-there
# a pipeline's status is its last command's, whichever ends first (D87)
(sleep 1; exit 3) | true; echo "first ends last: $?"
true | (sleep 1; exit 3); echo "last ends last: $?"
(sleep 1; exit 3) | (exit 7); echo "the last ends first: $?"
