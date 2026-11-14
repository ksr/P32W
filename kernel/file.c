/* file.c -- open files, pipes, the character-device switch (M3, D22). */
#include "defs.h"
#include <fcntl.h>

static struct file ftab[NFILE];

static int memread(int minor, uint dst, int n, uint off);
static int memwrite(int minor, uint src, int n, uint off);
static int rdopen(int minor, int flags) { return curproc->euid ? -EACCES : cf_open(minor, flags); }
static int rdread(int minor, uint dst, int n, uint off);
static int rdwrite(int minor, uint src, int n, uint off);
static int nulldev(int minor) { return 0; }
static int nullopen(int minor, int flags) { return minor == 1 && curproc->euid ? -EACCES : 0; }

struct bdevsw bdevsw[NBDEV] = {
    { cf_open, cf_close, cf_strategy, cf_readv },               /* 0: the disk card's drives (CF, IDE) */
    { ram_open, ram_close, ram_strategy, 0 },                   /* 1: the RAM disk */
    { usb_open, usb_close, usb_strategy, 0 },                   /* 2: a USB drive (D43) */
};

struct cdevsw cdevsw[NCDEV] = {
    { 0, 0, 0, 0, 0 },
    { ttyopen, ttyclose, ttyread, ttywrite, ttyioctl },         /* 1: /dev/tty0, tty1 */
    { nullopen, nulldev, memread, memwrite, 0 },                /* 2: null, mem, port */
    { rdopen, nulldev, rdread, rdwrite, 0 },                    /* 3: rdisk */
    { cttyopen, cttyclose, cttyread, cttywrite, cttyioctl },    /* 4: /dev/tty, the controlling terminal */
    { usb_rawopen, nulldev, usb_rawread, usb_rawwrite, usb_rawioctl },  /* 5: /dev/rusb, the USB drive's sectors */
    { ptyopen, ptyclose, ptyread, ptywrite, 0 },                /* 6: /dev/ptyp0-3, pty masters (D46) */
    { vgopen, vgclose, vgread, vgwrite, vgioctl },              /* 7: /dev/video, the video card's chip (D110) */
    { msopen, msclose, msread, 0, 0 },                          /* 8: /dev/mouse, its PS/2 mouse (D120) */
};

void fileinit(void) { memset(ftab, 0, sizeof ftab); }

struct file *filealloc(void)
{
    struct file *f;
    for (f = ftab; f < ftab + NFILE; f++)
        if (f->ref == 0) {
            memset(f, 0, sizeof *f);
            f->ref = 1;
            return f;
        }
    return NULL;
}

struct file *filedup(struct file *f) { f->ref++; return f; }

static int isdev(struct inode *ip) { return (ip->d.mode & IFMT) == IFCHR; }
static int isblk(struct inode *ip) { return (ip->d.mode & IFMT) == IFBLK; }

/* opening a device special file: its driver's open */
int devopen(struct inode *ip, int flags)
{
    uint dev = ip->d.addr[0];
    if (isdev(ip)) {
        if (major(dev) >= NCDEV || !cdevsw[major(dev)].open) return -ENXIO;
        return cdevsw[major(dev)].open(minor(dev), flags);
    }
    if (isblk(ip)) {
        if (major(dev) >= NBDEV) return -ENXIO;
        return bdevsw[major(dev)].open(minor(dev), flags);
    }
    return 0;
}

/* a block special file, byte by byte through the buffer cache (so a reader
   sees exactly what the filesystem code sees) */
static int blkrw(uint dev, uint ua, int n, uint off, int write)
{
    int k = 0;
    while (k < n) {
        uint bn = (off + k) / BSIZE, o = (off + k) % BSIZE, c = BSIZE - o;
        struct buf *b;
        if (bn >= sb.nblocks && dev == ROOTDEV) break;          /* the end of the device */
        if (c > (uint)(n - k)) c = n - k;
        b = bread(dev, bn);
        if (b->flags & B_ERROR) { brelse(b); return k ? k : -EIO; }
        if (write) {
            if (copyin(b->data + o, ua + k, c) < 0) { brelse(b); return -EFAULT; }
            bwrite(b);
        } else {
            if (copyout(ua + k, b->data + o, c) < 0) { brelse(b); return -EFAULT; }
            brelse(b);
        }
        k += c;
    }
    return k;
}

