#!/usr/bin/env python3
"""p32fs -- make, fill, inspect and check P32W Unix disk images (M3, D22;
docs/os-design.md 5).

    p32fs.py mkfs IMAGE SIZE_MB [--inodes N] [--kernel FILE]
    p32fs.py build IMAGE SIZE_MB ROOT_MANIFEST [--kernel FILE] [--inodes N]
    p32fs.py ls IMAGE [PATH]            p32fs.py cat IMAGE PATH
    p32fs.py put IMAGE HOSTFILE PATH [--mode OCT] [--uid N] [--gid N]
    p32fs.py mkdir IMAGE PATH [--mode OCT] [--uid N] [--gid N]
    p32fs.py mknod IMAGE PATH c|b MAJOR MINOR [--mode OCT]
    p32fs.py rm IMAGE PATH              p32fs.py ln IMAGE OLD NEW
    p32fs.py chmod IMAGE OCT PATH       p32fs.py chown IMAGE UID GID PATH
    p32fs.py fsck IMAGE

THE DISK (CompactFlash, 512-byte sectors):
  sector 0          boot header: "P32B", kernel sector count, load address,
                    entry (the monitor's `b`), then the filesystem's first
                    sector (word 4)
  sectors 1..       the kernel image (the boot area: FS_START - 1 sectors)
  sector FS_START   the filesystem (default 2048: 1 MiB of boot area)

THE FILESYSTEM (1 KiB blocks, numbered from the filesystem's start):
  block 0           superblock (below)
  blocks 1..        the inode table: 64-byte inodes, 16 per block; inode 0
                    unused, inode 1 the root directory
  then              the free-block bitmap: bit b of the map = block b in use
  then              data blocks
  superblock (little-endian 32-bit words): magic 0x50333246 ("F23P"),
    nblocks, ninodes, inode_start, bmap_start, data_start, clean (1 = cleanly
    unmounted), free_blocks, free_inodes
  inode (64 bytes): mode u16, nlink u16, uid u16, gid u16, size u32,
    mtime u32, ctime u32, addr[11] u32 (8 direct, single-, double-,
    triple-indirect; a device file keeps major << 8 | minor in addr[0])
  directory entry (32 bytes): inode u32 (0 = free), name 28 bytes NUL-padded
  mode: type (octal) 0170000 -- 0100000 file, 040000 directory, 020000 char
    device, 060000 block device, 010000 pipe -- 04000 set-uid, 02000 set-gid,
    01000 sticky, 0777 permissions

The kernel (kernel/fs.c) reads and writes exactly this; `fsck` checks every
invariant: the bitmap against the blocks in use, link counts against
directory entries, sizes against blocks, `.` and `..` in every directory,
every block owned once.
"""
import os, sys, struct, time, argparse

SECTOR = 512
BLOCK = 1024
FS_START = 2048                     # sectors: 1 MiB boot area
MAGIC = 0x50333246
BOOT_MAGIC = 0x42323350             # "P32B"
INODE_SIZE = 64
IPB = BLOCK // INODE_SIZE           # inodes per block
NDIRECT = 8
NINDIRECT = BLOCK // 4
DIRENT = 32
NAMELEN = 28
ROOT_INO = 1
IFMT, IFREG, IFDIR, IFCHR, IFBLK, IFIFO = 0o170000, 0o100000, 0o040000, 0o020000, 0o060000, 0o010000
MAXFILE = NDIRECT + NINDIRECT + NINDIRECT ** 2 + NINDIRECT ** 3

class FSError(Exception):
    pass

class Inode:
    FMT = "<HHHHIII11I"
    def __init__(self, ino, raw=None):
        self.ino = ino
        if raw is None:
            raw = bytes(INODE_SIZE)
        v = struct.unpack(self.FMT, raw)
        self.mode, self.nlink, self.uid, self.gid, self.size, self.mtime, self.ctime = v[:7]
        self.addr = list(v[7:18])
    def pack(self):
        return struct.pack(self.FMT, self.mode, self.nlink, self.uid, self.gid, self.size,
                           self.mtime, self.ctime, *self.addr)
    def isdir(self):
        return self.mode & IFMT == IFDIR

