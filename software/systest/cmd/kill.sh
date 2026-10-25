# kill -- signals by name and number, -l, -s, -0, several pids, errors
kill -l
kill -l 1; kill -l 9; kill -l 15
kill -l 137; echo "status $?"
kill -l 143 130
kill -l 0; echo "status $?"
kill -l 16; echo "status $?"
kill -l x; echo "status $?"
echo '--- signal 0: does it exist?'
kill -0 $$; echo "status $?"
kill -s 0 $$; echo "status $?"
kill -0 99999; echo "status $?"
kill -0 1; echo "status $?"
echo '--- names'
kill -HUP 99999; echo "status $?"
kill -SIGHUP 99999; echo "status $?"
kill -hup 99999; echo "status $?"
kill -sigterm 99999; echo "status $?"
kill -s KILL 99999; echo "status $?"
kill -s 9 99999; echo "status $?"
kill -- 99999; echo "status $?"
kill -15 -- 99999; echo "status $?"
echo '--- bad signals'
kill -16 99999; echo "status $?"
kill -99 99999; echo "status $?"
kill -FOO 99999; echo "status $?"
kill -SIG 99999; echo "status $?"
kill -s FOO 99999; echo "status $?"
kill - 99999; echo "status $?"
echo '--- usage and pids'
kill; echo "status $?"
kill -9; echo "status $?"
kill -s; echo "status $?"
kill -s 9; echo "status $?"
kill abc; echo "status $?"
kill 12x; echo "status $?"
kill 1; echo "status $?"
kill -0 $$ 99999 $$; echo "status $?"
echo '--- a real kill'
sh -c 'kill -0 $$ && echo alive'
sh -c 'kill $$; echo not reached'; echo "status $?"
sh -c 'kill -9 $$; echo not reached'; echo "status $?"
sh -c 'kill -s INT $$; echo not reached'; echo "status $?"
sh -c 'kill -HUP $$ $$; echo not reached'; echo "status $?"
