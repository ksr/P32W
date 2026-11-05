/* fs.c -- the P32W Unix filesystem (M3, D22; os-design.md 5). The on-disk
 * format is tools/p32fs.py's: the host tool builds images, the kernel reads
 * and writes them, and either one's fsck must pass on the other's work.
 *
 * Inodes are cached in itab (iget / iput); an inode is locked (ilock) while
 * a process reads or changes it. Free blocks are a bitmap, free inodes have
 * mode 0. Every change goes through the buffer cache.
 */
#include "defs.h"
#include <sys/stat.h>

struct mount mounts[NMOUNT];
uint boottime;                          /* seconds; set by stime() */
static struct inode itab[NINODE];

uint fs_time(void) { return boottime + ticks / HZ; }

struct superblock *sbof(int dev)
{
    int k;
    for (k = 0; k < NMOUNT; k++)
        if (mounts[k].used && mounts[k].dev == dev) return &mounts[k].super;
    panic("no filesystem mounted on the device");
    return 0;
}

static struct mount *mountof(int dev)
{
    int k;
    for (k = 0; k < NMOUNT; k++)
        if (mounts[k].used && mounts[k].dev == dev) return &mounts[k];
    return 0;
}

static void sb_write(struct mount *m)
{
    struct buf *b = bread(m->dev, 0);
    memcpy(b->data, &m->super, sizeof m->super);
    bwrite(b);
}

void fsinit(int dev)
{
    struct buf *b = bread(dev, 0);
    mounts[0].used = 1;
    mounts[0].dev = dev;
    memcpy(&sb, b->data, sizeof sb);
    brelse(b);
    if (sb.magic != FS_MAGIC) panic("no filesystem on the disk");
    if (!sb.clean) kprintf("warning: the filesystem was not unmounted cleanly (run fsck)\n");
    mounts[0].wasclean = sb.clean;
    sb.clean = 0;
    sb_write(&mounts[0]);
}

void fs_sync(void)
{
    int k;
    bflush();
    for (k = 0; k < NMOUNT; k++)
        if (mounts[k].used) { mounts[k].super.clean = 0; sb_write(&mounts[k]); }
}

/* every filesystem written out and marked clean (halt, reboot) */
void fs_clean(void)
{
    int k;
    bflush();
    for (k = 0; k < NMOUNT; k++)
        if (mounts[k].used) { mounts[k].super.clean = 1; sb_write(&mounts[k]); }
}

/* mount the filesystem on block device dev on directory on (unlocked,
   referenced: kept until umount); 0 or an error */
int fs_mount(int dev, struct inode *on)
{
    int k;
    struct mount *m = 0;
    struct buf *b;
    for (k = 1; k < NMOUNT; k++) {
        if (mounts[k].used && mounts[k].dev == dev) return -EBUSY;
        if (mounts[k].used && mounts[k].on == on) return -EBUSY;
        if (!mounts[k].used && !m) m = &mounts[k];
    }
    if (dev == ROOTDEV) return -EBUSY;
    if (!m) return -ENOMEM;
    b = bread(dev, 0);
    if (b->flags & B_ERROR) { brelse(b); return -EIO; }
    memcpy(&m->super, b->data, sizeof m->super);
    brelse(b);
    if (m->super.magic != FS_MAGIC) return -EINVAL;
    m->used = 1;
    m->dev = dev;
    m->on = on;
    m->bnext = 0;
    m->root = iget(dev, ROOTINO);
    if (!m->super.clean) kprintf("warning: the filesystem on %d,%d was not unmounted cleanly (run fsck)\n", major(dev), minor(dev));
    m->wasclean = m->super.clean;
    m->super.clean = 0;
    sb_write(m);
    return 0;
}

/* unmount it: 0, or -EBUSY while a file or directory on it is in use */
int fs_umount(int dev)
{
    struct mount *m = mountof(dev);
    struct inode *ip;
    if (!m || m == &mounts[0]) return -EINVAL;
    for (ip = itab; ip < itab + NINODE; ip++)
        if (ip->ref > 0 && ip->dev == dev && !(ip == m->root && ip->ref == 1)) return -EBUSY;
    bflush();
    m->super.clean = 1;
    sb_write(m);
    binval(dev);
    iput(m->root);
    iput(m->on);
    m->used = 0;
    return 0;
}

/* ---------------------------------------------------------------- blocks */
/* A free block: the search starts just after the last one allocated (a
   large disk is not scanned from its start every time, and a file's blocks
   come out in order) and steps over full bytes eight blocks at a time. */
