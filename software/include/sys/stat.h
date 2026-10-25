/* sys/stat.h -- P32W Unix (D22) */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>
struct stat {
    int st_dev;
    unsigned st_ino;
    unsigned short st_mode;
    unsigned short st_nlink;
    unsigned short st_uid;
    unsigned short st_gid;
    int st_rdev;
    int st_size;
    int st_atime;
    int st_mtime;
    int st_ctime;
};
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFSOCK 0140000         /* a socket (fstat; D45) */
#define S_ISUID  04000
#define S_ISGID  02000
#define S_ISVTX  01000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int chmod(const char *path, int mode);
int chown(const char *path, int uid, int gid);
int mkdir(const char *path, int mode);
int mknod(const char *path, int mode, int dev);
int umask(int mask);
#endif
