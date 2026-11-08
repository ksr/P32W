#!/usr/bin/env python3
"""run_tests.py -- the p32cc test suite (M2, D22; make test).

Each tests/*.c is compiled twice: by p32cc (stand-alone, run in p32emu) and
by the host's C compiler. Their output and exit status must be identical.
A test with a tests/NAME.expect file is P32W-only: its output (and a last
line "exit N") must match the file instead.

    python3 cc/run_tests.py [name ...]
"""
import os, sys, subprocess, tempfile, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
EMU = os.path.join(ROOT, "emulator", "p32emu")
CC = os.environ.get("HOSTCC", "cc")

def run_p32(src, tmp):
    out = os.path.join(tmp, "t.bin")
    r = subprocess.run([sys.executable, os.path.join(HERE, "p32cc.py"), "-w", "--sa", src, "-o", out],
                       capture_output=True, text=True)
    if r.returncode:
        return None, "compile: " + (r.stderr or r.stdout).strip()
    r = subprocess.run([EMU, "-l", "200000000", out], stdin=subprocess.DEVNULL, capture_output=True)
    return (r.stdout.decode("latin-1"), r.returncode), None

def run_host(src, tmp):
    exe = os.path.join(tmp, "host")
    extra = []
    if os.path.basename(src) == "softfp.c":     # (P32W's library code, compiled for the host too)
        extra = [os.path.join(ROOT, "software", "libc", "softfp.c"), "-idirafter", os.path.join(ROOT, "software", "include")]
    r = subprocess.run([CC, "-w", "-o", exe, src] + extra + ["-I", os.path.join(HERE, "tests"), "-lm"], capture_output=True, text=True)   # (-lm: Linux)
    if r.returncode:
        return None, "host compile: " + r.stderr.strip()
    r = subprocess.run([exe], stdin=subprocess.DEVNULL, capture_output=True)
    return (r.stdout.decode("latin-1"), r.returncode), None

def main():
    names = sys.argv[1:] or sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(HERE, "tests", "*.c")))
    fails = []
    for name in names:
        src = os.path.join(HERE, "tests", name + ".c")
        tmp = tempfile.mkdtemp(prefix="p32cct")
        got, e = run_p32(src, tmp)
        if e:
            fails.append("%s: %s" % (name, e.splitlines()[0] if e else e))
            continue
        exp_file = os.path.join(HERE, "tests", name + ".expect")
        if os.path.exists(exp_file):
            want = (open(exp_file).read(), None)
            text = got[0] + "exit %d\n" % got[1]
            ok = text == want[0]
        else:
            want, e = run_host(src, tmp)
            if e:
                fails.append("%s: %s" % (name, e))
                continue
            ok = got == want
        if not ok:
            fails.append("%s: output differs from the %s" % (name, "expected file" if os.path.exists(exp_file) else "host's"))
            if os.environ.get("VERBOSE"):
                import difflib
                w = want[0] if want[1] is None else want[0] + "exit %d\n" % want[1]
                g = got[0] + "exit %d\n" % got[1]
                sys.stderr.write("".join(difflib.unified_diff(w.splitlines(1), g.splitlines(1), "want", "got")))
    if fails:
        print("FAIL p32cc tests: %d of %d: %s" % (len(fails), len(names), "; ".join(fails)))
        sys.exit(1)
    print("PASS p32cc tests: %d programs, output identical to the host compiler's" % len(names))

if __name__ == "__main__":
    main()