int fs_ioerr;                           /* balloc failed reading, not full (writei: EIO, not ENOSPC) */

static uint balloc(int dev)
{
    struct mount *mt = mountof(dev);
    struct superblock *S = &mt->super;
    uint nb = S->nblocks, start = mt->bnext < nb ? mt->bnext : 0, n = 0;
    while (n < nb) {
        uint blk = (start + n) % nb, m = blk / (BSIZE * 8), i = blk % (BSIZE * 8);
        struct buf *b = bread(dev, S->bmap_start + m);
        if (b->flags & B_ERROR) {       /* (the bitmap unreadable: a drive gone) */
            brelse(b);
            fs_ioerr = 1;
            return 0;
        }
        for (; i < BSIZE * 8 && m * BSIZE * 8 + i < nb && n < nb; i++, n++) {
            if ((i & 7) == 0 && b->data[i / 8] == 0xFF && n + 8 <= nb) { i += 7; n += 7; continue; }
            if (b->data[i / 8] & (1 << (i % 8))) continue;
            b->data[i / 8] |= 1 << (i % 8);
            bdwrite(b);
            S->free_blocks--;
            blk = m * BSIZE * 8 + i;
            mt->bnext = blk + 1;
            b = bnoread(dev, blk);              /* (its old contents do not matter) */
            memset(b->data, 0, BSIZE);
            bdwrite(b);
            return blk;
        }
        brelse(b);
    }
    kprintf("filesystem full\n");
    return 0;
}

static void bfree(int dev, uint bn)
{
    struct superblock *S = sbof(dev);
    struct buf *b = bread(dev, S->bmap_start + bn / (BSIZE * 8));
    uint i = bn % (BSIZE * 8);
    if (!(b->data[i / 8] & (1 << (i % 8)))) panic("freeing a free block");
    b->data[i / 8] &= ~(1 << (i % 8));
    bdwrite(b);
    S->free_blocks++;
}

/* ---------------------------------------------------------------- inodes */
struct inode *iget(int dev, uint inum)
{
    struct inode *ip, *empty = NULL;
    for (ip = itab; ip < itab + NINODE; ip++) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            return ip;
        }
        if (!empty && ip->ref == 0) empty = ip;
    }
    if (!empty) panic("iget: no inodes");
    empty->dev = dev;
    empty->inum = inum;
    empty->ref = 1;
    empty->flags = 0;
    return empty;
}

struct inode *idup(struct inode *ip) { ip->ref++; return ip; }

void ilock(struct inode *ip)
{
    while (ip->flags & I_LOCKED) {
        ip->flags |= I_WANTED;
        sleep(ip);
    }
    ip->flags |= I_LOCKED;
    if (!(ip->flags & I_VALID)) {
        struct buf *b = bread(ip->dev, sbof(ip->dev)->inode_start + ip->inum / IPB);
        memcpy(&ip->d, b->data + (ip->inum % IPB) * 64, 64);
        brelse(b);
        ip->flags |= I_VALID;
        if (ip->d.mode == 0) panic("ilock: free inode");
    }
}

void iunlock(struct inode *ip)
{
    if (ip->flags & I_WANTED) wakeup(ip);
    ip->flags &= ~(I_LOCKED | I_WANTED);
}

void iupdate(struct inode *ip)
{
    struct buf *b = bread(ip->dev, sbof(ip->dev)->inode_start + ip->inum / IPB);
    memcpy(b->data + (ip->inum % IPB) * 64, &ip->d, 64);
    bdwrite(b);
}

void iput(struct inode *ip)
{
    if (ip->ref == 1 && (ip->flags & I_VALID) && ip->d.nlink == 0) {
        ilock(ip);
        itrunc(ip);
        ip->d.mode = 0;
        iupdate(ip);
        sbof(ip->dev)->free_inodes++;
        ip->flags = 0;
    }
    ip->ref--;
}

void iunlockput(struct inode *ip) { iunlock(ip); iput(ip); }

struct inode *ialloc(int dev, ushort mode)
{
    uint inum;
    struct superblock *S = sbof(dev);
    for (inum = ROOTINO; inum < S->ninodes; inum++) {
        struct buf *b = bread(dev, S->inode_start + inum / IPB);
        struct dinode *d = (struct dinode *)(b->data + (inum % IPB) * 64);
        if (d->mode == 0) {
            memset(d, 0, 64);
            d->mode = mode;
            d->mtime = d->ctime = fs_time();
            bdwrite(b);
            S->free_inodes--;
            return iget(dev, inum);
        }
        brelse(b);
    }
    return NULL;
}