void fileclose(struct file *f)
{
    if (--f->ref > 0) return;
    if (f->type == FD_PIPE) {
        struct pipe *p = f->pipe;
        if ((f->flags & O_ACCMODE) == O_RDONLY) p->readopen = 0;
        else p->writeopen = 0;
        wakeup(p);
        if (!p->readopen && !p->writeopen) frame_free((uint)p);
    } else if (f->type == FD_SOCKET) {
        sock_close(f->sock);
    } else if (f->type == FD_INODE) {
        struct inode *ip = f->ip;
        if (isdev(ip)) {
            int maj = major(ip->d.addr[0]);
            if (maj < NCDEV && cdevsw[maj].close) cdevsw[maj].close(minor(ip->d.addr[0]));
        } else if (isblk(ip) && major(ip->d.addr[0]) < NBDEV) {
            bflush();                   /* (a block device's writes go out on its last close) */
            bdevsw[major(ip->d.addr[0])].close(minor(ip->d.addr[0]));
        }
        iput(ip);
    }
    f->type = FD_NONE;
}

/* a file of the inode kind with input waiting (select): a terminal with a
   line (or a character, in CBREAK and RAW), the mouse with a packet;
   anything else always */
int file_inready(struct file *f)
{
    uint dev;
    if (!isdev(f->ip)) return 1;
    dev = f->ip->d.addr[0];
    if (major(dev) == DEV_TTY) return tty_ready(minor(dev));
    if (major(dev) == DEV_PTY) return pty_ready(minor(dev));
    if (major(dev) == 4) return curproc->tty < 0 || tty_ready(curproc->tty);   /* /dev/tty */
    if (major(dev) == 8) return ms_ready();
    return 1;
}

/* ---------------------------------------------------------------- pipes */
int pipealloc(struct file **r, struct file **w)
{
    struct pipe *p = (struct pipe *)frame_alloc();
    if (!p) return -ENOMEM;
    if (!(*r = filealloc())) { frame_free((uint)p); return -ENFILE; }
    if (!(*w = filealloc())) { fileclose(*r); frame_free((uint)p); return -ENFILE; }
    p->readopen = p->writeopen = 1;
    (*r)->type = (*w)->type = FD_PIPE;
    (*r)->pipe = (*w)->pipe = p;
    (*r)->flags = O_RDONLY;
    (*w)->flags = O_WRONLY;
    return 0;
}

static int piperead(struct pipe *p, uint dst, int n)
{
    int k;
    while (p->nread == p->nwrite && p->writeopen) {
        if (curproc->killed) return -EINTR;
        sleep(p);
    }
    for (k = 0; k < n && p->nread != p->nwrite; k++) {
        uchar c = p->data[p->nread++ % PIPESIZE];
        if (copyout(dst + k, &c, 1) < 0) return k ? k : -EFAULT;
    }
    wakeup(p);
    return k;
}

static int pipewrite(struct pipe *p, uint src, int n)
{
    int k;
    for (k = 0; k < n; k++) {
        uchar c;
        while (p->nwrite == p->nread + PIPESIZE) {
            if (!p->readopen) { psignal(curproc, SIGPIPE); return -EPIPE; }
            if (curproc->killed) return k ? k : -EINTR;
            wakeup(p);
            sleep(p);
        }
        if (!p->readopen) { psignal(curproc, SIGPIPE); return -EPIPE; }
        if (copyin(&c, src + k, 1) < 0) return k ? k : -EFAULT;
        p->data[p->nwrite++ % PIPESIZE] = c;
    }
    wakeup(p);
    return n;
}

/* ---------------------------------------------------------------- read / write */
int fileread(struct file *f, uint dst, int n)
{
    int r;
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -EBADF;
    if (f->type == FD_PIPE) return piperead(f->pipe, dst, n);
    if (f->type == FD_SOCKET) return sock_read(f, dst, n);
    if (isdev(f->ip)) {
        uint dev = f->ip->d.addr[0];
        if (major(dev) >= NCDEV || !cdevsw[major(dev)].read) return -ENODEV;
        r = cdevsw[major(dev)].read(minor(dev), dst, n, f->off);
        if (r > 0) f->off += r;
        return r;
    }
    if (isblk(f->ip)) {
        r = blkrw(f->ip->d.addr[0], dst, n, f->off, 0);
        if (r > 0) f->off += r;
        return r;
    }
    ilock(f->ip);
    r = readi(f->ip, dst, 1, f->off, n);
    if (r > 0) f->off += r;
    iunlock(f->ip);
    return r;
}

