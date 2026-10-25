/* fsck -- check the filesystem on the CF card (M5; os-design.md 5.4).
 *
 * fsck [-q] [-y] [-c] [special]: reads /dev/disk, or another block device
 * holding a filesystem (/dev/ram) (root only: through the kernel's buffer
 * cache, so it sees what the kernel sees) and checks what tools/p32fs.py's
 * fsck checks: every block in use is in range and owned once; the
 * free-block bitmap matches; directories hold . and .. and name only
 * allocated inodes; link counts match the directory entries; the
 * superblock's free counts are right. Reports (with -q: only when something
 * is wrong), and exits 1 if anything is wrong. -c checks only a filesystem
 * that was not shut down cleanly (its superblock's flag; for a mounted one,
 * the flag as it was at the mount, from ustat): /etc/rc's way, so that a
 * big disk does not slow every boot (D40).
 *
 * -y repairs: an inode with a bad type, a block out of range or owned
 * twice is cleared; a directory entry naming a free inode is removed; an
 * inode no entry names is cleared; link counts are set to the entries;
 * the bitmap and the superblock's counts are rebuilt. A mounted
 * filesystem other than / is refused (umount it first). Repairing / (whose
 * blocks the kernel has cached) ends with an immediate restart without
 * writing the cache back, as V7's fsck did.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ustat.h>

#define BSIZE 1024
#define NDIRECT 8
#define NIND (BSIZE / 4)
#define IFMT 0170000
#define IFDIR 0040000
#define IFREG 0100000
#define IFCHR 0020000
#define IFBLK 0060000
#define IFIFO 0010000

struct sb { unsigned magic, nblocks, ninodes, inode_start, bmap_start, data_start, clean, free_blocks, free_inodes; };
struct dinode { unsigned short mode, nlink, uid, gid; unsigned size, mtime, ctime; unsigned addr[NDIRECT + 3]; };
struct dirent_ { unsigned ino; char name[28]; };

static int fd, problems, repair, repaired;
static unsigned char *bad;          /* inodes to clear (-y) */
static struct sb sb;
static unsigned char *used;         /* one bit per block */
static unsigned short *links;       /* directory entries naming each inode */
static unsigned char *types;        /* each inode's type: 0 free, else IFMT >> 12 */

static void rd(unsigned blk, void *buf)
{
    lseek(fd, blk * BSIZE, 0);
    if (read(fd, buf, BSIZE) != BSIZE) { printf("fsck: cannot read block %u\n", blk); exit(2); }
}

static void problem(const char *fmt, unsigned a, unsigned b)
{
    printf(fmt, a, b);
    putchar('\n');
    problems++;
}

static void wr(unsigned blk, void *buf)
{
    lseek(fd, blk * BSIZE, 0);
    if (write(fd, buf, BSIZE) != BSIZE) { printf("fsck: cannot write block %u\n", blk); exit(2); }
}

static int quietclaims;             /* (the second count of blocks, after repairs: no reports) */

static void claim(unsigned b, unsigned ino)
{
    if (b < sb.data_start || b >= sb.nblocks) {
        if (!quietclaims) { problem("inode %u: block %u out of range", ino, b); bad[ino] = 1; }
        return;
    }
    if (used[b / 8] & (1 << (b % 8))) {
        if (!quietclaims) { problem("block %u owned twice (again by inode %u)", b, ino); bad[ino] = 1; }
        return;
    }
    used[b / 8] |= 1 << (b % 8);
}

static void claim_ind(unsigned b, int depth, unsigned ino)
{
    unsigned v[NIND];
    int k;
    claim(b, ino);
    rd(b, v);
    for (k = 0; k < NIND; k++)
        if (v[k]) { if (depth > 1) claim_ind(v[k], depth - 1, ino); else claim(v[k], ino); }
}

static struct dinode iblk[BSIZE / 64];
static unsigned ihave = 0xFFFFFFFF;    /* (16 inodes a block: read each block once) */

static void iread(unsigned ino, struct dinode *d)
{
    if (ihave != sb.inode_start + ino / 16) rd(ihave = sb.inode_start + ino / 16, iblk);
    *d = iblk[ino % 16];
}

static void iwrite(unsigned ino, struct dinode *d)
{
    iread(ino, &iblk[ino % 16]);        /* (the block in hand) */
    iblk[ino % 16] = *d;
    wr(ihave, iblk);
}

static void iclear(unsigned ino)
{
    struct dinode d;
    memset(&d, 0, sizeof d);
    iwrite(ino, &d);
    types[ino] = 0;
    repaired++;
}

