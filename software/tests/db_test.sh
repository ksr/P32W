#!/bin/sh
# db_test.sh -- the debugger on P32W Unix (p32emu): a program compiled there
# by cc, a breakpoint, a backtrace, finish, stepping, a variable, to the end.
# Checks the facts (addresses move as libc changes). Prints PASS / FAIL.
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32db.$$
mkdir -p $T
trap '[ -z "$KEEP" ] && rm -rf $T' EXIT
cat > $T/t.c <<'C'
#include <stdio.h>

int total;

int square(int x)
{
    return x * x;
}

int main(void)
{
    int i;
    for (i = 1; i <= 3; i++) total += square(i);
    printf("total %d\n", total);
    return 0;
}
C
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
cp build/test.img $T/os.img
python3 ../tools/p32fs.py put $T/os.img $T/t.c /tmp/t.c
printf 'b\nroot\nroot\ncd /tmp\ncc -o t t.c\ndb t\nb square\nc\nbt\nf\np total\nc\np total\nd\nc\nq\nhalt\n' > $T/in
../emulator/p32emu -w -P '# ' -P 'db> ' -P 'login: ' -P 'Password: ' -P '> ' -R 1791936000 -l 4000000000 \
    -d $T/os.img $T/monitor.bin < $T/in 2>&1 | tr -d '\r' > $T/log
bad=""
grep -q "^stopped: 0x[0-9a-f]* <square>" $T/log || bad="$bad breakpoint"
grep -q "^#1  0x[0-9a-f]* <main+" $T/log || bad="$bad backtrace"
grep -q "^returned to: 0x[0-9a-f]* <main+" $T/log || bad="$bad finish"
grep -q "^total = 0x[0-9a-f]*: 00000000 (0)" $T/log || bad="$bad variable-before"
grep -q "^total = 0x[0-9a-f]*: 00000001 (1)" $T/log || bad="$bad variable-after"
grep -q "^total 14" $T/log || bad="$bad output"
grep -q "^the program exited, status 0" $T/log || bad="$bad exit"
if [ -z "$bad" ]; then echo "PASS db: a breakpoint, backtrace, finish, a variable before and after, the program to its end (compiled on P32W)"
else echo "FAIL db:$bad"; [ -n "$KEEP" ] && echo "(log: $T/log)"; exit 1; fi
