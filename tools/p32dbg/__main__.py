"""p32dbg -- the P32W debugger (D97): the machine in p32emu, or p32uemu with
its microcode cycle by cycle, driven from a page in the browser (the video
card's screen, the console, the registers, the code, memory, breakpoints and
the CPU's insides) or by commands. tools/p32dbg/README.md says more.

    python3 tools/p32dbg                     the browser UI: open http://127.0.0.1:8832/
    python3 tools/p32dbg -u                  the same with p32uemu (the microcode, cycle by cycle)
    python3 tools/p32dbg -c "s 10; r; d"     commands, their output on stdout, then exit
    python3 tools/p32dbg --script FILE       commands from a file (then the UI, with --ui web)
"""
import argparse, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from p32dbg.debugger import ROOT, Symbols       # noqa: E402
from p32dbg.emu import Emulator, EmulatorError  # noqa: E402
from p32dbg.shell import Shell                  # noqa: E402


def split_commands(text):
    """'a; b' -> ['a', 'b'] (a backslashed semicolon stays in its command)"""
    out, cur, k = [], "", 0
    while k < len(text):
        if text[k] == "\\" and text[k + 1:k + 2] == ";":
            cur += ";"
            k += 2
            continue
        if text[k] == ";":
            out.append(cur.strip())
            cur = ""
        else:
            cur += text[k]
        k += 1
    out.append(cur.strip())
    return [c for c in out if c]


def main():
    ap = argparse.ArgumentParser(prog="p32dbg", description="the P32W debugger (tools/p32dbg/README.md)")
    ap.add_argument("-u", "--micro", action="store_true", help="p32uemu: the microcode, cycle by cycle")
    ap.add_argument("-d", "--disk", default=os.environ.get("P32W_DISK", os.path.join(ROOT, "software", "build", "disk.img")),
                    help="the disk (default software/build/disk.img, or P32W_DISK, as tools/p32w)")
    ap.add_argument("--no-disk", action="store_true", help="no disk: the monitor alone")
    ap.add_argument("--emu-arg", action="append", default=[], help="another emulator option (e.g. --emu-arg=-M --emu-arg=2)")
    ap.add_argument("-c", action="append", default=[], help="commands (cmd; cmd), their output on stdout")
    ap.add_argument("--script", help="commands from a file")
    ap.add_argument("--ui", choices=("web", "none"), help="web (the default without -c or --script) or none")
    ap.add_argument("--port", type=int, default=8832, help="the browser UI's port on 127.0.0.1 (default 8832; 0: any free one)")
    ap.add_argument("--timeout", type=float, default=60, help="how long a scripted c, until or waitfor may run (s)")
    ap.add_argument("--log", help="a file for the emulator's stderr")
    a = ap.parse_args()
    ui = a.ui or ("none" if a.c or a.script else "web")

    tmp = tempfile.mkdtemp(prefix="p32dbg.")
    rom = os.path.join(tmp, "monitor.bin")
    mon = os.path.join(ROOT, "software", "monitor", "monitor.asm")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "assembler", "p32asm.py"), mon, "-o", rom], capture_output=True, text=True)
    if r.returncode:
        sys.exit("p32dbg: the monitor did not assemble: %s" % (r.stderr or r.stdout).strip())
    args = list(a.emu_arg)
    if not a.no_disk:
        if not os.path.exists(a.disk):
            sys.exit("p32dbg: no disk %s (make -C software disk, or --no-disk)" % a.disk)
        args = ["-d", a.disk] + args
    syms = Symbols()
    syms.load_monitor(mon)
    kmap = os.path.join(ROOT, "kernel", "unix.map")
    if os.path.exists(kmap):
        syms.load_kernel(kmap)
    log = open(a.log, "w") if a.log else None
    try:
        emu = Emulator(rom, args, micro=a.micro, log=log)
    except EmulatorError as e:
        sys.exit("p32dbg: %s" % e)
    sh = Shell(emu, syms, wait=(ui == "none"), timeout=a.timeout)
    try:
        cmds = []
        for c in a.c:
            cmds += split_commands(c)
        if a.script:
            cmds += [l.rstrip("\n") for l in open(a.script)]
        for c in cmds:
            out = sh.exec(c)
            if out:
                print(out, flush=True)
            if sh.quit:
                return
        if ui == "web":
            from p32dbg import web
            web.serve(sh, a.port)
    except KeyboardInterrupt:
        pass
    finally:
        emu.close()


if __name__ == "__main__":
    main()
