/* cat [-nu] [file ...] -- copy files (or the standard input) to the standard output */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
static int nflag, atstart = 1;
static long line;
static char obuf[1024];
static int olen;

static int flush(void)
{
    int r = olen && write(1, obuf, olen) != olen;
    olen = 0;
    return r;
}

static int copy(int fd)
{
    char buf[1024];
    int n, i, err = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        if (!nflag) {
            if (write(1, buf, n) != n) return 1;
            continue;
        }
        for (i = 0; i < n; i++) {               /* -n: each line after its number */
            if (atstart) {
                char num[16];
                int k;
                sprintf(num, "%6ld\t", ++line);
                for (k = 0; num[k]; k++) {
                    if (olen == sizeof obuf) err |= flush();
                    obuf[olen++] = num[k];
                }
                atstart = 0;
            }
            if (olen == sizeof obuf) err |= flush();
            obuf[olen++] = buf[i];
            if (buf[i] == '\n') atstart = 1;
        }
        err |= flush();
        if (err) return 1;
    }
    return n < 0;
}

int main(int argc, char **argv)
{
    int i, st = 0;
    struct stat s;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'n') nflag = 1;
            else if (*f == 'u') ;                       /* (cat never buffers its output) */
            else { fprintf(stderr, "cat: unknown option -%c\nusage: cat [-nu] [file ...]\n", *f); return 2; }
    }
    if (i == argc) return copy(0);
    for (; i < argc; i++) {
        int fd = strcmp(argv[i], "-") ? open(argv[i], O_RDONLY) : 0;
        if (fd < 0) { fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        if (fd && fstat(fd, &s) == 0 && S_ISDIR(s.st_mode)) {
            fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(EISDIR));
            close(fd);
            st = 1;
            continue;
        }
        if (copy(fd)) { fprintf(stderr, "cat: %s: %s\n", argv[i], fd ? "read or write error" : "write error"); st = 1; }
        if (fd) close(fd);
    }
    return st;
}
