#!/usr/bin/env python3
"""mksystest.py OUTDIR -- the test tree for /usr/tests on the disk (P32W Unix's
own tests, run on P32W with  sh /usr/tests/run).

  c/      the compiler's test programs (cc/tests): built with the host's cc
          and run here, their output and exit status become NAME.c.expect
  lib/    library tests that need the system (files, fork, signals):
          software/systest/lib, the same way (standard C and POSIX only)
  basic/  BASIC programs (software/systest/basic): run by a host build of
          basic (the same source), giving NAME.bas.expect
  sql/    SQL scripts (software/systest/sql): run by a host build of sql
  sh/     shell scripts (software/systest/sh), and
  cmd/    a script per command (software/systest/cmd): their .expect files
          are kept in the repository (recorded on P32W: the host's tools are not P32W's)
  video/  /dev/video, the graphics library and BASIC's graphics statements
          (software/systest/video), the same way (gfx.c's and graphics.bas's
          were made on the host, the library run against the emulators'
          model of the chip); not one of the parts run by default: it needs
          the video card (sh /usr/tests/run video)

So a test passes on P32W when P32W does what the build machine did with the
same program -- as make test checks the compilers. Run by software/Makefile.
"""
import os, shutil, subprocess, sys, tempfile, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SW = os.path.join(ROOT, "software")
CC = os.environ.get("HOSTCC", "cc")     # (each link ends in -lm: Linux keeps the maths library apart; a Mac has it in libSystem)

def run(argv, cwd, stdin=subprocess.DEVNULL):
    r = subprocess.run(argv, cwd=cwd, stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.stdout, r.returncode

# BASIC's graphics on the host: there is no video card, so SCREEN 1 fails as it does on a P32W without one
# (Device unavailable) and nothing else of the library is reached. (make test runs BASIC's graphics against the
# emulators' model of the chip: emulator/test/run_tests.sh.)
GFX_STUB = """#include <errno.h>
#include <gfx.h>
int gwidth, gheight, gcellw, gcellh;
int gopen(void) { errno = ENXIO; return -1; }
void gclose(void) {}
int gflush(void) { return -1; }
void gclear(void) {}
void gink(int ink) {}
void gpoint(int x, int y) {}
int gget(int x, int y) { return 0; }
void gline(int x0, int y0, int x1, int y1) {}
void gbox(int x0, int y0, int x1, int y1) {}
void gfbox(int x0, int y0, int x1, int y1) {}
void gellipse(int x, int y, int rx, int ry) {}
void garc(int x, int y, int rx, int ry, int a0, int a1) {}
void gtext(int x, int y, const char *s) {}
void glook(int look) {}
"""

def host_build(tmp):
    """basic and sql for the host, from the same sources as P32W's."""
    inc = os.path.join(tmp, "inc")
    os.makedirs(inc)
    shutil.copy(os.path.join(SW, "include", "softfp.h"), inc)
    shutil.copy(os.path.join(SW, "include", "gfx.h"), inc)
    stub = os.path.join(tmp, "gfxstub.c")
    open(stub, "w").write(GFX_STUB)
    sqls = [os.path.join(SW, "sql", f) for f in
            ("sqlstore.c", "sqlbtree.c", "sqlparse.c", "sqleval.c", "sqlselect.c", "sqldb.c")]
    basic = os.path.join(tmp, "basic")
    subprocess.run([CC, "-w", "-O", "-I", inc, "-I", os.path.join(SW, "sql"), "-o", basic] +
                   [os.path.join(SW, "cmd", f) for f in ("basic.c", "bexpr.c", "bio.c", "bgfx.c")] +
                   [stub, os.path.join(SW, "libc", "softfp.c")] + sqls + ["-lm"], check=True)
    sql = os.path.join(tmp, "sql")
    subprocess.run([CC, "-w", "-O", "-I", os.path.join(SW, "sql"), "-o", sql,
                    os.path.join(SW, "sql", "sql.c")] + sqls + ["-lm"], check=True)
    return basic, sql, inc

def main():
    out = sys.argv[1]
    if os.path.exists(out):
        shutil.rmtree(out)
    for d in ("c", "lib", "kern", "basic", "sql", "sh", "cmd", "video"):
        os.makedirs(os.path.join(out, d))
    shutil.copy(os.path.join(SW, "systest", "run"), out)
    shutil.copy(os.path.join(SW, "systest", "README"), out)
    tmp = tempfile.mkdtemp(prefix="mksystest")
    try:
        basic, sql, inc = host_build(tmp)
        work = os.path.join(tmp, "work")
        # C: the host's build of each test program
        for src in sorted(glob.glob(os.path.join(ROOT, "cc", "tests", "*.c"))):
            name = os.path.basename(src)
            shutil.copy(src, os.path.join(out, "c", name))
            os.makedirs(work, exist_ok=True)
            prog = os.path.join(work, "prog")
            extra = []
            if name == "softfp.c":      # (P32W's library code, compiled for the host too)
                extra = [os.path.join(SW, "libc", "softfp.c"), "-I", inc]
            host = os.path.join(ROOT, "cc", "tests", name[:-2] + ".host")
            if sys.platform != "darwin" and os.path.exists(host):   # (the Mac's output, where another C
                shutil.copy(host, os.path.join(out, "c", name + ".expect"))     # library chooses otherwise)
                shutil.rmtree(work)
                continue
            subprocess.run([CC, "-w", "-o", prog, src] + extra + ["-I", os.path.join(ROOT, "cc", "tests"), "-lm"], check=True)
            text, st = run([prog], work)
            open(os.path.join(out, "c", name + ".expect"), "wb").write(text + b"exit %d\n" % st)
            shutil.rmtree(work)
        # the library's tests: the host's build, run in a directory of their own
        for src in sorted(glob.glob(os.path.join(SW, "systest", "lib", "*.c"))):
            name = os.path.basename(src)
            shutil.copy(src, os.path.join(out, "lib", name))
            os.makedirs(work, exist_ok=True)
            prog = os.path.join(tmp, "libprog")
            subprocess.run([CC, "-w", "-o", prog, src, "-lm"], check=True)
            text, st = run([prog], work)
            open(os.path.join(out, "lib", name + ".expect"), "wb").write(text + b"exit %d\n" % st)
            shutil.rmtree(work)
        # BASIC and SQL: the host builds of basic and sql
        for kind, prog in (("basic", basic), ("sql", sql)):
            for src in sorted(glob.glob(os.path.join(SW, "systest", kind, "*"))):
                if src.endswith(".expect"):
                    continue
                name = os.path.basename(src)
                dst = os.path.join(out, kind, name)
                shutil.copy(src, dst)
                os.makedirs(work, exist_ok=True)
                if kind == "basic":
                    text, st = run([prog, src], work)
                    text += b"exit %d\n" % st
                else:
                    text, st = run([prog, "-c", "db"], work, stdin=open(src, "rb"))
                open(dst + ".expect", "wb").write(text)
                shutil.rmtree(work)
        # the shell: recorded on P32W
        for part in ("kern", "sh", "cmd", "video"):
            for src in sorted(glob.glob(os.path.join(SW, "systest", part, "*"))):
                shutil.copy(src, os.path.join(out, part, os.path.basename(src)))
    finally:
        shutil.rmtree(tmp)
    n = len(glob.glob(os.path.join(out, "*", "*.expect")))
    print("%s: %d tests" % (out, n))

if __name__ == "__main__":
    main()