class FS:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "r+b")
        hdr = self.sector(0)
        magic, = struct.unpack_from("<I", hdr)
        fs_start, = struct.unpack_from("<I", hdr, 16)
        self.start = fs_start if magic == BOOT_MAGIC and fs_start else FS_START
        sb = self.read(0)
        (self.magic, self.nblocks, self.ninodes, self.inode_start, self.bmap_start, self.data_start,
         self.clean, self.free_blocks, self.free_inodes) = struct.unpack_from("<9I", sb)
        if self.magic != MAGIC:
            raise FSError("%s: no P32W filesystem (magic %08x)" % (path, self.magic))
        self.now = int(os.environ.get("P32FS_TIME") or time.time())   # (fixed: reproducible images)

    # ---------------------------------------------------------------- blocks
    def sector(self, n):
        self.f.seek(n * SECTOR)
        return self.f.read(SECTOR).ljust(SECTOR, b"\0")
    def read(self, b):
        self.f.seek(self.start * SECTOR + b * BLOCK)
        return bytearray(self.f.read(BLOCK).ljust(BLOCK, b"\0"))
    def write(self, b, data):
        assert len(data) == BLOCK
        self.f.seek(self.start * SECTOR + b * BLOCK)
        self.f.write(data)
    def flush_super(self):
        sb = self.read(0)
        struct.pack_into("<9I", sb, 0, MAGIC, self.nblocks, self.ninodes, self.inode_start, self.bmap_start,
                         self.data_start, self.clean, self.free_blocks, self.free_inodes)
        self.write(0, sb)
    def close(self):
        self.flush_super()
        self.f.close()

    def bit(self, b):
        blk = self.read(self.bmap_start + b // (BLOCK * 8))
        return blk[(b % (BLOCK * 8)) // 8] >> (b % 8) & 1
    def setbit(self, b, v):
        n = self.bmap_start + b // (BLOCK * 8)
        blk = self.read(n)
        i, m = (b % (BLOCK * 8)) // 8, 1 << (b % 8)
        blk[i] = (blk[i] | m) if v else (blk[i] & ~m)
        self.write(n, blk)
    def balloc(self):
        for n in range((self.nblocks + BLOCK * 8 - 1) // (BLOCK * 8)):
            blk = self.read(self.bmap_start + n)
            for i, byte in enumerate(blk):
                if byte != 0xFF:
                    for k in range(8):
                        b = n * BLOCK * 8 + i * 8 + k
                        if b >= self.nblocks:
                            break
                        if not byte >> k & 1:
                            self.setbit(b, 1)
                            self.write(b, bytearray(BLOCK))
                            self.free_blocks -= 1
                            return b
        raise FSError("no space left on the filesystem")
    def bfree(self, b):
        if not self.bit(b):
            raise FSError("freeing free block %d" % b)
        self.setbit(b, 0)
        self.free_blocks += 1

    # ---------------------------------------------------------------- inodes
    def iget(self, ino):
        if not 1 <= ino < self.ninodes:
            raise FSError("bad inode number %d" % ino)
        blk = self.read(self.inode_start + ino // IPB)
        o = (ino % IPB) * INODE_SIZE
        return Inode(ino, bytes(blk[o:o + INODE_SIZE]))
    def iput(self, ip):
        n = self.inode_start + ip.ino // IPB
        blk = self.read(n)
        o = (ip.ino % IPB) * INODE_SIZE
        blk[o:o + INODE_SIZE] = ip.pack()
        self.write(n, blk)
    def ialloc(self, mode, uid=0, gid=0):
        for ino in range(ROOT_INO, self.ninodes):
            ip = self.iget(ino)
            if ip.mode == 0:
                ip = Inode(ino)
                ip.mode, ip.uid, ip.gid, ip.nlink = mode, uid, gid, 0
                ip.mtime = ip.ctime = self.now
                self.iput(ip)
                self.free_inodes -= 1
                return ip
        raise FSError("out of inodes")

    def bmap(self, ip, n, alloc=False):
        """The disk block of the file's block n (0 if a hole and not alloc)."""
        if n < NDIRECT:
            if ip.addr[n] == 0 and alloc:
                ip.addr[n] = self.balloc()
            return ip.addr[n]
        n -= NDIRECT
        if n < NINDIRECT:
            if ip.addr[NDIRECT] == 0:
                if not alloc:
                    return 0
                ip.addr[NDIRECT] = self.balloc()
            return self.ind(ip.addr[NDIRECT], n, alloc)
        n -= NINDIRECT
        if n < NINDIRECT * NINDIRECT:
            if ip.addr[NDIRECT + 1] == 0:
                if not alloc:
                    return 0
                ip.addr[NDIRECT + 1] = self.balloc()
            mid = self.ind(ip.addr[NDIRECT + 1], n // NINDIRECT, alloc)
            if mid == 0:
                return 0
            return self.ind(mid, n % NINDIRECT, alloc)
        n -= NINDIRECT * NINDIRECT
        if n >= NINDIRECT ** 3:
            raise FSError("file too large")
        if ip.addr[NDIRECT + 2] == 0:
            if not alloc:
                return 0
            ip.addr[NDIRECT + 2] = self.balloc()
        mid = self.ind(ip.addr[NDIRECT + 2], n // NINDIRECT ** 2, alloc)
        if mid == 0:
            return 0
        mid = self.ind(mid, n // NINDIRECT % NINDIRECT, alloc)
        if mid == 0:
            return 0
        return self.ind(mid, n % NINDIRECT, alloc)
    def ind(self, blkno, i, alloc):
        blk = self.read(blkno)
        v, = struct.unpack_from("<I", blk, 4 * i)
        if v == 0 and alloc:
            v = self.balloc()
            struct.pack_into("<I", blk, 4 * i, v)
            self.write(blkno, blk)
        return v

    def readi(self, ip):
        out = bytearray()
        for n in range((ip.size + BLOCK - 1) // BLOCK):
            b = self.bmap(ip, n)
            out += self.read(b) if b else bytes(BLOCK)
        return bytes(out[:ip.size])
    def writei(self, ip, data, off=0):
        end = off + len(data)
        k = 0
        while k < len(data):
            n, o = (off + k) // BLOCK, (off + k) % BLOCK
            b = self.bmap(ip, n, True)
            blk = self.read(b)
            c = min(BLOCK - o, len(data) - k)
            blk[o:o + c] = data[k:k + c]
            self.write(b, blk)
            k += c
        ip.size = max(ip.size, end)
        ip.mtime = self.now
        self.iput(ip)
    def truncate(self, ip):
        for i in range(NDIRECT):
            if ip.addr[i]:
                self.bfree(ip.addr[i]); ip.addr[i] = 0
        if ip.addr[NDIRECT]:
            self.free_ind(ip.addr[NDIRECT], 1); ip.addr[NDIRECT] = 0
        if ip.addr[NDIRECT + 1]:
            self.free_ind(ip.addr[NDIRECT + 1], 2); ip.addr[NDIRECT + 1] = 0
        if ip.addr[NDIRECT + 2]:
            self.free_ind(ip.addr[NDIRECT + 2], 3); ip.addr[NDIRECT + 2] = 0
        ip.size = 0
        self.iput(ip)
    def free_ind(self, b, depth):
        blk = self.read(b)
        for i in range(NINDIRECT):
            v, = struct.unpack_from("<I", blk, 4 * i)
            if v:
                if depth > 1:
                    self.free_ind(v, depth - 1)
                else:
                    self.bfree(v)
        self.bfree(b)

    # ---------------------------------------------------------------- directories
    def entries(self, dp):
        data = self.readi(dp)
        for o in range(0, len(data), DIRENT):
            ino, = struct.unpack_from("<I", data, o)
            name = data[o + 4:o + DIRENT].split(b"\0")[0].decode("latin-1")
            yield o, ino, name
    def lookup(self, dp, name):
        for o, ino, n in self.entries(dp):
            if ino and n == name:
                return ino
        return 0
    def link(self, dp, name, ino):
        if len(name.encode()) > NAMELEN or not name or "/" in name:
            raise FSError("bad name %r" % name)
        dp = self.iget(dp.ino)                          # never write back a stale copy
        if self.lookup(dp, name):
            raise FSError("%s exists" % name)
        ent = struct.pack("<I", ino) + name.encode("latin-1").ljust(NAMELEN, b"\0")
        for o, i, n in self.entries(dp):
            if i == 0:
                self.writei(dp, ent, o)
                break
        else:
            self.writei(dp, ent, dp.size)
        ip = self.iget(ino)
        ip.nlink += 1
        ip.ctime = self.now
        self.iput(ip)
    def unlink_entry(self, dp, name):
        for o, ino, n in self.entries(dp):
            if ino and n == name:
                self.writei(dp, bytes(DIRENT), o)
                return ino
        raise FSError("%s: not found" % name)
    def namei(self, path, parent=False):
        parts = [p for p in path.split("/") if p]
        ip = self.iget(ROOT_INO)
        if parent:
            if not parts:
                raise FSError("no parent of /")
            last = parts.pop()
        for p in parts:
            if not ip.isdir():
                raise FSError("%s: not a directory" % p)
            ino = self.lookup(ip, p)
            if not ino:
                raise FSError("%s: no such file or directory" % path)
            ip = self.iget(ino)
        return (ip, last) if parent else ip

    # ---------------------------------------------------------------- operations
    def mkdir(self, path, mode=0o755, uid=0, gid=0):
        dp, name = self.namei(path, True)
        if self.lookup(dp, name):
            raise FSError("%s exists" % name)
        ip = self.ialloc(IFDIR | mode, uid, gid)
        self.link(ip, ".", ip.ino)
        self.link(ip, "..", dp.ino)
        self.link(dp, name, ip.ino)
        return self.iget(ip.ino)
    def create(self, path, data, mode=0o644, uid=0, gid=0):
        dp, name = self.namei(path, True)
        if self.lookup(dp, name):
            raise FSError("%s exists" % name)
        ip = self.ialloc(IFREG | mode, uid, gid)
        self.link(dp, name, ip.ino)
        ip = self.iget(ip.ino)
        self.writei(ip, data)
        return ip
    def mknod(self, path, kind, major, minor, mode=0o666, uid=0, gid=0):
        dp, name = self.namei(path, True)
        if self.lookup(dp, name):
            raise FSError("%s exists" % name)
        ip = self.ialloc((IFCHR if kind == "c" else IFBLK) | mode, uid, gid)
        ip = self.iget(ip.ino)
        ip.addr[0] = major << 8 | minor
        self.iput(ip)
        self.link(dp, name, ip.ino)
    def remove(self, path):
        dp, name = self.namei(path, True)
        ino = self.lookup(dp, name)
        if not ino:
            raise FSError("%s: not found" % path)
        ip = self.iget(ino)
        if ip.isdir():
            if any(i and n not in (".", "..") for o, i, n in self.entries(ip)):
                raise FSError("%s: directory not empty" % path)
            dp = self.iget(dp.ino)
            dp.nlink -= 1                               # its ".."
            self.iput(dp)
            ip.nlink = 1                                # its "." goes with it
        self.unlink_entry(self.iget(dp.ino), name)
        ip = self.iget(ino)
        ip.nlink -= 1 if not ip.isdir() else ip.nlink
        if ip.nlink <= 0:
            if (ip.mode & IFMT) in (IFREG, IFDIR):
                self.truncate(ip)
            ip.mode = 0
            ip.nlink = 0
            self.free_inodes += 1
        self.iput(ip)

def mkfs(image, size_mb, ninodes=None, kernel=None):
    nsect = size_mb * 1024 * 1024 // SECTOR
    nblocks = (nsect - FS_START) * SECTOR // BLOCK
    if nblocks < 64:
        raise FSError("image too small")
    ninodes = ninodes or min(max(64, nblocks // 8), 512 * 1024)
    ninodes = (ninodes + IPB - 1) // IPB * IPB
    inode_start = 1
    bmap_start = inode_start + ninodes // IPB
    nbmap = (nblocks + BLOCK * 8 - 1) // (BLOCK * 8)
    data_start = bmap_start + nbmap
    with open(image, "wb") as fh:
        fh.truncate(nsect * SECTOR)
    fs_raw = open(image, "r+b")
    hdr = struct.pack("<5I", BOOT_MAGIC, 0, 0x00100000, 0x00100000, FS_START)
    fs_raw.write(hdr.ljust(SECTOR, b"\0"))
    sb = struct.pack("<9I", MAGIC, nblocks, ninodes, inode_start, bmap_start, data_start, 1,
                     nblocks - data_start, ninodes - 2)          # inode 0 unused, 1 the root
    fs_raw.seek(FS_START * SECTOR)
    fs_raw.write(sb.ljust(BLOCK, b"\0"))
    fs_raw.close()
    fs = FS(image)
    for b in range(data_start):
        fs.setbit(b, 1)
    root = Inode(ROOT_INO)
    root.mode = IFDIR | 0o755
    root.mtime = root.ctime = fs.now
    fs.iput(root)
    fs.link(fs.iget(ROOT_INO), ".", ROOT_INO)
    fs.link(fs.iget(ROOT_INO), "..", ROOT_INO)
    if kernel:
        install_kernel(fs, kernel)
    fs.close()

def install_kernel(fs, path):
    """The kernel image (a flat binary, linked at 0x00100000) into the boot area."""
    data = open(path, "rb").read()
    n = (len(data) + SECTOR - 1) // SECTOR
    if n >= fs.start:
        raise FSError("kernel (%d sectors) does not fit the boot area (%d)" % (n, fs.start - 1))
    fs.f.seek(0)
    fs.f.write(struct.pack("<5I", BOOT_MAGIC, n, 0x00100000, 0x00100000, fs.start).ljust(SECTOR, b"\0"))
    fs.f.write(data.ljust(n * SECTOR, b"\0"))

# ---------------------------------------------------------------- fsck
def fsck(fs):
    errs = []
    used = {}                           # block -> owner
    links = {}                          # inode -> directory entries naming it
    def claim(b, who):
        if not (fs.data_start <= b < fs.nblocks):
            errs.append("%s: block %d out of range" % (who, b))
        elif b in used:
            errs.append("block %d owned by %s and %s" % (b, used[b], who))
        else:
            used[b] = who
    inodes = {}
    for ino in range(ROOT_INO, fs.ninodes):
        ip = fs.iget(ino)
        if ip.mode == 0:
            continue
        inodes[ino] = ip
        t = ip.mode & IFMT
        if t not in (IFREG, IFDIR, IFCHR, IFBLK, IFIFO):
            errs.append("inode %d: bad type %o" % (ino, ip.mode))
            continue
        if t in (IFCHR, IFBLK):
            continue
        nb = (ip.size + BLOCK - 1) // BLOCK
        who = "inode %d" % ino
        for i in range(NDIRECT):
            if ip.addr[i]:
                claim(ip.addr[i], who)
                if i >= nb:
                    errs.append("%s: block beyond its size" % who)
        for depth, root in ((1, ip.addr[NDIRECT]), (2, ip.addr[NDIRECT + 1]), (3, ip.addr[NDIRECT + 2])):
            if root:
                stack = [(root, depth)]
                while stack:
                    b, d = stack.pop()
                    claim(b, who)
                    blk = fs.read(b)
                    for k in range(NINDIRECT):
                        v, = struct.unpack_from("<I", blk, 4 * k)
                        if v:
                            if d > 1:
                                stack.append((v, d - 1))
                            else:
                                claim(v, who)
    for ino, ip in inodes.items():
        if not ip.isdir():
            continue
        if ip.size % DIRENT:
            errs.append("directory %d: size %d not a multiple of %d" % (ino, ip.size, DIRENT))
        names = {}
        for o, child, name in fs.entries(ip):
            if not child:
                continue
            if child not in inodes:
                errs.append("directory %d: %s names free inode %d" % (ino, name, child))
                continue
            names[name] = child
            links[child] = links.get(child, 0) + 1
        if names.get(".") != ino:
            errs.append("directory %d: bad ." % ino)
        if ".." not in names:
            errs.append("directory %d: no .." % ino)
    for ino, ip in inodes.items():
        if links.get(ino, 0) != ip.nlink:
            errs.append("inode %d: link count %d, %d entries" % (ino, ip.nlink, links.get(ino, 0)))
    nfree = 0
    for b in range(fs.nblocks):
        bit = fs.bit(b)
        inuse = b < fs.data_start or b in used
        if bit != inuse:
            errs.append("block %d: bitmap says %s" % (b, "used" if bit else "free"))
        nfree += not bit
    if nfree != fs.free_blocks:
        errs.append("superblock: %d free blocks, counted %d" % (fs.free_blocks, nfree))
    nfi = fs.ninodes - 1 - len(inodes)
    if nfi != fs.free_inodes:
        errs.append("superblock: %d free inodes, counted %d" % (fs.free_inodes, nfi))
    return errs, len(inodes), len(used)

# ---------------------------------------------------------------- the manifest
def build(image, size_mb, manifest, kernel=None, ninodes=None):
    """A root filesystem from a manifest: one entry per line,
         d PATH MODE UID GID                     a directory
         f PATH MODE UID GID HOSTFILE            a file (HOSTFILE relative to the manifest)
         c PATH MODE UID GID MAJOR MINOR         a character device (b: block)
         l PATH TARGET                           a hard link
         t PATH MODE UID GID HOSTDIR             a host directory tree: its files
                                                 with MODE (executable ones +x), its
                                                 directories MODE with search; a
                                                 links.txt at its top ("NEW OLD" per
                                                 line, both inside the tree) makes
                                                 hard links instead of being copied
       '#' starts a comment."""
    mkfs(image, size_mb, ninodes, kernel)
    fs = FS(image)
    base = os.path.dirname(os.path.abspath(manifest))
    for num, line in enumerate(open(manifest), 1):
        line = line.split("#")[0].split()
        if not line:
            continue
        try:
            k, path = line[0], line[1]
            if k == "l":
                link_path(fs, line[2], path)
                continue
            mode, uid, gid = int(line[2], 8), int(line[3]), int(line[4])
            if k == "d":
                fs.mkdir(path, mode, uid, gid)
            elif k == "f":
                data = open(os.path.join(base, line[5]), "rb").read()
                fs.create(path, data, mode, uid, gid)
            elif k in "cb":
                fs.mknod(path, k, int(line[5]), int(line[6]), mode, uid, gid)
            elif k == "t":
                copy_tree_top(fs, os.path.join(base, line[5]), path, mode, uid, gid)
            else:
                raise FSError("unknown kind %s" % k)
        except (FSError, IndexError, ValueError, OSError) as e:
            raise FSError("%s:%d: %s" % (manifest, num, e))
    fs.close()

def copy_tree(fs, host, path, mode, uid, gid):
    """host's tree into path (made if missing), in name order; links.txt at
    its top lists hard links ("NEW OLD", relative to the tree)."""
    try:
        fs.namei(path)
    except FSError:                     # (directories: the mode with search where it reads)
        fs.mkdir(path, mode | (mode & 0o444) >> 2, uid, gid)
    for name in sorted(os.listdir(host)):
        h = os.path.join(host, name)
        if name.startswith(".") or (name == "links.txt" and host == copy_tree.top):
            continue
        if os.path.isdir(h):
            copy_tree(fs, h, path + "/" + name, mode, uid, gid)
        else:                           # (a host file that is executable stays so)
            x = (mode & 0o444) >> 2 if os.access(h, os.X_OK) else 0
            fs.create(path + "/" + name, open(h, "rb").read(), mode | x, uid, gid)

def copy_tree_top(fs, host, path, mode, uid, gid):
    copy_tree.top = host
    copy_tree(fs, host, path, mode, uid, gid)
    links = os.path.join(host, "links.txt")
    if os.path.exists(links):
        for ln in open(links):
            w = ln.split()
            if len(w) == 2:
                link_path(fs, path + "/" + w[1], path + "/" + w[0])
copy_tree.top = None

def link_path(fs, old, new):
    ip = fs.namei(old)
    if ip.isdir():
        raise FSError("cannot link a directory")
    dp, name = fs.namei(new, True)
    fs.link(dp, name, ip.ino)

def modestr(m):
    t = {IFDIR: "d", IFCHR: "c", IFBLK: "b", IFIFO: "p"}.get(m & IFMT, "-")
    s = ""
    for k in range(3):
        bits = m >> (6 - 3 * k) & 7
        s += ("r" if bits & 4 else "-") + ("w" if bits & 2 else "-") + ("x" if bits & 1 else "-")
    s = list(s)                         # (set-uid, set-gid and sticky, as ls shows them)
    if m & 0o4000: s[2] = "s" if s[2] == "x" else "S"
    if m & 0o2000: s[5] = "s" if s[5] == "x" else "S"
    if m & 0o1000: s[8] = "t" if s[8] == "x" else "T"
    return t + "".join(s)

def main():
    ap = argparse.ArgumentParser(description="P32W Unix disk images")
    ap.add_argument("cmd")
    ap.add_argument("image")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--inodes", type=int)
    ap.add_argument("--kernel")
    ap.add_argument("--mode")
    ap.add_argument("--uid", type=int, default=0)
    ap.add_argument("--gid", type=int, default=0)
    a = ap.parse_args()
    try:
        if a.cmd == "mkfs":
            mkfs(a.image, int(a.args[0]), a.inodes, a.kernel)
            return
        if a.cmd == "build":
            build(a.image, int(a.args[0]), a.args[1], a.kernel, a.inodes)
            fs = FS(a.image)
            errs, ni, nb = fsck(fs)
            if errs:
                sys.exit("p32fs: the new image fails fsck:\n  " + "\n  ".join(errs))
            print("%s: %d inodes, %d blocks used, fsck clean" % (a.image, ni, nb))
            return
        fs = FS(a.image)
        if a.cmd == "ls":
            ip = fs.namei(a.args[0] if a.args else "/")
            items = [(n, fs.iget(i)) for o, i, n in fs.entries(ip) if i] if ip.isdir() else [(a.args[0], ip)]
            for n, c in items:
                dev = c.mode & IFMT in (IFCHR, IFBLK)
                print("%s %2d %4d %4d %8s %s" % (modestr(c.mode), c.nlink, c.uid, c.gid,
                      "%d,%d" % (c.addr[0] >> 8, c.addr[0] & 255) if dev else c.size, n))
        elif a.cmd == "cat":
            sys.stdout.buffer.write(fs.readi(fs.namei(a.args[0])))
        elif a.cmd == "put":
            data = open(a.args[0], "rb").read()
            fs.create(a.args[1], data, int(a.mode or "644", 8), a.uid, a.gid)
        elif a.cmd == "mkdir":
            fs.mkdir(a.args[0], int(a.mode or "755", 8), a.uid, a.gid)
        elif a.cmd == "mknod":
            fs.mknod(a.args[0], a.args[1], int(a.args[2]), int(a.args[3]), int(a.mode or "666", 8))
        elif a.cmd == "rm":
            fs.remove(a.args[0])
        elif a.cmd == "ln":
            link_path(fs, a.args[0], a.args[1])
        elif a.cmd == "chmod":
            ip = fs.namei(a.args[1]); ip.mode = ip.mode & ~0o7777 | int(a.args[0], 8); fs.iput(ip)
        elif a.cmd == "chown":
            ip = fs.namei(a.args[2]); ip.uid, ip.gid = int(a.args[0]), int(a.args[1]); fs.iput(ip)
        elif a.cmd == "fsck":
            errs, ni, nb = fsck(fs)
            for e in errs:
                print(e)
            print("%s: %d inodes, %d data blocks, %s" % (a.image, ni, nb, "%d problem(s)" % len(errs) if errs else "clean"))
            fs.close()
            sys.exit(1 if errs else 0)
        else:
            sys.exit("p32fs: unknown command %s" % a.cmd)
        fs.close()
    except FSError as e:
        sys.exit("p32fs: %s" % e)

if __name__ == "__main__":
    main()
