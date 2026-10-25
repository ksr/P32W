# uname -- the fields, and combinations of options
uname
uname -s
uname -n
uname -m
uname -sn
uname -s -m
uname -ms
uname -nsm
uname -a | wc -w
test "`uname -a`" = "`uname -snrvm`"; echo "-a is -snrvm $?"
test "`uname -a`" = "`uname -s` `uname -n` `uname -r` `uname -v` `uname -m`"; echo "-a is the five $?"
uname -r | grep -c '^[0-9][0-9]*\.[0-9][0-9]*$'
uname -v | grep -c '^M[0-9][0-9]*$'
echo '--- usage'
uname -x; echo "status $?"
uname x; echo "status $?"
uname -; echo "status $?"
uname -s -q; echo "status $?"
