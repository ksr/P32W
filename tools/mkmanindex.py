#!/usr/bin/env python3
"""mkmanindex.py MANDIR -- the manual's index: MANDIR/whatis (one line per name,
for man -k) and MANDIR/links.txt (each extra name on a page's NAME line as a
hard link to it, for p32fs build). Run by software/Makefile."""
import os, re, sys
root = sys.argv[1]
whatis, links = [], []
for sec in "123458":
    d = os.path.join(root, "man" + sec)
    for f in sorted(os.listdir(d)):
        m = re.match(r"(.+)\.([1-8])$", f)
        if not m: continue
        lines = open(os.path.join(d, f)).read().split("\n")
        nl = lines[lines.index("NAME") + 1].strip()
        names, desc = nl.split(" - ", 1)
        names = [n.strip() for n in names.split(",")]
        for n in names:
            whatis.append("%s (%s) - %s" % (n, sec, desc))
            if n != m.group(1):
                links.append("man%s/%s.%s man%s/%s" % (sec, n, sec, sec, f))
whatis.sort(key=lambda s: (s.split(" (")[0].lower(), s))
links.sort()
open(os.path.join(root, "whatis"), "w").write("\n".join(whatis) + "\n")
open(os.path.join(root, "links.txt"), "w").write("\n".join(links) + "\n")
print(len(whatis), "whatis lines,", len(links), "links")
