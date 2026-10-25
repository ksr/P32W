#!/usr/bin/env python3
"""mkbootdisk.py PAYLOAD.bin DISK.img [LOAD [ENTRY]] -- a CompactFlash image the
monitor's "b" command boots: sector 0 = "P32B", sector count, load address,
entry; sectors 1.. = the payload."""
import struct, sys
payload = open(sys.argv[1], "rb").read()
load = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x00100000
entry = int(sys.argv[4], 16) if len(sys.argv) > 4 else load
n = (len(payload) + 511) // 512
img = struct.pack("<4I", 0x42323350, n, load, entry).ljust(512, b"\0") + payload.ljust(n * 512, b"\0")
open(sys.argv[2], "wb").write(img.ljust(64 * 512, b"\0"))