/* the disk block of the file's block bn; 0 for a hole (or when full) */
static uint ind(int dev, uint blk, uint i, int alloc)
{
    struct buf *b = bread(dev, blk);
    uint v = ((uint *)b->data)[i];
    brelse(b);                          /* (balloc needs buffers of its own) */
    if (!v && alloc && (v = balloc(dev)) != 0) {
        b = bread(dev, blk);
        ((uint *)b->data)[i] = v;
        bdwrite(b);
    }
    return v;
}

static uint bmap(struct inode *ip, uint bn, int alloc)
{
    uint *a = ip->d.addr, mid;
    if (bn < NDIRECT) {
        if (!a[bn] && alloc) a[bn] = balloc(ip->dev);
        return a[bn];
    }
    bn -= NDIRECT;
    if (bn < NINDIRECT) {
        if (!a[NDIRECT] && (!alloc || !(a[NDIRECT] = balloc(ip->dev)))) return 0;
        return ind(ip->dev, a[NDIRECT], bn, alloc);
    }
    bn -= NINDIRECT;
    if (bn < NINDIRECT * NINDIRECT) {
        if (!a[NDIRECT + 1] && (!alloc || !(a[NDIRECT + 1] = balloc(ip->dev)))) return 0;
        if (!(mid = ind(ip->dev, a[NDIRECT + 1], bn / NINDIRECT, alloc))) return 0;
        return ind(ip->dev, mid, bn % NINDIRECT, alloc);
    }
    bn -= NINDIRECT * NINDIRECT;        /* triple: up to 16 GiB (the 32-bit size stops it first) */
    if (!a[NDIRECT + 2] && (!alloc || !(a[NDIRECT + 2] = balloc(ip->dev)))) return 0;
    if (!(mid = ind(ip->dev, a[NDIRECT + 2], bn / (NINDIRECT * NINDIRECT), alloc))) return 0;
    if (!(mid = ind(ip->dev, mid, bn / NINDIRECT % NINDIRECT, alloc))) return 0;
    return ind(ip->dev, mid, bn % NINDIRECT, alloc);
}

static void free_ind(int dev, uint blk, int depth)
{
    struct buf *b = bread(dev, blk);
    uint v[NINDIRECT], i;
    memcpy(v, b->data, BSIZE);
    brelse(b);
    for (i = 0; i < NINDIRECT; i++)
        if (v[i]) {
            if (depth > 1) free_ind(dev, v[i], depth - 1);
            else bfree(dev, v[i]);
        }
    bfree(dev, blk);
}

void itrunc(struct inode *ip)
{
    int i;
    ushort t = ip->d.mode & IFMT;
    if (t != IFREG && t != IFDIR) return;       /* devices keep their number in addr[0] */
    for (i = 0; i < NDIRECT; i++)
        if (ip->d.addr[i]) { bfree(ip->dev, ip->d.addr[i]); ip->d.addr[i] = 0; }
    if (ip->d.addr[NDIRECT]) { free_ind(ip->dev, ip->d.addr[NDIRECT], 1); ip->d.addr[NDIRECT] = 0; }
    if (ip->d.addr[NDIRECT + 1]) { free_ind(ip->dev, ip->d.addr[NDIRECT + 1], 2); ip->d.addr[NDIRECT + 1] = 0; }
    if (ip->d.addr[NDIRECT + 2]) { free_ind(ip->dev, ip->d.addr[NDIRECT + 2], 3); ip->d.addr[NDIRECT + 2] = 0; }
    ip->d.size = 0;
    ip->d.mtime = fs_time();
    iupdate(ip);
}

/* read / write the locked inode; dst / src a user address if user, else a
   kernel one. The bytes moved, or -errno. */
