#!/usr/bin/env python3
"""net_test.py [uemu] -- P32W Unix's sockets (D45) end to end: nettest.c,
compiled for P32W and put on a copy of the test disk, runs as ken under the
kernel in the emulator with the network coprocessor (-N), against servers
this script runs on the Mac's loopback: TCP, UDP and TLS echo (TLS with
emulator/test/net_tls.pem, a test certificate for "localhost", made on first use), and a
client for nettest's listening socket. Prints the session's nettest lines
and what the client got; run_tests.sh compares them with net_test.expect."""
import os, re, shutil, socket, ssl, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(HERE)
ROOT = os.path.dirname(SW)
sys.path.insert(0, os.path.join(ROOT, "emulator", "test"))
from net_peer import serve_tcp, serve_udp, listener, tls_cert   # (the same servers as the model's test)


def run_emu(argv, data):
    """the emulator's session; on a timeout (P32W_NETTIMEOUT seconds, default 900),
    where P32W was: the session's end, instead of a traceback"""
    try:
        return subprocess.run(argv, input=data, capture_output=True,
                              timeout=int(os.environ.get("P32W_NETTIMEOUT", "900")))
    except subprocess.TimeoutExpired as e:
        sys.exit("TIMEOUT after %s s; the session's end:\n%s" % (e.timeout,
                 (e.stdout or b"").decode(errors="replace").replace("\r", "")[-3000:]))

def main():
    uemu = len(sys.argv) > 1 and sys.argv[1] == "uemu"
    T = tempfile.mkdtemp(prefix="p32net.")
    try:
        subprocess.run(["python3", os.path.join(ROOT, "cc", "p32cc.py"), "-w", os.path.join(HERE, "nettest.c"),
                        "-o", os.path.join(T, "nettest")], check=True, cwd=SW)
        shutil.copy(os.path.join(SW, "build", "test.img"), os.path.join(T, "os.img"))
        subprocess.run(["python3", os.path.join(ROOT, "tools", "p32fs.py"), "put", os.path.join(T, "os.img"),
                        os.path.join(T, "nettest"), "/tmp/nettest", "--mode", "755"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["python3", os.path.join(ROOT, "assembler", "p32asm.py"), os.path.join(SW, "monitor", "monitor.asm"),
                        "-o", os.path.join(T, "monitor.bin")], check=True, stdout=subprocess.DEVNULL)
        tcp = listener(); tls = listener()
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.bind(("127.0.0.1", 0))
        pem = tls_cert()
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(pem)
        for f, a in ((serve_tcp, (tcp,)), (serve_tcp, (tls, ctx)), (serve_udp, (udp,))):
            threading.Thread(target=f, args=a, daemon=True).start()
        probe = socket.socket(); probe.bind(("127.0.0.1", 0)); lport = probe.getsockname()[1]; probe.close()
        got = []
        def client():                   # nettest's server: connect once it listens
            for _ in range(2400):
                try:
                    c = socket.create_connection(("127.0.0.1", lport), timeout=30)
                    break
                except OSError:
                    time.sleep(0.05)
            else:
                got.append("never connected"); return
            c.sendall(b"hi from the mac\n")
            d = b""
            while not d.endswith(b"\n"):
                r = c.recv(100)
                if not r: break
                d += r
            got.append(d.decode().strip())
        th = threading.Thread(target=client, daemon=True); th.start()
        cmd = "/tmp/nettest %d %d %d %d" % (tcp.getsockname()[1], udp.getsockname()[1], tls.getsockname()[1], lport)
        lines = "\n".join(["b", "ken", "p32", cmd, "su", "root", "halt"]) + "\n"
        emu = [os.path.join(ROOT, "emulator", "p32uemu"), "-u", os.path.join(ROOT, "microcode", "p32ucode.bin")] if uemu \
              else [os.path.join(ROOT, "emulator", "p32emu")]
        r = run_emu(emu + ["-N", "-Nca", pem, "-w", "-R", "1791936000", "-l", "20000000000",
                                  "-d", os.path.join(T, "os.img"), os.path.join(T, "monitor.bin")],
                           lines.encode())
        out = r.stdout.decode(errors="replace").replace("\r", "")
        keep = False
        for line in out.split("\n"):    # the boot's network line, then nettest's output
            if line.startswith("net: "): print(line)
            if line.startswith("$ /tmp/nettest"): keep = True; continue
            if keep and (line.startswith("$ su") or line.startswith("$ ")): break
            if keep: print(line)
        th.join(10)
        print("the mac's client got:", got[0] if got else "nothing")
        if "STOP" in r.stderr.decode(errors="replace"): print("STOP: the instruction limit")
    finally:
        if not os.environ.get("KEEP"): shutil.rmtree(T, ignore_errors=True)

if __name__ == "__main__":
    main()
