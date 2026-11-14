#!/bin/sh
# run_tests.sh -- P32W assembler + reference emulator regression tests.
# Run via `make test` in emulator/ (or: sh test/run_tests.sh).
set -u
cd "$(dirname "$0")"
ASM="python3 ../../assembler/p32asm.py"
ulimit -f 4194304     # no file over 2 GiB: a runaway test once wrote 20 GB of trace
EMU=../p32emu
fails=0
skips=0

ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1: $2"; fails=$((fails + 1)); }
skip() { echo "SKIP  $1"; skips=$((skips + 1)); }     # (a test that needs a Mac's own tools, elsewhere: D96)
asm() { $ASM "$1.asm" -o "$1.bin" -l "$1.lst" 2>"$1.err" || { bad "$1" "assembly failed: $(cat "$1.err")"; return 1; }; }

# 1. exact encodings, word for word
if asm encode; then
    got=$(python3 -c "import sys;d=open('encode.bin','rb').read();print(' '.join('%08X'%int.from_bytes(d[i:i+4],'little') for i in range(0,len(d),4)))")
    want=$(tr '\n' ' ' < encode.expect | sed 's/ *$//')
    [ "$got" = "$want" ] && ok "encode (exact instruction words)" || bad encode "got:  $got
            want: $want"
fi

# 2. hello: console output + call/return
if asm hello; then
    out=$($EMU hello.bin 2>hello.err); st=$?
    [ $st -eq 0 ] && [ "$out" = "P32W lives! instruction-level reference emulator" ] \
        && ok "hello ($out)" || bad hello "status $st, output '$out'"
fi

# 3. every instruction, self-checking: exit status = failing case number
if asm isa_test; then
    $EMU isa_test.bin >isa_test.out 2>isa_test.err; st=$?
    [ $st -eq 0 ] && ok "isa_test (65 cases, $(sed -n 's/.*(\([0-9]*\) instructions).*/\1/p' isa_test.err) instructions)" \
        || bad isa_test "case $st failed: $(cat isa_test.err)"
fi

# 3b. the D59 instructions, self-checking: indexed and post-increment loads
#     and stores, LI32, and their faults (no effect, restartable)
if asm d59_test; then
    $EMU d59_test.bin >d59_test.out 2>d59_test.err; st=$?
    [ $st -eq 0 ] && ok "d59_test (27 cases)" || bad d59_test "case $st failed: $(cat d59_test.err)"
fi

# 4. console input
if asm echo; then
    out=$(printf 'Hello, P32W!\n' | $EMU echo.bin 2>echo.err); st=$?
    [ $st -eq 0 ] && [ "$out" = "HELLO, P32W!" ] && ok "echo (console input)" || bad echo "status $st, output '$out'"
fi

# 4b. Fibonacci: F(0)..F(47) in decimal, checked against a Python reference
if asm fib; then
    $EMU fib.bin >fib.out 2>fib.err; st=$?
    python3 -c "
a, b, out = 0, 1, []
while a < 2**32:
    out.append(str(a)); a, b = b, a + b
print('\\n'.join(out))" > fib.expect.out
    if [ $st -eq 0 ] && cmp -s fib.out fib.expect.out; then
        ok "fib ($(wc -l < fib.out | tr -d ' ') numbers, last $(tail -1 fib.out))"
    else bad fib "status $st; diff: $(diff fib.out fib.expect.out | head -3)"; fi
fi

# 4c. primes below 1000 (sieve), checked against a Python reference
if asm primes; then
    $EMU primes.bin >primes.out 2>primes.err; st=$?
    python3 -c "
N = 1000
ps = [n for n in range(2, N) if all(n % d for d in range(2, int(n ** 0.5) + 1))]
print('\\n'.join(map(str, ps)))
print('%d primes below %d' % (len(ps), N))" > primes.expect.out
    if [ $st -eq 0 ] && cmp -s primes.out primes.expect.out; then
        ok "primes ($(tail -1 primes.out))"
    else bad primes "status $st; diff: $(diff primes.out primes.expect.out | head -3)"; fi
fi

# 4d. stage A: traps, privilege, banked SP (self-checking, like isa_test)
if asm trap_test; then
    $EMU trap_test.bin >trap_test.out 2>trap_test.err; st=$?
    [ $st -eq 0 ] && ok "trap_test (29 cases)" || bad trap_test "case $st failed: $(cat trap_test.err)"
fi

# 4e. stage-A kernel: a user program using only system calls
if asm kernel_hello; then
    out=$(printf 'shout please\n' | $EMU kernel_hello.bin 2>kernel_hello.err); st=$?
    want=$(printf 'Hello from user mode!\n0123456789\nSHOUT PLEASE')
    [ $st -eq 0 ] && [ "$out" = "$want" ] && ok "kernel_hello (user mode, syscalls)" \
        || bad kernel_hello "status $st, output '$out'"
fi

# 4f. stage-A kernel: a user program touching the I/O page is killed, and the
#     report names the faulting instruction's address (taken from the listing)
if asm kernel_fault; then
    out=$($EMU kernel_fault.bin 2>kernel_fault.err); st=$?
    pc=$(sed -n 's/^\([0-9A-F]\{8\}\) .* bad: .*/\1/p' kernel_fault.lst)
    want="kernel: user fault, cause 2, pc $pc, addr FFFFFF00"
    if [ $st -eq 102 ] && [ -n "$pc" ] && printf '%s\n' "$out" | grep -qx "$want"; then
        ok "kernel_fault (killed at pc $pc, status 102)"
    else bad kernel_fault "status $st, want '$want', output '$out'"; fi
fi

# 4g. stage B: interrupts, timer, WAIT (self-checking)
if asm irq_test; then
    $EMU irq_test.bin >irq_test.out 2>irq_test.err; st=$?
    [ $st -eq 0 ] && ok "irq_test (17 cases)" || bad irq_test "case $st failed: $(cat irq_test.err)"
fi
if asm wait_nowake; then
    $EMU wait_nowake.bin >wait_nowake.out 2>wait_nowake.err; st=$?
    [ $st -eq 255 ] && grep -q "STOP: WAIT with no possible wake-up" wait_nowake.err \
        && ok "wait_nowake (stops instead of hanging)" || bad wait_nowake "status $st: $(head -1 wait_nowake.err)"
fi

# 4h. stage B: preemptive multitasking kernel. Three tasks that never yield
#     must each finish (6 letters, correct register checksums = no X) with
#     their output interleaved by the timer; a fourth is killed at the
#     faulting instruction (address from the listing) while the others go on.
if asm mt_demo; then
    out=$($EMU mt_demo.bin 2>mt_demo.err); st=$?
    letters=$(printf '%s' "$out" | grep -v kernel | tr -d '\n')
    pc=$(sed -n 's/^\([0-9A-F]\{8\}\) .* d_bad: .*/\1/p' mt_demo.lst)
    na=$(printf '%s' "$letters" | tr -cd A | wc -c | tr -d ' ')
    nb=$(printf '%s' "$letters" | tr -cd B | wc -c | tr -d ' ')
    nc=$(printf '%s' "$letters" | tr -cd C | wc -c | tr -d ' ')
    nx=$(printf '%s' "$letters" | tr -cd X | wc -c | tr -d ' ')
    runs=$(printf '%s' "$letters" | fold -w1 | uniq | wc -l | tr -d ' ')
    if [ $st -eq 0 ] && [ "$na$nb$nc" = "666" ] && [ "$nx" = 0 ] && [ "$runs" -gt 3 ] \
       && printf '%s\n' "$out" | grep -qx "kernel: task 3 killed, cause 2, pc $pc"; then
        ok "mt_demo ($letters; $runs switches between tasks; task 3 killed at $pc)"
    else bad mt_demo "status $st, A=$na B=$nb C=$nc X=$nx runs=$runs, output: $out"; fi
fi

# 4i. stage C: the MMU itself (self-checking, TLB entries written by hand)
if asm vm_test; then
    $EMU vm_test.bin >vm_test.out 2>vm_test.err; st=$?
    [ $st -eq 0 ] && ok "vm_test (18 cases)" || bad vm_test "case $st failed: $(cat vm_test.err)"
fi

# 4j. stage C: protected processes under vmkernel.inc (checked by check_vm_demo.py)
if asm vm_demo; then
    $EMU vm_demo.bin >vm_demo.out 2>vm_demo.err; st=$?
    if [ $st -ne 0 ]; then bad vm_demo "status $st: $(cat vm_demo.err)"
    elif msg=$(python3 check_vm_demo.py vm_demo.out vm_demo.lst 2>&1); then ok "vm_demo ($msg)"
    else bad vm_demo "$msg"; fi
fi

# 4d. the I/O card (D50): UARTs, ports, CF disk -- on both emulators, which
#     must agree instruction by instruction and leave identical disk images
if asm io_test; then
    python3 -c "open('io_disk.img','wb').write(bytes(((7*n+i)&0xFF) for n in range(8) for i in range(512)))"
    cp io_disk.img io_disk.u.img
    $EMU -p 0x5A -a io_aux.out -d io_disk.img -s io_test.ref.trace io_test.bin </dev/null >io_test.out 2>io_test.err; st=$?
    ../p32uemu -u ../../microcode/p32ucode.bin -p 0x5A -a io_aux.u.out -d io_disk.u.img -s io_test.u.trace io_test.bin \
        </dev/null >io_test.u.out 2>io_test.u.err; us=$?
    sector5=$(python3 -c "d=open('io_disk.img','rb').read();print(int(d[2560:3072]==bytes((i&255)^0x55 for i in range(512))))")
    if [ $st -ne 0 ]; then bad io_test "case $st failed: $(head -1 io_test.err)"
    elif [ "$(cat io_test.out)" != "io ok" ] || [ "$(cat io_aux.out)" != "aux port" ]; then bad io_test "UART output wrong"
    elif [ "$sector5" != 1 ]; then bad io_test "CF sector 5 not written"
    elif [ $us -ne 0 ] || ! cmp -s io_test.ref.trace io_test.u.trace || ! cmp -s io_disk.img io_disk.u.img; then
        bad io_test "microcode-level emulator differs (status $us)"
    else ok "io_test (UARTs, ports, CF disk: 17 cases; both emulators identical)"; fi
fi

