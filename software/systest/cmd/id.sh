# id, -u -g -n -r, usage (run as an ordinary user: the names and ids are
# compared with what whoami and /etc/passwd say, not printed)
me=`whoami`
uid=`id -u`
test "`id -un`" = "$me"; echo "id -un is whoami $?"
test "`id -run`" = "$me"; echo "id -run is whoami $?"
test "`id -ur`" = "$uid"; echo "id -ur is id -u $?"
grep "^$me:[^:]*:$uid:`id -g`:" /etc/passwd > /dev/null; echo "passwd has name:uid:gid $?"
grep "^`id -gn`::`id -g`:" /etc/group > /dev/null; echo "group has name:gid $?"
test "`id`" = "uid=$uid($me) gid=`id -g`(`id -gn`)"; echo "id is uid=(name) gid=(group) $?"
test "`id -gr`" = "`id -g`"; echo "id -gr is id -g $?"
expr "$uid" : '[0-9][0-9]*$' > /dev/null; echo "uid is a number $?"
test $uid -ne 0; echo "not root $?"
echo '--- usage'
id -x; echo "status $?"
id -u -g; echo "status $?"
id -ug; echo "status $?"
id -n; echo "status $?"
id -r; echo "status $?"
id x; echo "status $?"
id -; echo "status $?"
