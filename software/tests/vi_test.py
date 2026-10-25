#!/usr/bin/env python3
"""vi_test.py -- vi on P32W Unix, driven as a person would: logged in on the
emulator's second terminal (TCP, -T), keystrokes typed into vi, and the
file it writes checked. Covers dd p G w cw gg A . :%s / x u o 0 l D J yy P
:wq. Prints PASS / FAIL."""
import os, shutil, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(HERE)
ROOT = os.path.dirname(SW)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pykermit as pk          # (its Line: the TCP terminal)

START = "line one\nline two\nline three\nalpha beta gamma\n"
KEYS = [
    "dd",                       # delete "line one"
    "p",                        # put it after "line two"
    "G", "w", "cwBETA\x1b",     # last line: beta -> BETA
    "gg", "A!\x1b",             # "line two!"
    "j", ".",                   # repeat: "line one!"
    ":%s/line/LINE/\r",         # LINE on every line (the first on each)
    "/three\r", "x", "u",       # delete a character, undo it
    "o", "inserted text\x1b",   # a new line after LINE three
    "0", "ll", "D",             # "inserted text" -> "in"
    "k", "yy", "P",             # LINE three copied above itself
    "G", "kJ",                  # "in" joined with the last line
    ":wq\r",
]
WANT = "LINE two!\nLINE one!\nLINE three\nLINE three\nin alpha BETA gamma\n"

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def main():
    T = tempfile.mkdtemp(prefix="p32vi.")
    emu = None
    ok = False
    detail = ""
    try:
        mon = os.path.join(T, "monitor.bin")
        subprocess.run([sys.executable, os.path.join(ROOT, "assembler", "p32asm.py"),
                        os.path.join(SW, "monitor", "monitor.asm"), "-o", mon], check=True)
        img = os.path.join(T, "os.img")
        shutil.copy(os.path.join(SW, "build", "test.img"), img)    # (never the live disk.img)
        open(os.path.join(T, "t.txt"), "w").write(START)
        fs = [sys.executable, os.path.join(ROOT, "tools", "p32fs.py")]
        subprocess.run(fs + ["put", img, os.path.join(T, "t.txt"), "/tmp/t.txt"], check=True)
        open(os.path.join(T, "in"), "w").write("b\n")
        port = free_port()
        emu = subprocess.Popen([os.path.join(ROOT, "emulator", "p32emu"), "-w", "-R", "1791936000",
                                "-l", "200000000000", "-T", str(port), "-d", img, mon],
                               stdin=open(os.path.join(T, "in")), stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
        line = None
        for _ in range(100):
            try:
                line = pk.Line(tcp=port); break
            except OSError:
                time.sleep(0.1)
        for _ in range(30):
            line.write(b"\r")
            try:
                line.expect("login: ", timeout=2); break
            except pk.KermitError:
                pass
        line.write(b"root\r"); line.expect("Password: ")
        line.write(b"root\r"); line.expect("# ")
        line.write(b"vi /tmp/t.txt\r")
        time.sleep(1.0)
        for k in KEYS:
            line.write(k.encode())
            time.sleep(0.15)
        line.expect("# ", timeout=30)
        line.write(b"halt\r")
        emu.wait(timeout=60)
        got = subprocess.run(fs + ["cat", img, "/tmp/t.txt"], capture_output=True).stdout.decode("latin-1")
        ok = got == WANT
        detail = repr(got)
    except Exception as e:
        detail = "%s: %s" % (type(e).__name__, e)
    finally:
        if emu and emu.poll() is None: emu.kill()
        shutil.rmtree(T, ignore_errors=True)
    if ok:
        print("PASS vi: %d editing commands typed on a terminal, the file as expected" % len(KEYS))
        return 0
    print("FAIL vi: got %s" % detail)
    return 1

if __name__ == "__main__":
    sys.exit(main())
