#!/usr/bin/env python3
"""check_syscalls.py -- the kernel's system-call table (kernel/syscall.c,
positional) must give every call the number in software/include/sys/syscall.h
(make test)."""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
s = open(os.path.join(HERE, "syscall.c")).read()
i = s.index("static int (*const calls[])(void) = {")
names = [x.strip() for x in s[i:s.index("};", i)].split("{", 1)[1].replace("\n", " ").split(",") if x.strip()]
h = open(os.path.join(HERE, "..", "software", "include", "sys", "syscall.h")).read()
nums = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define SYS_(\w+)\s+(\d+)", h)}
bad = ["%s is entry %d, sys/syscall.h says %s" % (n[4:], k, nums.get(n[4:])) for k, n in enumerate(names)
       if n != "0" and nums.get(n[4:]) != k]
if bad:
    print("FAIL syscall table: " + "; ".join(bad))
    sys.exit(1)
print("PASS syscall table: %d calls at the numbers sys/syscall.h gives" % sum(n != "0" for n in names))
