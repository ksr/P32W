#!/bin/sh
# toolchain_test.sh -- the native as, ld and ar (M7, D24) on P32W Unix, in
# p32emu, against the host's p32asm / p32ld / p32ar: the compiler's output
# for a few commands is assembled on P32W, linked with /usr/lib/libc.a, run,
# and archived; every object, executable and archive must be byte for byte
# what the host tools make of the same input. Prints PASS / FAIL lines.
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32tool.$$
P=../tools/p32fs.py
mkdir -p $T/host
trap '[ -z "$KEEP" ] && rm -rf $T' EXIT
PROGS="echo wc sort"
for p in $PROGS; do
    python3 ../cc/p32cc.py -w -S -I include cmd/$p.c -o $T/$p.s || { echo "FAIL toolchain: p32cc $p"; exit 1; }
    python3 ../assembler/p32asm.py -c $T/$p.s -o $T/host/$p.o
    python3 ../assembler/p32ld.py build/crt0.o $T/host/$p.o build/libc.a -o $T/host/$p
done
python3 ../assembler/p32ar.py $T/host/my.a $T/host/echo.o $T/host/wc.o
# the spellings as reads quickly and those it reads the long way (D94)
cat > $T/forms.s <<'S'
; a comment line
        .text
        .global forms
forms:  add   r0, r9, r10
        add   r11, r12, r13
        add   r14, r15, sp
        add   R1, R15, SP
        add   zero, ZERO, r1
        add    r1 , r2 , r3    ; a comment after an instruction
        addi  r1, r2, 0
        addi  r1, r2, -0
        addi  r1, r2, -32768
        addi  r1, r2, 32767
        addi  r1, r2, 0x10
        addi  r1, r2, 0b101
        addi  r1, r2, 2 + 3
        addi  r1, r2, -(2)
        addi  r1, r2, 'a'
        lw    r1, -12(r2)
        lw    r1, 0(sp)
        ori   r1, r2, 65535
        li    r1, 123456789
        li    r1, 1234567890
        li    r1, 4294967295
        MOV   r1, r2
        mov   r1, r2
        .data
        .word 123456789, 1234567890, 4294967295, -1, 0x7fffffff
        .byte 59, ';', 'a'
S
python3 ../assembler/p32asm.py -c $T/forms.s -o $T/host/forms.o
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
cp build/test.img $T/os.img
python3 $P mkdir $T/os.img /tmp/m
for p in $PROGS forms; do python3 $P put $T/os.img $T/$p.s /tmp/m/$p.s; done
cat > $T/in <<'IN'
b
root
root
cd /tmp/m
time as echo.s
as wc.s
as forms.s
time as sort.s
time ld -o echo /usr/lib/crt0.o echo.o /usr/lib/libc.a
ld -o wc /usr/lib/crt0.o wc.o /usr/lib/libc.a
ld -o sort /usr/lib/crt0.o sort.o /usr/lib/libc.a
./echo built and linked on P32W
ls /bin | ./wc -l
ar my.a echo.o wc.o
ar -t my.a
halt
IN
../emulator/p32emu -w -R 1791936000 -l 40000000000 -d $T/os.img $T/monitor.bin < $T/in 2>&1 | tr -d '\r' > $T/log
st=0
same=""; diff=""
for f in echo.o wc.o sort.o forms.o echo wc sort my.a; do
    python3 $P cat $T/os.img /tmp/m/$f > $T/$f 2>/dev/null
    if cmp -s $T/$f $T/host/$f; then same="$same $f"; else diff="$diff $f"; fi
done
if [ -z "$diff" ]; then
    echo "PASS native as / ld / ar on P32W: objects, executables and an archive identical to the host's ($(echo $same | wc -w | tr -d ' ') files)"
else
    echo "FAIL native toolchain: differ:$diff"; st=1
fi
if grep -q "^built and linked on P32W" $T/log; then echo "PASS a program assembled and linked on P32W runs there"
else echo "FAIL the natively built echo did not run: $(grep -A2 'echo built' $T/log | head -3 | tr '\n' ' ')"; st=1; fi
# timings (information)
grep -A4 "^# time" $T/log | grep -E "^# time|^real" | paste - - | sed 's/^# time /  time: /' >&2
exit $st
