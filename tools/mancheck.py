#!/usr/bin/env python3
"""mancheck.py -- header generator and format checker for P32W Unix man pages.
  python3 mancheck.py header NAME S      print the header line, e.g. header ls 1
  python3 mancheck.py check FILE...      check pages; prints problems, exit 1 if any
"""
import sys, os, re

def header(name, sec):
    t = "%s(%s)" % (name.upper(), sec)
    mid = "P32W Unix"
    start = (72 - len(mid)) // 2
    left = t + " " * (start - len(t))
    line = left + mid
    line += " " * (72 - len(line) - len(t)) + t
    return line

SECTS = {"NAME", "SYNOPSIS", "DESCRIPTION", "OPTIONS", "COMMANDS", "EXAMPLES",
         "FILES", "ENVIRONMENT", "EXIT STATUS", "RETURN VALUE", "ERRORS",
         "SEE ALSO", "BUGS", "LIMITATIONS", "BUGS AND LIMITATIONS", "DIAGNOSTICS", "NOTES",
         "FORMAT", "KEYS"}

def check(path):
    probs = []
    base = os.path.basename(path)
    m = re.match(r"(.+)\.([1-8])$", base)
    if not m:
        return ["%s: file name must be name.S" % path]
    name, sec = m.group(1), m.group(2)
    data = open(path, "rb").read()
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        probs.append("non-ASCII bytes")
        text = data.decode("ascii", "replace")
    if not text.endswith("\n") or text.endswith("\n\n"):
        probs.append("file must end with exactly one newline")
    lines = text.split("\n")[:-1]
    if not lines or lines[0] != header(name, sec):
        probs.append("line 1 must be exactly: %r" % header(name, sec))
    if len(lines) < 2 or lines[1] != "":
        probs.append("line 2 must be blank")
    heads = []
    for i, l in enumerate(lines, 1):
        if "\t" in l: probs.append("line %d: tab" % i)
        if len(l) > 72: probs.append("line %d: %d chars" % (i, len(l)))
        if l != l.rstrip(): probs.append("line %d: trailing space" % i)
        if any(ord(c) < 32 or ord(c) > 126 for c in l if c != "\t"):
            probs.append("line %d: control/non-ASCII char" % i)
        if i > 1 and l and not l[0].isspace():
            heads.append(l)
            if l not in SECTS and not re.match(r"^[A-Z][A-Z /]+$", l):
                probs.append("line %d: text at column 0 that is not a heading: %r" % (i, l))
        elif l and l[0] == " " and not l.startswith("       "):
            probs.append("line %d: body indented less than 7" % i)
    if heads[:2] != ["NAME", "SYNOPSIS"] and not (sec == "5" and heads[:2] == ["NAME", "DESCRIPTION"]) and not base.startswith("intro."):
        if heads[:1] != ["NAME"]:
            probs.append("first heading must be NAME (then SYNOPSIS)")
    # NAME line
    try:
        k = lines.index("NAME")
        nl = lines[k + 1]
        mm = re.match(r"^       ([A-Za-z0-9_\[.,+ -]+?) - (\S.*)$", nl)
        if not mm:
            probs.append("NAME line must be '       a, b - description' on one line")
        else:
            names = [x.strip() for x in mm.group(1).split(",")]
            if name not in names:
                probs.append("NAME line must list %r" % name)
        if lines[k + 2] != "":
            probs.append("NAME must be a single line followed by a blank line")
    except (ValueError, IndexError):
        probs.append("no NAME section")
    return probs

if __name__ == "__main__":
    if sys.argv[1] == "header":
        print(header(sys.argv[2], sys.argv[3]))
    else:
        bad = 0
        for f in sys.argv[2:]:
            p = check(f)
            for x in p: print("%s: %s" % (f, x))
            bad |= bool(p)
        if not bad: print("ok: %d page(s)" % len(sys.argv[2:]))
        sys.exit(bad)
