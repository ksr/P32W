#!/bin/sh
# hwtests.sh -- the long tests on the hardware models, for a night (D14).
# make test runs a few programs and session 1 on the microcode-level
# emulator; this runs the rest:
#   1. every recorded P32W Unix session on p32uemu (the sequencer running the
#      real microcode, the cards modelled cycle by cycle), each against the
#      same recording as on p32emu;
#   2. with a tests directory: MicroPython's tests/basics and tests/float on
#      p32uemu, against the host reference build (tools/mptests.py).
#
#   sh hwtests.sh [MICROPYTHON_TESTS_DIR]       (from anywhere)
#
# Prints PASS / FAIL lines with their times, and a summary; exit status 0
# when all pass. Uses software/build (it runs make first) and a work
# directory of its own, removed at the end.
cd "$(dirname "$0")"
pass=0; fail=0
ok()  { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1: $2"; fail=$((fail + 1)); }
now() { date +%s; }
W=$(pwd)/hw.work
rm -rf "$W"; mkdir -p "$W"
UEMU="../p32uemu -u ../../microcode/p32ucode.bin"
echo "hwtests: started $(date)"
make -s -C .. >/dev/null && make -s -C ../../software all testdisk >/dev/null || { echo "hwtests: the build failed"; exit 1; }

# 1. the sessions on the microcode-level emulator
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19; do
    flags=""; usb=""; usbout=""
    case $n in 16) flags="-M 2";; 17) usb=4;; 18) usb=4; usbout=600;; 19) usb=40;; esac
    t0=$(now)
    if EMUFLAGS="$flags" USBMB="$usb" USBOUT="$usbout" ../../software/tests/run_session.sh session$n uemu > "$W/s$n.out" 2>&1 &&
       cmp -s "$W/s$n.out" ../../software/tests/session$n.expect; then
        ok "session $n on p32uemu, as recorded ($(( $(now) - t0 )) s)"
    else bad "session $n on p32uemu ($(( $(now) - t0 )) s)" "$(diff "$W/s$n.out" ../../software/tests/session$n.expect | head -4)"; fi
done

# 2. MicroPython's tests on p32uemu
if [ -n "$1" ]; then
    T=$(cd "$1" && pwd)
    t0=$(now)
    python3 ../../tools/mptests.py stage "$T" "$W/stage" > /dev/null &&
    ( cd ../../software/micropython && HOST=1 sh ports/p32/build.sh "$W/mph" > "$W/mph.log" 2>&1 ) &&
    python3 ../../tools/mptests.py host "$W/stage" "$W/mph/micropython" "$W/host" &&
    python3 ../../tools/mptests.py image ../../software/build/test.img "$W/stage" "$W/mp.img" || { bad "MicroPython tests" "setting up failed"; T=""; }
    if [ -n "$T" ]; then
        python3 ../../assembler/p32asm.py ../../software/monitor/monitor.asm -o "$W/monitor.bin"
        printf '%s\n' b ken p32 'sh /tmp/mpt/run.sh' su root halt |
            $UEMU -w -R 1791936000 -l 0 -d "$W/mp.img" "$W/monitor.bin" > "$W/mp.raw" 2>&1
        if msg=$(python3 ../../tools/mptests.py compare "$W/mp.img" "$W/host"); then
            ok "MicroPython's tests on p32uemu: $(echo "$msg" | head -1), as the host build ($(( ($(now) - t0) / 60 )) min)"
        else bad "MicroPython's tests on p32uemu" "$(echo "$msg" | head -5)"; fi
    fi
fi

echo "hwtests: $pass passed, $fail failed; finished $(date)"
[ $fail -eq 0 ] && rm -rf "$W"
[ $fail -eq 0 ]
