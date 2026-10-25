/* ramdisk.c -- a RAM disk: block device 1 (/dev/ram), RAMBLOCKS 1 KiB
 * blocks in kernel frames, taken as blocks are first written (a block never
 * written reads as zeros). For trying mount, mkfs and fsck, and scratch
 * space; it is empty after a reboot. */
#include "defs.h"

#define RAMBLOCKS 256                   /* 256 KiB at most */
static uint frames[RAMBLOCKS / 4];      /* a frame holds 4 blocks */

int ram_open(int minor, int flags) { (void)flags; return minor == 0 ? 0 : -ENXIO; }
int ram_close(int minor) { (void)minor; return 0; }

void ram_strategy(struct buf *b)
{
    uint bn = b->blkno, *f;
    if (bn >= RAMBLOCKS) { b->flags |= B_ERROR; return; }
    f = &frames[bn / 4];
    if (b->flags & B_WRITE) {
        if (!*f && !(*f = frame_alloc())) { b->flags |= B_ERROR; return; }
        memcpy((void *)(*f + bn % 4 * BSIZE), b->data, BSIZE);
    } else if (*f)
        memcpy(b->data, (void *)(*f + bn % 4 * BSIZE), BSIZE);
    else
        memset(b->data, 0, BSIZE);
}
