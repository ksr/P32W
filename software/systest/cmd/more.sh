# more: when the output is not a terminal it copies like cat, with no
# --More-- prompts, however long the input and whatever LINES says.
# (Paging at a terminal is not tested here.)
for i in 1 2 3 4 5 6 7 8 9 10
do
    echo line $i >> a
    echo line $i >> a
    echo line $i >> a
done
echo -n end >> a
echo short > b
echo "== one file"
more a > out
echo "status $?"
cmp a out
wc out
echo "== several files, and LINES set small"
LINES=5
export LINES
more a b > out
echo "status $?"
cat a b | cmp - out
echo "== the standard input"
cat b | more
echo "status $?"
more < a | wc -l
echo "== in a pipe"
more a b | tail -2
echo "== a file that cannot be opened is reported and skipped"
more nosuch b
echo "status $?"