# 4d2. the cards (D50, D52): the card ID registers, UARTs 2 and 3, the
#      shared interrupt levels -- on both emulators, instruction by instruction
if asm card_test; then
    printf x >card_in2; printf y >card_in3
    $EMU -a2 card_two.out -A2 card_in2 -a3 card_three.out -A3 card_in3 -s card_test.ref.trace card_test.bin </dev/null >card_test.out 2>card_test.err; st=$?
    ../p32uemu -u ../../microcode/p32ucode.bin -a2 card_two.u.out -A2 card_in2 -a3 card_three.u.out -A3 card_in3 -s card_test.u.trace card_test.bin \
        </dev/null >card_test.u.out 2>card_test.u.err; us=$?
    if [ $st -ne 0 ]; then bad card_test "case $st failed: $(head -1 card_test.err)"
    elif [ "$(cat card_test.out)" != "cards ok" ] || [ "$(cat card_two.out)" != "two" ] || [ "$(cat card_three.out)" != "three" ]; then bad card_test "UART output wrong"
    elif [ $us -ne 0 ] || ! cmp -s card_test.ref.trace card_test.u.trace; then bad card_test "microcode-level emulator differs (status $us)"
    else ok "card_test (card IDs, UARTs 2 and 3, shared levels: 10 cases; both emulators identical)"; fi
    rm -f card_in2 card_in3 card_two*.out card_three*.out
fi

# 4d3. the video card, revision 2 (D103, D104; docs/video-card-acrtc.md).
#      First the emulators' model of its chip, the HD63484, by itself, driven
#      as the kernel's console drives it (acrtc_test.c).
if cc -O2 -std=c99 -I.. -o acrtc_test acrtc_test.c 2>acrtc_test.err && out=$(./acrtc_test 2>>acrtc_test.err); then
    ok "the HD63484's model (${out#acrtc_test: }: the 16-bit bus, the FIFOs, IRQ, the font's eight pictures, characters, the cursor, a scroll)"
else bad "the HD63484's model" "$(./acrtc_test 2>/dev/null | head -3; head -2 acrtc_test.err)"; fi
#      Then the card on both emulators: its ID, the chip's registers and FIFOs
#      through the card's two addresses, the font from the EPROM, characters,
#      a line, the chip's interrupt, the keyboard's ASCII and the mouse's
#      packet from the PS/2 section, bus errors; the screen dump (-V) holds
#      the text and the picture. And with the card's timing (p32uemu -C:
#      three wait states for each access to the chip), at 3 MHz and at 4.
if asm video_test; then
    printf aB >video_keys; printf xy >video_keys2
    $EMU -K video_keys -Km 09,05,FB -K video_keys2 -V video_screen -s video_test.ref.trace video_test.bin </dev/null >video_test.out 2>video_test.err; st=$?
    ../p32uemu -u ../../microcode/p32ucode.bin -K video_keys -Km 09,05,FB -K video_keys2 -V video_screen.u -s video_test.u.trace video_test.bin \
        </dev/null >video_test.u.out 2>video_test.u.err; us=$?
    ../p32uemu -u ../../microcode/p32ucode.bin -C -K video_keys -Km 09,05,FB -K video_keys2 -V video_screen.c video_test.bin </dev/null >/dev/null 2>video_test.c.err; cs=$?
    ../p32uemu -u ../../microcode/p32ucode.bin -C -F -K video_keys -Km 09,05,FB -K video_keys2 video_test.bin </dev/null >/dev/null 2>video_test.f.err; fs=$?
    if [ $st -ne 0 ]; then bad video_test "case $st failed: $(head -1 video_test.err)"
    elif [ "$(head -1 video_screen)" != "HELLO P32W" ] || [ "$(wc -l < video_screen | tr -d ' ')" != 25 ] \
         || [ "$(head -c 13 video_screen.pbm | tail -c 2 | od -An -tx1 | tr -d ' ')" != "ffff" ]; then bad video_test "screen dump wrong"
    elif [ $us -ne 0 ] || ! cmp -s video_test.ref.trace video_test.u.trace || ! cmp -s video_screen video_screen.u \
         || ! cmp -s video_screen.pbm video_screen.u.pbm; then
        bad video_test "microcode-level emulator differs (status $us)"
    elif [ $cs -ne 0 ] || [ $fs -ne 0 ] || ! cmp -s video_screen video_screen.c; then
        bad video_test "with the card's timing (p32uemu -C): case $cs at 3 MHz, $fs at 4 MHz"
    else ok "video_test (the chip's registers and FIFOs, the font, characters, a line, interrupts, keyboard, mouse, the PS/2 firmware's time from its start on (FULL, BUSY, a command lost, the flush), bus errors: 14 cases; both emulators identical; $(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' video_test.c.err) cycles with the card's three wait states)"; fi
    rm -f video_keys video_keys2 video_screen* acrtc_test
fi
#      And the PS/2 section as a reset leaves it (D109): the ATmega328P is two
#      seconds starting (its bootloader, then the keyboard's and the mouse's
#      resets), a command written meanwhile is dropped, and a command that
#      talks to a device holds the next one up. The emulators' model of the
#      firmware's time, measured with the machine's timer.
if asm ps2boot_test; then
    printf k >ps2boot_keys
    pb=""
    $EMU -K ps2boot_keys ps2boot_test.bin </dev/null >/dev/null 2>ps2boot_test.err || pb="p32emu: case $?"
    for f in "" "-C" "-C -F"; do
        [ -n "$pb" ] && break
        ../p32uemu -u ../../microcode/p32ucode.bin $f -K ps2boot_keys ps2boot_test.bin </dev/null >/dev/null 2>ps2boot_test.u.err || pb="p32uemu $f: case $?"
    done
    if [ -n "$pb" ]; then bad ps2boot_test "$pb"
    else ok "ps2boot_test (the PS/2 section from a reset: BUSY through the bootloader's second and the command dropped, the first byte after two seconds, a device's command holding the next up: 5 cases; p32emu, p32uemu, and with the card's timing at 3 and 4 MHz)"; fi
    rm -f ps2boot_keys
fi

# 4d4. the bus lock (D51, D54): SWAP against a DMA master (p32uemu -D) that
#      keeps adding 1 to the word: with -LOCK nothing is lost (the total plus
#      the word = the additions) at several periods; with the arbiter
#      ignoring -LOCK (-Dnolock) additions are lost -- the test can tell
swap_lost() {   # $1 option, $2 period: additions lost
    ../p32uemu -u ../../microcode/p32ucode.bin $1 0x00200000:$2 swap_dma.bin 2>&1 | python3 -c "
import sys, re
t = sys.stdin.read()
r1 = int(re.search('r1=([0-9A-F]+)', t).group(1), 16)
n, v = map(int, re.search(r'DMA: (\d+) added, the word is (\d+)', t).groups())
print(n - r1 - v)"
}
if asm swap_dma; then
    l5=$(swap_lost -D 5); l7=$(swap_lost -D 7); l13=$(swap_lost -D 13); n7=$(swap_lost -Dnolock 7)
    if [ "$l5$l7$l13" = 000 ] && [ "$n7" -gt 0 ]; then ok "swap_dma (SWAP under -LOCK loses nothing to a DMA master; without the lock $n7 lost)"
    else bad swap_dma "lost with -LOCK: $l5 $l7 $l13; without: $n7"; fi
fi

# 4d5. the measuring gate (p32uemu -G, D85): bit 7 of the output port, and
#     nothing else, starts and stops the count, and measuring changes nothing
if asm gate_test; then
    ../p32uemu -u ../../microcode/p32ucode.bin -G gate_test.bin > gate.out 2>&1
    ../p32uemu -u ../../microcode/p32ucode.bin -G -F gate_test.bin > gate.f.out 2>&1
    ../p32uemu -u ../../microcode/p32ucode.bin -C gate_test.bin > gate.c.out 2>&1
    if msg=$(python3 -I - gate.out gate.f.out gate.c.out <<'PY'
import re, sys
g, f, c = (open(n).read() for n in sys.argv[1:4])
num = lambda s: int(s.replace(",", ""))
counts = re.findall(r"\[cycles #\d+(, the gate still up at the end)?: ([\d,]+) cycles.*\n ([\d,]+) instructions, [\d.]+ MIPS busy; idle ([\d,]+)", g)
want = len(counts) == 4 and [num(k[2]) for k in counts[:2]] == [2002, 6004] and num(counts[3][2]) == 22
want = want and bool(counts[3][0]) and not counts[0][0] and 2900 <= num(counts[2][3]) <= 3000
want = want and all(num(k[1]) >= 3 * num(k[2]) for k in counts)
total = lambda s: re.search(r"HALT.*", s).group(0)
if not want or "[cycles" in c or total(g) != total(c) or "at 4 MHz" not in f:
    sys.exit("counts %s; %s; %s" % (counts, total(g), total(c)))
print("%s, %s and %s instructions counted, %s idle cycles" % (counts[0][2], counts[1][2], counts[3][2], counts[2][3]))
PY
    ); then ok "gate_test (p32uemu -G: $msg, and the run unchanged)"
    else bad gate_test "$msg"; fi
    rm -f gate.out gate.f.out gate.c.out
fi

# 4e. the I/O library software/lib/p32io.inc (the programmer's guide's
#     routines): UART setup and output, console input, CF read / write
if asm lib_test; then
    python3 -c "open('lib_disk.img','wb').write(bytes(((7*n+i)&0xFF) for n in range(8) for i in range(512)))"
    cp lib_disk.img lib_disk.u.img
    printf 'xyz' | $EMU -d lib_disk.img -s lib_test.ref.trace lib_test.bin >lib_test.out 2>lib_test.err; st=$?
    printf 'xyz' | ../p32uemu -u ../../microcode/p32ucode.bin -d lib_disk.u.img -s lib_test.u.trace lib_test.bin \
        >lib_test.u.out 2>lib_test.u.err; us=$?
    want="p32io library: CAFE0123
XYZ
disk ok"
    if [ $st -ne 0 ] || [ "$(cat lib_test.out)" != "$want" ]; then bad lib_test "step $st: $(cat lib_test.out | head -3)"
    elif [ $us -ne 0 ] || ! cmp -s lib_test.ref.trace lib_test.u.trace || ! cmp -s lib_disk.img lib_disk.u.img; then
        bad lib_test "microcode-level emulator differs (status $us)"
    else ok "lib_test (software/lib/p32io.inc: UARTs, console input, CF read/write; both emulators identical)"; fi
