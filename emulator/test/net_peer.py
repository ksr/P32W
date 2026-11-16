#!/usr/bin/env python3
"""net_peer.py EMULATOR... -- the Mac's side of the network model's test
(D45): TCP, UDP and TLS echo servers on loopback (the TLS one with
net_tls.pem, a test certificate for "localhost": not a secret, made by
tls_cert() on first use and kept out of git), and a
client for the socket net_model.c listens on (P32W port 77, moved up by
-Nport). Runs the emulator command given (with net_model.bin) with -N, the
servers' ports typed on its console, and prints its output, then what the
client got. run_tests.sh compares that with net_model.with.expect."""
import os, socket, ssl, subprocess, sys, tempfile, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
CERT_CNF = """[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = localhost
O = P32 emulator tests (not secret)
[ext]
subjectAltName = DNS:localhost
basicConstraints = critical, CA:TRUE
keyUsage = digitalSignature, keyCertSign
"""

def tls_cert():
    """The TLS test certificate for "localhost" (certificate + key in one file), made with openssl the
    first time and kept (emulator/test/net_tls.pem, in .gitignore). It is not a secret: it only lets the
    tests' TLS echo server on the loopback prove its name to the emulator (-Nca)."""
    pem = os.path.join(HERE, "net_tls.pem")
    if not os.path.exists(pem):
        with tempfile.TemporaryDirectory() as t:
            cnf, key, crt = (os.path.join(t, n) for n in ("c.cnf", "k.pem", "c.pem"))
            with open(cnf, "w") as f: f.write(CERT_CNF)
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "36500",
                            "-config", cnf, "-keyout", key, "-out", crt], check=True, capture_output=True)
            tmp = pem + ".tmp"
            with open(tmp, "w") as f: f.write(open(crt).read() + open(key).read())
            os.replace(tmp, pem)            # whole or not at all
    return pem

def serve_tcp(ls, wrap=None):
    while True:
        c, _ = ls.accept()
        def one(c):
            try:
                if wrap:
                    c = wrap.wrap_socket(c, server_side=True)
                while True:
                    d = c.recv(4096)
                    if not d:
                        break
                    c.sendall(d)
            except (OSError, ssl.SSLError):
                pass
            c.close()
        threading.Thread(target=one, args=(c,), daemon=True).start()

def serve_udp(us):
    while True:
        d, a = us.recvfrom(2048)
        us.sendto(d, a)

def listener(n=1):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    s.listen(8)
    return s

def main():
    tcp = listener(); tls = listener()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.bind(("127.0.0.1", 0))
    pem = tls_cert()
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(pem)
    for f, a in ((serve_tcp, (tcp,)), (serve_tcp, (tls, ctx)), (serve_udp, (udp,))):
        threading.Thread(target=f, args=a, daemon=True).start()
    probe = socket.socket(); probe.bind(("127.0.0.1", 0)); base = probe.getsockname()[1]; probe.close()
    offset = base - 77                  # P32W's port 77 is the Mac's port base
    got = []
    def client():                       # P32W's listening socket: connect when it is there
        for _ in range(600):
            try:
                c = socket.create_connection(("127.0.0.1", base), timeout=10)
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
    ports = "%d %d %d\n" % (tcp.getsockname()[1], udp.getsockname()[1], tls.getsockname()[1])
    cmd = sys.argv[1:-1] + ["-N", "-Nport", str(offset), "-Nca", pem, sys.argv[-1]]
    r = subprocess.run(cmd, input=ports.encode(), capture_output=True, timeout=300)
    sys.stdout.write(r.stdout.decode(errors="replace"))
    th.join(5)
    print("the mac's client got:", got[0] if got else "nothing")

if __name__ == "__main__":
    main()
