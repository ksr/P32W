#!/usr/bin/env python3
"""mkpasswd.py -- the P32W password hash on the host (software/libc/pwd.c's
pwhash, bit for bit), to build /etc/passwd for a disk image.

    mkpasswd.py PASSWORD [SALT]      prints SSSS$HHHHHHHHHHHHHHHH
"""
import sys, random
M = 0xFFFFFFFF
def rotl(x, k):
    return ((x << k) | (x >> (32 - k))) & M
def pwhash(pw, salt):
    s = int(salt, 16) & 0xFFFF
    a, b = 0x811C9DC5 ^ s, 0x9E3779B9 ^ ((s * 0x10001) & M)
    data = pw.encode("latin-1")
    for r in range(2000):
        for c in data:
            a = ((a ^ c) * 0x01000193) & M
            b = (rotl(b ^ a, 5) * 0x9E3779B1) & M
        a ^= (rotl(b, 13) + r) & M
        b = (b + (a ^ 0x5BD1E995)) & M
    return "%04x$%08x%08x" % (s, a, b)
if __name__ == "__main__":
    salt = sys.argv[2] if len(sys.argv) > 2 else "%04x" % random.randrange(65536)
    print(pwhash(sys.argv[1], salt))