fi

# 4f. the boot ROM monitor (software/monitor): a scripted session -- boot
#     from a CF image, dump, call, load -- against monitor.expect, both emulators
if $ASM ../../software/monitor/monitor.asm -o monitor.bin -l monitor.lst 2>monitor.err && asm boot_payload; then
    python3 mkbootdisk.py boot_payload.bin boot.img && cp boot.img boot.u.img
    script='b\nd 100000 8\ng 100000\nl 0 1 200000\nd 200000 10\nq\n'
    printf "$script" | $EMU -d boot.img -s monitor.ref.trace monitor.bin 2>/dev/null | tr -d '\r' >monitor.out; st=$?
    printf "$script" | ../p32uemu -u ../../microcode/p32ucode.bin -d boot.u.img -s monitor.u.trace monitor.bin 2>/dev/null \
        | tr -d '\r' >monitor.u.out
    if ! cmp -s monitor.out monitor.expect; then bad monitor "transcript differs: $(diff monitor.out monitor.expect | head -3)"
    elif ! cmp -s monitor.ref.trace monitor.u.trace || ! cmp -s monitor.out monitor.u.out; then bad monitor "microcode-level emulator differs"
    else ok "monitor (boot ROM: boot from CF, dump, call, load; both emulators identical)"; fi
else bad monitor "assembly failed: $(cat monitor.err)"; fi

