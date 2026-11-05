#!/bin/sh
# kernel_test.sh -- the kernel built on P32W itself (D86): make in
# /usr/src/kernel, in p32emu, must give a unix.bin byte for byte the host's,
# and its link map the host's but for the objects' paths. A touched source
# must rebuild that file and the link, nothing else. Prints PASS / FAIL lines.
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32kernel.$$
P=../tools/p32fs.py
mkdir -p $T
trap '[ -z "$KEEP" ] && rm -rf $T' EXIT
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
# (the disk's files stamped as the clock starts, -R: make compares their times)
P32FS_TIME=1791936000 python3 $P build $T/os.img 512 root.manifest --kernel ../kernel/unix.bin --inodes 16384 > /dev/null
printf 'b\nroot\nroot\ncd /usr/src/kernel\nmake\nmake\ntouch rtc.c\nmake\nsync\nhalt\n' > $T/in
../emulator/p32emu -w -R 1791936000 -l 4000000000 -d $T/os.img $T/monitor.bin < $T/in > $T/out 2> $T/err
python3 $P cat $T/os.img /usr/src/kernel/unix.bin > $T/unix.bin
python3 $P cat $T/os.img /usr/src/kernel/unix.map > $T/unix.map
if ! cmp -s $T/unix.bin ../kernel/unix.bin; then
    echo "FAIL kernel built on P32W: unix.bin differs from the host's"; tail -3 $T/out; exit 1
fi
sed 's/build\///' ../kernel/unix.map | tr -s ' ' > $T/host.map
tr -s ' ' < $T/unix.map > $T/native.map
if ! cmp -s $T/host.map $T/native.map; then echo "FAIL kernel built on P32W: the link map differs"; exit 1; fi
python3 -I - $T/out <<'PY'
import re, sys
t = open(sys.argv[1]).read().replace("\r", "")
parts = re.split(r"^# make\n", t, flags=re.M)
if len(parts) != 4:
    sys.exit(print("FAIL kernel built on P32W: expected three makes, got %d" % (len(parts) - 1)))
first, second, third = [re.split(r"^# ", p, flags=re.M)[0] for p in parts[1:]]
if first.count("cc -w") != 18 or "ld --format bin" not in first:
    sys.exit(print("FAIL kernel built on P32W: the first make: %s" % first[-200:]))
if "cc -w" in second or "ld " in second:
    sys.exit(print("FAIL kernel built on P32W: a second make rebuilt something: %s" % second[:200]))
if third.count("cc -w") != 1 or "rtc.c" not in third or "ld --format bin" not in third:
    sys.exit(print("FAIL kernel built on P32W: a touched file: %s" % third[:200]))
print("PASS the kernel built on P32W: 18 files, unix.bin byte for byte the host's; one touched file rebuilds alone")
PY
