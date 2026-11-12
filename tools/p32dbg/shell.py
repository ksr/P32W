"""The command language, the same in the browser, in -c commands and in
scripts (help prints it). Shell.exec(line) runs one command and returns what
it prints. In the browser, c, until and n start a run and return at once (the
page follows the state); in scripts they wait for the stop, as waitfor does,
at most the timeout, then pause.
"""
import re, time
from .debugger import UNAMES, code, parse_addr, screen_text, uaddr_name, usummary, words
from .emu import EmulatorError

HELP = """\
s [N]            step N instructions (default 1)       n           step over a CALL or CALLR (else s)
us [N]           step N microcycles (p32uemu)          c           continue: to a breakpoint or HALT
pause | stop     pause a run                           until ADDR  run to ADDR
reset            the reset (memory, disks, the picture in the video chip's RAM keep their contents)
b ADDR | bd ADDR | bd all | bl                         breakpoints (the next instruction is at ADDR)
r                registers                             set R=V [R=V...]   (r1..r15, sp, pc, status, epc, ...)
m [ADDR] [LEN]   memory (hex, LEN default 128; as a load sees it)     pm ADDR [LEN]  physical memory
poke ADDR BB...  write bytes                           d [ADDR] [N]  disassemble N (default 16; at PC)
trace [N]        the last N instructions run (default 20)
u                the microcycle last run (p32uemu): microaddress, the word's fields, the buses
screen           the video card's console as text, read from the picture's dots
key TEXT         type on UART 0, the serial console (\\r Enter, \\e Esc, \\xHH a byte, \\\\ a backslash)
kbd TEXT         type on the PS/2 keyboard: the screen's console, /dev/tty4 (the same escapes)
console          UART 0's output since the last console
waitfor TEXT [S] run until UART 0 prints TEXT (at most S seconds; for scripts)
sym              where the symbols come from           sleep S     wait S seconds (a run runs on)
help | quit
An address is hex (F06A, $F06A, 0xF06A), a symbol (uart_puts, swtch), r0..r15, sp, pc, or one of them
plus or minus a hex offset (ttread+10). Counts are decimal."""


def hx(v):
    return "%08X" % (v & 0xFFFFFFFF)