# 5. exceptions stop the emulator with status 255 and a message
for t in "exc_misaligned:misaligned load" "exc_illegal:illegal instruction" \
         "exc_unused:illegal instruction" "exc_romwrite:write to ROM" \
         "exc_unmapped:load from unmapped address" "exc_badjump:misaligned jump target" \
         "exc_ioword:no device answers"; do
    name=${t%%:*}; want=${t#*:}
    asm "$name" || continue
    $EMU "$name.bin" >"$name.out" 2>"$name.err"; st=$?
    if [ $st -eq 255 ] && grep -q "EXCEPTION: $want" "$name.err"; then ok "$name"
    else bad "$name" "status $st, stderr: $(head -1 "$name.err")"; fi
done

# 6. microcode-level emulator vs. the reference (D14): every program runs on
#    both; the architectural state at every instruction start (-s traces),
#    the console output and the exit status must be identical.
UEMU="../p32uemu -u ../../microcode/p32ucode.bin"
for t in "hello:" "isa_test:" "d59_test:" "echo:Hello, P32W!" "fib:" "primes:" "trap_test:" \
         "kernel_hello:shout please" "kernel_fault:" "irq_test:" "wait_nowake:" \
         "mt_demo:" "vm_test:" "vm_demo:" "exc_misaligned:" "exc_illegal:" \
         "exc_unused:" "exc_romwrite:" "exc_unmapped:" "exc_badjump:" "exc_ioword:"; do
    name=${t%%:*}; input=${t#*:}
    [ -f "$name.bin" ] || { bad "ucode $name" "no $name.bin"; continue; }
    printf '%s' "$input" | $EMU -s "$name.ref.trace" "$name.bin" >"$name.ref.out" 2>/dev/null; rs=$?
    printf '%s' "$input" | $UEMU -s "$name.u.trace" "$name.bin" >"$name.u.out" 2>"$name.u.err"; us=$?
    if [ $rs -ne $us ]; then bad "ucode $name" "exit status $us, reference $rs: $(head -1 "$name.u.err")"
    elif ! cmp -s "$name.ref.out" "$name.u.out"; then bad "ucode $name" "console output differs"
    elif ! cmp -s "$name.ref.trace" "$name.u.trace"; then
        bad "ucode $name" "state differs at instruction $(cmp "$name.ref.trace" "$name.u.trace" | sed 's/.*line //'):
            ref:   $(diff "$name.ref.trace" "$name.u.trace" | sed -n 2p)
            ucode: $(diff "$name.ref.trace" "$name.u.trace" | grep '^>' | head -1)"
    else ok "ucode $name ($(wc -l < "$name.u.trace" | tr -d ' ') instructions identical, $(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' "$name.u.err") cycles)"; fi
done

# 6b. the FPGA ALU (D21): the Verilog against the emulator's ALU, all 19
#     functions, results sampled when the bus needs them (skipped without iverilog)
if command -v iverilog >/dev/null 2>&1; then
    A=../../fpga/alu
    cp $A/p32alu_codes.vh alu.codes.old 2>/dev/null
    if ( cd $A && python3 gen_alu.py >/dev/null && iverilog -g2012 -DSIMULATION -o tb.vvp tb_p32alu.v p32alu.v && vvp tb.vvp ) > alu.out 2>&1 \
       && grep -q "^PASS" alu.out && cmp -s alu.codes.old $A/p32alu_codes.vh; then ok "fpga alu ($(grep '^PASS' alu.out | sed 's/^PASS p32alu core: //'))"
    else bad "fpga alu" "$(grep -E 'FAIL|rror' alu.out | head -3)$(cmp -s alu.codes.old $A/p32alu_codes.vh || echo ' p32alu_codes.vh was stale')"; fi
    rm -f alu.codes.old $A/tb.vvp
else ok "fpga alu (iverilog not installed: skipped)"; fi

# 6c. the instruction reference is generated from the ISA table: up to date
if python3 ../../generators/gen_isaref.py isaref.check.md >/dev/null 2>&1 && cmp -s isaref.check.md ../../docs/instruction-reference.md; then
    ok "instruction reference (docs/instruction-reference.md current)"
else bad "instruction reference" "stale or an instruction lacks a description (python3 generators/gen_isaref.py)"; fi
rm -f isaref.check.md

# 6c. the CPU card (docs/cpu-card.md): its GALs (each exhaustively against its
#     spec) and the netlist (electrical checks); then the card AS WIRED, in
#     the gate-level simulator with the real control store, runs programs:
#     the state at every instruction must be p32emu's, the cycles p32uemu's
if out=$(python3 ../../generators/card_cpu.py 2>&1); then ok "${out}"
else bad "cpu card" "$out"; fi
for t in hello isa_test d59_test trap_test irq_test; do
    $EMU -s $t.r.trace $t.bin >/dev/null 2>&1; rs=$?
    uc=$($UEMU $t.bin 2>&1 >/dev/null | sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p')
    python3 ../../generators/sim_cpu.py -s $t.g.trace $t.bin >$t.g.out 2>$t.g.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' $t.g.err)
    if [ $rs -ne $gs ]; then bad "cpu card: $t" "exit status $gs, reference $rs: $(head -1 $t.g.err)"
    elif ! cmp -s $t.r.trace $t.g.trace; then bad "cpu card: $t" "state differs at instruction $(cmp $t.r.trace $t.g.trace | sed 's/.*line //')"
    elif [ "$uc" != "$gc" ]; then bad "cpu card: $t" "$gc cycles, p32uemu $uc"
    else ok "cpu card, gate level: $t ($(wc -l < $t.g.trace | tr -d ' ') instructions identical, $gc cycles as p32uemu)"; fi
done

# 6c2. the MMU / data card (docs/mmu-card.md): its GALs and netlist; then
#      the CPU card and the MMU / data card together at gate level (only
#      memory and the devices modelled): the TLB, faults, the acknowledge cycle
if out=$(python3 ../../generators/card_mmu.py 2>&1); then ok "${out}"
else bad "mmu card" "$out"; fi
for t in isa_test vm_test irq_test; do
    $EMU -s $t.r.trace $t.bin >/dev/null 2>&1; rs=$?
    uc=$($UEMU $t.bin 2>&1 >/dev/null | sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p')
    python3 ../../generators/sim_sys.py -s $t.y.trace $t.bin >$t.y.out 2>$t.y.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' $t.y.err)
    if [ $rs -ne $gs ]; then bad "cpu + mmu cards: $t" "exit status $gs, reference $rs: $(tail -1 $t.y.err)"
    elif ! cmp -s $t.r.trace $t.y.trace; then bad "cpu + mmu cards: $t" "state differs at instruction $(cmp $t.r.trace $t.y.trace | sed 's/.*line //')"
    elif [ "$uc" != "$gc" ]; then bad "cpu + mmu cards: $t" "$gc cycles, p32uemu $uc"
    else ok "cpu + mmu cards, gate level: $t ($(wc -l < $t.y.trace | tr -d ' ') instructions identical, $gc cycles as p32uemu)"; fi
done

# 6c3. the memory card (docs/mem-card.md): its GALs and netlist; then the
#      CPU, MMU / data and memory cards together at gate level (the ROM and
#      the RAM in its chips; only the devices modelled)
if out=$(python3 ../../generators/card_mem.py 2>&1); then ok "${out}"
else bad "memory card" "$out"; fi
for t in vm_test; do
    $EMU -s $t.r.trace $t.bin >/dev/null 2>&1; rs=$?
    uc=$($UEMU $t.bin 2>&1 >/dev/null | sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p')
    python3 ../../generators/sim_sys.py --mem -s $t.m.trace $t.bin >$t.m.out 2>$t.m.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' $t.m.err)
    if [ $rs -ne $gs ]; then bad "cpu + mmu + memory cards: $t" "exit status $gs, reference $rs: $(tail -1 $t.m.err)"
    elif ! cmp -s $t.r.trace $t.m.trace; then bad "cpu + mmu + memory cards: $t" "state differs at instruction $(cmp $t.r.trace $t.m.trace | sed 's/.*line //')"
    elif [ "$uc" != "$gc" ]; then bad "cpu + mmu + memory cards: $t" "$gc cycles, p32uemu $uc"
    else ok "cpu + mmu + memory cards, gate level: $t ($(wc -l < $t.m.trace | tr -d ' ') instructions identical, $gc cycles as p32uemu)"; fi
done

# 6c4. the I/O card (docs/io-card.md): its GALs and netlist; then all four
#      cards at gate level (only the UARTs', the clock's, the USB modules'
#      and the coprocessor's insides modelled). The card's timer counts as
#      the hardware does, so the reference is p32uemu -C.
if out=$(python3 ../../generators/card_io.py 2>&1); then ok "${out}"
else bad "I/O card" "$out"; fi
if asm iocard_test; then
    $UEMU -C -s iocard_test.c.trace iocard_test.bin </dev/null >/dev/null 2>iocard_test.c.err; rs=$?
    uc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' iocard_test.c.err)
    python3 ../../generators/sim_sys.py --mem --io -s iocard_test.i.trace iocard_test.bin </dev/null >iocard_test.i.out 2>iocard_test.i.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' iocard_test.i.err)
    if [ $rs -ne 0 ]; then bad "iocard_test" "case $rs failed on p32uemu -C: $(head -1 iocard_test.c.err)"
    elif [ $gs -ne 0 ]; then bad "four cards: iocard_test" "exit status $gs: $(tail -1 iocard_test.i.err)"
    elif ! cmp -s iocard_test.c.trace iocard_test.i.trace; then bad "four cards: iocard_test" "state differs at instruction $(cmp iocard_test.c.trace iocard_test.i.trace | sed 's/.*line //')"
    elif [ "$uc" != "$gc" ]; then bad "four cards: iocard_test" "$gc cycles, p32uemu -C $uc"
    else ok "cpu + mmu + memory + I/O cards, gate level: iocard_test (timer, vectored interrupt, clock, USB, bus errors: $(wc -l < iocard_test.i.trace | tr -d ' ') instructions, $gc cycles as p32uemu -C)"; fi
fi
# the CPU card's clock jumper fitted (D74): -FAST low, the 4 MHz oscillator,
# the I/O card's timer dividing by 4 (p32uemu -C -F)
if [ -f iocard_test.bin ]; then
    $UEMU -C -F -s iocard_test.cf.trace iocard_test.bin </dev/null >/dev/null 2>iocard_test.cf.err; rs=$?
    uc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' iocard_test.cf.err)
    python3 ../../generators/sim_sys.py --mem --io --fast -s iocard_test.if.trace iocard_test.bin </dev/null >iocard_test.if.out 2>iocard_test.if.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' iocard_test.if.err)
    if [ $rs -ne 0 ]; then bad "iocard_test, 4 MHz" "case $rs failed on p32uemu -C -F: $(head -1 iocard_test.cf.err)"
    elif [ $gs -ne 0 ]; then bad "four cards at 4 MHz: iocard_test" "exit status $gs: $(tail -1 iocard_test.if.err)"
    elif ! cmp -s iocard_test.cf.trace iocard_test.if.trace; then bad "four cards at 4 MHz: iocard_test" "state differs at instruction $(cmp iocard_test.cf.trace iocard_test.if.trace | sed 's/.*line //')"
    elif [ "$uc" != "$gc" ]; then bad "four cards at 4 MHz: iocard_test" "$gc cycles, p32uemu -C -F $uc"
    else ok "the clock jumper fitted (D74), gate level: iocard_test with the timer dividing by 4 ($gc cycles as p32uemu -C -F)"; fi
fi
if asm mbox_test; then
    $UEMU -C -N mbox_test.bin </dev/null >/dev/null 2>mbox_test.c.err; rs=$?
    python3 ../../generators/sim_sys.py --mem --io --opt=-N mbox_test.bin </dev/null >mbox_test.i.out 2>mbox_test.i.err; gs=$?
    if [ $rs -ne 0 ]; then bad "mbox_test" "case $rs failed on p32uemu -C"
    elif [ $gs -ne 0 ]; then bad "four cards: mbox_test" "case $gs failed: $(tail -1 mbox_test.i.err)"
    else ok "cpu + mmu + memory + I/O cards, gate level: mbox_test (the network mailbox: HELLO through the latches)"; fi
fi

# 6c5. the disk card (docs/disk-card.md): its GALs and netlist; then five
#      cards at gate level (only the drives' insides modelled), against
#      p32uemu -C, which adds the card's two wait states per port access
if out=$(python3 ../../generators/card_disk.py 2>&1); then ok "${out}"
else bad "disk card" "$out"; fi
if asm disk_test; then
    python3 -c "
for n, tag in (('dkA', b'MASTER'), ('dkB', b'SLAVE!'), ('dk1', b'')):
    for v in ('c', 'g'):
        open('disk_test.%s.%s.img' % (n, v), 'wb').write(tag.ljust(16 * 512, b'\\0'))"
    $UEMU -C -s disk_test.c.trace -d2 disk_test.dkA.c.img -d2s disk_test.dkB.c.img -d1 disk_test.dk1.c.img \
        disk_test.bin </dev/null >/dev/null 2>disk_test.c.err; rs=$?
    uc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' disk_test.c.err)
    python3 ../../generators/sim_sys.py --mem --io --disk -s disk_test.g.trace --opt=-d2 --opt=disk_test.dkA.g.img \
        --opt=-d2s --opt=disk_test.dkB.g.img --opt=-d1 --opt=disk_test.dk1.g.img disk_test.bin </dev/null >/dev/null 2>disk_test.g.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' disk_test.g.err)
    if [ $rs -ne 0 ]; then bad "disk_test" "case $rs failed on p32uemu -C"
    elif [ $gs -ne 0 ]; then bad "five cards: disk_test" "exit status $gs: $(tail -1 disk_test.g.err)"
    elif ! cmp -s disk_test.c.trace disk_test.g.trace; then bad "five cards: disk_test" "state differs at instruction $(cmp disk_test.c.trace disk_test.g.trace | sed 's/.*line //')"
    elif [ "$uc" != "$gc" ]; then bad "five cards: disk_test" "$gc cycles, p32uemu -C $uc"
    elif ! cmp -s disk_test.dk1.c.img disk_test.dk1.g.img; then bad "five cards: disk_test" "the written CF image differs"
    else ok "cpu + mmu + memory + I/O + disk cards, gate level: disk_test (four ports, master and slave, write and read back, the vector: $(wc -l < disk_test.g.trace | tr -d ' ') instructions, $gc cycles as p32uemu -C)"; fi
fi
# the disk card's DMA engine (D75): diskdma_test on p32emu and p32uemu -C,
# then at gate level (the engine a bus master through the MMU / data card's
# arbiter, into the gate-level memory card). Self-checking: the CPU polls,
# so the cycle counts differ; the sector it wrote must be the emulators'.
if asm diskdma_test; then
    python3 -c "
import struct
b = bytearray(16 * 512)
for j in range(256): struct.pack_into('<H', b, 512 + 2 * j, 0x5A00 + j)
for v in 'ecg':
    open('diskdma_test.dmA.%s.img' % v, 'wb').write(b)
    open('diskdma_test.dm1.%s.img' % v, 'wb').write(bytes(16 * 512))"
    $EMU -d2 diskdma_test.dmA.e.img -d1 diskdma_test.dm1.e.img diskdma_test.bin </dev/null >/dev/null 2>diskdma_test.e.err; es=$?
    $UEMU -C -d2 diskdma_test.dmA.c.img -d1 diskdma_test.dm1.c.img diskdma_test.bin </dev/null >/dev/null 2>diskdma_test.c.err; rs=$?
    python3 ../../generators/sim_sys.py --mem --io --disk --opt=-d2 --opt=diskdma_test.dmA.g.img --opt=-d1 --opt=diskdma_test.dm1.g.img \
        diskdma_test.bin </dev/null >/dev/null 2>diskdma_test.g.err; gs=$?
    gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' diskdma_test.g.err)
    if [ $es -ne 0 ]; then bad "diskdma_test" "case $es failed on p32emu"
    elif [ $rs -ne 0 ]; then bad "diskdma_test" "case $rs failed on p32uemu -C"
    elif [ $gs -ne 0 ]; then bad "five cards: diskdma_test" "case $gs failed: $(tail -1 diskdma_test.g.err)"
    elif ! cmp -s diskdma_test.dm1.e.img diskdma_test.dm1.g.img || ! cmp -s diskdma_test.dm1.c.img diskdma_test.dm1.g.img; then
        bad "five cards: diskdma_test" "the sector written by DMA differs"
    else ok "the disk card's DMA engine (D75), gate level: diskdma_test (a sector in and out by DMA beside the CPU's accesses, the interrupt, a bus error; $gc cycles; on p32emu and p32uemu -C too)"; fi
fi

# 6c7. the video card, revision 2 (docs/video-card-acrtc.md; D103, D104,
#      D106): its GALs and netlist, and the kept part (35 ICs of YACC1-D's
#      video card V3) against the copy of that card's netlist, part for
#      part, pin for pin, net for net; that copy's own check; the bus side
#      at gate level, as wired (the chip's four-clock cycle half by half,
#      every register, the bus errors, the PS/2 handshake, the interrupts).
#      Then with delays (D107): YACC1-D's own timing check of the memory
#      side and the dots, run on this netlist at its eight corners; and the
#      bus side against the chip's host timing from Hitachi's manual, at
#      every phase of the chip's clock against the bus clock, 3 and 4 MHz.
#      And both again with the routed board's tracks as delays (D108): the
#      kept part's margins are a few ns; hardware/video-card/kicad/
#      video-card-tracks.json (generators/board_tracks.py, written by
#      build_board.sh) must be of the board file beside it.
if out=$(python3 ../../generators/card_video.py 2>&1); then ok "${out}"
else bad "video card" "$out"; fi
if out=$(python3 ../../hardware/video-card/yacc1-v3/hardware/cards/video/kicad/v3.0/v30_netlist.py 2>&1); then ok "YACC1-D's video card V3 netlist, the copy the kept part is checked against ($(echo "$out" | tail -1 | sed 's/^check: //'))"
else bad "YACC1-D's V3 netlist (hardware/video-card/yacc1-v3)" "$(echo "$out" | tail -3)"; fi
if out=$(python3 ../../generators/sim_video.py 2>&1); then ok "${out}"
else bad "video card, bus side at gate level" "$(echo "$out" | tail -4)"; fi
if out=$(python3 ../../generators/sim_video_timing.py 2>&1); then
    ok "$(echo "$out" | sed -n 1p)"
    case "$(echo "$out" | sed -n 2p)" in
        "video card with the board's tracks"*) ok "$(echo "$out" | sed -n 2p)";;
        *) bad "video card with the board's tracks as delays" "$(echo "$out" | sed -n '2,$p' | tail -3)";;
    esac
else bad "video card with delays" "$(echo "$out" | tail -8)"; fi
#      And the card in a machine at gate level (the CPU, MMU / data, memory,
#      I/O and video cards) against p32uemu -C, instruction for instruction
#      and cycle for cycle, at 3 MHz and with the clock jumper at 4:
#      vidcard_test (the chip's cycle, its registers and FIFO, the PS/2
#      latches, both interrupts taken, each with its vector from the
#      acknowledge cycle, bus errors). Only the chip's and the ATmega328P's
#      insides are p32io.h's models, at their pins.
if asm vidcard_test; then
    printf abc >vidcard_keys
    $EMU -K vidcard_keys vidcard_test.bin </dev/null >/dev/null 2>vidcard_test.e.err; es=$?
    vbad=""; vcyc=""
    for f in "" "-F"; do
        $UEMU -C $f -Kr -K vidcard_keys -s vidcard_test.c.trace vidcard_test.bin </dev/null >/dev/null 2>vidcard_test.c.err; rs=$?
        uc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' vidcard_test.c.err)
        python3 ../../generators/sim_sys.py --mem --io --video ${f:+--fast} -s vidcard_test.g.trace --opt=-K --opt=vidcard_keys \
            vidcard_test.bin </dev/null >/dev/null 2>vidcard_test.g.err; gs=$?
        gc=$(sed -n 's/.*instructions, \([0-9]*\) cycles.*/\1/p' vidcard_test.g.err)
        if [ $rs -ne 0 ]; then vbad="case $rs failed on p32uemu -C $f"
        elif [ $gs -ne 0 ]; then vbad="gate level${f:+ at 4 MHz}: exit status $gs: $(tail -1 vidcard_test.g.err)"
        elif ! cmp -s vidcard_test.c.trace vidcard_test.g.trace; then
            vbad="gate level${f:+ at 4 MHz}: state differs at instruction $(cmp vidcard_test.c.trace vidcard_test.g.trace | sed 's/.*line //')"
        elif [ "$uc" != "$gc" ]; then vbad="gate level${f:+ at 4 MHz}: $gc cycles, p32uemu -C $f $uc"; fi
        [ -n "$vbad" ] && break
        vcyc="$vcyc${vcyc:+, }$gc cycles as p32uemu -C${f:+ $f}"
    done
    if [ $es -ne 0 ]; then bad "vidcard_test" "case $es failed on p32emu"
    elif [ -n "$vbad" ]; then bad "five cards: vidcard_test" "$vbad"
    else ok "cpu + mmu + memory + I/O + video cards, gate level: vidcard_test (the chip's cycle, its registers and FIFO, the PS/2 latches and the firmware's time (FULL clear after a read, BUSY, a command written over another), three interrupts taken with their vectors, bus errors: $(wc -l < vidcard_test.g.trace | tr -d ' ') instructions, $vcyc)"; fi
    rm -f vidcard_keys
fi

# 6c6. the debug card (docs/debug-card.md, D64): its GALs and netlist; then
#      at gate level in slot 0: LISTEN (a running program's bus cycles
#      captured and read back through its host link), DRIVE by DMA (a word
#      written while the CPU runs: the arbiter's slot 0), and ALONE (its own
#      clock, with a memory card only)
if out=$(python3 ../../generators/card_debug.py 2>&1); then ok "${out}"
else bad "debug card" "$out"; fi
out=$(python3 ../../generators/sim_debug.py listen hello.bin 2>&1 >/dev/null); st=$?
if [ $st -eq 0 ]; then ok "debug card, gate level: $(echo "$out" | grep '^listen')"
else bad "debug card: listen" "$(echo "$out" | tail -2)"; fi
if asm dma_test; then
    out=$(python3 ../../generators/sim_debug.py dma dma_test.bin 2>&1 >/dev/null); st=$?
    if [ $st -eq 0 ]; then ok "debug card, gate level: $(echo "$out" | grep '^dma') (DMA in slot 0, the CPU running)"
    else bad "debug card: dma" "$(echo "$out" | tail -2)"; fi
fi
if out=$(python3 ../../generators/sim_debug.py alone 2>&1); then ok "debug card, gate level: $out"
else bad "debug card: alone" "$out"; fi
# the clock stepping (D76): the debug card clocks the CPU card over XCLK,
# one phase per host STEP; the CPU card's own CLOCK STEP button
if out=$(python3 ../../generators/sim_debug.py xclk 2>&1); then ok "debug + CPU cards, gate level: $out"
else bad "debug card: xclk" "$out"; fi

# 6c8. the GALs' JEDEC files (hardware/gal, D67): each checked through its
#      fuse map against the GAL's spec, and the committed files up to date
jt=$(mktemp -d)
if out=$(python3 ../../generators/p32jed.py "$jt" 2>&1) && diff -r -q -x README.md "$jt" ../../hardware/gal >/dev/null 2>&1; then
    ok "GAL fuse files: $(echo "$out" | tail -1 | sed "s|$jt|hardware/gal|"), the committed ones current"
else bad "GAL fuse files" "$(echo "$out" | tail -3; diff -r -q -x README.md "$jt" ../../hardware/gal | head -3)"; fi
rm -rf "$jt"

# 6d. the project's footprints (hardware/lib) are generated: up to date
if python3 -c "
import sys; sys.path.insert(0, '../../generators'); import gen_footprints
sys.exit(not all(open('../../hardware/lib/P32W.pretty/%s.kicad_mod' % n).read() == f() for n, f in gen_footprints.FOOTPRINTS.items()))" 2>/dev/null
then ok "footprints (the Tang Nano 20K and WIZ850io sockets current)"
else bad "footprints" "stale (python3 generators/gen_footprints.py)"; fi
if python3 ../../generators/gen_nano_cst.py nano.check.cst >/dev/null 2>&1 && cmp -s nano.check.cst ../../fpga/alu/p32alu_nano.cst
then ok "the ALU module's FPGA pin constraints current (fpga/alu/p32alu_nano.cst)"
else bad "nano cst" "stale (python3 generators/gen_nano_cst.py)"; fi
rm -f nano.check.cst

# 7. the bus definition (D50, D52): consistent with the microcode, every
#    radial line two-ended, the document up to date
if msg=$(python3 ../../generators/p32bus.py --check --doc bus.check.md 2>&1) \
   && cmp -s bus.check.md ../../docs/bus.md; then ok "bus ($msg)"
else bad bus "$msg; or docs/bus.md is stale (python3 generators/p32bus.py --doc docs/bus.md)"; fi
rm -f bus.check.md

# 7a. the backplane (D68): every socket pin on busnet(), every card fits the
#     slots it may use, the pull-up rule against every card's drivers
if out=$(python3 ../../generators/card_backplane.py 2>&1); then ok "${out}"
else bad "backplane" "$out"; fi

# 7b. the power budget (D69): every part of every card has a supply current,
#     and docs/power-budget.md is current
if out=$(python3 ../../generators/p32power.py --doc power.check.md 2>&1) && cmp -s power.check.md ../../docs/power-budget.md; then
    ok "power budget ($(echo "$out" | tail -1))"
else bad "power budget" "$(echo "$out" | tail -2); or docs/power-budget.md is stale (python3 generators/p32power.py --doc docs/power-budget.md)"; fi
rm -f power.check.md

# 7c. the timing pass (D71): every part has its switching times; the
#     analysis of the machine, the video card's dots and the debug card, and
#     docs/timing.md current
if out=$(python3 ../../generators/p32timing.py --doc timing.check.md 2>&1) && cmp -s timing.check.md ../../docs/timing.md; then
    ok "timing ($(echo "$out" | head -1))"
else bad "timing" "$(echo "$out" | tail -2); or docs/timing.md is stale (python3 generators/p32timing.py --doc docs/timing.md)"; fi
rm -f timing.check.md

# 12. assembler rejects bad input
printf '        addi r1, r0, 40000\n' > bad_range.asm
if $ASM bad_range.asm -o bad_range.bin 2>bad_range.err; then bad bad_range "out-of-range immediate accepted"
elif grep -q "out of range" bad_range.err; then ok "assembler rejects out-of-range immediate"
else bad bad_range "wrong message: $(cat bad_range.err)"; fi
rm -f bad_range.asm

# 13. M1 (D22): relocatable objects, the linker, archives, a.out
if msg=$(python3 ../../assembler/test_link.py 2>&1); then ok "${msg#PASS }"
else bad "link tests" "$msg"; fi

# (the next two link stand-alone programs: on a fresh tree, before section 14
# builds the library, their start-up file is not there yet)
make -s -C ../../software build/sa/crt0_sa.o

# 13u. the I/O card's USB controller (D43): the CH376S model in
#      p32io.h, driven by a stand-alone program in both emulators -- with a
#      drive, with the drive pulled out after one sector, and with none
python3 -c "
b = bytearray(64 * 1024); b[0:4] = b'P32W'; b[1023] = 0xA5
open('usb_stick.img', 'wb').write(b)"
if python3 ../../cc/p32cc.py -w --sa usb_model.c -o usb_model.bin 2>usb_model.err; then
    usbbad=""
    for c in with out none; do
        case $c in with) ua="-U usb.img";; out) ua="-U usb.img -Uout 1";; none) ua="";; esac
        for emu in "../p32emu" "../p32uemu -u ../../microcode/p32ucode.bin"; do
            cp usb_stick.img usb.img
            $emu $ua -l 50000000 usb_model.bin </dev/null 2>&1 | grep -a -v "^HALT at" > usb_model.out
            cmp -s usb_model.out usb_model.$c.expect || usbbad="$usbbad $c(${emu%% *})"
        done
    done
    if [ -z "$usbbad" ]; then ok "USB (CH376S model): a drive read, written, past its end; pulled out mid-transfer; none (both emulators)"
    else bad "USB (CH376S model)" "differs:$usbbad"; fi
