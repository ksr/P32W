#!/usr/bin/env python3
"""test_p32fs.py -- the disk-image tool's tests (make test): build an image,
fill it (directories, a file using double-indirect blocks, a sparse one
using triple-indirect blocks, hard links,
device files), read everything back, remove it all, and fsck after every
step; then break an image on purpose and check fsck notices."""
import os, sys, tempfile, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p32fs  # noqa: E402

fails = []
def check(name, cond):
    if not cond:
        fails.append(name)
def clean(img, what):
    fs = p32fs.FS(img)
    errs, _, _ = p32fs.fsck(fs)
    fs.close()
    check("fsck clean after %s: %s" % (what, "; ".join(errs[:3])), not errs)

d = tempfile.mkdtemp()
img = os.path.join(d, "t.img")
p32fs.mkfs(img, 8)
clean(img, "mkfs")
fs = p32fs.FS(img)
big = bytes((i * 7 + i // 1000) & 255 for i in range(400000))     # > 8 + 256 blocks
for p in ("/bin", "/etc", "/dev", "/usr", "/usr/ken"):
    fs.mkdir(p)
fs.create("/etc/passwd", b"root::0:0:root:/:/bin/sh\n")
fs.create("/usr/ken/big", big, 0o600, 100, 10)
link_ok = True
p32fs.link_path(fs, "/usr/ken/big", "/usr/ken/big2")
fs.mknod("/dev/tty0", "c", 1, 0)
# a sparse file reaching past the double-indirect blocks (64.3 MiB): triple
far = 70 * 1024 * 1024
sp = fs.create("/sparse", b"head")
fs.writei(fs.namei("/sparse"), b"tail at 70 MiB", far)
fs.close()
clean(img, "filling")
fs = p32fs.FS(img)
check("big file reads back", fs.readi(fs.namei("/usr/ken/big2")) == big)
check("owner kept", fs.namei("/usr/ken/big").uid == 100)
check("link count 2", fs.namei("/usr/ken/big").nlink == 2)
check("dir link count (.., subdirs)", fs.namei("/usr").nlink == 3)
check("device number", fs.namei("/dev/tty0").addr[0] == 1 << 8)
spi = fs.namei("/sparse")
check("triple-indirect block in use", spi.addr[p32fs.NDIRECT + 2] != 0)
check("sparse file reads back", fs.readi(spi)[far:] == b"tail at 70 MiB" and fs.readi(spi)[:4] == b"head")
fs.remove("/sparse")
try:
    fs.mkdir("/usr/ken")
    check("mkdir of an existing name refused", False)
except p32fs.FSError:
    pass
try:
    fs.remove("/usr")
    check("removing a full directory refused", False)
except p32fs.FSError:
    pass
free0 = fs.free_blocks
fs.remove("/usr/ken/big")
check("one link gone, data kept", fs.free_blocks == free0)
fs.remove("/usr/ken/big2")
check("last link frees the blocks", fs.free_blocks > free0 + 390)
for p in ("/usr/ken", "/usr", "/dev/tty0", "/dev", "/etc/passwd", "/etc", "/bin"):
    fs.remove(p)
fs.close()
clean(img, "removing everything")
fs = p32fs.FS(img)
errs, ninodes, nblocks = p32fs.fsck(fs)
check("back to the root alone", ninodes == 1 and nblocks == 1)
# break it: a block in use but free in the bitmap; a wrong link count
fs.create("/x", b"data")
ip = fs.namei("/x")
fs.setbit(ip.addr[0], 0)
ip.nlink = 3
fs.iput(ip)
errs, _, _ = p32fs.fsck(fs)
check("fsck sees a bitmap error", any("bitmap" in e for e in errs))
check("fsck sees a link-count error", any("link count" in e for e in errs))
if fails:
    print("FAIL p32fs: " + "; ".join(fails))
    sys.exit(1)
print("PASS p32fs: mkfs, files to triple-indirect, links, devices, remove, fsck (and fsck finds damage)")
