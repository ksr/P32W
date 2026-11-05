#!/usr/bin/env python3
"""kprof.py PROFILE [MAP] [-n N] -- where the kernel's time goes.

PROFILE is what  p32emu -k PROFILE  wrote at exit: the instructions run at
each kernel address (supervisor mode, the kernel's MiB), and in user mode.
MAP is the kernel's link map (default kernel/unix.map). Prints the N (20)
functions that ran the most kernel instructions, with their share of the
kernel's and of all instructions.
"""
import bisect, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

def main():
    args = sys.argv[1:]
    n = 20
    if "-n" in args:
        k = args.index("-n")
        n = int(args[k + 1])
        del args[k:k + 2]
    if not args:
        sys.exit(__doc__.strip())
    prof = os.path.abspath(args[0])
    mapf = args[1] if len(args) > 1 else os.path.join(HERE, "..", "kernel", "unix.map")
    sys.path.insert(0, HERE)
    from uprof import functions
    os.chdir(os.path.dirname(os.path.abspath(mapf)))    # (the map's paths are from there)
    syms = functions(os.path.basename(mapf))            # (static functions too: D88)
    addrs = [a for a, _ in syms]
    user = other = kernel = 0
    per = {}
    for line in open(prof):
        what, count = line.split()
        count = int(count)
        if what == "user":
            user = count
        elif what == "other":
            other = count
        else:
            a = int(what, 16)
            k = bisect.bisect_right(addrs, a) - 1
            name = syms[k][1] if k >= 0 else "?"
            per[name] = per.get(name, 0) + count
            kernel += count
    total = user + other + kernel
    print("instructions: %d in all; user %d (%.1f%%), kernel %d (%.1f%%), ROM %d"
          % (total, user, 100.0 * user / max(total, 1), kernel, 100.0 * kernel / max(total, 1), other))
    print("%-24s %14s %8s %8s" % ("function", "instructions", "kernel", "all"))
    for name, c in sorted(per.items(), key=lambda x: -x[1])[:n]:
        print("%-24s %14d %7.1f%% %7.1f%%" % (name, c, 100.0 * c / max(kernel, 1), 100.0 * c / max(total, 1)))

if __name__ == "__main__":
    main()
