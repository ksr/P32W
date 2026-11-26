# The P32W Unix filesystem

**Status:** DESCRIBES THE SYSTEM AS BUILT (2026-10-26). Two programs read
and write this format, and they must agree:
- **`tools/p32fs.py`** on the Mac: builds disk images, puts files in,
  lists, checks (`fsck`). Its opening comment is the format's reference.
- **The kernel**, `kernel/fs.c` (files, directories, inodes, blocks),
  `kernel/bio.c` (the buffer cache) and `kernel/cf.c` (the disk), with
  `mkfs` and `fsck` on P32W itself (`software/cmd`).

`make test` checks each against the other: an image built on the Mac is
used and changed by P32W Unix, and either side's `fsck` must pass on the
other's work. The design began in `os-design.md` §5 (D22); this chapter is
what was built.

It is a **Unix V7-style inode filesystem**: files are numbered inodes,
directories are files of (number, name) entries, and a file's data is
found through a table of block numbers in its inode. It differs from V7's
in four ways: 1 KiB blocks, 28-character names, a free-block **bitmap**
instead of V7's free list, and a triple-indirect block (files to 2 GiB).

## 1. The disk

A CompactFlash card (or any ATA drive) is a row of 512-byte sectors.
P32W Unix's boot card has three regions:

| Sectors | Region | Who uses it |
|---|---|---|
| 0 | the **boot header** (`"P32B"`, kernel size, load address, entry, the filesystem's first sector) | the monitor's `b` ([boot](boot.md)) |
| 1 … 2047 | the **boot area** (1 MiB): the kernel image | the monitor's `b` |
| 2048 … end | **the filesystem** | the kernel, as block device 0, minor 0 (`/dev/disk`) |

The filesystem's first sector is the boot header's word 4 (2048 unless the
image says otherwise; `cf.c` reads it at boot). Everything below counts in
**blocks of 1 KiB** (two sectors) from that point: block *b* is sectors
`2048 + 2b` and `2048 + 2b + 1`, moved by one two-sector ATA command.

Other drives (the second CF socket, the IDE drives, a USB stick, the RAM
disk) hold a filesystem from their sector 0, with no boot area.

## 2. The layout inside the filesystem

| Blocks | Contents |
|---|---|
| 0 | **the superblock** |
| 1 … | **the inode table**: 64-byte inodes, 16 per block |
| then | **the free-block bitmap**: one bit per block of the filesystem |
| then | **data blocks**: file contents, directories, indirect blocks |

**The superblock** (block 0), nine little-endian 32-bit words:

| Word | Field | Meaning |
|---|---|---|
| 0 | `magic` | `0x5033_3246` (the bytes `F23P`) |
| 1 | `nblocks` | blocks in the filesystem |
| 2 | `ninodes` | inodes (a multiple of 16) |
| 3 | `inode_start` | the inode table's first block (1) |
| 4 | `bmap_start` | the bitmap's first block |
| 5 | `data_start` | the first data block |
| 6 | `clean` | 1: written out and unmounted cleanly; 0: in use, or it crashed |
| 7 | `free_blocks` | free data blocks (kept up to date by the kernel) |
| 8 | `free_inodes` | free inodes |

**An example:** the disk `make disk` builds (`DISK_MB = 512`,
`DISK_INODES = 16384`): 1,048,576 sectors; the filesystem from sector 2048
has 523,264 blocks; 16,384 inodes fill blocks 1–1,024; the bitmap
(523,264 bits) is blocks 1,025–1,088; data starts at block 1,089. On the
Mac, `p32fs.py mkfs` gives one inode per 8 blocks by default (at least 64,
at most 512 Ki).

**The bitmap:** bit *b* (bit `b mod 8` of byte `b / 8`, counting from
`bmap_start`) is 1 if block *b* is in use. The blocks of the superblock,
the inode table and the bitmap itself are marked in use when the
filesystem is made, so the allocator never hands them out.

## 3. Inodes

Inode *n* is the 64 bytes at offset `(n mod 16) × 64` of block
`inode_start + n / 16`. **Inode 0 is never used** (a directory entry with
inode 0 is an empty slot). **Inode 1 is the root directory.** An inode
whose `mode` is 0 is free.

| Bytes | Field | |
|---|---|---|
| 0–1 | `mode` | the type and permissions (below) |
| 2–3 | `nlink` | how many directory entries name it |
| 4–5 | `uid` | owner |
| 6–7 | `gid` | group |
| 8–11 | `size` | in bytes |
| 12–15 | `mtime` | last modified (seconds since 1970, UTC) |
| 16–19 | `ctime` | created / inode changed |
| 20–63 | `addr[11]` | block numbers: 8 direct, 1 single-, 1 double-, 1 triple-indirect |

**`mode`** (octal, as in V7):

| Bits | Meaning |
|---|---|
| `0170000` | type: `0100000` regular file, `0040000` directory, `0020000` character device, `0060000` block device, `0010000` pipe (`0140000` socket only in `fstat`, never on disk) |
| `04000` | set-user-ID: the program runs with its owner's user ID |
| `02000` | set-group-ID |
| `01000` | sticky (on `/tmp`: only a file's owner may remove it) |
| `0777` | read, write, execute for the owner, the group, others |

**A device file** has no data: `addr[0]` holds its device number,
`major << 8 | minor`. Its type says which device table the major indexes
(character or block, §8).

**Where a file's bytes are.** Byte offset *o* is in the file's block
`fb = o / 1024`. `bmap` (fs.c) finds its disk block:

| File blocks | Found through | Reach |
|---|---|---|
| 0–7 | `addr[0]`–`addr[7]` directly | 8 KiB |
| 8–263 | `addr[8]`: an indirect block of 256 block numbers | + 256 KiB |
| 264–65,799 | `addr[9]`: an indirect block of 256 indirect blocks | + 64 MiB |
| beyond | `addr[10]`: triple-indirect | + 16 GiB |

A file's size is a signed 32-bit offset, so it stops at 2 GiB before the
triple-indirect block runs out. A block number of 0 is a **hole**: it
reads as zeros, and the block is allocated only when written.

## 4. Directories

A directory is a file of **32-byte entries**: a 32-bit inode number and a
name of up to **28 bytes**, padded with NULs (no NUL when it is exactly 28
long). Inode 0 marks a free slot. Every directory begins with `.` (itself)
and `..` (its parent; the root's `..` is the root).

- **Lookup** (`dirlookup`) reads the entries in order and compares names.
  There is no hashing or sorting, as in V7: a directory of a few hundred
  entries is still read in a few blocks.
- **Adding** a name (`dirlink`) reuses the first free slot, or appends.
- **Removing** one writes inode 0 into its slot. Directories never shrink.
- **Paths** (`namex`): from `/` or the process's current directory,
  element by element. Each directory along the way must be searchable
  (execute permission). A name longer than 28 characters is
  `ENAMETOOLONG`. Crossing a mount point goes to the mounted filesystem's
  root, and `..` from that root goes back to the directory it is mounted on.

## 5. How the kernel uses it

### 5.1 The buffer cache (`bio.c`)

Every block the filesystem reads or writes passes through **48 buffers**
of 1 KiB, the least recently used reused first (Unix V6's `bio`):

- `bread` returns the block's buffer, read from the disk if it is not
  already there; `brelse` gives it back. A buffer is **busy** while one
  process holds it, and another wanting it sleeps.
- **Delayed writes:** `bdwrite` only marks a buffer dirty. It is written
  when the buffer is reused, by `sync`, or by the kernel's own flush every
  30 seconds. `bwrite` writes at once (the superblock does).
- `bnoread` gives a buffer for a block about to be overwritten whole,
  without reading the disk first (a full-block write, a new block).
- **exec's fast path** (`readi_pages`): runs of consecutive blocks not in
  the cache go straight from the drive into the new program's pages, up to
  64 blocks per ATA command, past the cache.

### 5.2 Inodes in memory (`fs.c`)

Up to **96 inodes** are held in a table (`itab`): `iget` finds or claims
an entry by (device, number) and counts references; `iput` drops one. When
the last reference goes and the link count is 0, the file is freed:
`itrunc` returns its blocks and the inode's `mode` becomes 0. A process
**locks** an inode (`ilock`, reading it from disk the first time) while it
reads or changes it, so two processes never interleave inside one file's
blocks.

### 5.3 Allocation

- **Blocks** (`balloc`): the search in the bitmap starts just after the
  last block allocated on that filesystem. A file being written therefore
  gets consecutive blocks, and a large disk is not scanned from the start
  every time. Bytes that are all ones (eight blocks in use) are stepped
  over at once. A new block is zeroed in its buffer.
- **Inodes** (`ialloc`): the first inode with mode 0, from inode 1.
- The superblock's `free_blocks` and `free_inodes` follow every change.
  `df` and the boot line report them.

### 5.4 Permissions

`access_ok`: root (effective user ID 0) may read and write anything, and
execute a regular file if any execute bit is set. Anyone else gets the
owner's bits if they own the file, else the group's if their effective
group matches, else the others'. A set-user-ID program sets the effective
user ID at `exec`.

### 5.5 Mounting

`mounts[0]` is the root filesystem (`/dev/disk`). Three more can be
mounted (`mount /dev/usb /mnt`), each on a directory. Mounting reads the
superblock, checks the magic, and writes `clean = 0`. Unmounting (refused
while a file on it is open, or a process's current directory is there)
writes everything out with `clean = 1`.

## 6. Crash safety

There is no journal, as in V6 and V7. Instead:
- dirty blocks reach the disk within 30 seconds (the clock sets a flag,
  and the next process returning to user mode flushes the cache), and at
  every `sync`;
- **the clean flag**: mounting writes `clean = 0`; `halt`, `reboot` and
  `umount` write everything and then `clean = 1`. A filesystem found not
  clean at boot gets the warning `not unmounted cleanly (run fsck)`, and
  `/etc/rc` runs `fsck -q -c`, which checks fully only in that case (D40),
  so a large disk does not slow every boot.

**`fsck`** (on P32W and in `p32fs.py`) checks every invariant:
- every block in use is in range and owned by exactly one file;
- the bitmap matches the blocks in use;
- every directory holds `.` and `..`, and names only allocated inodes;
- every link count equals the number of entries naming the inode;
- the superblock's free counts are right.

With `-y` it repairs: it clears bad inodes and blocks owned twice, removes
entries that name free inodes, clears inodes nothing names, sets the link
counts, and rebuilds the bitmap and the counts. Repairing the mounted root
ends with an immediate restart, without writing the kernel's cache back,
as V7's fsck did.

## 7. Building and inspecting images on the Mac

`tools/p32fs.py` works on image files:

```
p32fs.py mkfs IMAGE SIZE_MB [--inodes N] [--kernel FILE]
p32fs.py build IMAGE SIZE_MB ROOT_MANIFEST [--kernel FILE] [--inodes N]
p32fs.py ls IMAGE [PATH]          p32fs.py cat IMAGE PATH
p32fs.py put IMAGE HOSTFILE PATH [--mode OCT] [--uid N] [--gid N]
p32fs.py mkdir / mknod / rm / ln / chmod / chown ...
p32fs.py fsck IMAGE
```

`make -C software disk` runs `build` with `software/root.manifest`, one
line per directory, file or device node: kind, path, mode, owner, group,
then the host file or the device numbers. On P32W itself, `mkfs special
nblocks [ninodes]` makes a filesystem on any block device, and `fsck` checks
one.

## 8. Devices

A device number is `major << 8 | minor`. **Block devices** move 1 KiB
blocks through the buffer cache, and a filesystem can live on one;
**character devices** move bytes straight between the driver and the
process (`file.c`, `bdevsw` and `cdevsw`).

| Block | Major | Minors | Driver |
|---|---|---|---|
| `/dev/disk` | 0 | 0 | the boot card's filesystem area (`cf.c`) |
| `/dev/disk1`, `/dev/hda`–`hdd` | 0 | 1–5 | the second CF card, IDE A master/slave, IDE B master/slave: whole drives |
| `/dev/ram` | 1 | 0 | a 256 KiB RAM disk (`ramdisk.c`) |
| `/dev/usb`, `/dev/usb1` | 2 | 0, 1 | a USB drive in either port (`usb.c`, D43) |

| Character | Major | Minors | Driver |
|---|---|---|---|
| `/dev/tty0`–`tty3`, `tty4`, `ttyp0`–`ttyp15` | 1 | 0–3, 4, 5–20 | the UARTs, the video console, the pseudo-terminals' slave ends (`tty.c`; `ttyp4`–`ttyp15`, minors 9–20, for `wm`'s windows, D122) |
| `/dev/null`, `/dev/mem`, `/dev/port` | 2 | 0, 1, 2 | discard; physical memory (root); the I/O card's ports |
| `/dev/rdisk`, `/dev/rdisk1`, `/dev/rhda`… | 3 | 0–5 | the drives' sectors, raw (root) |
| `/dev/tty` | 4 | 0 | the process's controlling terminal |
| `/dev/rusb`, `/dev/rusb1` | 5 | 0, 1 | a USB drive's sectors, raw (the FAT tools) |
| `/dev/ptyp0`–`ptyp15` | 6 | 0–15 | the pseudo-terminals' master ends (`telnetd` 0–3, D46; `wm` 4–15, D122) |
| `/dev/video` | 7 | 0 | the video card's chip, for a program's own drawing (`video.c`, D110) |
| `/dev/mouse` | 8 | 0 | the video card's PS/2 mouse (`video.c`, D120) |