int filewrite(struct file *f, uint src, int n)
{
    int r;
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -EBADF;
    if (f->type == FD_PIPE) return pipewrite(f->pipe, src, n);
    if (f->type == FD_SOCKET) return sock_write(f, src, n);
    if (isdev(f->ip)) {
        uint dev = f->ip->d.addr[0];
        if (major(dev) >= NCDEV || !cdevsw[major(dev)].write) return -ENODEV;
        r = cdevsw[major(dev)].write(minor(dev), src, n, f->off);
        if (r > 0) f->off += r;
        return r;
    }
    if (isblk(f->ip)) {
        r = blkrw(f->ip->d.addr[0], src, n, f->off, 1);
        if (r > 0) f->off += r;
        return r;
    }
    ilock(f->ip);
    if (f->flags & O_APPEND) f->off = f->ip->d.size;
    r = writei(f->ip, src, 1, f->off, n);
    if (r > 0) f->off += r;
    iunlock(f->ip);
    return r;
}

/* ---------------------------------------------------------------- /dev/null, /dev/mem, /dev/port */
#define ROMSIZE_DEVMEM 0x00100000    /* (the boot ROM's 1 MiB at 0) */

/* /dev/mem: the offset is a physical address -- the ROM (read only) and
   the RAM; elsewhere (the I/O page included) reads end, writes fail */
static int physmem(uint user, int n, uint off, int write)
{
    int done = 0;
    while (done < n) {
        uint a = off + done, lim, len;
        if (a < ROMSIZE_DEVMEM && !write) lim = ROMSIZE_DEVMEM;
        else if (ram_fitted(a)) lim = (a | (2u << 20) - 1) + 1;    /* (to the end of its 2 MiB bank, D60) */
        else break;
        len = lim - a;
        if (len > (uint)(n - done)) len = n - done;
        if (write ? copyin((void *)a, user + done, len) < 0 : copyout(user + done, (void *)a, len) < 0) return -EFAULT;
        done += len;
    }
    if (!done && n) return write ? -EIO : 0;
    return done;
}
static int memread(int minor, uint dst, int n, uint off)
{
    if (minor == 0) return 0;
    if (minor == 2) {                   /* the input port, once per byte asked for */
        int k;
        for (k = 0; k < n; k++) {
            uchar c = IOB(PORTIN);
            if (copyout(dst + k, &c, 1) < 0) return -EFAULT;
        }
        return n;
    }
    if (minor == 1) return physmem(dst, n, off, 0);
    return -ENXIO;
}

static int memwrite(int minor, uint src, int n, uint off)
{
    if (minor == 0) return n;
    if (minor == 2) {                   /* the output port: the last byte stays */
        int k;
        for (k = 0; k < n; k++) {
            uchar c;
            if (copyin(&c, src + k, 1) < 0) return -EFAULT;
            IOB(PORTOUT) = c;
        }
        return n;
    }
    if (minor == 1) return physmem(src, n, off, 1);
    return -ENXIO;
}

/* ---------------------------------------------------------------- /dev/rdisk */
/* a drive (minor: as the block device's, but 0 is the whole boot card)
   byte by byte, through a sector buffer; writes bypass the buffer
   cache (fsck checks a quiet, synced filesystem) */
static uint secbuf_[128];             /* (halfword-aligned: the CF moves 16 bits at a time) */
#define secbuf ((uchar *)secbuf_)

static int rdread(int minor, uint dst, int n, uint off)
{
    int k = 0;
    while (k < n) {
        uint sec = (off + k) / 512, o = (off + k) % 512, c = 512 - o;
        if (c > (uint)(n - k)) c = n - k;
        if (ata_rw(minor, sec, secbuf, 1, 0) < 0) return k ? k : -EIO;
        if (copyout(dst + k, secbuf + o, c) < 0) return -EFAULT;
        k += c;
    }
    return n;
}

static int rdwrite(int minor, uint src, int n, uint off)
{
    int k = 0;
    while (k < n) {
        uint sec = (off + k) / 512, o = (off + k) % 512, c = 512 - o;
        if (c > (uint)(n - k)) c = n - k;
        if (c < 512 && ata_rw(minor, sec, secbuf, 1, 0) < 0) return k ? k : -EIO;
        if (copyin(secbuf + o, src + k, c) < 0) return -EFAULT;
        if (ata_rw(minor, sec, secbuf, 1, 1) < 0) return k ? k : -EIO;
        k += c;
    }
    return n;
}
