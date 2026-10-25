#!/usr/bin/env python3
"""p32ar -- bundle P32W objects into an archive (M1, D22).

    p32ar.py LIB.a file.o ...       (create / replace)
    p32ar.py -t LIB.a               (list members and the symbols they define)

The linker takes a member only when it defines a symbol still undefined, so
a program carries only the library functions it uses.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p32obj  # noqa: E402

def main():
    a = sys.argv[1:]
    if a and a[0] == "-t":
        ar = p32obj.read(a[1])
        for name, m in sorted(ar["members"].items()):
            print("%s: %s" % (name, " ".join(sorted(n for n, s in m["symbols"].items() if s["global"]))))
        return
    if len(a) < 2:
        sys.exit("usage: p32ar.py LIB.a file.o ...  |  p32ar.py -t LIB.a")
    members = {}
    for p in a[1:]:
        o = p32obj.read(p)
        if o["format"] != "p32obj":
            sys.exit("p32ar: %s is not an object" % p)
        members[os.path.basename(p)] = o
    p32obj.write(a[0], {"format": "p32ar", "members": members})

if __name__ == "__main__":
    main()
