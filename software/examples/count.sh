# count.sh -- a shell script: run it with  sh count.sh a b c
# (variables, a for loop, command substitution with expr, if and test)
n=0
for f in $*
do
    n=`expr $n + 1`
    echo "$n: $f"
done
if test $n -eq 0
then
    echo "no arguments; try: sh count.sh one two three"
else
    echo "$n arguments"
fi
