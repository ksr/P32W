#!/bin/sh
# cycles_test.sh -- the measuring gate on the running system (D85): P32W Unix
# in p32uemu -G, measuring with cycles(1). Two independent clocks must agree:
# the emulator's count of the machine's cycles and the system's own, the 50 Hz
# ticks that cycles prints as real time. A sleep between cycles start and
# cycles stop must show as idle. Prints PASS / FAIL lines.
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32cycles.$$
mkdir -p $T
trap '[ -z "$KEEP" ] && rm -rf $T' EXIT
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
cp build/test.img $T/os.img
printf 'b\nroot\nroot\ncycles ls -l /bin\ncycles start\nsleep 1\ncycles stop\nhalt\n' > $T/in
../emulator/p32uemu -u ../microcode/p32ucode.bin -G -w -R 1791936000 -l 2000000000 -d $T/os.img \
    $T/monitor.bin < $T/in > $T/out 2> $T/err
python3 -I - $T/out $T/err <<'PY'
import re, sys
out, err = open(sys.argv[1]).read(), open(sys.argv[2]).read()
num = lambda s: int(s.replace(",", ""))
counts = re.findall(r"\[cycles #\d+: ([\d,]+) cycles, \S+ at 3 MHz \(([\d.]+) s\)\n"
                    r" ([\d,]+) instructions, [\d.]+ MIPS busy; idle ([\d,]+) cycles \([\d.]+%\)\n"
                    r" disk ([\d,]+) sectors read, ([\d,]+) written, ([\d,]+) commands; console ([\d,]+) bytes\]", err)
real = re.search(r"^real +([\d.]+)", out, re.M)
if len(counts) != 2 or not real or "HALT" not in err:
    sys.exit(print("FAIL gate: expected two counts and a clean halt: %s" % err[-200:]))
secs, r = float(counts[0][1]), float(real.group(1))
if abs(secs - r) > 0.04:
    sys.exit(print("FAIL gate: the count says %.3f s, the system's clock %.2f s" % (secs, r)))
if not num(counts[0][4]) or not num(counts[0][7]):
    sys.exit(print("FAIL gate: no disk sectors or console bytes counted: %s" % (counts[0],)))
if num(counts[1][3]) < 2500000:
    sys.exit(print("FAIL gate: sleep 1 idled only %s cycles" % counts[1][3]))
print("PASS cycles(1) in p32uemu -G: the count %.3f s, the system's clock %.2f s; a 1 s sleep idle %s cycles"
      % (secs, r, counts[1][3]))
PY
# the profile (D88): cc compiling, by function, adding up to the count, and
# naming the static functions the link map leaves out
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
cp build/test.img $T/prof.img
printf 'b\nroot\nroot\ncycles cc -c -o /tmp/h.o /usr/examples/hello.c\nhalt\n' > $T/in
python3 ../assembler/p32ld.py build/crt0.o build/cc/*.o build/libc.a -M $T/cc.map -o $T/cc.x
../emulator/p32uemu -u ../microcode/p32ucode.bin -G -q $T/prof -w -R 1791936000 -l 2000000000 -d $T/prof.img \
    $T/monitor.bin < $T/in > $T/out 2> $T/err
python3 ../tools/uprof.py $T/prof $T/cc.map -n 400 > $T/report
python3 -I - $T/err $T/report <<'PY'
import re, sys
err, rep = open(sys.argv[1]).read(), open(sys.argv[2]).read()
count = int(re.search(r"\[cycles #1: ([\d,]+) cycles", err).group(1).replace(",", ""))
total = int(re.search(r"cycles: (\d+) in all", rep).group(1))
if abs(total - count) > count / 1000:
    sys.exit(print("FAIL profile: it adds up to %d, the count is %d" % (total, count)))
if not re.search(r"^tokenize ", rep, re.M) or not re.search(r"^strcmp ", rep, re.M):
    sys.exit(print("FAIL profile: no static function or no libc function named:\n%s" % rep[:300]))
print("PASS uprof: cc's profile adds up to the count (%d cycles) and names static functions" % count)
PY
