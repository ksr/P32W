# time -- the report's form (the figures vary), the command's status,
# signals, errors
time true 2> err; echo "status $?"
wc -l < err
head -1 err | wc -c
grep -c '^real  *[0-9][0-9]*\.[0-9][0-9]$' err
grep -c '^user  *[0-9][0-9]*\.[0-9][0-9]$' err
grep -c '^sys  *[0-9][0-9]*\.[0-9][0-9]$' err
time echo hello 2> err; echo "status $?"
time false 2> err; echo "status $?"
time sh -c 'exit 3' 2> err; echo "status $?"
time sh -c 'kill -9 $$' 2> err; echo "status $?"
time sh -c 'echo to stdout; echo to stderr 1>&2' > out 2> err; echo "status $?"
cat out
head -1 err
time nosuchcommand 2> err; echo "status $?"
head -1 err
time ls nosuchfile > out 2> err; echo "status $?"
time time true 2> err; echo "status $?"
wc -l < err
echo '--- usage'
time; echo "status $?"
echo '--- -l: the memory events (three lines more; the counts vary with the library)'
time -l true 2> err; echo "status $?"
wc -l < err
grep -c 'TLB refills' err
grep -c 'copy-on-write page copies' err
grep -c 'stack pages added' err
