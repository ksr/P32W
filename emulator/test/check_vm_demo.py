#!/usr/bin/env python3
"""Check vm_demo's output (run by run_tests.sh): isolation, killed processes
with the faulting addresses taken from the assembler listing."""
import re, sys

out, lst = open(sys.argv[1]).read(), open(sys.argv[2]).read()

def addr(label):
    m = re.search(r"^([0-9A-F]{8}) .*\b%s:" % re.escape(label), lst, re.M)
    if not m:
        sys.exit("label %s not in listing" % label)
    return m.group(1)

kills = re.findall(r"kernel: process (\d+) killed, cause (\d+), pc ([0-9A-F]{8}), addr ([0-9A-F]{8})\n", out)
want = {("2", "11", addr("c_store"), addr("proc_c")),       # C: wrote its own code
        ("3", "9", addr("d_load"), "00500000"),              # D: unmapped page
        ("4", "11", "00400000", "00400000")}                 # E: executed data
rest = re.sub(r"kernel: process .*?\n", "", out)
rest = rest.replace("C:EFAULT\n", "", 1).replace("A:ok\n", "", 1).replace("B:ok\n", "", 1)
problems = []
if set(kills) != want or len(kills) != 3:
    problems.append("kills %s, want %s" % (sorted(kills), sorted(want)))
for line in ("C:EFAULT\n", "A:ok\n", "B:ok\n"):
    if line not in out:
        problems.append("missing %r" % line)
if sorted(rest) != sorted("AAAAABBBBB"):
    problems.append("twin letters %r: each of A and B must read back only its own letter, 5 times" % rest)
if problems:
    sys.exit("; ".join(problems))
print("A/B isolated (%s), EFAULT returned, 3 processes killed at the right addresses" % rest)