else bad "USB (CH376S model)" "$(cat usb_model.err)"; fi
rm -f usb.img usb_stick.img usb_model.out usb_model.err usb_model.bin

# 13n. the I/O card's network coprocessor (D45; the mailbox model in
#      p32io.h, carried out with the Mac's sockets), driven by a stand-alone
#      program in both emulators against servers net_peer.py runs on the
#      loopback (TCP, UDP, TLS with a test certificate, a client for P32W's
#      listener); and with no coprocessor (no -N)
if python3 ../../cc/p32cc.py -w --sa net_model.c -o net_model.bin 2>net_model.err; then
    netbad=""
    for emu in "../p32emu" "../p32uemu -u ../../microcode/p32ucode.bin"; do
        python3 net_peer.py $emu -l 2000000000 net_model.bin > net_model.out 2>&1
        cmp -s net_model.out net_model.with.expect || netbad="$netbad with(${emu%% *})"
        $emu -l 2000000 net_model.bin </dev/null 2>&1 | grep -a -v "^HALT at" > net_model.out
        cmp -s net_model.out net_model.none.expect || netbad="$netbad none(${emu%% *})"
    done
    if [ -z "$netbad" ]; then ok "network (coprocessor mailbox model): TCP, a refusal, DNS, UDP, listen and accept with ATTN, TLS and a wrong name, all sockets used, bad requests, reset; none (both emulators)"
    else bad "network (coprocessor mailbox model)" "differs:$netbad"; fi