/* every block every inode owns, into used[] */
static void count_blocks(void)
{
    unsigned ino;
    struct dinode d;
    memset(used, 0, sb.nblocks / 8 + 1);
    for (ino = 1; ino < sb.ninodes; ino++) {
        unsigned t, k;
        if (!types[ino]) continue;
        iread(ino, &d);
        t = d.mode & IFMT;
        if (t == IFCHR || t == IFBLK) continue;
        for (k = 0; k < NDIRECT; k++) if (d.addr[k]) claim(d.addr[k], ino);
        if (d.addr[NDIRECT]) claim_ind(d.addr[NDIRECT], 1, ino);
        if (d.addr[NDIRECT + 1]) claim_ind(d.addr[NDIRECT + 1], 2, ino);
        if (d.addr[NDIRECT + 2]) claim_ind(d.addr[NDIRECT + 2], 3, ino);
    }
}

/* the disk block of a file's block n (0: a hole) */
static unsigned bmap(struct dinode *d, unsigned n)
{
    unsigned v[NIND];
    if (n < NDIRECT) return d->addr[n];
    n -= NDIRECT;
    if (n < NIND) { if (!d->addr[NDIRECT]) return 0; rd(d->addr[NDIRECT], v); return v[n]; }
    n -= NIND;
    if (!d->addr[NDIRECT + 1]) return 0;
    rd(d->addr[NDIRECT + 1], v);
    if (!v[n / NIND]) return 0;
    { unsigned mid = v[n / NIND]; rd(mid, v); return v[n % NIND]; }
}