class Shell:
    def __init__(self, emu, syms, wait=False, timeout=60):
        """wait: c, until, n wait for the stop (scripts); timeout: how long at most"""
        self.emu, self.syms, self.wait, self.timeout = emu, syms, wait, timeout
        self.con = ""                   # UART 0's output, all of it (the last 1 MB)
        self.con_new = ""               # since the last `console`
        self.quit = False

    # ------------------------------------------------------------ the machine's pieces
    def fetch_console(self):
        """UART 0's new output into the console (CR LF as one newline)"""
        r = self.emu.ok("console")
        t = r["text"].replace("\r\n", "\n")
        if t:
            self.con = (self.con + t)[-(1 << 20):]
            self.con_new += t
        return t

    def regs(self):
        return self.emu.ok("regs")

    def addr(self, text, regs=None):
        if regs is None and re.match(r"^(r\d+|sp|zero)\b", text.strip().lower()):
            regs = self.regs()
        return parse_addr(text, regs, self.syms, self.emu.status.get("pc"))

    def where(self, a):
        s = self.syms.near(a)
        return " <%s>" % s if s else ""

    def pc_line(self):
        st = self.emu.status
        pc = st.get("pc", 0)
        lines = code(self.emu, pc, 1, self.syms)
        text = lines[0][2] if lines else "??"
        return "%s %s%s  %s   [%s %s, %d instructions%s]" % (
            st.get("mode", "?"), hx(pc), self.where(pc), text, st.get("state"), st.get("reason") or "-",
            st.get("instrs", 0), ", %d cycles" % st["cycles"] if self.emu.micro else "")

    def wait_stop(self, timeout=None):
        """a run: wait for it to stop (at most the timeout, then pause)"""
        end = time.time() + (timeout if timeout is not None else self.timeout)
        while self.emu.cmd("status").get("state") == "running":
            if time.time() > end:
                self.emu.ok("pause")
                return "(%d s: paused)" % (timeout if timeout is not None else self.timeout)
            time.sleep(0.02)
        return ""

    # ------------------------------------------------------------ the commands
    def exec(self, line):
        line = line.strip()
        if not line or line.startswith("#"):
            return ""
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        f = getattr(self, "c_" + {"stop": "pause", "q": "quit"}.get(word, word), None)
        if f is None:
            return "no command %s (help lists them)" % word
        try:
            return f(rest) or ""
        except EmulatorError as e:
            return "error: %s" % e
        except ValueError as e:
            return "error: %s" % e

    def c_help(self, rest):
        return HELP

    def c_quit(self, rest):
        self.quit = True
        return ""

    def c_s(self, rest):
        n = int(rest) if rest else 1
        self.emu.ok("step %d" % n)
        return self.pc_line()

    def c_us(self, rest):
        if not self.emu.micro:
            return "us: the microcode emulator only (p32dbg -u)"
        self.emu.ok("ustep %d" % (int(rest) if rest else 1))
        return self.c_u("") + "\n" + self.pc_line()

    def c_n(self, rest):
        pc = self.emu.status.get("pc", 0)
        r = self.emu.ok("mem %X 4" % pc)
        w = words(r["hex"], pc).get(pc)
        if w is not None and (w >> 24) in (0x61, 0x63):     # CALL, CALLR: run to the next instruction
            return self.c_until("%X" % (pc + 4))
        return self.c_s("")

    def c_c(self, rest):
        self.emu.ok("run")
        if not self.wait:
            return "running"
        note = self.wait_stop()
        return (note + "\n" if note else "") + self.pc_line()

    def c_until(self, rest):
        a = self.addr(rest)
        self.emu.ok("until %X" % a)
        if not self.wait:
            return "running to %s%s" % (hx(a), self.where(a))
        note = self.wait_stop()
        return (note + "\n" if note else "") + self.pc_line()

    def c_pause(self, rest):
        self.emu.ok("pause")
        return self.pc_line()

    def c_reset(self, rest):
        self.emu.ok("reset")
        return self.pc_line()

    def c_b(self, rest):
        a = self.addr(rest)
        self.emu.ok("break add %X" % a)
        return "breakpoint at %s%s" % (hx(a), self.where(a))

    def c_bd(self, rest):
        if rest == "all":
            self.emu.ok("break del all")
            return "no breakpoints"
        a = self.addr(rest)
        self.emu.ok("break del %X" % a)
        return "breakpoint at %s gone" % hx(a)

    def c_bl(self, rest):
        r = self.emu.ok("break list")
        return "\n".join("%s%s" % (hx(a), self.where(a)) for a in r["breaks"]) or "no breakpoints"

    def c_r(self, rest):
        r = self.regs()
        st = r["status"]
        out = ["pc %s%s  mode %s  status %s (%s%s)  imask %02X" % (
            hx(r["pc"]), self.where(r["pc"]), r["mode"], hx(st), "S" if st & 1 else "U", " IE" if st & 2 else "", r["imask"])]
        for k in range(0, 16, 4):
            out.append("  ".join("%-3s %s" % ("sp" if i == 15 else "r%d" % i, hx(r["r"][i])) for i in range(k, k + 4)))
        out.append("epc %s  cause %s  badaddr %s  vbr %s  other sp %s" % (
            hx(r["epc"]), hx(r["cause"]), hx(r["badaddr"]), hx(r["vbr"]), hx(r["sp_other"])))
        t = r["timer"]
        out.append("asid %02X  tlbhi %s  tlblo %s  timer %d of %d%s%s%s" % (
            r["asid"], hx(r["tlbhi"]), hx(r["tlblo"]), t[0], t[1], " on" if t[2] else " off", " pending" if t[3] else "",
            "  ir %s" % hx(r["ir"]) if "ir" in r else ""))
        return "\n".join(out)

    def c_set(self, rest):
        for item in rest.split():
            name, _, val = item.partition("=")
            if not val:
                return "set R=V (R: r1..r15, sp, pc, sp_other, status, imask, epc, cause, badaddr, vbr, asid)"
            self.emu.ok("set %s %X" % (name.lower(), self.addr(val)))
        return self.c_r("")

    def dump(self, rest, phys):
        parts = rest.split()
        a = self.addr(parts[0]) if parts else self.emu.status.get("pc", 0)
        n = int(parts[1]) if len(parts) > 1 else 128
        r = self.emu.ok("mem %X %d%s" % (a, n, " phys" if phys else ""))
        h, out = r["hex"], []
        for k in range(0, n, 16):
            row = [h[2 * i:2 * i + 2] for i in range(k, min(k + 16, n))]
            text = "".join(chr(int(b, 16)) if b != "??" and 32 <= int(b, 16) < 127 else "." for b in row)
            out.append("%s  %-48s %s" % (hx(a + k), " ".join(row), text))
        return "\n".join(out)

    def c_m(self, rest):
        return self.dump(rest, False)

    def c_pm(self, rest):
        return self.dump(rest, True)

    def c_poke(self, rest):
        parts = rest.split()
        if len(parts) < 2:
            return "poke ADDR BB..."
        a = self.addr(parts[0])
        self.emu.ok("poke %X %s" % (a, " ".join(parts[1:])))
        return self.dump("%X %d" % (a, len(parts) - 1), False)

    def c_d(self, rest):
        parts = rest.split()
        a = self.addr(parts[0]) if parts else self.emu.status.get("pc", 0)
        n = int(parts[1]) if len(parts) > 1 else 16
        out = []
        pc = self.emu.status.get("pc")
        for addr, w, text, sym in code(self.emu, a, n, self.syms):
            if sym:
                out.append("%s:" % sym)
            out.append("%s %s  %s  %s" % ("=>" if addr == pc else "  ", hx(addr), "%08X" % w if w is not None else "????????", text))
        return "\n".join(out)

    def c_trace(self, rest):
        r = self.emu.ok("trace %d" % (int(rest) if rest else 20))
        out = []
        for a in r["trace"]:
            c = code(self.emu, a, 1, self.syms)
            out.append("%s%s  %s" % (hx(a), self.where(a), c[0][2] if c else "??"))
        return "\n".join(out)

    def c_u(self, rest):
        if not self.emu.micro:
            return "u: the microcode emulator only (p32dbg -u)"
        u = self.emu.ok("ustate")
        out = ["microaddress %s  %s   word %s" % (":".join(str(x) for x in u["uaddr"]), uaddr_name(u["uaddr"]), u["word"]),
               "  " + usummary(u["word"]),
               "  buses A %s  B %s  R %s   ALU %s %s, %s (EQ %d LT %d LTU %d)   MAR %s  MDR %s  IR %s" % (
                   hx(u["a"]), hx(u["b"]), hx(u["r"]), UNAMES["ALU"].get(u["aluop"], "op %d" % u["aluop"]),
                   hx(u["alua"]), hx(u["alub"]), u["flags"][0], u["flags"][1], u["flags"][2],
                   hx(u["mar"]), hx(u["mdr"]), hx(u["ir"])),
               "  next %s  %s" % (":".join(str(x) for x in u["next"]), uaddr_name(u["next"]))]
        return "\n".join(out)

    def c_screen(self, rest):
        v = self.emu.ok("video")
        lines = screen_text(v["text"])
        return "\n".join(lines) + "\n(%s)" % ("the picture is on" if v["on"] else "the chip is stopped: no picture")

    def c_key(self, rest):
        r = self.emu.ok("key " + rest)
        return "%d queued" % r["queued"]

    def c_kbd(self, rest):
        r = self.emu.ok("kbd " + rest)
        return "%d typed" % r["queued"]

    def c_console(self, rest):
        self.fetch_console()
        t, self.con_new = self.con_new, ""
        return t.rstrip("\n")

    def c_waitfor(self, rest):
        m = re.match(r"^(.*?)(?:\s+(\d+(?:\.\d+)?))?$", rest)
        text, secs = m.group(1), float(m.group(2)) if m.group(2) else self.timeout
        text = text.replace("\\n", "\n").replace("\\s", " ")
        if not text:
            return "waitfor TEXT [S]"
        start = len(self.con)
        if self.emu.cmd("status")["state"] != "running":
            self.emu.ok("run")
        end = time.time() + secs
        while text not in self.con[max(0, start - len(text)):]:
            if time.time() > end:
                self.emu.ok("pause")
                return "waitfor: no %r in %g s (paused)" % (text, secs)
            if self.emu.cmd("status")["state"] != "running":
                self.fetch_console()
                if text in self.con[max(0, start - len(text)):]:
                    break
                return "waitfor: stopped (%s) before %r" % (self.emu.status.get("reason"), text)
            self.fetch_console()
            time.sleep(0.02)
        return "found %r" % text

    def c_sym(self, rest):
        return "symbols: " + (", ".join(self.syms.sources) or "none")

    def c_sleep(self, rest):
        time.sleep(float(rest or 1))
        return ""
