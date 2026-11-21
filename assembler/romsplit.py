#!/usr/bin/env python3
"""romsplit.py [--device NAME] IMAGE.bin [OUTPREFIX] -- split a boot ROM image
into the four byte-lane files for the memory card's ROM sockets (D142): lane k
holds byte k of every word (lane 0 = D0-D7 ... lane 3 = D24-D31; U44 to U47
on the card, docs/mem-card.md).

The sockets are 28-pin ROM sites with pins 1 and 27 at +5 V and the card's
address line MA13 on pin 26 (generators/p32net.py, ROM28), so each kind of
part is read in its own place. --device says which part is fitted:

    28C64   AT28C64B (the default), 2764, 27C64   8K: the whole part
    27128   27128, 27C128                         16K: the whole part
    28C256  AT28C256                              32K: its top 16K (A14 is high)
    27256   27256, 27C256                         32K: its top 16K (A14 is high)
    27512   27512, 27C512                         64K: its top 16K (A15, A14 high)

A file is as large as its part, the image where the card reads it and 0xFF
elsewhere: program a chip with its file from address 0 (a TL866-class
programmer; minipro -p AT28C64B -w FILE). An 8K part holds 8K words of image,
32 KiB in all; the others 16K words, 64 KiB. The card repeats the ROM through
its first MiB.
"""
import sys

# device: (the part's size in bytes, where the card reads in it, how much it reads)
DEVICES = {"28C64": (8192, 0, 8192), "2764": (8192, 0, 8192), "27C64": (8192, 0, 8192),
           "27128": (16384, 0, 16384), "27C128": (16384, 0, 16384),
           "28C256": (32768, 16384, 16384), "27256": (32768, 16384, 16384), "27C256": (32768, 16384, 16384),
           "27512": (65536, 49152, 16384), "27C512": (65536, 49152, 16384)}

def lanes(img, device="28C64"):
    """the four files' contents"""
    size, at, window = DEVICES[device]
    img = img.ljust((len(img) + 3) & ~3, b"\xff")
    if len(img) > 4 * window:
        raise ValueError("image of %d bytes is larger than the %d KiB that four %s hold for the card"
                         % (len(img), 4 * window // 1024, device))
    out = []
    for k in range(4):
        lane = bytes(img[i] for i in range(k, len(img), 4))
        out.append(b"\xff" * at + lane.ljust(window, b"\xff") + b"\xff" * (size - at - window))
    return out

def device_of(name):
    """AT28C64B-15PU -> 28C64, M27C256B -> 27C256: the part's kind in its name, or None"""
    n = name.upper()
    for key in sorted(DEVICES, key=len, reverse=True):
        if key in n:
            return key
    return None

def main():
    args = sys.argv[1:]
    device = "28C64"
    if "--device" in args:
        i = args.index("--device")
        given = args[i + 1] if i + 1 < len(args) else ""
        device = device_of(given)
        del args[i:i + 2]
        if device is None:
            sys.exit("romsplit: no device %s (one of %s)" % (given, ", ".join(sorted(DEVICES))))
    if not args:
        sys.exit(__doc__)
    img = open(args[0], "rb").read()
    pre = args[1] if len(args) > 1 else args[0].rsplit(".", 1)[0]
    try:
        files = lanes(img, device)
    except ValueError as e:
        sys.exit("romsplit: " + str(e))
    for k, lane in enumerate(files):
        name = "%s.lane%d.bin" % (pre, k)
        open(name, "wb").write(lane)
        print("%s  (lane %d, D%d-D%d: U%d; %s, %d bytes)" % (name, k, 8 * k, 8 * k + 7, 44 + k, device, len(lane)))

if __name__ == "__main__":
    main()
