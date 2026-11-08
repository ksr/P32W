#!/usr/bin/env python3
"""netcmd_test.py [uemu] -- the network programs (D45): ifconfig, ping,
fetch and telnet in a P32W Unix session with the coprocessor model (-N),
against servers this script runs on the Mac's loopback: HTTP and HTTPS
(a page, a 20000-byte file, a redirection, a chunked reply, a 404; HTTPS
with emulator/test/net_tls.pem, a test certificate for "localhost") and a
small telnet server that negotiates terminal type and window size. Prints
the transcript with the ports named, the times blanked, and checks on the
Mac's side; run_tests.sh compares that with netcmd_test.expect."""
import http.server, os, re, shutil, socket, socketserver, ssl, subprocess, sys, tempfile, threading

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(HERE)
ROOT = os.path.dirname(SW)
PEM = os.path.join(ROOT, "emulator", "test", "net_tls.pem")
BIG = bytes((i * 7 + i // 256) & 255 for i in range(20000))

class Web(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def version_string(self): return "P32TestServer"
    def date_time_string(self, t=None): return "Wed, 14 Oct 2026 00:00:00 GMT"
    def log_message(self, *a): pass
    def send(self, code, body, ctype="text/plain", extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        for k, v in extra: self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        if self.path == "/hello.txt": self.send(200, b"hello from the Mac's web server\n")
        elif self.path == "/big.bin": self.send(200, BIG, "application/octet-stream")
        elif self.path == "/redirect": self.send(302, b"", extra=(("Location", "/hello.txt"),))
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for part in (b"sent in ", b"three ", b"chunks\n"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        else: self.send(404, b"not here\n")

class Threaded(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True

seen = []                               # what the telnet client negotiated

def telnet_server(ls):
    IAC, DO, WILL, SB, SE = 255, 253, 251, 250, 240
    while True:
        c, _ = ls.accept()
        def one(c):
            c.sendall(bytes([IAC, DO, 24, IAC, DO, 31]) + b"Welcome to the Mac's test server\r\n> ")
            buf = b""
            while True:
                d = c.recv(1024)
                if not d: break
                buf += d
                while True:             # commands out of the stream, noted
                    i = buf.find(bytes([IAC]))
                    if i < 0 or i + 2 >= len(buf): break
                    if buf[i + 1] == WILL and buf[i + 2] == 24:
                        seen.append("WILL TERMINAL-TYPE"); c.sendall(bytes([IAC, SB, 24, 1, IAC, SE]))
                    elif buf[i + 1] == WILL: seen.append("WILL %d" % buf[i + 2])
                    if buf[i + 1] == SB:
                        j = buf.find(bytes([IAC, SE]), i)
                        if j < 0: break
                        sub = buf[i + 2:j]
                        if sub[:1] == b"\x1f": seen.append("window %dx%d" % (sub[1] * 256 + sub[2], sub[3] * 256 + sub[4]))
                        if sub[:2] == b"\x18\x00": seen.append("terminal " + sub[2:].decode())
                        buf = buf[:i] + buf[j + 2:]
                    else:
                        buf = buf[:i] + buf[i + 3:]
                while b"\r\n" in buf:
                    line, buf = buf.split(b"\r\n", 1)
                    if line == b"quit":
                        c.sendall(b"bye\r\n"); c.close(); return
                    c.sendall(line.upper() + b"\r\n> ")
            c.close()
        threading.Thread(target=one, args=(c,), daemon=True).start()


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
    T = tempfile.mkdtemp(prefix="p32netcmd.")
    try:
        shutil.copy(os.path.join(SW, "build", "test.img"), os.path.join(T, "os.img"))
        subprocess.run(["python3", os.path.join(ROOT, "assembler", "p32asm.py"), os.path.join(SW, "monitor", "monitor.asm"),
                        "-o", os.path.join(T, "monitor.bin")], check=True, stdout=subprocess.DEVNULL)
        web = Threaded(("127.0.0.1", 0), Web)
        sweb = Threaded(("127.0.0.1", 0), Web)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(PEM)
        sweb.socket = ctx.wrap_socket(sweb.socket, server_side=True)
        tl = socket.socket(); tl.bind(("127.0.0.1", 0)); tl.listen(4)
        for f in (web.serve_forever, sweb.serve_forever, lambda: telnet_server(tl)):
            threading.Thread(target=f, daemon=True).start()
        hp, sp, tp = web.server_address[1], sweb.server_address[1], tl.getsockname()[1]
        names = {str(hp): "HTTPPORT", str(sp): "HTTPSPORT", str(tp): "TELNETPORT"}
        lines = ["b", "ken", "p32", "cd /tmp", "ifconfig", "ping -c 3 10.0.2.2", "ping -c 1 no.such.host.invalid",
                 "fetch http://10.0.2.2:%d/hello.txt" % hp,
                 "fetch -O http://10.0.2.2:%d/big.bin" % hp,
                 "fetch http://10.0.2.2:%d/redirect" % hp,
                 "fetch http://10.0.2.2:%d/chunked" % hp,
                 "fetch http://10.0.2.2:%d/missing; echo status $?" % hp,
                 "fetch -v https://localhost:%d/hello.txt" % sp,
                 "fetch -o page.txt https://localhost:%d/chunked; cat page.txt" % sp,
                 "fetch https://10.0.2.2:%d/hello.txt; echo status $?" % sp,
                 "fetch ftp://example.com/; echo status $?",
                 "telnet 10.0.2.2 %d" % tp, "hello there", "quit",
                 "telnet 10.0.2.2 1",
                 "ifconfig 10.0.2.20",
                 "su", "root", "ifconfig 10.0.2.20 netmask 255.255.0.0", "ifconfig dhcp", "halt"]
        emu = [os.path.join(ROOT, "emulator", "p32uemu"), "-u", os.path.join(ROOT, "microcode", "p32ucode.bin")] if uemu \
              else [os.path.join(ROOT, "emulator", "p32emu")]
        r = run_emu(emu + ["-N", "-Nca", PEM, "-w", "-R", "1791936000", "-l", "30000000000",
                                  "-d", os.path.join(T, "os.img"), os.path.join(T, "monitor.bin")],
                           ("\n".join(lines) + "\n").encode())
        out = r.stdout.decode(errors="replace").replace("\r", "")
        out = out[out.find("Welcome to P32W Unix"):]
        for p, n in names.items(): out = out.replace(":" + p, ":" + n).replace(" " + p + "\n", " " + n + "\n")
        out = re.sub(r"time=\d+ ms", "time=t ms", out)
        out = re.sub(r"min/avg/max = [\d/]+ ms", "min/avg/max = t/t/t ms", out)
        sys.stdout.write(out)
        got = subprocess.run(["python3", os.path.join(ROOT, "tools", "p32fs.py"), "cat", os.path.join(T, "os.img"), "/tmp/big.bin"],
                             capture_output=True).stdout
        print("---- on the Mac")
        print("big.bin as fetched: %s" % ("the same 20000 bytes" if got == BIG else "DIFFERENT (%d bytes)" % len(got)))
        print("the telnet client negotiated: %s" % ", ".join(seen))
        if "STOP" in r.stderr.decode(errors="replace"): print("STOP: the instruction limit")
    finally:
        if not os.environ.get("KEEP"): shutil.rmtree(T, ignore_errors=True)

if __name__ == "__main__":
    main()
