# echo -- arguments, blanks, -n (only first), what is printed as typed
echo
echo a
echo a b   c
echo 'a   b'
echo '' x ''
echo -n abc; echo '<'
echo -n; echo '<'
echo -n -n x; echo '<'
echo x -n
echo -- -n x
echo '\n' '\t' '\c' '\\'
echo -e x
echo -
echo --help x
echo a > f; echo b >> f; cat f
echo "status $?"
