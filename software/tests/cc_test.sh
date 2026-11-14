#!/bin/sh
# cc_test.sh -- the C compiler in C (M7, D24; software/cc) against p32cc.py:
#   1. built on the Mac, it writes the same assembly (-S) as p32cc for every
#      C file of the system (libc, commands, kernel, compiler tests, itself),
#      and the same preprocessed text (-E) for a sample;
#   2. on P32W Unix (p32emu), cc compiles, assembles and links a program that
#      then runs;
#   3. self-hosting: on P32W Unix, cc compiles its own source, and the result
#      is byte for byte the cc that the host tools built.
# Prints PASS / FAIL lines.
cd "$(dirname "$0")/.."
R=$(cd .. && pwd)
T=${TMPDIR:-/tmp}/p32cc.$$
P=../tools/p32fs.py
mkdir -p $T
trap '[ -z "$KEEP" ] && rm -rf $T' EXIT
st=0
# ---- 1. on the Mac
if ! cc -std=c11 -w -o $T/cc-host cc/*.c 2>$T/err; then
    echo "FAIL cc: the host build: $(head -2 $T/err)"; exit 1
fi
n=0; bad=""
for c in libc/*.c cmd/*.c ../cc/tests/*.c ../kernel/*.c cc/*.c libgfx/*.c; do
    inc="-I include"; case $c in ../kernel/*) inc="$inc -I ../kernel";; esac
    b=$(echo $c | tr '/.' '__')
    python3 ../cc/p32cc.py -w -S $inc $c -o $T/$b.py.s 2>/dev/null || continue
    n=$((n + 1))
    $T/cc-host -w -S $inc $c -o $T/$b.c.s 2>/dev/null
    cmp -s $T/$b.py.s $T/$b.c.s || bad="$bad $c"
done
for c in cmd/sh.c cc/cpp.c ../kernel/fs.c; do
    inc="-I include -I ../kernel"; b=$(echo $c | tr '/.' '__')
    python3 ../cc/p32cc.py -w -E $inc $c -o $T/$b.py.i 2>/dev/null
    $T/cc-host -w -E $inc $c -o $T/$b.c.i 2>/dev/null
    cmp -s $T/$b.py.i $T/$b.c.i || bad="$bad (-E)$c"
done
printf 'int a[] = { 1 X, Y, Z };\n' > $T/defs.c         # -D: empty, 1, a value
python3 ../cc/p32cc.py -E -DX= -DY -DZ=3 $T/defs.c -o $T/defs.py.i 2>/dev/null
$T/cc-host -E -DX= -DY -DZ=3 $T/defs.c -o $T/defs.c.i 2>/dev/null
grep -q '1 , 1 , 3' $T/defs.c.i && cmp -s $T/defs.py.i $T/defs.c.i || bad="$bad (-D)"
if [ -z "$bad" ]; then echo "PASS cc in C: the same assembly as p32cc for all $n C files of the system (and the same -E text)"
else echo "FAIL cc in C differs from p32cc:$bad"; st=1; fi
# ---- 2 and 3. on P32W Unix
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
cp build/test.img $T/os.img
python3 $P mkdir $T/os.img /tmp/c
for f in cc/*.c cc/cc.h; do python3 $P put $T/os.img $f /tmp/c/$(basename $f); done
cat > $T/hello.c <<'EOF'
#include <stdio.h>
#include <string.h>

struct pt { int x, y; };
static int sq(int v) { return v * v; }

int main(int argc, char **argv)
{
    struct pt p[3];
    int i, s = 0;
    for (i = 0; i < 3; i++) { p[i].x = i + 1; p[i].y = sq(i + 2); }
    for (i = 0; i < 3; i++) s += p[i].x * p[i].y;
    printf("compiled on P32W: %d %s\n", s, argc > 1 ? argv[1] : "");
    return strlen(argv[0]) > 0 ? 0 : 1;
}
EOF
python3 $P put $T/os.img $T/hello.c /tmp/c/hello.c
cat > $T/in <<'IN'
b
root
root
cd /tmp/c
cc -o hello hello.c && ./hello natively
cc -o cc2 cpp.c gen.c lex.c main.c parse.c type.c
cmp cc2 /bin/cc && echo SELF-HOSTED
halt
IN
../emulator/p32emu -w -R 1791936000 -l 2000000000000 -d $T/os.img $T/monitor.bin < $T/in 2>&1 | tr -d '\r' > $T/log
# (1*4 + 2*9 + 3*16 = 70)
if grep -q "^compiled on P32W: 70 natively" $T/log; then echo "PASS cc on P32W: a program compiled, assembled and linked there runs there"
else echo "FAIL cc on P32W: $(grep -A3 'cc -o hello' $T/log | head -4 | tr '\n' ' ')"; st=1; fi
if grep -q "^SELF-HOSTED" $T/log; then echo "PASS self-hosting: on P32W, cc compiles its own source into a cc byte for byte the host-built one"
else echo "FAIL self-hosting: $(grep -A3 'cc -o cc2' $T/log | head -4 | tr '\n' ' ')"; st=1; fi
exit $st
