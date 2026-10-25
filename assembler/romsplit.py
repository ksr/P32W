#!/usr/bin/env python3
"""romsplit.py IMAGE.bin [OUTPREFIX] -- split a boot ROM image into the four
byte-lane files for the memory card's SST39SF020A flash chips (D50): lane k
holds byte k of every word (U1 = lane 0 = D0-D7 ... U4 = lane 3 = D24-D31),
256 KiB each, padded with 0xFF (erased flash). Program them in a TL866-class
programmer, or write them in place from the debug card.
"""
import sys

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    img = open(sys.argv[1], "rb").read()
    if len(img) > 1 << 20:
        sys.exit("romsplit: image larger than the 1 MiB ROM")
    pre = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].rsplit(".", 1)[0]
    img = img.ljust((len(img) + 3) & ~3, b"\xff")
    for k in range(4):
        lane = bytes(img[i] for i in range(k, len(img), 4)).ljust(256 * 1024, b"\xff")
        name = "%s.lane%d.bin" % (pre, k)
        open(name, "wb").write(lane)
        print("%s  (U%d, D%d-D%d)" % (name, k + 1, 8 * k, 8 * k + 7))

if __name__ == "__main__":
    main()
