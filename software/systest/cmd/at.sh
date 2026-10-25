# at, atq, atrm, and cron running a job: times and dates checked, the
# queue listed in time order, removal, a job run with the saved directory
# and environment, and its copy removed afterwards
TZ=UTC0; export TZ
echo '--- queueing (far in the future)'
echo 'echo one' | at 23:59 2037/12/31; echo "status $?"
echo 'echo two' | at 12:00 2030/01/01; echo "status $?"
echo 'echo three' | at 12:00 2030/01/01; echo "status $?"
echo 'echo four' | at 0:05 2031/02/28; echo "status $?"
me=`id -un`
atq > q
grep -c "  $me$" q
n=`grep -c '^' q`
i=1
while test $i -le $n
do
    line="`head -$i q | tail -1`"
    expr "$line" : '\(.*[0-9]\)  [a-z0-9]*$'
    i=`expr $i + 1`
done
echo '--- the job file'
cat /usr/spool/at/1893499200.1 > job
grep -c "^# at `id -u` `id -g` `pwd`$" job
grep '^# env TZ=' job
grep -c "^# env HOME=$HOME$" job
grep -c '^# umask [0-7][0-7][0-7]$' job
tail -1 job
ls -l /usr/spool/at/1893499200 | grep -c '^-rw------- '
echo '--- atrm'
atrm 1893499200.1; echo "status $?"
atrm 1893499200.1; echo "status $?"
atrm 2145916740 1893499200 1925164800; echo "status $?"
atq | grep -c .
atrm nosuch; echo "status $?"
atrm ../../etc/passwd; echo "status $?"
atrm; echo "status $?"
atq x; echo "status $?"
echo '--- bad times and dates'
at < /dev/null; echo "status $?"
at 25:00 < /dev/null; echo "status $?"
at 12:60 < /dev/null; echo "status $?"
at 12 < /dev/null; echo "status $?"
at 12:5 < /dev/null; echo "status $?"
at 1x:00 < /dev/null; echo "status $?"
at 12:00 13/01 < /dev/null; echo "status $?"
at 12:00 13/40 < /dev/null; echo "status $?"
at 12:00 2/30 < /dev/null; echo "status $?"
at 12:00 2029/02/29 < /dev/null; echo "status $?"
at 12:00 2038/01/01 < /dev/null; echo "status $?"
at 12:00 30/01/01 < /dev/null; echo "status $?"
at 12:00 12 < /dev/null; echo "status $?"
at 12:00 1/2/3/4 < /dev/null; echo "status $?"
at 12:00 2025/06/01 < /dev/null; echo "status $?"
at 12:00 2026/01/01 x < /dev/null; echo "status $?"
at now + < /dev/null; echo "status $?"
at now + 5 < /dev/null; echo "status $?"
at now + x minutes < /dev/null; echo "status $?"
at now + 5 weeks < /dev/null; echo "status $?"
at now + 5 minutes x < /dev/null; echo "status $?"
at now - 5 minutes < /dev/null; echo "status $?"
at tomorrow < /dev/null; echo "status $?"
atq | grep -c .
echo '--- a job run by cron (within a minute)'
mkdir sub
cd sub
TZ=JST-9; export TZ
echo 'pwd > ran; echo $TZ >> ran; echo $HOME >> ran; echo $USER >> ran' | at now > queued; echo "status $?"
q="`cat queued`"
job=`expr "$q" : 'job \([0-9.]*\) at '`
test -f /usr/spool/at/$job; echo "queued $?"
sleep 65
pwd > want; echo JST-9 >> want; echo $HOME >> want; echo $me >> want
cmp want ran; echo "ran here with my TZ, HOME and USER $?"
test -f /usr/spool/at/$job; echo "still queued $?"
test -f /tmp/at$job; echo "copy left in /tmp $?"
cd ..
atrm 1930003500; echo "status $?"
