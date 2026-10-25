/* mkfs -- make an empty filesystem on a block device (root):
 *   mkfs special nblocks [ninodes]
 * The format of tools/p32fs.py (its comment describes it): a superblock,
 * the inode table, the free-block bitmap, the root directory. One inode per
 * 8 blocks by default (at least 64, at most 512 Ki). /dev/ram holds 256
 * blocks. A mounted device is refused, and so is a size the device does
 * not have (its last block must be readable). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ustat.h>

#define BLOCK 1024
#define IPB 16
static unsigned char blk[BLOCK];
static int fd;

static void put32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

static int wblock(unsigned b)
{
    lseek(fd, b * BLOCK, 0);
    return write(fd, blk, BLOCK) == BLOCK ? 0 : -1;
}

int main(int argc, char **argv)
{
    unsigned nblocks, ninodes, inode_start = 1, bmap_start, nbmap, data_start, b, now = time(0);
    if (argc < 3) { fprintf(stderr, "usage: mkfs special nblocks [ninodes]\n"); return 2; }
    nblocks = atoi(argv[2]);
    if (nblocks < 64) { fprintf(stderr, "mkfs: at least 64 blocks\n"); return 2; }
    ninodes = argc > 3 ? (unsigned)atoi(argv[3]) : nblocks / 8;
    if (ninodes < 64) ninodes = 64;
    if (ninodes > 512 * 1024) ninodes = 512 * 1024;
    ninodes = (ninodes + IPB - 1) / IPB * IPB;
    bmap_start = inode_start + ninodes / IPB;
    nbmap = (nblocks + BLOCK * 8 - 1) / (BLOCK * 8);
    data_start = bmap_start + nbmap;
    if (data_start + 1 >= nblocks) { fprintf(stderr, "mkfs: too small for %u inodes\n", ninodes); return 2; }
    {
        struct stat st;
        struct ustat u;
        if (stat(argv[1], &st) == 0 && (st.st_mode & S_IFMT) == S_IFBLK && ustat(st.st_rdev, &u) == 0) {
            fprintf(stderr, "mkfs: %s is mounted (umount it first)\n", argv[1]);
            return 1;
        }
    }
    if ((fd = open(argv[1], O_RDWR)) < 0) { perror(argv[1]); return 1; }
    lseek(fd, (nblocks - 1) * BLOCK, 0);  /* (the device must have the blocks asked for) */
    if (read(fd, blk, BLOCK) != BLOCK) {
        fprintf(stderr, "mkfs: %s has fewer than %u blocks\n", argv[1], nblocks);
        return 1;
    }
    /* the superblock */
    memset(blk, 0, BLOCK);
    put32(blk, 0x50333246); put32(blk + 4, nblocks); put32(blk + 8, ninodes);
    put32(blk + 12, inode_start); put32(blk + 16, bmap_start); put32(blk + 20, data_start);
    put32(blk + 24, 1); put32(blk + 28, nblocks - data_start - 1); put32(blk + 32, ninodes - 2);
    if (wblock(0) < 0) { perror(argv[1]); return 1; }
    /* the inode table: the root, inode 1, in the first block */
    for (b = inode_start; b < bmap_start; b++) {
        memset(blk, 0, BLOCK);
        if (b == inode_start) {
            unsigned char *ip = blk + 64;
            put16(ip, 040755); put16(ip + 2, 2); put16(ip + 4, 0); put16(ip + 6, 0);
            put32(ip + 8, 64); put32(ip + 12, now); put32(ip + 16, now);
            put32(ip + 20, data_start);
        }
        if (wblock(b) < 0) { perror(argv[1]); return 1; }
    }
    /* the bitmap: the blocks before the data, and the root directory's */
    for (b = 0; b < nbmap; b++) {
        unsigned k;
        memset(blk, 0, BLOCK);
        for (k = 0; k < BLOCK * 8; k++) {
            unsigned bn = b * BLOCK * 8 + k;
            if (bn <= data_start) blk[k / 8] |= 1 << (k % 8);
        }
        if (wblock(bmap_start + b) < 0) { perror(argv[1]); return 1; }
    }
    /* the root directory: . and .. */
    memset(blk, 0, BLOCK);
    put32(blk, 1); blk[4] = '.';
    put32(blk + 32, 1); blk[36] = '.'; blk[37] = '.';
    if (wblock(data_start) < 0) { perror(argv[1]); return 1; }
    close(fd);
    sync();
    printf("%s: %u blocks, %u inodes, data from block %u\n", argv[1], nblocks, ninodes, data_start);
    return 0;
}
