/* mknod -- make a special file (V7's /etc/mknod)
 *
 *   /etc/mknod name b|c major minor
 *
 * b: a block device, c: a character device; major and minor are 0-255 and
 * make the device number major * 256 + minor (mknod(2)). The mode is 0666
 * less the umask. Only root may make one. Named pipes (p) do not exist on
 * P32W Unix. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* 0-255 in decimal; otherwise -1 */
static int byte(const char *s)
{
    int n = 0;
    if (!*s || strlen(s) > 3) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        n = n * 10 + *s - '0';
    }
    return n > 255 ? -1 : n;
}

int main(int argc, char **argv)
{
    int type, maj, min;
    struct stat s;
    if (argc != 5 || argv[2][1] || (argv[2][0] != 'b' && argv[2][0] != 'c')) {
        if (argc >= 3 && !strcmp(argv[2], "p")) fprintf(stderr, "mknod: named pipes are not supported\n");
        else fprintf(stderr, "usage: mknod name b|c major minor\n");
        return 2;
    }
    type = argv[2][0] == 'b' ? S_IFBLK : S_IFCHR;
    if ((maj = byte(argv[3])) < 0) { fprintf(stderr, "mknod: bad major number %s (0-255)\n", argv[3]); return 2; }
    if ((min = byte(argv[4])) < 0) { fprintf(stderr, "mknod: bad minor number %s (0-255)\n", argv[4]); return 2; }
    if (stat(argv[1], &s) == 0) { fprintf(stderr, "mknod: %s: file exists\n", argv[1]); return 1; }
    if (mknod(argv[1], type | 0666, maj * 256 + min) < 0) {
        if (errno == EPERM) fprintf(stderr, "mknod: %s: not owner (only root may make special files)\n", argv[1]);
        else fprintf(stderr, "mknod: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    return 0;
}
