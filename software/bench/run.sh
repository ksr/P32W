#!/bin/sh
# run.sh -- the asm-vs-C measurement: each routine in bench.c built three
# ways (p32cc, p32cc without register variables, hand-written assembly in
# bench_asm.s), run on the microcode-level emulator; the cycles of the
# setup-only baseline (BENCH=0) are subtracted. Prints a Markdown table.
set -e
cd "$(dirname "$0")"
R=../..
CC="python3 $R/cc/p32cc.py"
UEMU="$R/emulator/p32uemu -u $R/microcode/p32ucode.bin -l 0"
T=${TMPDIR:-/tmp}/p32bench.$$
mkdir -p $T
trap 'rm -rf $T' EXIT
python3 $R/assembler/p32asm.py -c bench_asm.s -o $T/asm.o

cycles() {              # cycles <variant> <n> -> "instructions cycles"
    case $1 in
    c)     $CC --sa -DBENCH=$2 bench.c -o $T/b.bin ;;
    noreg) P32CC_NOREGVARS=1 $CC --sa -DBENCH=$2 bench.c -o $T/b.bin ;;
    asm)   $CC --sa -DBENCH=$2 -DASM bench.c $T/asm.o -o $T/b.bin ;;
    esac
    $UEMU $T/b.bin </dev/null 2>&1 >/dev/null | sed -n 's/.*(\([0-9]*\) instructions, \([0-9]*\) cycles).*/\1 \2/p' || true
}

NAMES="- strlen wcopy bytesum crc32 isort count hcopy"
for v in c noreg asm; do
    set -- $(cycles $v 0); eval base_i_$v=$1 base_c_$v=$2
done
printf '| routine | C cycles | C, no regvars | asm cycles | C / asm | no-regvars / asm |\n'
printf '|---|---:|---:|---:|---:|---:|\n'
n=1
for name in $(echo $NAMES | cut -d' ' -f2-); do
    for v in c noreg asm; do
        set -- $(cycles $v $n)
        eval "bc=\$base_c_$v"
        eval "r_$v=$(( $2 - bc ))"
    done
    printf '| %s | %d | %d | %d | %s | %s |\n' $name $r_c $r_noreg $r_asm \
        $(echo "scale=2; $r_c / $r_asm" | bc) $(echo "scale=2; $r_noreg / $r_asm" | bc)
    n=$((n + 1))
done
