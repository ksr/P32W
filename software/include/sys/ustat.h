/* sys/ustat.h -- P32W Unix: a mounted filesystem's space (df) */
#ifndef _SYS_USTAT_H
#define _SYS_USTAT_H
struct ustat {
    unsigned f_blocks, f_bfree;         /* 1 KiB blocks: all, free */
    unsigned f_files, f_ffree;          /* inodes: all, free */
    unsigned f_clean;                   /* 1: it was shut down cleanly before this mount */
};
int ustat(int dev, struct ustat *u);    /* dev: a block device's st_rdev; -1 (EINVAL) if not mounted */
#endif