else bad "network (coprocessor mailbox model)" "$(cat net_model.err)"; fi
rm -f net_model.out net_model.err net_model.bin

# 14. M2 (D22): the C library builds with p32cc; every test program behaves
#     exactly as the host compiler's build of it
if ! out=$(make -s -C ../../software 2>&1); then bad "libc build" "$out"
elif msg=$(python3 ../../cc/run_tests.py 2>&1); then ok "${msg#PASS }"
else bad "p32cc tests" "$msg"; fi

# 15. M3 (D22): the disk-image tool and its fsck
if msg=$(python3 ../../tools/test_p32fs.py 2>&1); then ok "${msg#PASS }"
else bad "p32fs" "$msg"; fi

if msg=$(python3 ../../kernel/check_syscalls.py 2>&1); then ok "${msg#PASS }"; else bad "syscall table" "$msg"; fi

# 16. M3 (D22): P32W Unix boots from a fresh disk in p32emu and runs a shell
#     session (files, pipes, background, ps, errors, halt) exactly as
#     recorded; the host's fsck finds the disk it leaves clean
if ! out=$(make -s -C ../../software testdisk 2>&1); then bad "P32W Unix build" "$out"
elif ../../software/tests/run_session.sh session1 > session1.out 2>&1 && cmp -s session1.out ../../software/tests/session1.expect; then
    ok "P32W Unix session (boot, $(grep -c '^# ' session1.out) commands as recorded, disk fsck clean)"
else bad "P32W Unix session" "$(diff session1.out ../../software/tests/session1.expect | head -4)"; fi
#     ... the same on the microcode-level emulator: the real control store runs the OS
if ../../software/tests/run_session.sh session1 uemu > session1.u.out 2>&1 && cmp -s session1.u.out ../../software/tests/session1.expect; then
    ok "P32W Unix session on the microcode-level emulator (identical transcript)"
else bad "P32W Unix session (p32uemu)" "$(diff session1.u.out ../../software/tests/session1.expect | head -4)"; fi
rm -f session1.u.out
rm -f session1.out
#     ... and two users at once (M4): logins on both terminals, wrong
#     password, permissions between users, su, passwd refused, who
if ../../software/tests/run_session.sh session2 > session2.out 2>&1 && cmp -s session2.out ../../software/tests/session2.expect; then
    ok "P32W Unix multi-user session (two terminals, logins, permissions, su; as recorded)"
else bad "P32W Unix multi-user session" "$(diff session2.out ../../software/tests/session2.expect | head -4)"; fi
rm -f session2.out
#     ... and scripting (M5): a shell script with for / if / while, `...`,
#     expr, test / [, multi-line constructs at the prompt, kill $!, ed
if ../../software/tests/run_session.sh session3 > session3.out 2>&1 && cmp -s session3.out ../../software/tests/session3.expect; then
    ok "P32W Unix scripting session (control flow, command substitution, ed; as recorded)"
else bad "P32W Unix scripting session" "$(diff session3.out ../../software/tests/session3.expect | head -4)"; fi
rm -f session3.out

# 16d. shutdown, reboot and the clock: uptime, time, shutdown -r (init stops
#      the logins, the machine restarts into the monitor), a second boot
if ../../software/tests/run_session.sh session4 > session4.out 2>&1 && cmp -s session4.out ../../software/tests/session4.expect; then
    ok "P32W Unix shutdown session (uptime, time, shutdown -r, a second boot; as recorded)"
else bad "P32W Unix shutdown session" "$(diff session4.out ../../software/tests/session4.expect | head -4)"; fi
rm -f session4.out

# 16e. the shell's history and line editing: history, !! !n !-n !text, the
#      arrow keys, ^B ^U ^W, history kept across logins ($HOME/.sh_history)
if ../../software/tests/run_session.sh session5 > session5.out 2>&1 && cmp -s session5.out ../../software/tests/session5.expect; then
    ok "P32W Unix shell history session (history, !!, !n, !text, arrows, ^U ^W, across logins; as recorded)"
else bad "P32W Unix shell history session" "$(diff session5.out ../../software/tests/session5.expect | head -4)"; fi
rm -f session5.out

# 16f. the manual and the newer tools: man (pages, sections, -k), cc -h -S -o - -s
#      -v -l, mv to the same file, a failed cd's status
if ../../software/tests/run_session.sh session6 > session6.out 2>&1 && cmp -s session6.out ../../software/tests/session6.expect; then
    ok "P32W Unix manual session (man, man -k, cc options, mv, cd status; as recorded)"
else bad "P32W Unix manual session" "$(diff session6.out ../../software/tests/session6.expect | head -4)"; fi
rm -f session6.out

# 16h. make and /usr/src: nothing to do on a fresh system; an edited command
#      is rebuilt alone, installed as root; the compiler and library up to date
if ../../software/tests/run_session.sh session7 > session7.out 2>&1 && cmp -s session7.out ../../software/tests/session7.expect; then
    ok "P32W Unix make session (/usr/src: rebuild what changed, make install; as recorded)"
else bad "P32W Unix make session" "$(diff session7.out ../../software/tests/session7.expect | head -4)"; fi
rm -f session7.out

# 16i. the shell's redirections of any descriptor (2> 2>> 2>&1 >&2 : > f) and
#      long argument lists (a glob of 148 names; the kernel takes 250 arguments)
if ../../software/tests/run_session.sh session8 > session8.out 2>&1 && cmp -s session8.out ../../software/tests/session8.expect; then
    ok "P32W Unix redirection session (2> 2>&1 >&2, 148-name glob, 100 arguments; as recorded)"
else bad "P32W Unix redirection session" "$(diff session8.out ../../software/tests/session8.expect | head -4)"; fi
rm -f session8.out

# 16j. BASIC (D27): a program loaded, listed, run (INPUT, PRINT USING, FN,
#      doubles), renumbered, saved; basic file.bas from the shell. And
#      INKEY$ (D113): keys taken one at a time at the terminal, unechoed;
#      none waiting at the prompt; SHELL after it; from a pipe; the
#      terminal as it was when BASIC has left (stty). And D114: a line
#      with a two-byte keyword (SQLERR$) listed, run, renumbered and
#      saved; INPUT$(3) at the terminal
if ../../software/tests/run_session.sh session9 > session9.out 2>&1 && cmp -s session9.out ../../software/tests/session9.expect; then
    ok "P32W BASIC session (LOAD, LIST, RUN, INPUT, USING, RENUM, SAVE; INKEY\$ a key at a time, from a pipe, the terminal restored; INPUT\$; a two-byte keyword listed and renumbered; as recorded)"
else bad "P32W BASIC session" "$(diff session9.out ../../software/tests/session9.expect | head -4)"; fi
rm -f session9.out

# 16k. the shell's Tab completion: commands, files, directories, the choices
if ../../software/tests/run_session.sh session10 > session10.out 2>&1 && cmp -s session10.out ../../software/tests/session10.expect; then
    ok "P32W Unix completion session (Tab: commands, files, directories, the choices; as recorded)"
else bad "P32W Unix completion session" "$(diff session10.out ../../software/tests/session10.expect | head -4)"; fi
rm -f session10.out

# 16l. the SQL database: its own tests on the host (every statement, indexes,
#      joins, transactions, a killed writer), then on P32W: sql, a join with
#      GROUP BY, and a C program built with -lsql
if msg=$(sh ../../software/sql/test.sh 2>&1); then ok "SQL library and sql command on the host (${msg##*: })"
else bad "SQL tests on the host" "$(printf '%s' "$msg" | tail -4)"; fi
if ../../software/tests/run_session.sh session11 > session11.out 2>&1 && cmp -s session11.out ../../software/tests/session11.expect; then
    ok "P32W SQL session (sql: create, insert, join, group by; a C program with -lsql; as recorded)"
else bad "P32W SQL session" "$(diff session11.out ../../software/tests/session11.expect | head -4)"; fi
rm -f session11.out

# 16m. BASIC with SQL: SQLOPEN, SQLEXEC, SQLQUERY / SQLNEXT / SQLVAL, an SQL
#      error caught by ON ERROR; BASIC rebuilt on P32W with -lsql, identical
if ../../software/tests/run_session.sh session12 > session12.out 2>&1 && cmp -s session12.out ../../software/tests/session12.expect; then
    ok "P32W BASIC + SQL session (inventory.bas; make basic -lsql identical; as recorded)"
else bad "P32W BASIC + SQL session" "$(diff session12.out ../../software/tests/session12.expect | head -4)"; fi
rm -f session12.out

# 16o. the system's own tests on P32W (/usr/tests: sh /usr/tests/run): C
#      programs compiled there, BASIC, SQL and the shell
if ../../software/tests/run_session.sh session13 > session13.out 2>&1 && cmp -s session13.out ../../software/tests/session13.expect; then
    ok "P32W Unix's own test suite, run on P32W ($(grep -c '^PASS' session13.out) tests; as recorded)"
else bad "P32W Unix's own test suite" "$(grep FAIL session13.out | head -4)"; fi
rm -f session13.out

# 16p. the system's own administration: login's three tries, df for any user
#      (ustat), su - / su -c, mkfs and rmdir refusing what is mounted, and
#      fsck -y repairing a deliberately broken ram disk
if ../../software/tests/run_session.sh session14 > session14.out 2>&1 && cmp -s session14.out ../../software/tests/session14.expect; then
    ok "P32W Unix administration session (login, df, su, mkfs, fsck -y repairs; as recorded)"
else bad "P32W Unix administration session" "$(diff session14.out ../../software/tests/session14.expect | head -4)"; fi
rm -f session14.out

