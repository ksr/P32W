/* fs.h -- the P32W Unix filesystem (M3, D22; os-design.md 5). The on-disk
 * format is tools/p32fs.py's, which documents it; the two must agree. */
#define BSIZE      1024
#define FS_MAGIC   0x50333246
#define BOOT_MAGIC 0x42323350
#define FS_START   2048            /* sectors (the boot header's word 4 says) */
#define NDIRECT    8
#define NINDIRECT  (BSIZE / 4)
#define IPB        (BSIZE / 64)
#define DIRSIZ     28
#define ROOTINO    1

#define IFMT  0170000
#define IFREG 0100000
#define IFDIR 0040000
#define IFCHR 0020000
#define IFBLK 0060000
#define IFIFO 0010000
#define IFSOCK 0140000          /* (fstat of a socket: D45; never on disk) */
#define ISUID 04000
#define ISGID 02000
#define ISVTX 01000

struct superblock {
    uint magic, nblocks, ninodes, inode_start, bmap_start, data_start, clean, free_blocks, free_inodes;
};

struct dinode {             /* 64 bytes */
    ushort mode, nlink, uid, gid;
    uint size, mtime, ctime;
    uint addr[NDIRECT + 3];         /* direct, single-, double-, triple-indirect */
};

struct dirent {             /* 32 bytes */
    uint ino;
    char name[DIRSIZ];
};

/* in memory */
struct inode {
    int dev;
    uint inum;
    int ref;
    int flags;              /* I_LOCKED I_WANTED I_VALID */
    struct dinode d;
};
#define I_LOCKED 1
#define I_WANTED 2
#define I_VALID  4

#define major(d) (((d) >> 8) & 0xFF)
#define minor(d) ((d) & 0xFF)

/* the mounted filesystems (mount, D22 backlog): [0] is the root */
#define NMOUNT 4
struct mount {
    int used, dev;
    struct superblock super;
    struct inode *on;       /* the directory it is mounted on (NULL: the root) */
    struct inode *root;     /* its root directory */
    uint bnext;             /* where the block allocator looks first */
    uint inext;             /* every inode below this is in use */
    int wasclean;           /* its superblock said clean when mounted (ustat: fsck -c) */
};
