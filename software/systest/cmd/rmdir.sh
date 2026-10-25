# rmdir: empty directories, errors, -p
mkdir a b c
rmdir a; echo "status $?"; ls
rmdir b c; echo "status $?"; ls
echo "-- errors"
mkdir d; echo x > d/x
rmdir d; echo "not empty: status $?"
rmdir d/x; echo "a file: status $?"
rmdir nosuch; echo "status $?"
rmdir nosuch d e; echo "status $?"
mkdir e
rmdir nosuch e; echo "the next is removed: status $?"; ls
rmdir; echo "status $?"
rmdir -x d; echo "status $?"
echo "-- -p"
mkdir p; mkdir p/q; mkdir p/q/r
rmdir -p p/q/r; echo "status $?"; ls
mkdir p; mkdir p/q; mkdir p/q/r; echo x > p/x
rmdir -p p/q/r/; echo "p not empty: status $?"; ls; ls p
rm p/x
mkdir p/q
rmdir -p p//q; echo "status $?"; ls
mkdir -- -m
rmdir -- -m; echo "-- : status $?"; ls
