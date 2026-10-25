# write: the refusals and errors (a message itself would appear on someone's
# terminal, not here); names are replaced by ME
me=`whoami`
write > o 2>&1; s=$?; cat o; echo "status $s"
write a tty0 x > o 2>&1; s=$?; cat o; echo "status $s"
write -x > o 2>&1; s=$?; cat o; echo "status $s"
write nosuchuser > o 2>&1; s=$?; cat o; echo "status $s"
write $me tty > o 2>&1; s=$?; sed "s/$me/ME/" o; echo "status $s"
write $me tty12 > o 2>&1; s=$?; sed "s/$me/ME/" o; echo "status $s"
write $me ttyx > o 2>&1; s=$?; sed "s/$me/ME/" o; echo "status $s"
write $me tty9 > o 2>&1; s=$?; sed "s/$me/ME/" o; echo "status $s"
echo '--- messages disabled'
mesg > /dev/null 2>&1
was=$?
if test -t 0 -a $was -lt 2 -a `who | grep -c "^$me "` -eq 1
then
    mesg n
    echo hello | write $me > o 2>&1; s=$?; sed "s/$me/ME/" o; echo "status $s"
    if test $was -eq 0; then mesg y; fi
else
    echo "(not logged in once on this terminal: not tested)"
fi
