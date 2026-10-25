#!/usr/bin/env python3
"""mkmpsrc.py OUTDIR -- /usr/src/micropython for the disk (D39): MicroPython's
sources as the P32W build uses them (py, extmod, lib, ports/p32 and three
parts of shared), the tables build.sh made in genhdr/, the Makefile for make
on P32W (ports/p32/Makefile), README.P32W and LICENSE, and the objects the
host built, each beside its source (py/vm.o), so that a first make on P32W
only relinks and an edit rebuilds just that file. Run by software/Makefile.
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.normpath(os.path.join(HERE, "..", "software"))
M = os.path.join(SW, "micropython")
B = os.path.join(SW, "build", "micropython")
TREE = ["py", "extmod", "lib", "ports/p32", "shared/timeutils", "shared/readline", "shared/runtime"]
GENHDR = ["mpversion.h", "qstrdefs.generated.h", "moduledefs.h", "root_pointers.h"]

def objects():
    """the Makefile's OBJS, in order"""
    text = open(os.path.join(M, "ports", "p32", "Makefile")).read().replace("\\\n", " ")
    for line in text.split("\n"):
        if line.startswith("OBJS ="):
            return line.split("=", 1)[1].split()
    sys.exit("mkmpsrc: no OBJS in ports/p32/Makefile")

def main():
    out = sys.argv[1]
    shutil.rmtree(out, ignore_errors=True)
    for top in TREE:
        for root, _, files in os.walk(os.path.join(M, top)):
            for f in sorted(files):
                if f.endswith((".c", ".h")):
                    rel = os.path.relpath(os.path.join(root, f), M)
                    os.makedirs(os.path.join(out, os.path.dirname(rel)), exist_ok=True)
                    shutil.copyfile(os.path.join(root, f), os.path.join(out, rel))
    os.makedirs(os.path.join(out, "genhdr"))
    for f in GENHDR:
        shutil.copyfile(os.path.join(B, "genhdr", f), os.path.join(out, "genhdr", f))
    for o in objects():
        shutil.copyfile(os.path.join(B, "obj", o.replace("/", "_")), os.path.join(out, o))
    shutil.copyfile(os.path.join(M, "ports", "p32", "Makefile"), os.path.join(out, "Makefile"))
    for f in ["README.P32W", "LICENSE"]:
        shutil.copyfile(os.path.join(M, f), os.path.join(out, f))

if __name__ == "__main__":
    main()