int readi(struct inode *ip, uint dst, int user, uint off, uint n)
{
    uint tot, m;
    if (off >= ip->d.size) return 0;
    if (off + n > ip->d.size || off + n < off) n = ip->d.size - off;
    for (tot = 0; tot < n; tot += m, off += m, dst += m) {
        uint bn = bmap(ip, off / BSIZE, 0);
        struct buf *b;
        m = BSIZE - off % BSIZE;
        if (m > n - tot) m = n - tot;
        if (!bn) {                      /* a hole reads as zeros */
            static uchar zeros[BSIZE];
            if (user) { if (copyout(dst, zeros, m) < 0) return -EFAULT; }
            else memset((void *)dst, 0, m);
            continue;
        }
        b = bread(ip->dev, bn);
        if (b->flags & B_ERROR) { brelse(b); return tot ? (int)tot : -EIO; }     /* (a drive gone) */
        if (user) {
            if (copyout(dst, b->data + off % BSIZE, m) < 0) { brelse(b); return -EFAULT; }
        } else
            memcpy((void *)dst, b->data + off % BSIZE, m);
        brelse(b);
    }
    return n;
}

/* n bytes from src (0: zeros) to user address va of page table pt, a page
   at a time (the pages are mapped) */
static int topages(uint *pt, uint va, const uchar *src, uint n)
{
    while (n) {
        uint pa = va2pa(pt, va, 0), c = PGSIZE - (va & (PGSIZE - 1));
        if (!pa) return -EFAULT;
        if (c > n) c = n;
        if (src) { memcpy((void *)pa, src, c); src += c; }
        else memset((void *)pa, 0, c);
        va += c;
        n -= c;
    }
    return 0;
}

/* readi_pages' bmap, for a long file: the indirect block that holds the
   number of file block fb (its "leaf") is kept in *leaf while fb walks
   through its 256 entries, rather than read again for every block. *base is
   the first file block it covers. 0 for a hole. */
static uint bmap_walk(struct inode *ip, uint fb, struct buf **leaf, uint *base)
{
    uint *a = ip->d.addr, bn, lb, first;
    if (fb < NDIRECT) return a[fb];
    if (*leaf && fb >= *base && fb < *base + NINDIRECT)
        return ((uint *)(*leaf)->data)[fb - *base];
    bn = fb - NDIRECT;
    if (bn < NINDIRECT) {
        lb = a[NDIRECT];
        first = NDIRECT;
    } else if ((bn -= NINDIRECT) < NINDIRECT * NINDIRECT) {
        lb = a[NDIRECT + 1] ? ind(ip->dev, a[NDIRECT + 1], bn / NINDIRECT, 0) : 0;
        first = NDIRECT + NINDIRECT + bn / NINDIRECT * NINDIRECT;
    } else
        return bmap(ip, fb, 0);         /* (triple indirect: past 64 MiB) */
    if (*leaf) { brelse(*leaf); *leaf = 0; }
    if (!lb) return 0;
    *leaf = bread(ip->dev, lb);
    *base = first;
    return ((uint *)(*leaf)->data)[fb - first];
}

static int in_sorted(const uint *v, int n, uint x)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (v[mid] < x) lo = mid + 1;
        else hi = mid;
    }
    return lo < n && v[lo] == x;
}

/* exec's load: n bytes of the file from off into user address va of page
   table pt (its pages mapped, not necessarily zeroed); the inode locked.
   Runs of consecutive blocks that are not in the buffer cache go straight
   from the device into the pages, in one transfer (bdevsw readv); a block
   in the cache comes from there (it may be newer than the disk), and a hole
   is zeros. n, or an error. */
int readi_pages(struct inode *ip, uint *pt, uint va, uint off, uint n)
{
    struct bdevsw *d = &bdevsw[major(ip->dev)];
    struct buf *leaf = 0;
    uint done = 0, base = 0, cached[NBUF];
    int ncached, err = 0;
    if (off >= ip->d.size) return 0;
    if (off + n > ip->d.size || off + n < off) n = ip->d.size - off;
    ncached = cached_blocks(ip->dev, cached, NBUF);    /* (a snapshot holds: a locked file's
                                           blocks are written only under its lock, and reading
                                           them in adds only clean copies) */
    while (done < n) {
        uint fb = (off + done) / BSIZE, boff = (off + done) % BSIZE;
        uint bn = bmap_walk(ip, fb, &leaf, &base), m = BSIZE - boff, nb = 1;
        struct seg sg[20];
        int ns = 0;
        if (m > n - done) m = n - done;
        if (!bn || !d->readv || in_sorted(cached, ncached, bn)) {
            struct buf *b = bn ? bread(ip->dev, bn) : 0;
            err = topages(pt, va + done, b ? b->data + boff : 0, m);
            if (b) brelse(b);
            if (err < 0) break;
            done += m;
            continue;
        }
        while (done + m < n && nb < 64) {       /* the run: up to 64 blocks (17 pages) */
            uint next = bmap_walk(ip, fb + nb, &leaf, &base), mm = n - done - m;
            if (next != bn + nb || in_sorted(cached, ncached, next)) break;
            m += mm < BSIZE ? mm : BSIZE;
            nb++;
        }
        {   /* its pages */
            uint v = va + done, left = m;
            while (left) {
                uint pa = va2pa(pt, v, 0), c = PGSIZE - (v & (PGSIZE - 1));
                if (!pa) { err = -EFAULT; break; }
                if (c > left) c = left;
                sg[ns].addr = pa;
                sg[ns++].len = c;
                v += c;
                left -= c;
            }
        }
        if (err < 0 || (err = d->readv(minor(ip->dev), bn, nb, boff, sg, ns)) < 0) break;
        done += m;
    }
    if (leaf) brelse(leaf);
    return err < 0 ? err : (int)n;
}