# 16q. Python (MicroPython) at a terminal: the REPL's prompts, auto-indent
#      ended with Backspace and with blank lines, an error and on, Tab
#      completion, ^D back to the shell; then python -c
if ../../software/tests/run_session.sh session15 > session15.out 2>&1 && cmp -s session15.out ../../software/tests/session15.expect; then
    ok "P32W Python session (the REPL: blocks, an error, Tab completion, ^D; as recorded)"
else bad "P32W Python session" "$(diff session15.out ../../software/tests/session15.expect | head -4)"; fi
rm -f session15.out

# 16r0. memory cards with only some banks fitted, and a hole (D60): 6 MiB
#       in window 0, no card in window 1, 4 MiB in window 2 -- the kernel's
#       2 MiB probe finds 9 MiB; eight processes and cc run across the hole
if EMUFLAGS="-M 6,0,4" ../../software/tests/run_session.sh session24 > session24.out 2>&1 && cmp -s session24.out ../../software/tests/session24.expect; then
    ok "P32W Unix on partly fitted memory cards with a hole (9 MiB: 6 + 0 + 4; as recorded)"
else bad "P32W Unix on partly fitted memory cards" "$(diff session24.out ../../software/tests/session24.expect | head -4)"; fi
rm -f session24.out

# 16r. two memory cards (32 MiB, p32emu -M 2; D52): the kernel's probe finds
#      the second (and stops at the empty third window), and python, cc and
#      lua still run
if EMUFLAGS="-M 2" ../../software/tests/run_session.sh session16 > session16.out 2>&1 && cmp -s session16.out ../../software/tests/session16.expect; then
    ok "P32W Unix with two memory cards (the kernel's probe; python, cc, lua; as recorded)"
else bad "P32W Unix with two memory cards" "$(diff session16.out ../../software/tests/session16.expect | head -4)"; fi
rm -f session16.out

# 16r2. the I/O card's four UARTs (D50): P32W Unix writes to /dev/tty2
#       and /dev/tty3 (UARTs 2 and 3, -a2 / -a3); the pseudo-terminals now
#       follow them, from minor 4
if EMUFLAGS="-a2 $PWD/s20_tty2.out -a3 $PWD/s20_tty3.out" ../../software/tests/run_session.sh session20 > session20.out 2>&1 \
   && cmp -s session20.out ../../software/tests/session20.expect \
   && [ "$(tr -d '\r' < s20_tty2.out)" = "to the third serial port" ] && [ "$(tr -d '\r' < s20_tty3.out)" = "and the fourth" ]; then
    ok "P32W Unix on UARTs 2 and 3 (/dev/tty2, /dev/tty3; as recorded)"
else bad "P32W Unix on UARTs 2 and 3" "$(diff session20.out ../../software/tests/session20.expect | head -4)"; fi
rm -f session20.out s20_tty2.out s20_tty3.out

# 16r3. the second USB port (D50): a drive in each port;
#       mkfs, mount, copy, fsck on /dev/usb1, while /dev/rusb reads the other
python3 -c "open('s21_usb1.img','wb').truncate(2*1024*1024)"
if USBMB=1 EMUFLAGS="-U1 $PWD/s21_usb1.img" ../../software/tests/run_session.sh session21 > session21.out 2>&1 && cmp -s session21.out ../../software/tests/session21.expect; then
    ok "P32W Unix with a drive in each USB port (/dev/usb1; as recorded)"
else bad "P32W Unix with two USB drives" "$(diff session21.out ../../software/tests/session21.expect | head -4)"; fi
rm -f session21.out s21_usb1.img

# 16r4. the disk card (D50): the second CF socket and IDE channel A's
#       master and slave (-d1, -d2, -d2s): found at boot, a filesystem on
#       each (/dev/disk1, /dev/hda, /dev/hdb), master and slave kept apart;
#       an empty channel (/dev/rhdc) is no device
for f in d1 d2 d2s; do python3 -c "open('s22_$f.img','wb').truncate(2*1024*1024)"; done
if EMUFLAGS="-d1 $PWD/s22_d1.img -d2 $PWD/s22_d2.img -d2s $PWD/s22_d2s.img" ../../software/tests/run_session.sh session22 > session22.out 2>&1 \
   && cmp -s session22.out ../../software/tests/session22.expect; then
    ok "P32W Unix with CF 1 and IDE A's master and slave (/dev/disk1, hda, hdb; as recorded)"
else bad "P32W Unix with the disk card's drives" "$(diff session22.out ../../software/tests/session22.expect | head -4)"; fi
rm -f session22.out s22_d1.img s22_d2.img s22_d2s.img

# 16r5. the video card's screen and keyboard (D50, D53): -K types a login
#       and three commands on the PS/2 keyboard; init's getty on tty4 takes
#       them; the screen (-V) shows the kernel's messages, the login and the
#       commands' output, as recorded (less the memory line)
if EMUFLAGS="-K $PWD/../../software/tests/session23.keys -V $PWD/s23_screen" ../../software/tests/run_session.sh session23 > session23.out 2>&1 \
   && cmp -s session23.out ../../software/tests/session23.expect \
   && sed '/^memory: /d' s23_screen | cmp -s - ../../software/tests/session23.screen; then
    ok "P32W Unix on the video console (tty4: PS/2 keyboard, the screen; as recorded)"
else bad "P32W Unix on the video console" "$(diff session23.out ../../software/tests/session23.expect | head -2; sed '/^memory: /d' s23_screen | diff - ../../software/tests/session23.screen | head -4)"; fi
rm -f session23.out s23_screen s23_screen.pbm      # (the dump's text, and the picture beside it)

# 16r6a. the graphics library (D111; gfx(3)) against the chip's model, on
#        this machine: libgfx's own sources with /dev/video replaced by the
#        model (gfxhost/), and every dot of its circles, ellipses, arcs,
#        lines, boxes and text looked at in the model's memory (gfx_test.c).
#        Then the on-machine test (/usr/tests/video/gfx.c) built the same
#        way: what it prints here is the file it must print on P32W. And
#        BASIC (D112) built the same way, with the library and the model
#        behind its graphics statements: it runs /usr/tests/video/graphics.bas,
#        which draws with each statement and asks for the dots with POINT
GFXCC="cc -O1 -I gfxhost -I.. -include gfxhost/host.h"
GFXLIB="../../software/libgfx/gfx.c ../../software/libgfx/gell.c ../../software/libgfx/gtext.c ../../software/libgfx/gsin.c ../../software/libgfx/gfill.c ../../software/libgfx/gcopy.c"
if $GFXCC -o gfx_test gfx_test.c gfxhost/dev.c $GFXLIB -lm 2>gfx_test.err && out=$(./gfx_test 2>>gfx_test.err) \
   && $GFXCC -o gfx_systest ../../software/systest/video/gfx.c gfxhost/dev.c $GFXLIB -lm 2>>gfx_test.err \
   && { ./gfx_systest; echo "exit $?"; } | cmp -s - ../../software/systest/video/gfx.c.expect; then
    ok "libgfx against the HD63484's model (${out#gfx_test: }: circles and ellipses dot for dot against a 64-bit walk, arcs, lines, boxes, clipping, text, each dot drawn once; line styles, fill patterns, filled outlines and gfill against a dot-by-dot reference, pieces copied, kept, read and written; the on-machine test's expected output is this machine's)"
else bad "libgfx against the HD63484's model" "$(./gfx_test 2>&1 | head -3; head -2 gfx_test.err; { ./gfx_systest; echo "exit $?"; } 2>/dev/null | diff - ../../software/systest/video/gfx.c.expect | head -3)"; fi
rm -rf gfx_o; mkdir gfx_o               # (BASIC's own files and SQL's: compiled as they are, without host.h)
if (cd gfx_o && cc -O1 -w -I ../gfxhost -I ../../../software/sql -c ../../../software/cmd/basic.c ../../../software/cmd/bexpr.c \
        ../../../software/cmd/bio.c ../../../software/cmd/bgfx.c ../../../software/libc/softfp.c ../../../software/sql/sqlstore.c \
        ../../../software/sql/sqlbtree.c ../../../software/sql/sqlparse.c ../../../software/sql/sqleval.c \
        ../../../software/sql/sqlselect.c ../../../software/sql/sqldb.c) 2>gfx_basic.err \
   && $GFXCC -o gfx_basic gfx_o/*.o gfxhost/dev.c $GFXLIB -lm 2>>gfx_basic.err \
   && { ./gfx_basic ../../software/systest/video/graphics.bas; echo "exit $?"; } 2>>gfx_basic.err | cmp -s - ../../software/systest/video/graphics.bas.expect; then
    ok "BASIC's graphics statements against the HD63484's model ($(sed -n 's/^graphics.bas: *//p' ../../software/systest/video/graphics.bas.expect): SCREEN, PSET, PRESET, LINE with B and BF, CIRCLE with arcs, radii and aspect, COLOR, LOCATE and PRINT, CLS, POINT; the on-machine test's expected output is this machine's)"
else bad "BASIC's graphics statements against the HD63484's model" "$(head -2 gfx_basic.err; { ./gfx_basic ../../software/systest/video/graphics.bas; echo "exit $?"; } 2>&1 | diff - ../../software/systest/video/graphics.bas.expect | head -4)"; fi
rm -rf gfx_test gfx_systest gfx_basic gfx_o

# 16r6. /dev/video (D110; video(4)) and the graphics library (D111; gfx(3)):
#       a program draws with the video card's chip. The on-machine tests
#       (sh /usr/tests/run video): one draws through the device, reads
#       back what it drew, writes to the console meanwhile, wrecks
#       everything the console depends on in the chip and leaves in the
#       middle of a command; the other draws the library's figures and
#       reads them back; the third is BASIC's (D112: graphics.bas, each
#       statement and POINT). Then the two C examples, compiled there (the
#       clock with -lgfx), BASIC's example, make in the library's source
#       (nothing to do) and its page. The transcript as recorded, and the
#       screen at the end: the console's text, with the line written while
#       a program had the screen
if EMUFLAGS="-V $PWD/s25_screen" ../../software/tests/run_session.sh session25 > session25.out 2>&1 \
   && cmp -s session25.out ../../software/tests/session25.expect \
   && sed '/^memory: /d' s25_screen | cmp -s - ../../software/tests/session25.screen; then
    ok "P32W Unix: /dev/video, libgfx and BASIC's graphics (a program draws and reads back, 19 checks on the machine; the library's figures read back, 29 checks; BASIC's statements, 29 checks; the three examples run; the console's screen given back; as recorded)"
