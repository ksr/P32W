# whoami -- the effective user's name, as /etc/passwd has it
me=`whoami`; echo "status $?"
grep "^$me:[^:]*:`id -u`:" /etc/passwd > /dev/null; echo "in passwd with my uid $?"
test "$me" = "`id -un`"; echo "same as id -un $?"
whoami | wc -l
whoami x y | wc -l