int writei(struct inode *ip, uint src, int user, uint off, uint n)
{
    uint tot, m;
    if (off + n < off || off + n > 0x7FFFFFFFu) return -EFBIG;     /* (offsets are signed 32-bit) */
    fs_ioerr = 0;
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        uint bn = bmap(ip, off / BSIZE, 1);
        struct buf *b;
        if (!bn) { if (tot) break; return fs_ioerr ? -EIO : -ENOSPC; }
        m = BSIZE - off % BSIZE;
        if (m > n - tot) m = n - tot;
        b = m == BSIZE ? bnoread(ip->dev, bn) : bread(ip->dev, bn);    /* (a whole block: no read) */
        if (b->flags & B_ERROR) { brelse(b); if (tot) break; return -EIO; }
        if (user) {
            if (copyin(b->data + off % BSIZE, src, m) < 0) {
                if (m == BSIZE) b->flags &= ~B_VALID;
                brelse(b);
                return tot ? (int)tot : -EFAULT;
            }
        } else
            memcpy(b->data + off % BSIZE, (void *)src, m);
        bdwrite(b);
    }
    if (off > ip->d.size) ip->d.size = off;
    ip->d.mtime = fs_time();
    iupdate(ip);
    return tot;
}

/* ---------------------------------------------------------------- directories */
static int namecmp(const char *a, const char *b) { return strncmp(a, b, DIRSIZ); }

/* a block is read once and scanned in the buffer, not read through for every
   entry: finding a name is what every open and exec does (D87) */
struct inode *dirlookup(struct inode *dp, const char *name, uint *poff)
{
    uint off = 0;
    while (off < dp->d.size) {
        uint bn = bmap(dp, off / BSIZE, 0), end = (off / BSIZE + 1) * BSIZE;
        struct buf *b;
        if (end > dp->d.size) end = dp->d.size;
        if (!bn) { off = end; continue; }       /* a hole reads as zeros: no entries */
        b = bread(dp->dev, bn);
        if (b->flags & B_ERROR) panic("dirlookup");
        for (; off < end; off += sizeof(struct dirent)) {
            struct dirent *de = (struct dirent *)(b->data + off % BSIZE);
            if (de->ino && namecmp(name, de->name) == 0) {
                uint ino = de->ino;
                brelse(b);
                if (poff) *poff = off;
                return iget(dp->dev, ino);
            }
        }
        brelse(b);
    }
    return NULL;
}

int dirlink(struct inode *dp, const char *name, uint inum)
{
    uint off;
    struct dirent de;
    struct inode *ip;
    if ((ip = dirlookup(dp, name, 0)) != NULL) { iput(ip); return -EEXIST; }
    for (off = 0; off < dp->d.size; off += sizeof de) {
        readi(dp, (uint)&de, 0, off, sizeof de);
        if (de.ino == 0) break;
    }
    memset(&de, 0, sizeof de);
    de.ino = inum;
    { int k; for (k = 0; k < DIRSIZ && name[k]; k++) de.name[k] = name[k]; }
    return writei(dp, (uint)&de, 0, off, sizeof de) == sizeof de ? 0 : -ENOSPC;
}

int isdirempty(struct inode *dp)
{
    uint off;
    struct dirent de;
    for (off = 2 * sizeof de; off < dp->d.size; off += sizeof de) {
        readi(dp, (uint)&de, 0, off, sizeof de);
        if (de.ino) return 0;
    }
    return 1;
}