else bad "P32W Unix: /dev/video" "$(diff session25.out ../../software/tests/session25.expect | head -3; sed '/^memory: /d' s25_screen | diff - ../../software/tests/session25.screen | head -4)"; fi
rm -f session25.out s25_screen s25_screen.pbm

# 16s. a USB drive (D43; the CH376S model, a blank 4 MB image): mkfs, mount,
#      files copied on, fsck, read back, the raw device; then the drive
#      pulled out in the middle of a copy: an error, not a hang
if USBMB=4 ../../software/tests/run_session.sh session17 > session17.out 2>&1 && cmp -s session17.out ../../software/tests/session17.expect; then
    ok "P32W Unix with a USB drive (mkfs, mount, copy, fsck, /dev/rusb; as recorded)"
else bad "P32W Unix with a USB drive" "$(diff session17.out ../../software/tests/session17.expect | head -4)"; fi
if USBMB=4 USBOUT=600 ../../software/tests/run_session.sh session18 > session18.out 2>&1 && cmp -s session18.out ../../software/tests/session18.expect; then
    ok "P32W Unix with a USB drive pulled out mid-copy (cp: write error; the system carries on; as recorded)"
else bad "P32W Unix with a USB drive pulled out" "$(diff session18.out ../../software/tests/session18.expect | head -4)"; fi
if USBMB=40 ../../software/tests/run_session.sh session19 > session19.out 2>&1 && cmp -s session19.out ../../software/tests/session19.expect; then
    ok "P32W Unix's FAT tools (dosformat FAT16 and FAT32, doscp both ways, dosdir, dosrm, dosmkdir; as recorded)"
else bad "P32W Unix's FAT tools" "$(diff session19.out ../../software/tests/session19.expect | head -4)"; fi
rm -f session17.out session18.out session19.out

# 16t. sockets in P32W Unix (D45): nettest.c through the BSD calls, as ken,
#      against servers net_test.py runs on the Mac (TCP, UDP, TLS echo; a
#      client for its listening socket), with the coprocessor model (-N)
if python3 ../../software/tests/net_test.py > net_test.out 2>&1 && cmp -s net_test.out ../../software/tests/net_test.expect; then
    ok "P32W Unix's sockets: TCP, UDP, TLS, a server, DNS, select, O_NONBLOCK, a signal, the errors (as recorded)"
else bad "P32W Unix's sockets" "$(diff net_test.out ../../software/tests/net_test.expect | head -4)"; fi
rm -f net_test.out

# 16u. the network programs (D45): ifconfig, ping, fetch (HTTP and HTTPS,
#      a redirection, chunks, a 404, a wrong certificate) and telnet (its
#      negotiation), against netcmd_test.py's servers on the Mac
if python3 ../../software/tests/netcmd_test.py > netcmd_test.out 2>&1 && cmp -s netcmd_test.out ../../software/tests/netcmd_test.expect; then
    ok "network programs: ifconfig, ping, fetch over HTTP and HTTPS, telnet (as recorded)"
else bad "network programs" "$(diff netcmd_test.out ../../software/tests/netcmd_test.expect | head -4)"; fi
rm -f netcmd_test.out

# 16v. network services (D46): telnetd (two remote logins on a pseudo-
#      terminal, as the Mac's client), ftp (against an FTP server here) and
#      ntpdate (against an SNTP server here)
if python3 ../../software/tests/netsvc_test.py > netsvc_test.out 2>&1 && cmp -s netsvc_test.out ../../software/tests/netsvc_test.expect; then
    ok "network services: telnetd on pseudo-terminals, ftp, ntpdate (as recorded)"
else bad "network services" "$(diff netsvc_test.out ../../software/tests/netsvc_test.expect | head -4)"; fi
rm -f netsvc_test.out

# 16n. sqlui, the SQL text interface: driven through a pseudo-terminal on the
#      host (screens, browsing, the form, queries, the terminal restored)
if msg=$(cd ../../software/sql && python3 sqlui_test.py 2>&1); then ok "sqlui, the SQL text interface (61 checks through a pty on the host)"
else bad "sqlui" "$(printf '%s' "$msg" | grep -v '^ok' | tail -4)"; fi

# 16g. every manual page in the format man shows (72 columns, ASCII, the
#      header line, NAME first)
if msg=$(python3 ../../tools/mancheck.py check ../../software/man/man*/*.[1-8] 2>&1); then ok "manual pages: ${msg#ok: } well formed"
else bad "manual pages" "$(printf '%s' "$msg" | head -4)"; fi

# 17. the archivers against the host's (D25): archives made on the Mac are
#     extracted and made again on P32W, and the Mac checks the result
../../software/tests/archive_test.sh > archive.out 2>&1
while IFS= read -r line; do
    case "$line" in
    PASS\ *) ok "${line#PASS }" ;;
    FAIL\ *) bad "${line#FAIL }" "" ;;
    SKIP\ *) skip "${line#SKIP }" ;;
    esac
done < archive.out
grep -q '^PASS' archive.out || bad "archive tests" "$(head -3 archive.out)"
rm -f archive.out

# 17f. the FAT tools against the Mac's FAT code (D44): a stick the Mac made,
#      read and written on P32W; sticks dosformat made; one that fills up;
#      the Mac's fsck_msdos and a read-only mount check each
../../software/tests/fat_test.sh > fat.out 2>&1
while IFS= read -r line; do
    case "$line" in
    PASS\ *) ok "${line#PASS }" ;;
    FAIL\ *) bad "${line#FAIL }" "" ;;
    SKIP\ *) skip "${line#SKIP }" ;;
    esac
done < fat.out
grep -qE '^(PASS|SKIP)' fat.out || bad "FAT tests" "$(head -3 fat.out)"
rm -f fat.out

# 18. kermit against the host's pykermit over the emulator's second terminal
#     (TCP): the server, GET, remote dir, a lossy line, kermit -s
python3 ../../software/tests/kermit_test.py > kermit.out 2>&1
while IFS= read -r line; do
    case "$line" in
    PASS\ *) ok "${line#PASS }" ;;
    FAIL\ *) bad "${line#FAIL }" "" ;;
    esac
done < kermit.out
grep -q '^PASS' kermit.out || bad "kermit tests" "$(tail -3 kermit.out)"
rm -f kermit.out

# 19. the native toolchain (M7): as / ld / ar on P32W against the host's tools
sh ../../software/tests/toolchain_test.sh > tool.out 2>/dev/null
while IFS= read -r line; do
    case "$line" in
    PASS\ *) ok "${line#PASS }" ;;
    FAIL\ *) bad "${line#FAIL }" "" ;;
    esac
done < tool.out
grep -q '^PASS' tool.out || bad "toolchain tests" "$(tail -3 tool.out)"
rm -f tool.out

# 20. cc in C (M7): the same assembly as p32cc; on P32W it builds a program,
#     and itself (self-hosting: identical to the host-built cc)
sh ../../software/tests/cc_test.sh > cc.out 2>/dev/null
while IFS= read -r line; do
    case "$line" in
    PASS\ *) ok "${line#PASS }" ;;
    FAIL\ *) bad "${line#FAIL }" "" ;;
    esac
done < cc.out
grep -q '^PASS' cc.out || bad "cc tests" "$(tail -3 cc.out)"
rm -f cc.out

# 20a. the measuring gate on the running system (D85): the emulator's count of
#      cycles against the system's own clock
sh ../../software/tests/cycles_test.sh > cyc.out 2>&1
while IFS= read -r line; do
    case "$line" in
    PASS\ *) ok "${line#PASS }" ;;
    FAIL\ *) bad "${line#FAIL }" "" ;;
    esac
done < cyc.out
grep -q '^PASS' cyc.out || bad "cycles" "$(tail -2 cyc.out)"
rm -f cyc.out

# 20b. the kernel built on P32W itself (D86): byte for byte the host's
if sh ../../software/tests/kernel_test.sh > ker.out 2>&1 && grep -q '^PASS' ker.out; then ok "$(sed -n 's/^PASS //p' ker.out)"
else bad "kernel on P32W" "$(tail -2 ker.out)"; fi
rm -f ker.out

# 22. db, the debugger, on a program compiled on P32W
if sh ../../software/tests/db_test.sh > db.out 2>&1 && grep -q '^PASS' db.out; then ok "$(sed -n 's/^PASS //p' db.out)"
else bad "db" "$(tail -2 db.out)"; fi
rm -f db.out

# 21. vi, typed at on a terminal (the emulator's TCP line)
if python3 ../../software/tests/vi_test.py > vi.out 2>&1 && grep -q '^PASS' vi.out; then ok "$(sed -n 's/^PASS //p' vi.out)"
else bad "vi" "$(tail -2 vi.out)"; fi
rm -f vi.out

# 22. the debugger (D97, tools/p32dbg): its commands against the monitor alone,
#     in both emulators (the console's input queued before the run, so every
#     stop is at the same instruction); and its browser side, the web server
for e in p32emu p32uemu; do
    f=; [ $e = p32uemu ] && f=-u
    if python3 ../../tools/p32dbg --no-disk $f --timeout 30 --script ../../tools/p32dbg/tests/session.cmds > dbg.out 2>&1 \
       && cmp -s dbg.out ../../tools/p32dbg/tests/session.$e.expect; then
        ok "the debugger in $e: steps, breakpoints, a run to HALT, the console, memory, registers, reset$([ $e = p32uemu ] && echo ', microcycles') (as recorded)"
    else bad "the debugger in $e" "$(diff dbg.out ../../tools/p32dbg/tests/session.$e.expect | head -4)"; fi
    if msg=$(python3 ../../tools/p32dbg/tests/web_test.py $f 2>&1) && printf '%s' "$msg" | grep -q '^PASS'; then ok "${msg#PASS }"
    else bad "the debugger's browser side in $e" "$(printf '%s' "$msg" | tail -2)"; fi
done
rm -f dbg.out

echo
[ $skips -eq 0 ] || echo "$skips skipped: they need a Mac's own tools (SKIP above)"
[ $fails -eq 0 ] && echo "ALL TESTS PASSED" || echo "$fails TEST(S) FAILED"
[ $fails -eq 0 ]
