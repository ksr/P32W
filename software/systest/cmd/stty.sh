# stty -- print the modes; set and unset them, and the terminal's size (each
# change is undone; the standard input must be a terminal, so some checks need one)
if test -t 0
then
    stty > before; echo "status $?"
    stty -echo; echo "status $?"; stty
    stty echo; echo "status $?"; stty
    stty -crmod; stty; stty crmod
    stty cbreak; stty; stty -cbreak
    stty raw -echo; stty; stty -raw echo
    stty cbreak raw -echo -crmod; stty; stty sane; stty
    stty bogus; echo "status $?"
    stty -echo bogus; echo "status $?"; stty
    stty size; stty rows 40 columns 132; stty size; stty
    stty cols 80; stty size; stty rows 0; stty; stty columns 0; stty size
    stty rows; echo "status $?"
    stty rows x; echo "status $?"
    stty columns 1000; echo "status $?"; stty size
    stty > after; cmp before after; echo "the same again $?"
else
    echo "(not a terminal: the mode changes are not tested)"
fi
stty < /dev/null; echo "status $?"
stty -echo < /dev/null; echo "status $?"
