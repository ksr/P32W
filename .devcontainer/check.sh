#!/bin/sh
# check.sh -- run by .github/workflows/codespace.yml inside the codespace's container:
# P32W boots and runs on Linux as on the Mac. FULL=1 runs the whole test suite too.
# (Never on a Mac's working tree: tools/p32w boots software/build/disk.img, the disk
# P32W is used from; there, give it a copy with P32W_DISK.)
set -e
cd "$(dirname "$0")/.."
printf 'b\nroot\nroot\nuname -a\nexpr 6 \\* 7\nhalt\n' | tools/p32w -w -l 0 > /tmp/boot.out 2>&1 || true
tr -d '\r' < /tmp/boot.out | tail -8
if tr -d '\r' < /tmp/boot.out | grep -qx '42'; then echo "PASS P32W boots with tools/p32w, and runs"
else echo "FAIL: P32W did not boot and run"; exit 1; fi
sh software/tests/run_session.sh session1 > /tmp/s1.out 2>&1
if cmp -s /tmp/s1.out software/tests/session1.expect; then echo "PASS session1, as recorded on the Mac"
else diff /tmp/s1.out software/tests/session1.expect | head -20; exit 1; fi
sh software/tests/toolchain_test.sh
if [ -n "$FULL" ]; then cd emulator && make test; fi
