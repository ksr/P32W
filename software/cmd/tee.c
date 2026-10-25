/* tee -- copy the standard input to the output and to files: tee [-ai] [file ...]
 * A file that cannot be opened or written is reported and dropped; the status
 * is then 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
static int writeall(int fd, const char *buf, int k)
{
    int w;
    while (k > 0) {
        if ((w = write(fd, buf, k)) < 0) return -1;
        if (w == 0) { errno = ENOSPC; return -1; }
        buf += w;
        k -= w;
    }
    return 0;
}
int main(int argc, char **argv)
{
    int *fds, n = 0, i, k, st = 0, flags = O_WRONLY | O_CREAT | O_TRUNC;
    char **names, buf[512];
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++)
            if (*p == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
            else if (*p == 'i') signal(SIGINT, SIG_IGN);
            else {
                fprintf(stderr, "tee: unknown option -%c\nusage: tee [-ai] [file ...]\n", *p);
                return 1;
            }
    }
    fds = malloc((argc + 1) * sizeof *fds);
    names = malloc((argc + 1) * sizeof *names);
    if (!fds || !names) { fprintf(stderr, "tee: out of memory\n"); return 1; }
    fds[n] = 1;                         /* the standard output is the first */
    names[n++] = "standard output";
    for (; i < argc; i++) {
        if ((fds[n] = open(argv[i], flags, 0666)) >= 0) names[n++] = argv[i];
        else { fprintf(stderr, "tee: %s: %s\n", argv[i], strerror(errno)); st = 1; }
    }
    while ((k = read(0, buf, sizeof buf)) > 0)
        for (i = 0; i < n; i++)
            if (fds[i] >= 0 && writeall(fds[i], buf, k) < 0) {
                fprintf(stderr, "tee: %s: %s\n", names[i], strerror(errno));
                fds[i] = -1;            /* no more to this one */
                st = 1;
            }
    if (k < 0) { fprintf(stderr, "tee: read error: %s\n", strerror(errno)); st = 1; }
    for (i = 1; i < n; i++)
        if (fds[i] >= 0 && close(fds[i]) < 0) {
            fprintf(stderr, "tee: %s: %s\n", names[i], strerror(errno));
            st = 1;
        }
    return st;
}
