/* cf.c -- the disk card's drives, True-IDE / ATA PIO (M3, D22, D50, D50).
 * Port p's task file is at FFFF_EF00 + 16 p (the disk card, window 6:
 * D52): port 0 the boot CompactFlash card, 1 the second CF socket, 2 and 3
 * the IDE channels, a master and a slave on each. Minor 0 is the boot
 * card's filesystem area; minors 1-5 are whole drives: 1 the second CF, 2
 * and 3 IDE A's master and slave, 4 and 5 IDE B's. The
 * data register is 16 bits wide (D26): sectors move a halfword at a time,
 * in the card's own default mode, so any CF card (or CF adapter) works --
 * 8-bit mode is optional in the CF standard. A 1 KiB block is one
 * two-sector command. Polled: a sector moves in well under a
 * millisecond on the bus, so the process simply waits (a later version can
 * sleep on IRQ1). */
#include "defs.h"

#define CF(r)   IOB(CFBASE + (r))
#define CFDATA16 (*(volatile ushort *)CFBASE)
#define ATA(p, r) IOB(CFBASE + 16 * (p) + (r))
#define NDRIVE  6
static const uchar dport[NDRIVE] = { 0, 1, 2, 2, 3, 3 }, dunit[NDRIVE] = { 0, 0, 0, 1, 0, 1 };
static int present[NDRIVE];             /* (drive 0, the boot card, must be) */
#define DATA    0
#define ERRFEAT 1
#define COUNT   2
#define LBA0    3
#define LBA1    4
#define LBA2    5
#define HEAD    6
#define CMD     7
#define BSY     0x80
#define DRDY    0x40
#define DRQ     0x08
#define ERR     0x01

static int cf_wait(void)
{
    int st;
    while ((st = CF(CMD)) & BSY) ;
    return st;
}

static int ata_wait(int p)
{
    int st;
    while ((st = ATA(p, CMD)) & BSY) ;
    return st;
}

/* drive d there? Selected, it says it is ready: an empty socket or channel
   reads 0 (nothing drives the bus but the pull-downs) or FF (it floats) */
static int ata_probe(int d)
{
    int p = dport[d], st;
    ATA(p, HEAD) = 0xE0 | dunit[d] << 4;
    st = ATA(p, CMD);
    if (st == 0 || st == 0xFF || !(st & DRDY)) return 0;
    if (p < 2) {                        /* a CF card: as cf_init */
        ATA(p, ERRFEAT) = 0x81;
        ATA(p, CMD) = 0xEF;
        ata_wait(p);
    }
    return 1;
}

int cf_init(void)
{
    uchar sec[512];
    if (!(CF(CMD) & DRDY)) return -1;
    CF(ERRFEAT) = 0x81;                 /* SET FEATURES: no 8-bit transfers (undoes earlier */
    CF(CMD) = 0xEF;                     /* software's; a card that never had one may refuse) */
    cf_wait();
    if (cf_rw(0, sec, 1, 0) == 0 && *(uint *)sec == BOOT_MAGIC && *(uint *)(sec + 16))
        fs_start = *(uint *)(sec + 16);
    present[0] = 1;
    {
        int d;
        static const char *const name[NDRIVE] = { "", "disk1", "hda", "hdb", "hdc", "hdd" };
        for (d = 1; d < NDRIVE; d++)
            if ((present[d] = ata_probe(d)) != 0) kprintf("%s: a drive\n", name[d]);
    }
    return 0;
}

/* The card's layout (tools/p32fs.py): sector 0 the boot header, then the
   kernel, then the filesystem from the sector the header's word 4 names. */
uint fs_start = FS_START;

int cf_open(int minor, int flags) { return minor < NDRIVE && present[minor] ? 0 : -ENXIO; }
int cf_close(int minor) { return 0; }

/* the block driver: minor 0 = the boot card's filesystem area, 1-5 whole
   drives; in 1 KiB blocks */
void cf_strategy(struct buf *b)
{
    int m = minor(b->dev);
    if (m == 0 ? cf_rw(fs_start + b->blkno * 2, b->data, 2, b->flags & B_WRITE) < 0
               : ata_rw(m, b->blkno * 2, b->data, 2, b->flags & B_WRITE) < 0)
        b->flags |= B_ERROR;
}

/* d halfwords from the card to q (q 0: dropped). A function of its own, with
   two variables, so that they live in registers: 8 halfwords take 19
   instructions */
static void cf_in(ushort *q, uint d)
{
    uint k;
    if (!q) {
        for (k = d; k; k--) (void)CFDATA16;
        return;
    }
    for (k = d >> 3; k; k--) {
        q[0] = CFDATA16; q[1] = CFDATA16; q[2] = CFDATA16; q[3] = CFDATA16;
        q[4] = CFDATA16; q[5] = CFDATA16; q[6] = CFDATA16; q[7] = CFDATA16;
        q += 8;
    }
    for (k = d & 7; k; k--) *q++ = CFDATA16;
}

/* d halfwords from q to the card (as cf_in) */
static void cf_out(ushort *q, uint d)
{
    uint k;
    for (k = d >> 3; k; k--) {
        CFDATA16 = q[0]; CFDATA16 = q[1]; CFDATA16 = q[2]; CFDATA16 = q[3];
        CFDATA16 = q[4]; CFDATA16 = q[5]; CFDATA16 = q[6]; CFDATA16 = q[7];
        q += 8;
    }
    for (k = d & 7; k; k--) CFDATA16 = *q++;
}

