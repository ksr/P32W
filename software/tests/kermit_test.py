#!/usr/bin/env python3
"""kermit_test.py -- P32W's kermit against the host's pykermit (D25), over the
emulator's second terminal on TCP (-T), as a serial line would be: log in on
tty1, run P32W kermit as a server, send files to it (binary, text, empty,
long packets and windows, then short packets without), list them, get them
back (by name and by wildcard), then let P32W send one with kermit -s. Every
file must come back byte for byte (and with its time). Prints PASS / FAIL."""
import os, random, shutil, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(HERE)
ROOT = os.path.dirname(SW)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pykermit as pk

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def main():
    T = tempfile.mkdtemp(prefix="p32kermit.")
    results = []
    def ok(msg): results.append("PASS " + msg)
    def bad(msg): results.append("FAIL " + msg)
    emu = None
    try:
        mon = os.path.join(T, "monitor.bin")
        subprocess.run([sys.executable, os.path.join(ROOT, "assembler", "p32asm.py"),
                        os.path.join(SW, "monitor", "monitor.asm"), "-o", mon], check=True)
        img = os.path.join(T, "os.img")
        shutil.copy(os.path.join(SW, "build", "test.img"), img)    # (never the live disk.img)
        # the files
        rnd = random.Random(32)
        src = os.path.join(T, "src"); os.mkdir(src)
        files = {
            "random.bin": bytes(rnd.getrandbits(8) for _ in range(40000)),
            "zeros.bin": bytes(9000),
            "all256.bin": bytes(range(256)) * 20,
            "empty.dat": b"",
            "notes.txt": b"".join(b"line %d of a text file\n" % i for i in range(300)),
        }
        for n, d in files.items():
            p = os.path.join(src, n)
            open(p, "wb").write(d)
            os.utime(p, (1767323045, 1767323045))      # 2026-01-02 03:04:05 UTC
        open(os.path.join(T, "in"), "w").write("b\n")
        port = free_port()
        emu = subprocess.Popen([os.path.join(ROOT, "emulator", "p32emu"), "-w", "-R", "1791936000",
                                "-l", "200000000000", "-T", str(port), "-d", img, mon], stdin=open(os.path.join(T, "in")),
                               stdout=open(os.path.join(T, "tty0.log"), "w"), stderr=subprocess.STDOUT)
        line = None
        for _ in range(100):
            try:
                line = pk.Line(tcp=port); break
            except OSError:
                time.sleep(0.1)
        if not line: raise pk.KermitError("cannot connect to the emulator")
        # log in on tty1
        for _ in range(30):
            line.write(b"\r")
            try:
                line.expect("login: ", timeout=2); break
            except pk.KermitError:
                pass
        line.write(b"root\r"); line.expect("Password: ")
        line.write(b"root\r"); line.expect("# ")
        line.write(b"mkdir /tmp/k; cd /tmp/k; kermit -x\r")
        time.sleep(0.5)
        log = []
        k = pk.Kermit(line, maxlen=1000, window=3, log=lambda *a: log.append(a))
        t0 = time.time()
        k.send([os.path.join(src, n) for n in ("random.bin", "zeros.bin", "all256.bin", "empty.dat")])
        k.send([os.path.join(src, "notes.txt")], text=True)
        dt = time.time() - t0
        total = sum(len(files[n]) for n in files)
        ok("kermit: 5 files to the P32W server, long packets (1000) and a window of 3 "
           "(%d bytes, %d packets, %d retries)" % (total, k.stats["packets"], k.stats["retries"]))
        # short packets, no window, other check
        k2 = pk.Kermit(line, maxlen=90, window=1)
        k2.send([os.path.join(src, "all256.bin")])
        # a bad line: a tenth of the packets lost, both ways
        line.lossy(0.10, seed=7)
        kl = pk.Kermit(line, maxlen=500, window=4)
        kl.timeout = 3
        kl.send([os.path.join(src, "random.bin")])
        lost = os.path.join(T, "lost"); os.mkdir(lost)
        kl.get("random.bin", lost)
        line.lossy(0.0)
        p = os.path.join(lost, "random.bin")
        if os.path.exists(p) and open(p, "rb").read() == files["random.bin"]:
            ok("kermit: a line losing 10%% of the packets both ways: there and back intact (%d retries)"
               % kl.stats["retries"])
        else:
            bad("kermit: lossy line: the file did not survive")
        listing = k.generic("D")
        names = [l.split()[-1] for l in listing.splitlines() if l.strip()]
        if sorted(names) == sorted(files):
            ok("kermit: remote dir lists them (%d files)" % len(names))
        else:
            bad("kermit: remote dir: %r" % listing[:200])
        back = os.path.join(T, "back"); os.mkdir(back)
        got = k.get("*.bin", back)
        got += k.get("empty.dat", back)
        got += k.get("notes.txt", back)
        same = [n for n in files if os.path.exists(os.path.join(back, n)) and
                open(os.path.join(back, n), "rb").read() == files[n]]
        if len(same) == len(files):
            ok("kermit: all 5 back from the server with GET (wildcard and names), byte for byte "
               "(text arrived with CRLF, was stored with LF)")
        else:
            bad("kermit: GET: identical %s of %s; got %s" % (same, sorted(files), [os.path.basename(g) for g in got]))
        times = [n for n in same if int(os.stat(os.path.join(back, n)).st_mtime) == 1767323045]
        if len(times) == len(same) and same:
            ok("kermit: their modification times kept both ways (attribute packets)")
        else:
            bad("kermit: times: only %s kept" % times)
        k.generic("F")
        line.expect("# ", timeout=30)
        # P32W sends, the host receives
        line.write(b"kermit -s random.bin\r")
        time.sleep(0.5)
        k3 = pk.Kermit(line, maxlen=2000, window=4)
        os.mkdir(os.path.join(T, "recv"))
        got = k3.receive(os.path.join(T, "recv"))
        p = os.path.join(T, "recv", "random.bin")
        if os.path.exists(p) and open(p, "rb").read() == files["random.bin"]:
            ok("kermit -s: P32W sends a file, the host receives it intact (packets of 2000, window 4)")
        else:
            bad("kermit -s: %r" % got)
        line.expect("# ", timeout=30)
        line.write(b"halt\r")
        emu.wait(timeout=60)
    except Exception as e:
        bad("kermit: %s: %s" % (type(e).__name__, e))
        try:
            print(open(os.path.join(T, "tty0.log")).read()[-600:], file=sys.stderr)
        except OSError:
            pass
    finally:
        if emu and emu.poll() is None: emu.kill()
        if not os.environ.get("KEEP"): shutil.rmtree(T, ignore_errors=True)
        else: print("kept", T, file=sys.stderr)
    for r in results: print(r)
    return 0 if results and all(r.startswith("PASS") for r in results) else 1

if __name__ == "__main__":
    sys.exit(main())
