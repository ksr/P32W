"""The browser debugger: a local web server (the standard library only) around
the same Shell as the commands, as YACC1-D's y1dbg.

  python3 tools/p32dbg [--port 8832]      then open http://127.0.0.1:8832/

  GET  /              the page (web/index.html)
  GET  /api/font      the font's glyphs: 256 characters x 16 scan lines, a
                      byte a line, bit 7 the leftmost dot (from its source,
                      generators/gen_font.py). The page no longer needs it:
                      the video card's picture comes as its dots
  GET  /api/state?con=N&log=N&video=SUM
                      the status, the registers, the code around PC, the
                      breakpoints, the memory window, the video card's picture
                      (640 x 480 dots, when its checksum is not SUM) and its
                      console as text, the CPU's insides (p32uemu), and the
                      console and the commands' output past N (their totals)
  POST /api/cmd       {"cmd": "..."} -> {"out": "..."}: a command of the
                      language (shell.py); quit ends the debugger
  POST /api/key       {"text": "..."}: typed on UART 0, the serial console
  POST /api/scan      {"codes": [..]}: set-2 scan codes into the PS/2
                      keyboard (the screen's console, /dev/tty4)
  POST /api/mouse     {"dx": N, "dy": N, "buttons": B}: the PS/2 mouse moved
                      dx steps to the right and dy up (a dot of the picture
                      each, for gfx(3)'s pointer), the buttons B down (1
                      left, 2 right, 4 middle). What it moves while the
                      machine is stopped is dropped
  POST /api/mem       {"addr": "...", "phys": bool}: the memory window, kept as
                      written (so sp or r3 follows the register)

The page asks for the state ten times a second while the machine runs. A lock
keeps the commands to the emulator one at a time. Bound to 127.0.0.1 only: in
a codespace, GitHub forwards the port to a private https address.
"""
import json, os, sys, threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
from .debugger import ROOT, code, disasm, mouse_packets, ufields, usummary, uaddr_name, UNAMES
from .emu import EmulatorError

HERE = os.path.dirname(os.path.abspath(__file__))
BEFORE, AFTER = 6, 22                   # the code panel: instructions above PC, and from PC on
MEMLEN = 256


def font():
    """the card's font: the ROM's first 4 KiB, from its source (the ROM image itself is built,
    not kept in git)"""
    sys.path.insert(0, os.path.join(ROOT, "generators"))
    import gen_font                             # noqa: E402
    return list(gen_font.rom()[:4096])


def key_escape(text):
    """text for the key command: every byte that is not printable and not a space as \\xHH"""
    out = []
    for ch in text:
        c = ord(ch) & 0xFF
        out.append("\\\\" if ch == "\\" else ch if 33 <= c < 127 else "\\x%02X" % c)
    return "".join(out)


