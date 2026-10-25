# mesg: report, y, n (each change is undone; the standard input must be
# one's own terminal, so most checks need one)
mesg > /dev/null 2>&1
was=$?
if test -t 0 -a $was -lt 2
then
    mesg y; echo "y: status $?"
    mesg; echo "status $?"
    mesg n; echo "n: status $?"
    mesg; echo "status $?"
    mesg n; echo "n again: status $?"
    mesg y; echo "y again: status $?"
        if test $was -eq 0; then mesg y; else mesg n; fi
else
    echo "(not on a terminal of one's own: y and n are not tested)"
fi
echo '--- errors'
mesg < /dev/null; echo "status $?"
mesg y < /dev/null; echo "status $?"
mesg x; echo "status $?"
mesg y n; echo "status $?"
mesg -n; echo "status $?"
