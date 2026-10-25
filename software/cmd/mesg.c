/* mesg -- allow or refuse messages (write, wall) on your terminal
 *
 *   mesg [y | n]
 *
 * The terminal is the first of the standard input, output and error that
 * is one of /dev/tty0..9. Messages are allowed while its group-write bit
 * (020) is on: login makes it mode 0620, owner the user, group tty, and
 * write and wall are set-group-id tty. With no argument mesg says "is y"
 * or "is n". Exit status: 0 messages allowed, 1 refused, 2 an error. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define S_IWGRP 020                     /* (sys/stat.h has no permission names) */

/* the /dev/ttyN that fd is open on: N, its name in path; -1 if none */
static int ttyof(int fd, char *path)
{
    struct stat f, s;
    int k;
    if (fstat(fd, &f) < 0 || !S_ISCHR(f.st_mode)) return -1;
    for (k = 0; k < 10; k++) {
        sprintf(path, "/dev/tty%d", k);
        if (stat(path, &s) == 0 && S_ISCHR(s.st_mode) && s.st_rdev == f.st_rdev) return k;
    }
    return -1;
}

int main(int argc, char **argv)
{
    char path[16];
    struct stat s;
    int fd, mode;
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "y") && strcmp(argv[1], "n"))) {
        fprintf(stderr, "usage: mesg [y | n]\n");
        return 2;
    }
    for (fd = 0; fd < 3; fd++)
        if (ttyof(fd, path) >= 0) break;
    if (fd == 3) { fprintf(stderr, "mesg: not on a terminal\n"); return 2; }
    if (stat(path, &s) < 0) { fprintf(stderr, "mesg: %s: %s\n", path, strerror(errno)); return 2; }
    mode = s.st_mode & 07777;
    if (argc == 1) {
        printf("is %c\n", mode & S_IWGRP ? 'y' : 'n');
        return mode & S_IWGRP ? 0 : 1;
    }
    mode = argv[1][0] == 'y' ? mode | S_IWGRP : mode & ~S_IWGRP;
    if (mode != (s.st_mode & 07777) && chmod(path, mode) < 0) {
        fprintf(stderr, "mesg: %s: %s\n", path, strerror(errno));
        return 2;
    }
    return argv[1][0] == 'y' ? 0 : 1;
}
