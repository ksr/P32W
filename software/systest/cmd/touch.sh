# touch: create, update the time, -c, errors
touch a; echo "status $?"; ls; test -s a || echo "a is empty"
touch b c d; echo "status $?"; ls
echo "-- an existing file gets the time now; its contents stay"
echo old > old
sleep 1
echo new > new
sleep 1
for f in `ls -t old new`; do echo $f; done
touch old; echo "status $?"; cat old
for f in `ls -t old new`; do echo $f; done
echo "-- without write permission, the owner may still touch it"
chmod 444 old
touch old; echo "status $?"
chmod 644 old
echo "-- -c"
touch -c nosuch; echo "status $?"; ls
sleep 1
touch -c new; echo "status $?"
for f in `ls -t old new`; do echo $f; done
echo "-- errors"
touch nodir/x; echo "status $?"
touch; echo "status $?"
touch -z x; echo "status $?"
touch nodir/x e; echo "the next is made: status $?"; ls
touch -- -x; echo "-- : status $?"; ls
