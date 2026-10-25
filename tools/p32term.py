#!/usr/bin/env python3
"""p32term.py -- a terminal on P32W's second serial port (UART 1), which the
emulator offers on a TCP port with -T:

    emulator/p32emu -T 2323 -d software/build/disk.img monitor.bin
    python3 tools/p32term.py [port]          (default 2323)

Unlike nc, every key goes to P32W at once (the Mac terminal is put in raw
mode), so top's q, vi, more and ^C all work as on the console. ^] leaves.
If the emulator is not running yet, it waits for it."""
import os, select, socket, sys, termios, time, tty

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 2323
    s = None
    while s is None:
        try:
            s = socket.create_connection(("localhost", port))
        except OSError:
            sys.stderr.write("[p32term: waiting for the emulator on port %d]\r\n" % port)
            time.sleep(2)
    sys.stderr.write("[p32term: connected; ^] leaves]\r\n")
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd) if os.isatty(fd) else None
    try:
        if saved:
            tty.setraw(fd)
        while True:
            r, _, _ = select.select([fd, s], [], [])
            if s in r:
                data = s.recv(4096)
                if not data:
                    sys.stderr.write("\r\n[p32term: the emulator closed the line]\r\n")
                    break
                os.write(1, data)
            if fd in r:
                data = os.read(fd, 1024)
                if not data or b"\x1d" in data:         # ^] (or end of input): leave
                    data = data.split(b"\x1d")[0]
                    if data:
                        s.sendall(data)
                    sys.stderr.write("\r\n[p32term: ^] -- left]\r\n")
                    break
                s.sendall(data)
    finally:
        if saved:
            termios.tcsetattr(fd, termios.TCSADRAIN, saved)
        s.close()

if __name__ == "__main__":
    main()
