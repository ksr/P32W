/* chgrp -- change the group of files, keeping their owner
 *
 *   chgrp [-R] group file ...
 *
 * group is a name from /etc/group or a number. -R also changes everything
 * inside each directory, recursively. The kernel's chown(2) is root's only
 * (as in V7), so for anyone else chgrp can only "change" a file to the
 * group it already has (which is not an error). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <grp.h>
#include <dirent.h>
#include <sys/stat.h>

#define PATHMAX 256

static int gid, recurse, status;

/* a string of digits only: its value; otherwise -1 */
static int number(const char *s)
{
    int n = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > 6553) return -1;
        n = n * 10 + *s - '0';
    }
    return n > 65535 ? -1 : n;
}

static void complain(const char *path, int e)
{
    if (e == EPERM) fprintf(stderr, "chgrp: %s: not owner (only root may change a group)\n", path);
    else fprintf(stderr, "chgrp: %s: %s\n", path, strerror(e));
    status = 1;
}

/* change one file; with -R, then whatever is inside it */
static void change(const char *path)
{
    struct stat s;
    if (stat(path, &s) < 0) { complain(path, errno); return; }
    if (s.st_gid != gid && chown(path, s.st_uid, gid) < 0) {
        complain(path, errno);
        if (errno == EPERM) return;         /* (the same would happen inside) */
    }
    if (recurse && S_ISDIR(s.st_mode)) {
        DIR *d;
        struct dirent *e;
        char sub[PATHMAX];
        if (!(d = opendir(path))) { complain(path, errno); return; }
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (strlen(path) + strlen(e->d_name) + 2 > sizeof sub) { complain(e->d_name, ENAMETOOLONG); continue; }
            sprintf(sub, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", e->d_name);
            change(sub);
        }
        closedir(d);
    }
}

int main(int argc, char **argv)
{
    int i = 1;
    struct group *gr;
    if (i < argc && !strcmp(argv[i], "-R")) { recurse = 1; i++; }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    if (argc - i < 2 || (argv[i][0] == '-' && argv[i][1])) {
        fprintf(stderr, "usage: chgrp [-R] group file ...\n");
        return 2;
    }
    if ((gr = getgrnam(argv[i])) != NULL) gid = gr->gr_gid;
    else if ((gid = number(argv[i])) < 0) { fprintf(stderr, "chgrp: unknown group %s\n", argv[i]); return 1; }
    for (i++; i < argc; i++) change(argv[i]);
    return status;
}
