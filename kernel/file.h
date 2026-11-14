/* file.h -- open files and pipes (M3, D22) */
struct file {
    int type;               /* FD_NONE FD_INODE FD_PIPE FD_SOCKET */
    int ref;
    int flags;              /* O_RDONLY O_WRONLY O_RDWR, O_APPEND */
    struct inode *ip;
    struct pipe *pipe;
    struct sock *sock;      /* FD_SOCKET (net.c, D45) */
    uint off;
};
#define FD_NONE  0
#define FD_INODE 1
#define FD_PIPE  2
#define FD_SOCKET 3

#define PIPESIZE 1024
struct pipe {
    uchar data[PIPESIZE];
    uint nread, nwrite;
    int readopen, writeopen;
};

/* Two kinds of device (Unix V6/V7): BLOCK devices move fixed-size blocks
 * through the buffer cache -- the filesystem lives on one -- and are driven
 * by a strategy routine that transfers one buffer; CHARACTER devices move
 * bytes straight to and from the process (terminals, null, the port, and
 * the raw view of a disk). A device number is major << 8 | minor; the
 * inode's type (IFBLK / IFCHR) says which switch the major indexes. */
struct buf;
struct seg { uint addr, len; };         /* a piece of memory (physical: the kernel's view) */
struct bdevsw {
    int (*open)(int minor, int flags);
    int (*close)(int minor);
    void (*strategy)(struct buf *b);    /* read or write b (B_WRITE), synchronously; B_ERROR on failure */
    int (*readv)(int minor, uint blkno, int nblk, uint skip, struct seg *seg, int nseg);
        /* optional (0: none): nblk blocks from blkno, straight into memory, past the
           cache: the first skip bytes dropped, the next go to the pieces in turn, the
           rest dropped. 0 or -EIO. For exec (fs.c, readi_pages) */
};
extern struct bdevsw bdevsw[];
#define NBDEV 3
#define BDEV_CF 0               /* the CF card: minor 0 = the filesystem area */

/* character devices */
struct cdevsw {
    int (*open)(int minor, int flags);
    int (*close)(int minor);
    int (*read)(int minor, uint dst, int n, uint off);      /* dst: a user address; off: the file offset */
    int (*write)(int minor, uint src, int n, uint off);
    int (*ioctl)(int minor, int cmd, uint arg);
};
extern struct cdevsw cdevsw[];
#define NCDEV 9
#define DEV_TTY 1
#define DEV_MEM 2               /* minor 0 null, 1 mem, 2 port */
#define DEV_PTY   6             /* pty masters, /dev/ptyp0-3 (D46); the slaves are tty minors 2-5 */
#define DEV_RUSB  5             /* the USB drive, raw, byte-addressed (D43) */
#define DEV_RDISK 3             /* the whole CF card, raw, byte-addressed (root only) */
#define DEV_VIDEO 7             /* the video card's chip for a program's own drawing (D110) */
