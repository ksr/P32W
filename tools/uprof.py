#!/usr/bin/env python3
"""uprof.py PROFILE MAP [-n N] -- where a program's time goes.

PROFILE is what  p32uemu -G -q PROFILE  wrote at exit: the cycles at each user
address while the gate was up (D88), and the kernel's in all. MAP is the
program's link map (p32ld -M). The objects it names give the program's static
functions too, which a link map leaves out. Prints the N (20) functions that
took the most cycles, with their share of the program's and of all cycles.

Run on one program at a time, from the directory the map's paths are relative
to: addresses are a process's, so two programs would be added together.
"""
import bisect, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "assembler"))
import p32obj  # noqa: E402

def functions(mapf):
    """[(address, name)] of every function: the map's objects' text symbols."""
    archives, syms = {}, []
    for line in open(mapf):
        m = re.match(r"^(\S+?)(?:\((\S+)\))?\s+text ([0-9A-F]{8}) data", line)
        if not m:
            continue
        path, member, base = m.group(1), m.group(2), int(m.group(3), 16)
        if member:
            if path not in archives:
                archives[path] = p32obj.read(path)["members"]
            obj = archives[path][member]
        else:
            obj = p32obj.read(path)
        for name, s in obj["symbols"].items():
            if s["section"] == ".text":
                syms.append((base + s["value"], name))
    return sorted(syms)

def main():
    args = sys.argv[1:]
    n = 20
    if "-n" in args:
        k = args.index("-n")
        n = int(args[k + 1])
        del args[k:k + 2]
    if len(args) != 2:
        sys.exit(__doc__.strip())
    syms = functions(args[1])
    addrs = [a for a, _ in syms]
    per, kernel, other, user = {}, 0, 0, 0
    for line in open(args[0]):
        what, count = line.split()
        count = int(count)
        if what == "kernel":
            kernel = count
        elif what == "other":
            other = count
        else:
            k = bisect.bisect_right(addrs, int(what, 16)) - 1
            name = syms[k][1] if k >= 0 else "?"
            per[name] = per.get(name, 0) + count
            user += count
    total = user + kernel + other
    print("cycles: %d in all; user %d (%.1f%%), kernel %d (%.1f%%), other %d"
          % (total, user, 100.0 * user / max(total, 1), kernel, 100.0 * kernel / max(total, 1), other))
    print("%-24s %14s %7s %7s" % ("function", "cycles", "user", "all"))
    for name, c in sorted(per.items(), key=lambda x: -x[1])[:n]:
        print("%-24s %14d %6.1f%% %6.1f%%" % (name, c, 100.0 * c / max(user, 1), 100.0 * c / max(total, 1)))

if __name__ == "__main__":
    main()
