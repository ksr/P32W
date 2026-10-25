# true -- status 0, arguments ignored
true; echo "status $?"
true x y; echo "status $?"
true -x --help x; echo "status $?"
true > f; echo "status $?"; wc -c < f
if true; then echo yes; else echo no; fi
n=0
while true; do n=`expr $n + 1`; if test $n -eq 3; then break; fi; done; echo "loop $n"
false | true; echo "pipe $?"
