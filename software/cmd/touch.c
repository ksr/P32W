/* touch -- set the modification time of files to now, creating them: touch [-c] file ... */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
int main(int argc, char **argv)
{
    int i, fd, st = 0, cflag = 0;
    struct stat s;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'c') cflag = 1;
            else if (*f == 'a' || *f == 'm') ;          /* (P32W keeps only the modification time) */
            else { fprintf(stderr, "touch: unknown option -%c\n", *f); i = argc + 1; break; }
    }
    if (i >= argc) { fprintf(stderr, "usage: touch [-c] file ...\n"); return 2; }
    for (; i < argc; i++) {
        if (stat(argv[i], &s) == 0) {
            if (utime(argv[i], NULL) < 0) { fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno)); st = 1; }
            continue;
        }
        if (cflag) continue;
        if ((fd = open(argv[i], O_WRONLY | O_CREAT, 0666)) < 0) {
            fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
            st = 1;
            continue;
        }
        close(fd);
    }
    return st;
}
