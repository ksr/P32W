#!/bin/sh
# run_session.sh NAME [uemu] -- boot P32W Unix in p32emu (or, with uemu, in
# the microcode-level emulator running the real control store) from a fresh image (fixed
# timestamps), feed tests/NAME.in a line per prompt (-w), and print the
# transcript, without the lines that depend on binary sizes (the kernel's
# sector count, free memory and blocks). Then fsck the image the session
# leaves behind (the host's checker on the kernel's work). The real-time
# clock starts at 2026-10-14 00:00 UTC (-R) and follows emulated time. The
# instruction limit (12 billion: session13, the on-machine suite, went
# past 4 once bc and diff joined) only stops a session that hangs, and
# then says so in the transcript. EMUFLAGS adds emulator options (e.g. -m).
set -e
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32session.$$
mkdir -p $T
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
P32FS_TIME=1791936000 python3 ../tools/p32fs.py build $T/os.img 512 root.manifest --kernel ../kernel/unix.bin --inodes 16384 >/dev/null   # (as software/Makefile)
TTY1=""
[ -f tests/$1.tty1 ] && TTY1="-A tests/$1.tty1 -a $T/tty1.out"        # a second terminal
USB=""
if [ -n "$USBMB" ]; then                # a blank USB drive of USBMB MB (D43); USBOUT: pulled out after that many sectors
    python3 -c "open('$T/usb.img', 'wb').truncate($USBMB * 1024 * 1024)"
    USB="-U $T/usb.img"
    [ -n "$USBOUT" ] && USB="$USB -Uout $USBOUT"
fi
EMU="../emulator/p32emu"
[ "$2" = uemu ] && EMU="../emulator/p32uemu -u ../microcode/p32ucode.bin"
$EMU $EMUFLAGS $USB -w -R 1791936000 -l 12000000000 -d $T/os.img $TTY1 $T/monitor.bin < tests/$1.in 2>$T/emu.err | tr -d '\r' |
    sed -e '/^loaded .* sectors at /d' -e '/^memory: /d' -e 's/^\(... ... .. 00:00:\)[0-9][0-9]\( 2026\)$/\1ss\2/' -e 's/^\(... ... .. 12:30:4\)[5-9]\( 2026\)$/\1x\2/' |      # (the seconds since boot vary with speed)
    sed -E -e 's/^ [0-9]{2}:[0-9]{2}:[0-9]{2} up +[0-9]+:[0-9]{2},/ hh:mm:ss up h:mm,/' -e 's/^(real|user|sys)( +)[0-9]+\.[0-9]+$/\1\2n.nn/' -e 's/primes in all, [0-9]+ ticks \([0-9.]+ s\)/primes in all, n ticks (n.nn s)/' -e 's/,  load average: .*$/,  load average: n.nn/' -e 's/^(... ... .. ..:..:)[0-9][0-9]( [A-Z]{3,5} [0-9]{4})$/\1ss\2/'   # (uptime, time)
grep "^STOP" $T/emu.err || true        # (the instruction limit: the session did not finish)
sed -n '/^TLB statistics/,$p' $T/emu.err   # (-m in EMUFLAGS: the TLB report)
if [ -n "$TTY1" ]; then echo "---- tty1"; tr -d '\r' < $T/tty1.out; echo; fi
python3 ../tools/p32fs.py fsck $T/os.img | sed -e "s|$T/||" -e 's/: .*, /: /'      # (counts vary)
rm -rf $T
