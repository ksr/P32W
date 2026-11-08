#!/usr/bin/env python3
"""netsvc_test.py [uemu] -- telnetd, ftp and ntpdate (D46) in a P32W Unix
session with the coprocessor model (-N), against the Mac:
  * telnetd: this script logs in to P32W over telnet (P32W's port 23, the
    Mac's 23 + the -Nport offset), as ken, runs commands, logs out, then
    does it again (the pseudo-terminal is used again); the console waits
    for the sessions (a file they make), then shows who is logged in;
  * ftp: a small FTP server here (passive mode; files in a scratch
    directory): ls, get (binary and text), put, nlist, delete, mkdir, cd;
  * ntpdate: a small SNTP server here, answering 2030-01-01 00:00:00 UTC:
    -q as ken, then the clock set as root, and date.
Prints the transcripts (ports named, times blanked) and checks on the
Mac's side; run_tests.sh compares that with netsvc_test.expect."""
import os, re, shutil, socket, struct, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(HERE)
ROOT = os.path.dirname(SW)
BIN = bytes((i * 7 + i // 256) & 255 for i in range(5000))
TEXT = b"line one\r\nline two\r\nthe last line\r\n"
NTP_TIME = 1893456000                   # 2030-01-01 00:00:00 UTC

# ---------------------------------------------------------------- an FTP server
def ftp_server(ls, root):
    def one(c):
        cwd = "/"
        f = c.makefile("rb")
        def say(s): c.sendall(s.encode() + b"\r\n")
        def path(p):
            p = p if p.startswith("/") else os.path.join(cwd, p)
            p = os.path.normpath(p)
            return p, os.path.join(root, p.lstrip("/"))
        pasv = None
        say("220 the Mac's test FTP server")
        while True:
            line = f.readline()
            if not line: break
            line = line.decode().rstrip("\r\n")
            cmd, _, arg = line.partition(" ")
            cmd = cmd.upper()
            if cmd == "USER": say("331 password, please")
            elif cmd == "PASS": say("230 logged in" if arg == "secret" else "530 wrong password")
            elif cmd == "SYST": say("215 UNIX Type: L8")
            elif cmd == "TYPE": say("200 type %s" % arg)
            elif cmd == "PWD": say('257 "%s"' % cwd)
            elif cmd == "CWD":
                v, real = path(arg)
                if os.path.isdir(real): cwd = v; say("250 now in %s" % cwd)
                else: say("550 no such directory")
            elif cmd == "CDUP": cwd = os.path.dirname(cwd) or "/"; say("250 now in %s" % cwd)
            elif cmd == "PASV":
                pasv = socket.socket(); pasv.bind(("127.0.0.1", 0)); pasv.listen(1)
                p = pasv.getsockname()[1]
                say("227 Entering Passive Mode (127,0,0,1,%d,%d)" % (p >> 8, p & 255))
            elif cmd in ("LIST", "NLST", "RETR", "STOR"):
                if not pasv: say("425 PASV first"); continue
                v, real = path(arg or ".")
                if cmd == "RETR" and not os.path.isfile(real): say("550 no such file"); pasv.close(); pasv = None; continue
                say("150 here it comes")
                d, _ = pasv.accept(); pasv.close(); pasv = None
                if cmd == "LIST":
                    for n in sorted(os.listdir(real)):
                        st = os.stat(os.path.join(real, n))
                        isdir = os.path.isdir(os.path.join(real, n))   # (a directory's size is the host
                        # file system's, 128 on a Mac and 4096 on Linux: the listing gives 4096)
                        d.sendall(("%s 1 ken staff %8d Oct 14 00:00 %s\r\n" % ("drwxr-xr-x" if isdir else "-rw-r--r--", 4096 if isdir else st.st_size, n)).encode())
                elif cmd == "NLST":
                    for n in sorted(os.listdir(real)): d.sendall(n.encode() + b"\r\n")
                elif cmd == "RETR": d.sendall(open(real, "rb").read())
                else:
                    data = b""
                    while True:
                        r = d.recv(65536)
                        if not r: break
                        data += r
                    open(real, "wb").write(data)
                d.close()
                say("226 done")
            elif cmd == "DELE":
                v, real = path(arg)
                if os.path.isfile(real): os.remove(real); say("250 deleted")
                else: say("550 no such file")
            elif cmd == "MKD": v, real = path(arg); os.mkdir(real); say('257 "%s" made' % v)
            elif cmd == "RMD": v, real = path(arg); os.rmdir(real); say("250 removed")
            elif cmd == "QUIT": say("221 goodbye"); break
            else: say("502 %s: not here" % cmd)
        c.close()
    while True:
        c, _ = ls.accept()
        threading.Thread(target=one, args=(c,), daemon=True).start()

# ---------------------------------------------------------------- an SNTP server
def ntp_server(us):
    while True:
        d, a = us.recvfrom(512)
        if len(d) < 48: continue
        t = NTP_TIME + 2208988800
        r = bytearray(48)
        r[0] = 0x24; r[1] = 2                 # version 4, mode 4 (a server); stratum 2
        r[24:32] = d[40:48]                   # (the client's transmit time, as originate)
        struct.pack_into("!II", r, 32, t, 0); struct.pack_into("!II", r, 40, t, 0)
        us.sendto(bytes(r), a)

# ---------------------------------------------------------------- a telnet client
def telnet_login(port, log, n):
    for _ in range(600):
        try:
            c = socket.create_connection(("127.0.0.1", port), timeout=60); break
        except OSError:
            time.sleep(0.1)
    else:
        log.append("never connected"); return
    got, seen = b"", 0
    def until(pat):                     # (only output after the last match counts: an
        nonlocal got, seen              # earlier prompt is not the next one)
        end = time.time() + 120
        while pat not in got[seen:] and time.time() < end:
            d = c.recv(4096)
            if not d: return False
            got += d
        if pat not in got[seen:]: return False
        seen = got.index(pat, seen) + len(pat)
        return True
    until(b"login: "); c.sendall(b"ken\r\n")
    until(b"Password: "); c.sendall(b"p32\r\n")
    until(b"$ "); c.sendall(b"who\r\n")
    until(b"ttyp"); until(b"$ "); c.sendall(b"echo session %d over telnet\r\n" % n)
    until(b"telnet\r\n$ ")
    c.sendall(b"touch /tmp/telnet.%d; exit\r\n" % n)
    while True:
        d = c.recv(4096)
        if not d: break
        got += d
    c.close()
    text = re.sub(rb"\xff[\xfb-\xfe].", b"", got).decode(errors="replace").replace("\r", "")
    log.append("---- telnet session %d\n%s\n(the connection closed)" % (n, text.strip("\n")))

def main():
    uemu = len(sys.argv) > 1 and sys.argv[1] == "uemu"
    T = tempfile.mkdtemp(prefix="p32netsvc.")
    try:
        shutil.copy(os.path.join(SW, "build", "test.img"), os.path.join(T, "os.img"))
        subprocess.run(["python3", os.path.join(ROOT, "assembler", "p32asm.py"), os.path.join(SW, "monitor", "monitor.asm"),
                        "-o", os.path.join(T, "monitor.bin")], check=True, stdout=subprocess.DEVNULL)
        froot = os.path.join(T, "ftproot"); os.makedirs(os.path.join(froot, "pub"))
        open(os.path.join(froot, "pub", "data.bin"), "wb").write(BIN)
        open(os.path.join(froot, "pub", "notes.txt"), "wb").write(TEXT)
        fl = socket.socket(); fl.bind(("127.0.0.1", 0)); fl.listen(4)
        nu = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); nu.bind(("127.0.0.1", 0))
        threading.Thread(target=ftp_server, args=(fl, froot), daemon=True).start()
        threading.Thread(target=ntp_server, args=(nu,), daemon=True).start()
        probe = socket.socket(); probe.bind(("127.0.0.1", 0)); tport = probe.getsockname()[1]; probe.close()
        offset = tport - 23                         # P32W's port 23 is the Mac's tport
        fp, np_ = fl.getsockname()[1], nu.getsockname()[1]
        log = []
        def telnets():
            telnet_login(tport, log, 1)
            telnet_login(tport, log, 2)
        th = threading.Thread(target=telnets, daemon=True); th.start()
        lines = ["b", "ken", "p32", "cd /tmp",
                 "while test ! -f /tmp/telnet.2; do sleep 1; done; echo the telnet sessions are over",
                 "who",
                 "ftp -n 10.0.2.2 %d" % fp, "user ken wrong", "user ken secret", "pwd", "ls", "cd pub", "nlist",
                 "get data.bin", "ascii", "get notes.txt", "binary", "put data.bin back.bin", "nlist",
                 "delete back.bin", "mkdir made", "get missing.txt", "status", "quit",
                 "od -c notes.txt",
                 "/etc/ntpdate -q 10.0.2.2:%d" % np_, "/etc/ntpdate 10.0.2.2:%d" % np_,
                 "su", "root", "/etc/ntpdate 10.0.2.2:%d" % np_, "date", "halt"]
        emu = [os.path.join(ROOT, "emulator", "p32uemu"), "-u", os.path.join(ROOT, "microcode", "p32ucode.bin")] if uemu \
              else [os.path.join(ROOT, "emulator", "p32emu")]
        con = open(os.path.join(T, "console.log"), "wb")
        prompts = ["-P", "$ ", "-P", "# ", "-P", "> ", "-P", "login: ", "-P", "Password: ", "-P", "ftp> "]
        p = subprocess.Popen(emu + prompts + ["-N", "-Nport", str(offset), "-w", "-R", "1791936000", "-l", os.environ.get("LIMIT", "40000000000"),
                                    "-d", os.path.join(T, "os.img"), os.path.join(T, "monitor.bin")],
                             stdin=subprocess.PIPE, stdout=con, stderr=subprocess.PIPE)
        try:
            _, err = p.communicate(("\n".join(lines) + "\n").encode(), timeout=int(os.environ.get("TMO", "600")))
        except subprocess.TimeoutExpired:     # (stuck: show where)
            p.kill(); p.communicate()
            con.close()
            print("TIMEOUT; the console's last lines:")
            print(open(os.path.join(T, "console.log"), "rb").read()[-1500:].decode(errors="replace").replace("\r", ""))
            print("\n".join(log))
            return
        con.close()
        th.join(30)
        out = open(os.path.join(T, "console.log"), "rb").read().decode(errors="replace").replace("\r", "")
        out = out[out.find("Welcome to P32W Unix"):]
        out = out.replace(" %d\n" % fp, " FTPPORT\n").replace(":%d\n" % np_, ":NTPPORT\n")
        out = re.sub(r"\d+ bytes in \d+\.\d+ s \(\d+ bytes/s\)", "N bytes in T s (R bytes/s)", out)
        out = re.sub(r"offset [+-]\d+ s", "offset X s", out)
        out = re.sub(r"Passive Mode \(127,0,0,1,\d+,\d+\)", "Passive Mode (127,0,0,1,P,P)", out)
        out = re.sub(r"moved [+-]\d+ s", "moved X s", out)
        out = re.sub(r"(Jan  1 00:00:)\d\d( 2030)", r"\1ss\2", out)
        out = re.sub(r"ken +ttyp\d +\w+ +\d+ \d\d:\d\d", "ken      ttypN    (a login time)", out)
        sys.stdout.write(out)
        for t in log: print(re.sub(r"ttyp\d +\w+ +\d+ \d\d:\d\d", "ttypN    (a login time)", t))
        back = open(os.path.join(froot, "pub", "back.bin"), "rb").read() if os.path.exists(os.path.join(froot, "pub", "back.bin")) else None
        got = subprocess.run(["python3", os.path.join(ROOT, "tools", "p32fs.py"), "cat", os.path.join(T, "os.img"), "/tmp/data.bin"],
                             capture_output=True).stdout
        print("---- on the Mac")
        print("data.bin as fetched by ftp: %s" % ("the same %d bytes" % len(BIN) if got == BIN else "DIFFERENT"))
        print("made: %s" % ("yes" if os.path.isdir(os.path.join(froot, "pub", "made")) else "no"))
        print("back.bin: %s" % ("deleted again" if back is None else "still there"))
        if "STOP" in err.decode(errors="replace"): print("STOP: the instruction limit")
    finally:
        if not os.environ.get("KEEP"): shutil.rmtree(T, ignore_errors=True)

if __name__ == "__main__":
    main()
