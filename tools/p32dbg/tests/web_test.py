#!/usr/bin/env python3
"""web_test.py [-u] -- the debugger's browser side (emulator/test/run_tests.sh):
p32dbg's web server on a free port, with the monitor alone; the page, the font,
the state, a command, a key on UART 0, a scan code, the mouse, the memory
window, quit.
Prints one PASS or FAIL line."""
import json, os, re, subprocess, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))

def main():
    micro = "-u" in sys.argv[1:]
    p = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools", "p32dbg"), "--no-disk", "--port", "0"] + (["-u"] if micro else []),
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    bad = []
    try:
        line = p.stdout.readline()
        m = re.search(r"http://127\.0\.0\.1:(\d+)/", line)
        if not m:
            sys.exit(print("FAIL the debugger's web server did not start: %r" % line.strip()))
        base = "http://127.0.0.1:%s" % m.group(1)
        get = lambda path: urllib.request.urlopen(base + path, timeout=30).read()
        post = lambda path, obj: json.loads(urllib.request.urlopen(urllib.request.Request(
            base + path, json.dumps(obj).encode(), {"Content-Type": "application/json"}), timeout=30).read())
        if b"P32W debugger" not in get("/"): bad.append("the page")
        f = json.loads(get("/api/font"))["glyphs"]
        if len(f) != 4096 or f[ord("A") * 16] != 0x38 or f[ord("A") * 16 + 2] != 0x44: bad.append("the font")
        st = json.loads(get("/api/state?con=0&log=0&video="))
        if st["status"]["state"] != "stopped" or st["status"]["reason"] != "start": bad.append("the state at the start")
        v = st["video"]
        if v.get("on") != 0 or len(v.get("pix", "")) != 2 * 480 * 80 or v.get("text", "").count("\n") != 25: bad.append("the video card's picture")
        if not st["code"] or not st["code"][0]["sym"] == "reset": bad.append("the code panel")
        if (st.get("u") is not None) != micro: bad.append("the CPU's insides")
        if "reset+14" not in post("/api/cmd", {"cmd": "s 3"})["out"]: bad.append("a step")
        post("/api/key", {"text": "?\rq\r"})
        post("/api/scan", {"codes": [0x1C, 0xF0, 0x1C]})
        if post("/api/mouse", {"dx": 300, "dy": -4, "buttons": 1}) != {"ok": True}: bad.append("the mouse")
        if "mouse" not in st["video"]: bad.append("whether the mouse is on")
        if post("/api/cmd", {"cmd": "mouse 5 -3 1"})["out"] != "29 05 FD": bad.append("a packet of the mouse's")
        post("/api/mem", {"addr": "pc", "phys": False})
        out = post("/api/cmd", {"cmd": "c"})["out"]
        st = json.loads(get("/api/state?con=0&log=0&video=%d" % st["video"]["sum"]))
        for _ in range(100):
            if st["status"]["state"] != "running": break
            st = json.loads(get("/api/state?con=0&log=0&video="))
        if st["status"]["state"] != "halted": bad.append("the run to HALT (%s)" % st["status"]["state"])
        if "boot from the disk" not in st["con"]: bad.append("the console")
        if st["mem"].get("addr") != st["status"]["pc"]: bad.append("the memory window")
        if "> s 3" not in st["log"]: bad.append("the commands' log")
        post("/api/cmd", {"cmd": "quit"})
        p.wait(20)
    finally:
        if p.poll() is None:
            p.kill()
    if bad:
        print("FAIL the debugger's browser side%s: %s" % (" (-u)" if micro else "", ", ".join(bad)))
        sys.exit(1)
    print("PASS the debugger's browser side%s: the page, the font, the state, a step, UART 0, the keyboard, "
          "the mouse, memory, a run to HALT, quit" % (" (p32uemu)" if micro else ""))

if __name__ == "__main__":
    main()
