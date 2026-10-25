#!/usr/bin/env python3
"""net_peer.py EMULATOR... -- the Mac's side of the network model's test
(D45): TCP, UDP and TLS echo servers on loopback (the TLS one with
net_tls.pem, a test certificate for "localhost": not a secret), and a
client for the socket net_model.c listens on (P32W port 77, moved up by
-Nport). Runs the emulator command given (with net_model.bin) with -N, the
servers' ports typed on its console, and prints its output, then what the
client got. run_tests.sh compares that with net_model.with.expect."""
import socket, ssl, subprocess, sys, threading, time

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
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain("net_tls.pem")
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
    cmd = sys.argv[1:-1] + ["-N", "-Nport", str(offset), "-Nca", "net_tls.pem", sys.argv[-1]]
    r = subprocess.run(cmd, input=ports.encode(), capture_output=True, timeout=300)
    sys.stdout.write(r.stdout.decode(errors="replace"))
    th.join(5)
    print("the mac's client got:", got[0] if got else "nothing")

if __name__ == "__main__":
    main()