/* may the current process read (4) / write (2) / search or execute (1) it? */
int access_ok(struct inode *ip, int mode)
{
    struct proc *p = curproc;
    uint m = ip->d.mode;
    if (!p || p->euid == 0) {
        if (mode == 1 && (m & IFMT) == IFREG) return (m & 0111) != 0;
        return 1;
    }
    if (p->euid == ip->d.uid) m >>= 6;
    else if (p->egid == ip->d.gid) m >>= 3;
    return (m & mode) == mode;
}

/* the next path element into name; the rest of the path, or NULL at the end */
static const char *skipelem(const char *path, char *name, int *toolong)
{
    int len = 0;
    while (*path == '/') path++;
    if (!*path) return NULL;
    while (*path && *path != '/') {
        if (len < DIRSIZ) name[len] = *path;
        else *toolong = 1;
        len++;
        path++;
    }
    name[len < DIRSIZ ? len : DIRSIZ] = 0;
    while (*path == '/') path++;
    return path;
}

static struct inode *namex(const char *path, int parent, char *name, int *err)
{
    struct inode *ip, *next;
    char tmp[DIRSIZ + 1];
    int toolong = 0;
    if (!name) name = tmp;
    if (err) *err = 0;
    ip = (*path == '/' || !curproc || !curproc->cwd) ? iget(ROOTDEV, ROOTINO) : idup(curproc->cwd);
    while ((path = skipelem(path, name, &toolong)) != NULL) {
        ilock(ip);
        if ((ip->d.mode & IFMT) != IFDIR) { iunlockput(ip); if (err) *err = -ENOTDIR; return NULL; }
        if (!access_ok(ip, 1)) { iunlockput(ip); if (err) *err = -EACCES; return NULL; }
        if (toolong) { iunlockput(ip); if (err) *err = -ENAMETOOLONG; return NULL; }
        if (parent && *path == 0) { iunlock(ip); return ip; }
        if (name[0] == '.' && name[1] == '.' && !name[2] && ip->inum == ROOTINO && ip->dev != ROOTDEV) {
            struct mount *m = mountof(ip->dev);  /* .. from a mounted root: from where it is mounted */
            iunlockput(ip);
            ip = idup(m->on);
            ilock(ip);
        }
        if (!(next = dirlookup(ip, name, 0))) { iunlockput(ip); if (err) *err = -ENOENT; return NULL; }
        iunlockput(ip);
        {
            int k;                      /* a directory something is mounted on: that filesystem's root */
            for (k = 1; k < NMOUNT; k++)
                if (mounts[k].used && mounts[k].on->dev == next->dev && mounts[k].on->inum == next->inum) {
                    iput(next);
                    next = idup(mounts[k].root);
                    break;
                }
        }
        ip = next;
    }
    if (parent) { iput(ip); if (err) *err = -ENOENT; return NULL; }
    return ip;
}

struct inode *namei(const char *path, int *err) { return namex(path, 0, NULL, err); }
struct inode *nameiparent(const char *path, char *name, int *err) { return namex(path, 1, name, err); }

void stati(struct inode *ip, struct stat *st)
{
    st->st_dev = ip->dev;
    st->st_ino = ip->inum;
    st->st_mode = ip->d.mode;
    st->st_nlink = ip->d.nlink;
    st->st_uid = ip->d.uid;
    st->st_gid = ip->d.gid;
    st->st_rdev = (ip->d.mode & IFMT) == IFCHR || (ip->d.mode & IFMT) == IFBLK ? ip->d.addr[0] : 0;     /* (a file's addr[0] is a block number) */
    st->st_size = ip->d.size;
    st->st_atime = st->st_mtime = ip->d.mtime;
    st->st_ctime = ip->d.ctime;
}

/* is a filesystem mounted on this directory? (rmdir must not remove it) */
int mounted_on(struct inode *ip)
{
    int k;
    for (k = 0; k < NMOUNT; k++) if (mounts[k].used && mounts[k].on == ip) return 1;
    return 0;
}

/* the superblock of what is mounted from dev, or NULL */
struct superblock *mounted_super(int dev)
{
    int k;
    for (k = 0; k < NMOUNT; k++) if (mounts[k].used && mounts[k].dev == dev) return &mounts[k].super;
    return 0;
}

/* was dev's filesystem marked clean when it was mounted? (ustat, for fsck -c) */
int mounted_wasclean(int dev)
{
    int k;
    for (k = 0; k < NMOUNT; k++) if (mounts[k].used && mounts[k].dev == dev) return mounts[k].wasclean;
    return 0;
}
