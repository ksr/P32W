/* buf.h -- the buffer cache (M3, D22; V6's bio) */
struct buf {
    int flags;              /* B_BUSY B_VALID B_DIRTY B_WANTED */
    int dev;
    uint blkno;
    struct buf *prev, *next;    /* LRU list, most recent first */
    uchar data[BSIZE];
};
#define B_BUSY   1
#define B_VALID  2
#define B_DIRTY  4
#define B_WANTED 8
#define B_WRITE  16             /* (for the strategy routine) */
#define B_ERROR  32