class App:
    def __init__(self, sh):
        self.sh, self.emu = sh, sh.emu
        self.lock = threading.Lock()
        self.log = ""                   # the commands and what they printed
        self.mem = ("sp", False)        # the memory window: (address as written, physical)
        self.font = font()
        self.buttons = 0                # the mouse's buttons, as last sent
        self.stopping = False

    def run_cmd(self, line):
        with self.lock:
            out = self.sh.exec(line)
            self.log += "> %s\n%s" % (line, out + "\n" if out else "")
            self.log = self.log[-(1 << 18):]
            if self.sh.quit:
                self.stopping = True
            return out

    def mouse(self, dx, dy, buttons):
        """the page's mouse moved over the screen, or a button changed. A real mouse's movements are gone if nobody
        is reading; while the machine is stopped they would all wait and come at once when it runs, so they are
        dropped here (a button's change is not: it waits)"""
        with self.lock:
            if self.emu.status.get("state") != "running":
                if buttons == self.buttons:
                    return
                dx = dy = 0
            self.buttons = buttons
            for x, y in mouse_packets(max(-2000, min(2000, dx)), max(-2000, min(2000, dy))):
                self.emu.ok("mouse %d %d %d" % (x, y, buttons))

    def state(self, con, log, vsum):
        with self.lock:
            sh, emu = self.sh, self.emu
            sh.fetch_console()
            regs = emu.ok("regs")
            st = emu.status
            pc = st["pc"]
            breaks = set(emu.ok("break list")["breaks"])
            lines = [{"addr": a, "word": w, "text": t, "sym": s, "bp": a in breaks, "pc": a == pc}
                     for a, w, t, s in code(emu, pc, AFTER, sh.syms, BEFORE)]
            out = {"status": st, "where": sh.syms.near(pc), "regs": regs, "code": lines, "breaks": sorted(breaks),
                   "micro": emu.micro, "emulator": emu.hello.get("emulator")}
            try:
                ma = sh.addr(self.mem[0], regs)
                m = emu.ok("mem %X %d%s" % (ma, MEMLEN, " phys" if self.mem[1] else ""))
                out["mem"] = {"expr": self.mem[0], "addr": ma, "hex": m["hex"], "phys": self.mem[1],
                              "where": sh.syms.near(ma)}
            except (ValueError, EmulatorError) as e:
                out["mem"] = {"expr": self.mem[0], "error": str(e), "phys": self.mem[1]}
            v = emu.ok("video %d" % vsum if vsum is not None else "video")
            out["video"] = {k: v[k] for k in ("on", "mouse", "sum", "same", "text", "pix") if k in v}
            if emu.micro:
                u = emu.ok("ustate")
                u = {k: u[k] for k in ("uaddr", "word", "a", "b", "r", "alua", "alub", "aluop", "flags", "mar", "mdr", "ir", "rf", "next")}
                u["fields"] = ufields(int(u["word"], 16))
                u["summary"] = usummary(u["word"])
                u["uname"], u["nname"] = uaddr_name(u["uaddr"]), uaddr_name(u["next"])
                u["aluname"] = UNAMES["ALU"].get(u["aluop"], str(u["aluop"]))
                u["irtext"] = disasm(pc, u["ir"], None, sh.syms)[0]
                out["u"] = u
            out["con"], out["con_total"] = sh.con[con:] if con <= len(sh.con) else sh.con, len(sh.con)
            out["log"], out["log_total"] = self.log[log:] if log <= len(self.log) else self.log, len(self.log)
            out["syms"] = sh.syms.sources
            return out


def serve(sh, port):
    app = App(sh)

    class H(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def reply(self, obj, code=200, ctype="application/json"):
            body = obj if isinstance(obj, bytes) else json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            try:
                if u.path in ("/", "/index.html"):
                    self.reply(open(os.path.join(HERE, "web", "index.html"), "rb").read(), ctype="text/html; charset=utf-8")
                elif u.path == "/api/font":
                    self.reply({"glyphs": app.font})
                elif u.path == "/api/state":
                    vs = q.get("video", [""])[0]
                    self.reply(app.state(int(q.get("con", ["0"])[0]), int(q.get("log", ["0"])[0]),
                                         int(vs) if vs.isdigit() else None))
                else:
                    self.reply({"error": "no such page"}, 404)
            except EmulatorError as e:
                self.reply({"error": str(e), "gone": True}, 500)

        def do_POST(self):
            n = int(self.headers.get("Content-Length", "0"))
            try:
                body = json.loads(self.rfile.read(n) or b"{}")
            except ValueError:
                return self.reply({"error": "not JSON"}, 400)
            try:
                if self.path == "/api/cmd":
                    self.reply({"out": app.run_cmd(str(body.get("cmd", "")))})
                elif self.path == "/api/key":
                    with app.lock:
                        app.emu.ok("key " + key_escape(str(body.get("text", ""))))
                    self.reply({"ok": True})
                elif self.path == "/api/scan":
                    codes = [int(c) & 0xFF for c in body.get("codes", [])]
                    if codes:
                        with app.lock:
                            app.emu.ok("scan " + " ".join("%02X" % c for c in codes))
                    self.reply({"ok": True})
                elif self.path == "/api/mouse":
                    app.mouse(int(body.get("dx", 0)), int(body.get("dy", 0)), int(body.get("buttons", 0)) & 7)
                    self.reply({"ok": True})
                elif self.path == "/api/mem":
                    app.mem = (str(body.get("addr", "sp")) or "sp", bool(body.get("phys")))
                    self.reply({"ok": True})
                else:
                    self.reply({"error": "no such call"}, 404)
            except EmulatorError as e:
                self.reply({"error": str(e), "gone": True}, 500)
            if app.stopping:
                threading.Thread(target=httpd.shutdown, daemon=True).start()

    httpd = ThreadingHTTPServer(("127.0.0.1", port), H)    # (port 0: any free one)
    httpd.daemon_threads = True
    print("p32dbg: %s, the browser UI at http://127.0.0.1:%d/ (quit there, or ^C here)" % (
        sh.emu.hello.get("emulator"), httpd.server_address[1]), flush=True)
    try:
        httpd.serve_forever()
    finally:
        httpd.server_close()
