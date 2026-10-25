#!/bin/sh
# p32try.sh IMAGE INPUT -- boot a copy of a P32W Unix disk image in p32emu and
# feed INPUT a line per prompt (-w), printing the session. For trying
# changes on a private image: copy build/test.img, put new programs and
# test scripts into it with tools/p32fs.py, then run this. The copy is
# thrown away; IMAGE itself is not changed.
set -e
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32try.$$
mkdir -p $T
cp "$1" $T/os.img
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
../emulator/p32emu -w -R 1791936000 -l 4000000000 -d $T/os.img $T/monitor.bin < "$2" 2>/dev/null | tr -d '\r'
rm -rf $T