/* nsect sectors (at most 255) from / to data (halfword-aligned), one
   command; 0 or -EIO */
int cf_rw(uint sector, uchar *data, int nsect, int write)
{
    ushort *p = (ushort *)data;
    int n;
    cf_wait();
    CF(COUNT) = nsect;
    CF(LBA0) = sector;
    CF(LBA1) = sector >> 8;
    CF(LBA2) = sector >> 16;
    CF(HEAD) = 0xE0 | ((sector >> 24) & 0x0F);
    CF(CMD) = write ? 0x30 : 0x20;
    for (n = 0; n < nsect; n++) {
        int st;
        do {                            /* each sector: wait for DRQ */
            st = CF(CMD);
            if (st & ERR) return -EIO;
        } while ((st & BSY) || !(st & DRQ));
        if (write) cf_out(p, 256);
        else cf_in(p, 256);
        p += 256;
    }
    if (cf_wait() & ERR) return -EIO;
    return 0;
}

/* the same for drive d (1-5): any port, master or slave, a halfword at a
   time through the port's data register; 0 or -EIO */
int ata_rw(int d, uint sector, uchar *data, int nsect, int write)
{
    ushort *q = (ushort *)data;
    volatile ushort *dr;
    int n, k, p;
    if (d == 0) return cf_rw(sector, data, nsect, write);
    if (d >= NDRIVE || !present[d]) return -EIO;
    p = dport[d];
    dr = (volatile ushort *)(CFBASE + 16 * p);
    ATA(p, HEAD) = 0xE0 | dunit[d] << 4;
    ata_wait(p);
    ATA(p, COUNT) = nsect;
    ATA(p, LBA0) = sector;
    ATA(p, LBA1) = sector >> 8;
    ATA(p, LBA2) = sector >> 16;
    ATA(p, HEAD) = 0xE0 | dunit[d] << 4 | ((sector >> 24) & 0x0F);
    ATA(p, CMD) = write ? 0x30 : 0x20;
    for (n = 0; n < nsect; n++) {
        int st;
        do {
            st = ATA(p, CMD);
            if (st & ERR) return -EIO;
        } while ((st & BSY) || !(st & DRQ));
        if (write) for (k = 0; k < 256; k++) *dr = *q++;
        else for (k = 0; k < 256; k++) *q++ = *dr;
    }
    if (ata_wait(p) & ERR) return -EIO;
    return 0;
}

/* exec's loads (bdevsw readv): nblk blocks from blkno, past the buffer cache,
   in commands of up to 254 sectors. The data is a stream: its first skip
   bytes are dropped, the next fill the pieces seg[0..nseg-1] in turn, the
   rest are dropped. Halfwords go straight to an even address, as cf_rw's;
   an odd start or end goes a byte at a time. 0 or -EIO */
int cf_readv(int minor, uint blkno, int nblk, uint skip, struct seg *seg, int nseg)
{
    uint sector = fs_start + blkno * 2, left = nblk * 2, addr = 0, rem = 0;
    int si = -1;
    if (minor != 0) return -EIO;
    while (si < nseg && rem == 0)       /* the first piece (empty ones passed over) */
        if (++si < nseg) { addr = seg[si].addr; rem = seg[si].len; }
    while (left) {
        int nsect = left > 254 ? 254 : left, s;
        cf_wait();
        CF(COUNT) = nsect;
        CF(LBA0) = sector;
        CF(LBA1) = sector >> 8;
        CF(LBA2) = sector >> 16;
        CF(HEAD) = 0xE0 | ((sector >> 24) & 0x0F);
        CF(CMD) = 0x20;
        for (s = 0; s < nsect; s++) {
            int st, n = 256;            /* halfwords left in this sector */
            do {
                st = CF(CMD);
                if (st & ERR) return -EIO;
            } while ((st & BSY) || !(st & DRQ));
            while (n) {
                if (skip >= 2) {        /* dropped */
                    uint d = skip / 2 < (uint)n ? skip / 2 : (uint)n;
                    cf_in(0, d);
                    skip -= 2 * d;
                    n -= d;
                } else if (!skip && si < nseg && !(addr & 1) && rem >= 2) {
                    uint d = rem / 2 < (uint)n ? rem / 2 : (uint)n;
                    cf_in((ushort *)addr, d);
                    addr += 2 * d;
                    rem -= 2 * d;
                    n -= d;
                } else {                /* a byte at a time: an odd skip, address or length */
                    ushort h = CFDATA16;
                    int b;
                    n--;
                    for (b = 0; b < 2; b++, h >>= 8) {
                        if (skip) { skip--; continue; }
                        if (si >= nseg) continue;
                        *(uchar *)addr++ = h;
                        rem--;
                        while (si < nseg && rem == 0)   /* (a piece may end mid-halfword) */
                            if (++si < nseg) { addr = seg[si].addr; rem = seg[si].len; }
                    }
                }
                while (si < nseg && rem == 0)   /* the next piece */
                    if (++si < nseg) { addr = seg[si].addr; rem = seg[si].len; }
            }
        }
        if (cf_wait() & ERR) return -EIO;
        sector += nsect;
        left -= nsect;
    }
    return 0;
}
