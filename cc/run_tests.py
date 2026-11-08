#!/usr/bin/env python3
"""run_tests.py -- the p32cc test suite (M2, D22; make test).

Each tests/*.c is compiled twice: by p32cc (stand-alone, run in p32emu) and
by the host's C compiler. Their output and exit status must be identical.
A test with a tests/NAME.expect file is P32W-only: its output (and a last
line "exit N") must match the file instead.

The host is a Mac. Where C leaves the choice to the C library (how printf
shows a subnormal in %a or a negative NaN, scanf's pushback, the nonzero
value isupper returns), P32W's makes the Mac's; so on another host (Linux,
a codespace: D96) a test with a tests/NAME.host file is compared with that
file, the Mac's build's output, instead. On a Mac the file must still be the
host's output, and --record-host writes it again.

    python3 cc/run_tests.py [name ...]
    python3 cc/run_tests.py --record-host name ...      (on a Mac)
"""
import os, sys, subprocess, tempfile, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
EMU = os.path.join(ROOT, "emulator", "p32emu")
CC = os.environ.get("HOSTCC", "cc")
MAC = sys.platform == "darwin"

def host_file(name):
    return os.path.join(HERE, "tests", name + ".host")

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
    args = sys.argv[1:]
    if args[:1] == ["--record-host"]:
        if not MAC:
            sys.exit("--record-host: on a Mac (a .host file is the Mac's output)")
        for name in args[1:]:
            want, e = run_host(os.path.join(HERE, "tests", name + ".c"), tempfile.mkdtemp(prefix="p32cct"))
            if e:
                sys.exit("%s: %s" % (name, e))
            with open(host_file(name), "w", encoding="latin-1", newline="") as fh:
                fh.write(want[0] + "exit %d\n" % want[1])
            print("wrote cc/tests/%s.host" % name)
        return
    names = args or sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(HERE, "tests", "*.c")))
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
            what = "expected file"
        elif not MAC and os.path.exists(host_file(name)):
            with open(host_file(name), encoding="latin-1", newline="") as fh:
                want = (fh.read(), None)
            ok = got[0] + "exit %d\n" % got[1] == want[0]
            what = "Mac's (tests/%s.host)" % name
        else:
            want, e = run_host(src, tmp)
            if e:
                fails.append("%s: %s" % (name, e))
                continue
            ok = got == want
            what = "host's"
            if ok and MAC and os.path.exists(host_file(name)):
                with open(host_file(name), encoding="latin-1", newline="") as fh:
                    if fh.read() != want[0] + "exit %d\n" % want[1]:
                        fails.append("%s: tests/%s.host is no longer the Mac's output: python3 cc/run_tests.py "
                                     "--record-host %s" % (name, name, name))
                        continue
        if not ok:
            fails.append("%s: output differs from the %s" % (name, what))
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
