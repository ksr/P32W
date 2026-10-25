# cmp: the first difference, -l every difference, -s only the status
echo hello > a
echo world >> a
cp a same
echo hello > b
echo wOrlD >> b
echo hello > short
echo "== the same"
cmp a same
echo "status $?"
echo "== different"
cmp a b
echo "status $?"
echo "== one is a leading part of the other"
cmp a short
echo "status $?"
cmp short a
echo "status $?"
echo "== -l: each differing byte (number, then the two bytes in octal)"
cmp -l a b
echo "status $?"
cmp -l a same
echo "status $?"
cmp -l a short
echo "status $?"
echo "== -s: nothing printed"
cmp -s a same
echo "status $?"
cmp -s a b
echo "status $?"
cmp -s a short
echo "status $?"
echo "== - is the standard input"
cat a | cmp - same
echo "status $?"
cat b | cmp a -
echo "status $?"
echo "== empty files"
echo -n > e1
echo -n > e2
cmp e1 e2
echo "status $?"
cmp e1 a
echo "status $?"
echo "== errors"
cmp a
echo "status $?"
cmp a b c
echo "status $?"
cmp -x a b
echo "status $?"
cmp -ls a b
echo "status $?"
cmp a nosuch
echo "status $?"
cmp -s nosuch a
echo "status $?"
