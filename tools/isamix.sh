#!/bin/sh
# isamix.sh SESSION -- run a recorded session (software/tests/SESSION.in) on
# p32uemu, the real control store, from a fresh image, and stream its
# instruction trace (-I) through a FIFO into isamix: the instruction mix and
# the candidate ISA additions' savings (docs/isa-measurements.md). No trace
# file is written, so a session of any length fits.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/isamix.$$
mkdir -p $T
cc -O2 -I $R/emulator -o $T/isamix $R/tools/isamix.c
make -s -C $R/emulator p32uemu
cd $R/software
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
P32FS_TIME=1791936000 python3 ../tools/p32fs.py build $T/os.img 512 root.manifest --kernel ../kernel/unix.bin --inodes 16384 >/dev/null
mkfifo $T/trace
$T/isamix $T/trace > $T/mix &
../emulator/p32uemu -u ../microcode/p32ucode.bin -I $T/trace -w -R 1791936000 -l 12000000000 \
    -d $T/os.img $T/monitor.bin < tests/$1.in > /dev/null 2> $T/emu.err || true
wait
cat $T/mix
rm -rf $T
