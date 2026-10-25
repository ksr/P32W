# ed: long lines and substitutions, s escapes, W, r, a;b, the changed-buffer
# warnings and the exit status of a script
echo '== a line of 1400 characters'
echo 'a' > c
echo '0123456789' >> c
echo '.' >> c
echo 's/[0-9]/&&&&&&&&&&/g' >> c
echo 's/[0-9]/&&&&&&&/g' >> c
echo 'w long' >> c
echo 's/./&&/g' >> c
echo 'w' >> c
echo 'q' >> c
ed - < c
echo "exit $?"
wc -c long
echo '=' > c
echo 's/0*1*2*3*4*5*6*7*8*/</' >> c
echo 's/99999999999999999999/n/gp' >> c
echo 'Q' >> c
ed long < c
echo "exit $?"
echo '== escapes in the replacement'
echo 'a' > c
echo 'and' >> c
echo '.' >> c
echo 's/a/[\\&]/p' >> c
echo 's/n/\&/p' >> c
echo 's/d/\\/p' >> c
echo 's/\[/\//p' >> c
echo 's/&/<&&>/p' >> c
echo 'Q' >> c
ed - < c
echo "exit $?"
echo '== W appends, r sets dot, w keeps the name'
echo one > f1
echo two > f2
echo 'W f2' > c
echo 'w other' >> c
echo 'f' >> c
echo 'r f2' >> c
echo '.=' >> c
echo ',p' >> c
echo 'w' >> c
echo 'q' >> c
ed - f1 < c
echo "exit $?"
cat f2
cat f1
cat other
echo '== a;b'
echo 'a' > c
echo '1' >> c
echo '2' >> c
echo '3' >> c
echo '4' >> c
echo '5' >> c
echo '.' >> c
echo '1p' >> c
echo '3;+1p' >> c
echo '1p' >> c
echo '3,+1p' >> c
echo 'Q' >> c
ed - < c
echo "exit $?"
echo '== warnings and the exit status'
echo '1d' > c
ed - f1 < c
echo "exit $? (end of input on a changed buffer)"
echo '1d' > c
echo 'w' >> c
ed - other < c
echo "exit $?"
echo '$d' > c
echo '1w part' >> c
echo 'q' >> c
echo 'q' >> c
ed - f1 < c
echo "exit $? (only part written)"
cat part
echo '3,1p' > c
echo '100d' >> c
echo '9r f2' >> c
echo 's/zzz/y/' >> c
echo 'Q' >> c
ed - f1 < c
echo "exit $? (errors)"
echo '$p' > c
echo 'q' >> c
ed - f1 < c
echo "exit $?"
echo 'q' > c
ed nosuch < c
echo "exit $?"
