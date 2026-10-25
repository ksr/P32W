#!/usr/bin/env python3
"""mptests.py -- MicroPython's own tests (tests/basics and tests/float of the
1.29.0 release) on P32W, against the host reference build (D36, D39).

    mptests.py stage TESTS OUT        the .py files of TESTS/basics and TESTS/float
                                      into OUT/basics, OUT/float (names past 24
                                      bytes shortened: P32W's stop at 28)
    mptests.py host STAGE PYTHON OUT  each run by the host build PYTHON (HOST=1
                                      build.sh): output and exit status to OUT
    mptests.py image TESTIMG STAGE OUT
                                      a copy of TESTIMG with the tests in /tmp/mpt
                                      and /tmp/mpt/run.sh, which runs each with
                                      python, its output beside it
    mptests.py compare IMG HOSTOUT    P32W's outputs (in IMG) against the host's:
                                      a summary; exit status 1 if any differ

The tests are not in the repository (software/micropython has the
interpreter only); the release archive is (README.P32W gives its SHA-256).
emulator/test/hwtests.sh runs these steps on the microcode-level emulator.
"""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p32fs  # noqa: E402

DIRS = ("basics", "float")

def stage(tests, out):
    shutil.rmtree(out, ignore_errors=True)
    n = 0
    for d in DIRS:
        os.makedirs(os.path.join(out, d))
        for f in sorted(os.listdir(os.path.join(tests, d))):
            p = os.path.join(tests, d, f)
            if not (os.path.isfile(p) and f.endswith(".py")):
                continue
            g = f
            if len(f) > 24:
                n += 1
                g = f[:17] + "_%03d.py" % n
            shutil.copy(p, os.path.join(out, d, g))
    print("staged; %d names shortened" % n)

def host(st, python, out):
    for d in DIRS:
        os.makedirs(os.path.join(out, d), exist_ok=True)
        for f in sorted(os.listdir(os.path.join(st, d))):
            try:
                r = subprocess.run([python, f], cwd=os.path.join(st, d), stdin=subprocess.DEVNULL,
                                   capture_output=True, timeout=60)
                res = r.stdout + r.stderr + b"exit %d\n" % r.returncode
            except subprocess.TimeoutExpired:
                res = b"TIMEOUT\n"
            open(os.path.join(out, d, f + ".out"), "wb").write(res)

RUN = """for d in "basics [0-l]" "basics [m-z]" "float [0-z]"
do
    set $d
    d=$1
    cd /tmp/mpt/$d
    for f in $2*.py
    do
        python $f < /dev/null > ../out.$d/$f.out 2>&1
        echo "exit $?" >> ../out.$d/$f.out
    done
done
echo all done
"""

def image(testimg, st, out):
    shutil.copyfile(testimg, out)
    fs = p32fs.FS(out)
    fs.mkdir("/tmp/mpt", 0o777, 100, 100)
    for d in DIRS:
        fs.mkdir("/tmp/mpt/" + d, 0o777, 100, 100)
        fs.mkdir("/tmp/mpt/out." + d, 0o777, 100, 100)
        for f in sorted(os.listdir(os.path.join(st, d))):
            fs.create("/tmp/mpt/%s/%s" % (d, f), open(os.path.join(st, d, f), "rb").read(), 0o644, 100, 100)
    fs.create("/tmp/mpt/run.sh", RUN.encode(), 0o755, 100, 100)
    fs.close()

def compare(img, hostout):
    fs = p32fs.FS(img)
    same, diff, missing = [], [], []
    for d in DIRS:
        for f in sorted(os.listdir(os.path.join(hostout, d))):
            h = open(os.path.join(hostout, d, f), "rb").read()
            try:
                p = fs.readi(fs.namei("/tmp/mpt/out.%s/%s" % (d, f)))
            except p32fs.FSError:
                missing.append(d + "/" + f)
                continue
            (same if p == h else diff).append(d + "/" + f)
    print("%d identical, %d differ, %d missing" % (len(same), len(diff), len(missing)))
    for x in diff:
        print("DIFF", x)
    for x in missing[:10]:
        print("MISSING", x)
    return 1 if diff or missing else 0

def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "stage": stage(a[1], a[2])
    elif len(a) == 4 and a[0] == "host": host(a[1], a[2], a[3])
    elif len(a) == 4 and a[0] == "image": image(a[1], a[2], a[3])
    elif len(a) == 3 and a[0] == "compare": sys.exit(compare(a[1], a[2]))
    else: sys.exit(__doc__.strip())

if __name__ == "__main__":
    main()
