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

def host_build(tmp):
    """basic and sql for the host, from the same sources as P32W's."""
    inc = os.path.join(tmp, "inc")
    os.makedirs(inc)
    shutil.copy(os.path.join(SW, "include", "softfp.h"), inc)
    sqls = [os.path.join(SW, "sql", f) for f in
            ("sqlstore.c", "sqlbtree.c", "sqlparse.c", "sqleval.c", "sqlselect.c", "sqldb.c")]
    basic = os.path.join(tmp, "basic")
    subprocess.run([CC, "-w", "-O", "-I", inc, "-I", os.path.join(SW, "sql"), "-o", basic] +
                   [os.path.join(SW, "cmd", f) for f in ("basic.c", "bexpr.c", "bio.c")] +
                   [os.path.join(SW, "libc", "softfp.c")] + sqls + ["-lm"], check=True)
    sql = os.path.join(tmp, "sql")
    subprocess.run([CC, "-w", "-O", "-I", os.path.join(SW, "sql"), "-o", sql,
                    os.path.join(SW, "sql", "sql.c")] + sqls + ["-lm"], check=True)
    return basic, sql, inc

def main():
    out = sys.argv[1]
    if os.path.exists(out):
        shutil.rmtree(out)
    for d in ("c", "lib", "kern", "basic", "sql", "sh", "cmd"):
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
        for part in ("kern", "sh", "cmd"):
            for src in sorted(glob.glob(os.path.join(SW, "systest", part, "*"))):
                shutil.copy(src, os.path.join(out, part, os.path.basename(src)))
    finally:
        shutil.rmtree(tmp)
    n = len(glob.glob(os.path.join(out, "*", "*.expect")))
    print("%s: %d tests" % (out, n))

if __name__ == "__main__":
    main()
