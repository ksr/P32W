/* rmdir -- remove empty directories: rmdir [-p] dir ... */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
static int pflag;

static int rm(const char *path)
{
    if (rmdir(path) < 0) { fprintf(stderr, "rmdir: %s: %s\n", path, strerror(errno)); return 1; }
    return 0;
}

/* -p: remove the directory, then each parent named in the path, last first */
static int rmpath(const char *path)
{
    char buf[PATHMAX];
    int n = strlen(path);
    if (n >= PATHMAX) { fprintf(stderr, "rmdir: %s: %s\n", path, strerror(ENAMETOOLONG)); return 1; }
    strcpy(buf, path);
    for (;;) {
        while (n > 1 && buf[n - 1] == '/') buf[--n] = 0;
        if (rm(buf)) return 1;
        while (n > 0 && buf[n - 1] != '/') n--;       /* drop the last component */
        while (n > 0 && buf[n - 1] == '/') n--;       /* and the slashes before it */
        if (n == 0) return 0;                         /* (a relative path, or / reached) */
        buf[n] = 0;
    }
}

int main(int argc, char **argv)
{
    int i, st = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'p') pflag = 1;
            else { fprintf(stderr, "rmdir: unknown option -%c\n", *f); i = argc; break; }
    }
    if (i >= argc) { fprintf(stderr, "usage: rmdir [-p] dir ...\n"); return 2; }
    for (; i < argc; i++)
        st |= pflag ? rmpath(argv[i]) : rm(argv[i]);
    return st;
}
