# mail: sending to oneself, reading with commands, -f, -e, >From, errors.
# The user's own mailbox is used only if it is empty (and left empty).
# The From lines' names and dates are replaced by ME and DATE.
me=`whoami`
N="s/From $me [A-Z][a-z][a-z] .*/From ME DATE/"
if mail -e
then
    echo "(you have mail: sending and reading are not tested)"
else
    mail > o; echo "no mail: status $?"; cat o
    echo hello | mail $me; echo "sent: status $?"
    mail -e; echo "-e: $?"
    (echo second; echo 'From the start'; echo '>From before'; echo ' From not'; echo .; echo 'after the dot') | mail $me; echo "sent: status $?"
    (echo p; echo -; echo +; echo -; echo x) | mail > o; echo "p - + - x: status $?"; sed -e "$N" o
    (echo s saved; echo q) | mail > o; echo "s saved, q: status $?"; sed -e "$N" o
    echo '--- saved'; sed -e "$N" saved
    (echo w body) | mail > o; echo "w body (the last: quits): status $?"; sed -e "$N" o
    echo '--- body'; cat body
    mail -e; echo "-e: $?"
    mail > o; echo "status $?"; cat o
fi
echo '--- -f'
(echo 'From a Wed Oct 14 00:00:00 2026'; echo one; echo; echo 'From b Wed Oct 14 00:00:01 2026'; echo two; echo '>From x'; echo; echo 'From c Thu Oct 15 00:00:00 2026'; echo three; echo) > box
(echo d; echo n; echo q) | mail -f box > o; echo "d n q: status $?"; cat o
echo '--- box'; cat box
(echo 'w out1 out2'; echo x) | mail -f box > o; echo "w out1 out2, x: status $?"; cat o
echo '--- out1'; cat out1; echo '--- out2'; cat out2; echo '--- box'; cat box
(echo '!echo from the shell'; echo zz; echo h; echo x) | mail -f box > o; echo "! zz h x: status $?"; cat o
mail -f box < /dev/null > o; echo "end of input: status $?"; cat o
echo '--- not my mailbox'
(echo 's /usr/spool/mail/root'; echo x) | mail -f box > o 2>&1; echo "status $?"; cat o
mail -f /usr/spool/mail/root < /dev/null > o 2>&1; echo "status $?"; cat o
echo '--- errors'
mail -f nosuch < /dev/null; echo "status $?"
HOME=. mail -f < /dev/null; echo "no mbox: status $?"
echo lost | HOME=. mail nosuchuser; echo "status $?"
cat dead.letter
mail -z; echo "status $?"
mail -f a b; echo "status $?"
mail -e x; echo "status $?"
