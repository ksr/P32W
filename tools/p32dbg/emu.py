"""The connection to the emulator: p32emu or p32uemu with -dbg 0, started as a
child process, one TCP connection on 127.0.0.1 (emulator/p32dbg.h, the
protocol). A command is one line; its reply is one line of JSON. The emulator
exits when the connection closes, so closing this object, or this process
ending however it ends, leaves nothing running; close() also waits for the
process, and kills it if it does not go.
"""
import json, os, re, socket, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
EMU = os.path.join(ROOT, "emulator", "p32emu")
UEMU = os.path.join(ROOT, "emulator", "p32uemu")
UCODE = os.path.join(ROOT, "microcode", "p32ucode.bin")
STATUS = ("state", "reason", "pc", "mode", "instrs", "cycles")


class EmulatorError(Exception):
    pass


class Emulator:
    def __init__(self, rom, args=(), micro=False, log=None):
        """rom: the monitor's image; args: the emulator's own options (-d DISK, -M ...);
        micro: p32uemu (the microcode, cycle by cycle) instead of p32emu; log: a file for
        its stderr (None: inherit)"""
        exe = UEMU if micro else EMU
        if not os.path.exists(exe):
            raise EmulatorError("%s is not built (make -C emulator)" % exe)
        argv = [exe] + (["-u", UCODE] if micro else []) + ["-dbg", "0"] + list(args) + [rom]
        self.p = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=log)
        line = self.p.stdout.readline().decode("latin-1")
        self.p.stdout.close()
        m = re.search(r"port (\d+)", line)
        if not m:
            self.p.kill()
            self.p.wait()
            raise EmulatorError("%s did not start its debug server: %r" % (os.path.basename(exe), line.strip()))
        self.port = int(m.group(1))
        self.sock = socket.create_connection(("127.0.0.1", self.port))
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.rf = self.sock.makefile("rb")
        self.status = {}
        self.hello = self.ok("hello")
        self.micro = self.hello.get("emulator") == "p32uemu"

    def cmd(self, line):
        """send one command, return its reply (a dict); the status is kept in self.status"""
        if "\n" in line:
            raise EmulatorError("one line a command")
        try:
            self.sock.sendall(line.encode("latin-1") + b"\n")
            reply = self.rf.readline()
        except OSError as e:
            raise EmulatorError("the emulator went away (%s)" % e)
        if not reply:
            raise EmulatorError("the emulator went away")
        r = json.loads(reply)
        self.status = {k: r[k] for k in STATUS if k in r}
        return r

    def ok(self, line):
        """cmd(), raising the emulator's error"""
        r = self.cmd(line)
        if not r.get("ok"):
            raise EmulatorError(r.get("error", "failed"))
        return r

    def close(self):
        try:
            self.cmd("quit")
        except (EmulatorError, OSError, ValueError):
            pass
        try:
            self.sock.close()
        except OSError:
            pass
        try:
            self.p.wait(5)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()