int main(int argc, char **argv)
{
    int quiet = 0, k, root = 0, ifdirty = 0, wasclean = -1;
    const char *special = "/dev/disk";
    unsigned char *bmapblk;
    unsigned ino, b, nfree = 0, ninodes_used = 0, nblocks_used = 0, bitfixes = 0;
    struct dinode d;
    for (k = 1; k < argc; k++)
        if (!strcmp(argv[k], "-q")) quiet = 1;
        else if (!strcmp(argv[k], "-y")) repair = 1;
        else if (!strcmp(argv[k], "-c")) ifdirty = 1;
        else if (argv[k][0] == '-') { fprintf(stderr, "usage: fsck [-q] [-y] [-c] [special]\n"); return 2; }
        else special = argv[k];
    sync();                             /* the kernel's cached blocks and superblock to the disk first */
    {
        struct stat st;
        struct ustat u;
        if (stat(special, &st) == 0 && (st.st_mode & S_IFMT) == S_IFBLK && ustat(st.st_rdev, &u) == 0) {
            struct stat rs;
            root = stat("/", &rs) == 0 && rs.st_dev == st.st_rdev;
            wasclean = u.f_clean;
            if (repair && !root) { printf("fsck: %s is mounted (umount it first)\n", special); return 2; }
        }
    }
    if ((fd = open(special, repair ? O_RDWR : O_RDONLY)) < 0) { perror(special); return 2; }
    {
        static unsigned char blk[BSIZE];
        rd(0, blk);
        memcpy(&sb, blk, sizeof sb);
    }
    if (sb.magic != 0x50333246) { printf("fsck: no filesystem\n"); return 2; }
    if (ifdirty) {                      /* -c: only if it was not shut down cleanly */
        if (wasclean < 0) wasclean = sb.clean;          /* (not mounted: its own flag) */
        if (wasclean) {
            if (!quiet) printf("fsck: %s was shut down cleanly: not checked\n", special);
            return 0;
        }
    }
    used = calloc(sb.nblocks / 8 + 1, 1);
    links = calloc(sb.ninodes, sizeof *links);
    types = calloc(sb.ninodes, 1);
    bad = calloc(sb.ninodes, 1);
    if (!used || !links || !types || !bad) { printf("fsck: not enough memory for this filesystem\n"); return 2; }
    bmapblk = malloc(BSIZE);
    /* 1: inodes and the blocks they own */
    for (ino = 1; ino < sb.ninodes; ino++) {
        unsigned t;
        iread(ino, &d);
        types[ino] = d.mode >> 12;
        if (!d.mode) continue;
        t = d.mode & IFMT;
        if (t != IFREG && t != IFDIR && t != IFCHR && t != IFBLK && t != IFIFO) { problem("inode %u: bad type %o", ino, d.mode); bad[ino] = 1; }
    }
    count_blocks();
    if (repair)                         /* (cleared before the directories are read: their entries go too) */
        for (ino = 2; ino < sb.ninodes; ino++)
            if (bad[ino]) { printf("inode %u cleared\n", ino); iclear(ino); }
    /* 2: directories */
    for (ino = 1; ino < sb.ninodes; ino++) {
        unsigned off, dot = 0, dotdot = 0;
        if (types[ino] != IFDIR >> 12) continue;
        iread(ino, &d);
        if (d.size % 32) problem("directory %u: size %u not a multiple of 32", ino, d.size);
        for (off = 0; off + 32 <= d.size; off += 32) {
            static struct dirent_ ents[BSIZE / 32];
            static unsigned blk;
            struct dirent_ *e;
            if (off % BSIZE == 0) {
                blk = bmap(&d, off / BSIZE);
                if (blk) rd(blk, ents); else memset(ents, 0, sizeof ents);
            }
            e = &ents[(off % BSIZE) / 32];
            if (!e->ino) continue;
            if (e->ino >= sb.ninodes || !types[e->ino]) {
                problem("directory %u names free inode %u", ino, e->ino);
                if (repair && blk) { printf("  entry removed\n"); e->ino = 0; wr(blk, ents); repaired++; }
                continue;
            }
            if (links[e->ino] < 0xFFFF) links[e->ino]++;
            if (!strncmp(e->name, ".", 28)) dot = e->ino;
            if (!strncmp(e->name, "..", 28)) dotdot = 1;
        }
        if (dot != ino) problem("directory %u: bad . (%u)", ino, dot);
        if (!dotdot) problem("directory %u: no ..", ino, 0);
    }
    /* 3: link counts */
    for (ino = 1; ino < sb.ninodes; ino++) {
        if (!types[ino]) continue;
        iread(ino, &d);
        if (d.nlink != links[ino]) {
            printf("inode %u: link count %d, %d entries\n", ino, d.nlink, links[ino]);
            problems++;
            if (repair && ino != 1 && !links[ino]) { printf("  unreferenced: cleared\n"); iclear(ino); }
            else if (repair) { d.nlink = links[ino]; iwrite(ino, &d); printf("  set to %d\n", links[ino]); repaired++; }
        }
    }
    if (repair && repaired) { quietclaims = 1; count_blocks(); }    /* (the cleared inodes' blocks are free now) */
    for (ino = 1; ino < sb.ninodes; ino++) if (types[ino]) ninodes_used++;
    /* 4: the bitmap and the superblock */
    for (b = 0; b < sb.nblocks; b++) {
        int bit, inuse;
        if (b % (BSIZE * 8) == 0) rd(sb.bmap_start + b / (BSIZE * 8), bmapblk);
        bit = bmapblk[(b % (BSIZE * 8)) / 8] >> (b % 8) & 1;
        inuse = b < sb.data_start || (used[b / 8] >> (b % 8) & 1);
        if (inuse && b >= sb.data_start) nblocks_used++;
        if (bit != inuse) {
            if (!quietclaims) printf("block %u: the bitmap says %s\n", b, bit ? "used" : "free");
            problems++;
            if (repair) {
                bmapblk[(b % (BSIZE * 8)) / 8] ^= 1 << (b % 8);
                wr(sb.bmap_start + b / (BSIZE * 8), bmapblk);
                repaired++;
                bitfixes++;
                bit = inuse;
            }
        }
        nfree += !bit;
    }
    if (bitfixes) printf("  bitmap: %u block%s corrected\n", bitfixes, bitfixes == 1 ? "" : "s");
    if (nfree != sb.free_blocks || sb.ninodes - 1 - ninodes_used != sb.free_inodes) {
        if (nfree != sb.free_blocks) problem("superblock: %u free blocks, counted %u", sb.free_blocks, nfree);
        if (sb.ninodes - 1 - ninodes_used != sb.free_inodes)
            problem("superblock: %u free inodes, counted %u", sb.free_inodes, sb.ninodes - 1 - ninodes_used);
        if (repair) {
            static unsigned char blk[BSIZE];
            rd(0, blk);
            sb.free_blocks = nfree;
            sb.free_inodes = sb.ninodes - 1 - ninodes_used;
            memcpy(blk, &sb, sizeof sb);
            wr(0, blk);
            printf("  superblock counts corrected\n");
            repaired++;
        }
    }
    if (!quiet || problems)
        printf("fsck: %u inodes, %u data blocks, %s\n", ninodes_used, nblocks_used,
               !problems ? "clean" : repair ? "REPAIRED" : "PROBLEMS (see above)");
    if (repair && repaired && root) {   /* the kernel's cache of / is stale now: no sync, restart */
        printf("fsck: / was repaired: restarting without writing the cache back\n");
        fflush(stdout);
        sleep(1);
        reboot(RB_REBOOT | RB_NOSYNC);
    }
    return problems && !repair ? 1 : 0;
}
