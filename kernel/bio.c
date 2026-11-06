/* bio.c -- the buffer cache (M3, D22; V6's bio): NBUF one-block buffers,
 * least recently used reused first. A buffer is BUSY while a process holds
 * it (others sleep on it); DIRTY ones are written when reused, by bflush()
 * (sync, every 30 s) or at once by bwrite(). */
#include "defs.h"

static struct buf bufs[NBUF];
static struct buf head;         /* LRU ring: head.next most recent */
static int headwait;            /* processes asleep waiting for any buffer */

void binit(void)
{
    struct buf *b;
    head.next = head.prev = &head;
    for (b = bufs; b < bufs + NBUF; b++) {
        b->next = head.next;
        b->prev = &head;
        head.next->prev = b;
        head.next = b;
        b->dev = -1;
    }
}

/* one transfer through the block device switch */
static void disk(struct buf *b, int write)
{
    int maj = major(b->dev);
    b->flags &= ~(B_WRITE | B_ERROR);
    if (write) b->flags |= B_WRITE;
    if (maj >= NBDEV) panic("disk: no such block device");
    bdevsw[maj].strategy(b);
    b->flags &= ~B_WRITE;
    if (b->flags & B_ERROR) {
        static int quiet = -1;          /* a device already reported: no more until it works again */
        if (b->dev != quiet) kprintf("disk error: device %d/%d block %u\n", maj, minor(b->dev), b->blkno);
        quiet = b->dev;
        if (!write) memset(b->data, 0, BSIZE);
    }
}

static struct buf *bget(int dev, uint blkno)
{
    struct buf *b;
    int s;
again:
    s = splhi();
    for (b = head.next; b != &head; b = b->next)
        if (b->dev == dev && b->blkno == blkno) {
            if (b->flags & B_BUSY) {
                b->flags |= B_WANTED;
                splx(s);
                sleep(b);
                goto again;
            }
            b->flags |= B_BUSY;
            splx(s);
            return b;
        }
    for (b = head.prev; b != &head; b = b->prev)       /* least recently used */
        if (!(b->flags & B_BUSY)) {
            b->flags |= B_BUSY;
            splx(s);
            if (b->flags & B_DIRTY) {
                disk(b, 1);
                b->flags &= ~B_DIRTY;
            }
            b->dev = dev;
            b->blkno = blkno;
            b->flags &= ~B_VALID;
            return b;
        }
    headwait++;
    splx(s);
    sleep(&head);                       /* every buffer busy: wait for one */
    goto again;
}

/* a removable device gone (usb.c): its blocks forgotten, written or not
   (a buffer in use is left to finish, and fails) */
void bdrop(int dev)
{
    struct buf *b;
    int s = splhi();
    for (b = head.next; b != &head; b = b->next)
        if (b->dev == dev && !(b->flags & B_BUSY)) {
            b->dev = -1;
            b->flags &= ~(B_VALID | B_DIRTY);
        }
    splx(s);
}

/* the block numbers of dev that are in buffers (valid, or being filled or
   written), sorted, into out (at most max); how many */
int cached_blocks(int dev, uint *out, int max)
{
    struct buf *b;
    int s = splhi(), n = 0, i;
    for (b = head.next; b != &head && n < max; b = b->next)
        if (b->dev == dev && (b->flags & (B_VALID | B_BUSY))) {
            uint x = b->blkno;
            for (i = n; i > 0 && out[i - 1] > x; i--) out[i] = out[i - 1];
            out[i] = x;
            n++;
        }
    splx(s);
    return n;
}

struct buf *bread(int dev, uint blkno)
{
    struct buf *b = bget(dev, blkno);
    if (!(b->flags & B_VALID)) {
        disk(b, 0);
        if (!(b->flags & B_ERROR))      /* (a failed read is zeros, with B_ERROR for the caller to see; */
            b->flags |= B_VALID;        /* not kept: the next read tries the device again) */
    }
    return b;
}

/* a block about to be overwritten whole: its buffer, without reading the
   disk (the caller fills every byte; a failed fill clears B_VALID again) */
struct buf *bnoread(int dev, uint blkno)
{
    struct buf *b = bget(dev, blkno);
    b->flags = (b->flags | B_VALID) & ~B_ERROR;     /* (no transfer to clear an old failure) */
    return b;
}

void bwrite(struct buf *b)
{
    disk(b, 1);
    b->flags &= ~B_DIRTY;
    brelse(b);
}

void bdwrite(struct buf *b)
{
    b->flags |= B_DIRTY | B_VALID;
    brelse(b);
}

void brelse(struct buf *b)
{
    int s = splhi();
    b->next->prev = b->prev;            /* to the front: most recently used */
    b->prev->next = b->next;
    b->next = head.next;
    b->prev = &head;
    head.next->prev = b;
    head.next = b;
    if (b->flags & B_WANTED) wakeup(b);
    b->flags &= ~(B_BUSY | B_WANTED);
    if (headwait) {                     /* (a wakeup scans every process: only when needed) */
        headwait = 0;
        wakeup(&head);
    }
    splx(s);
}

/* forget a device's blocks (after umount: they are written out already) */
void binval(int dev)
{
    struct buf *b;
    for (b = bufs; b < bufs + NBUF; b++)
        if (b->dev == dev && !(b->flags & (B_BUSY | B_DIRTY))) b->flags &= ~B_VALID;
}

void bflush(void)
{
    struct buf *b;
    for (b = bufs; b < bufs + NBUF; b++) {
        int s = splhi();
        if ((b->flags & (B_DIRTY | B_BUSY)) == B_DIRTY) {
            b->flags |= B_BUSY;
            splx(s);
            disk(b, 1);
            b->flags &= ~B_DIRTY;
            brelse(b);
        } else
            splx(s);
    }
}
