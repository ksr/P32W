#!/usr/bin/env python3
"""p32cc -- the P32W C compiler (M2, D22; docs/os-design.md 3.3).

    p32cc.py [-c | -S | -E] [-o OUT] [-I DIR] [-D NAME[=VAL]] [-w] files...
    p32cc.py --sa prog.c [more.c lib.o ...] -o prog.bin     stand-alone program

C89 without floating point (and, for now, without long long, bit-fields,
struct arguments / results and K&R definitions). Stages:
  preprocess (cpp.py) -> parse and type (cparse.py) -> generate assembly
  (cgen.py) -> assemble (p32asm -c) -> link (p32ld)

  -E      preprocessed source to OUT (or stdout)
  -S      assembly (.s), each statement's source line above its code
  -c      object (.o)
  --sa    link a stand-alone ROM image with the stand-alone runtime
          (software/sa/crt0_sa.s: runs in the emulators, exit = HALT)
  (default) link a P32W Unix a.out with crt0 and libc (the OS is still to come)

Include path: -I dirs, then software/include. The C library and runtimes
are built by `make -C software`.
"""
import os, sys, argparse, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "assembler"))
from clex import CError  # noqa: E402
from cpp import Preprocessor  # noqa: E402
from cparse import Parser  # noqa: E402
from cgen import Gen  # noqa: E402
import p32asm, p32obj, p32ld  # noqa: E402

INCLUDE = os.path.join(ROOT, "software", "include")
BUILD = os.path.join(ROOT, "software", "build")

def preprocess(path, incs, defs):
    pp = Preprocessor(incs + [INCLUDE], defs)
    return pp.run(path)

def compile_c(path, incs=(), defs=(), warn=True, annotate=False):
    toks = preprocess(path, list(incs), list(defs))
    msgs = []
    p = Parser(toks, (lambda m: sys.stderr.write(m + "\n")) if warn else (lambda m: None))
    p.program()
    return Gen(p, annotate).program()

def assemble(src, name):
    asm, lines, _ = p32asm.assemble(name, src, True)
    if asm.errors:
        raise CError("\n".join(asm.errors))
    obj = p32asm.obj_of(asm, name)
    if asm.errors:
        raise CError("\n".join(asm.errors))
    return obj

def detok(toks):
    out, line = [], None
    for t in toks:
        if t.line != line and out:
            out.append("\n")
        elif t.space or (out and out[-1] not in ("\n",)):
            out.append(" ")
        line = t.line
        out.append(t.text)
    return "".join(out) + "\n"

def main():
    ap = argparse.ArgumentParser(description="P32W C compiler")
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--out")
    ap.add_argument("-c", action="store_true")
    ap.add_argument("-S", action="store_true")
    ap.add_argument("-E", action="store_true")
    ap.add_argument("-I", action="append", default=[])
    ap.add_argument("-D", action="append", default=[])
    ap.add_argument("-w", action="store_true", help="no warnings")
    ap.add_argument("--sa", action="store_true", help="stand-alone image (crt0_sa)")
    ap.add_argument("-M", "--map")
    a = ap.parse_args()
    try:
        if a.E:
            text = detok(preprocess(a.files[0], a.I, a.D))
            (open(a.out, "w") if a.out and a.out != "-" else sys.stdout).write(text)
            return
        objs, tmp = [], tempfile.mkdtemp(prefix="p32cc")
        for f in a.files:
            if f.endswith(".c"):
                asm = compile_c(f, a.I, a.D, not a.w, a.S)    # (-S: with the source lines)
                base = os.path.splitext(os.path.basename(f))[0]
                if a.S:
                    open(a.out if a.out and len(a.files) == 1 else base + ".s", "w").write(asm)
                    continue
                obj = assemble(asm, base + ".s")
                if a.c:
                    p32obj.write(a.out if a.out and len(a.files) == 1 else base + ".o", obj)
                    continue
                path = os.path.join(tmp, base + ".o")
                p32obj.write(path, obj)
                objs.append(path)
            elif f.endswith(".s"):
                with open(f) as fh:
                    obj = assemble(fh.read(), f)
                if a.c:
                    p32obj.write(a.out or os.path.splitext(os.path.basename(f))[0] + ".o", obj)
                    continue
                path = os.path.join(tmp, os.path.basename(f) + ".o")
                p32obj.write(path, obj)
                objs.append(path)
            else:
                objs.append(f)                               # .o / .a
        if a.c or a.S:
            return
        if a.sa:
            crt = os.path.join(BUILD, "sa", "crt0_sa.o")
            libs = [os.path.join(BUILD, "libc.a")]
            res = p32ld.link([crt] + objs + libs, "bin", 0, 0x00100000, "_start")
            img = p32ld.output(res, "bin")
        else:
            crt = os.path.join(BUILD, "crt0.o")
            libs = [os.path.join(BUILD, "libc.a")]
            res = p32ld.link([crt] + objs + libs, "aout")
            img = p32ld.output(res, "aout")
        with open(a.out or "a.out", "wb") as fh:
            fh.write(img)
        if a.map:
            with open(a.map, "w") as fh:
                fh.write(p32ld.mapfile(res))
    except CError as e:
        sys.exit(str(e))
    except p32ld.LinkError as e:
        sys.exit("p32ld: %s" % e)

if __name__ == "__main__":
    main()
