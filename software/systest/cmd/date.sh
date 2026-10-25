# date -- printing a given time (-r) in UTC and in zones, +format fields,
# and the checks on a new setting (the test is not root: setting fails)
echo '--- -r and -u'
date -u -r 0
date -u -r 1791936000
date -u -r 2147483647
date -r 0 -u
TZ=UTC0; export TZ
date -r 1791936000
TZ=PST8PDT,M3.2.0,M11.1.0; export TZ
date -r 1791936000
date -r 1797000000
date -u -r 1791936000
TZ=JST-9; export TZ
date -r 1791936000
echo '--- the current time'
TZ=UTC0; export TZ
date | wc -w
date -u | wc -w
TZ=JST-9; export TZ
date | wc -w
date +%Y | wc -c
date -u '+%H:%M:%S' | wc -c
echo '--- +format'
TZ=UTC0; export TZ
date -r 1791936000 '+%Y-%m-%d %H:%M:%S'
date -u -r 1791993845 '+%a %A %b %B %h %d %e %j'
date -u -r 1791993845 '+%H %I %k %l %M %S %p %y %C %w %u'
date -u -r 1791993845 '+%D %F %T %R %r %s'
date -u -r 1791993845 '+%c|%x|%X|%Z'
date -u -r 1775050200 '+%e %l %p %j %u'
date -u -r 0 '+%%%t%%%n%q %'
date -u -r 0 +
date -u -r 0 '+literal text'
TZ=PST8PDT,M3.2.0,M11.1.0; export TZ
date -r 1791936000 '+%H:%M %Z'
date -r 1797000000 '+%H:%M %Z'
date -u -r 1791936000 '+%H:%M %Z'
echo '--- errors'
date -s junk; echo "status $?"
date -s 12x; echo "status $?"
date -s 99999999999; echo "status $?"
date -s ''; echo "status $?"
date -s; echo "status $?"
date -r x; echo "status $?"
date -r -1; echo "status $?"
date -x; echo "status $?"
date junk; echo "status $?"
date +%Y +%m; echo "status $?"
date -r 0 -s 0; echo "status $?"
date +%Y 2610141230; echo "status $?"
date 26101412; echo "status $?"
date 2610141230.6; echo "status $?"
date 2610141230.x5; echo "status $?"
date 2602310000; echo "status $?"
date 2502290000; echo "status $?"
date 2604310000; echo "status $?"
date 2613010000; echo "status $?"
date 2600010000; echo "status $?"
date 2610142400; echo "status $?"
date 2610141260; echo "status $?"
date 2610141230.60; echo "status $?"
date 196912312359; echo "status $?"
date 203801010000; echo "status $?"
echo '--- valid, but only root may set the clock'
date 2802290000 > /dev/null; echo "status $?"
date -u 2610141230.59 > /dev/null; echo "status $?"
date 197001010000 > /dev/null; echo "status $?"
date -s 0 > /dev/null; echo "status $?"
TZ=JST-9; export TZ
date 197001010000; echo "status $?"
date -u 197001010000 > /dev/null; echo "status $?"
