/* mkdir -- make directories: mkdir [-p] [-m mode] dir ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
#define DIRSIZ 28                       /* the longest name in a directory */
static int pflag, mode = -1;

/* make one directory; with -p an existing directory is no error */
static int make(const char *path, int last)
{
    struct stat st;
    int e = strlen(path), s;
    while (e > 1 && path[e - 1] == '/') e--;
    s = e;
    while (s > 0 && path[s - 1] != '/') s--;
    if (e - s > DIRSIZ) { fprintf(stderr, "mkdir: %s: %s\n", path, strerror(ENAMETOOLONG)); return 1; }
    if (mkdir(path, last && mode >= 0 ? 0700 : 0777) < 0) {
        int err = errno;
        if (pflag && err == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "mkdir: %s: %s\n", path, strerror(err));
        return 1;
    }
    if (last && mode >= 0 && chmod(path, mode) < 0) { fprintf(stderr, "mkdir: %s: %s\n", path, strerror(errno)); return 1; }
    return 0;
}

/* -p: make each missing parent, then the directory */
static int makepath(const char *path)
{
    char buf[PATHMAX];
    int i, n = strlen(path);
    if (n >= PATHMAX) { fprintf(stderr, "mkdir: %s: %s\n", path, strerror(ENAMETOOLONG)); return 1; }
    strcpy(buf, path);
    for (i = 1; i < n; i++)
        if (buf[i] == '/' && buf[i - 1] != '/') {
            buf[i] = 0;
            if (make(buf, 0)) return 1;
            buf[i] = '/';
        }
    return make(buf, 1);
}

static int usage(void)
{
    fprintf(stderr, "usage: mkdir [-p] [-m mode] dir ...\n");
    return 2;
}

int main(int argc, char **argv)
{
    int i, st = 0;
    char *end;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'p') pflag = 1;
            else if (*f == 'm') {
                char *m = f[1] ? f + 1 : i + 1 < argc ? argv[++i] : NULL;
                if (!m) return usage();
                mode = strtol(m, &end, 8);
                if (*end || !*m || mode > 07777) { fprintf(stderr, "mkdir: %s: give the mode in octal\n", m); return 2; }
                break;
            } else { fprintf(stderr, "mkdir: unknown option -%c\n", *f); return usage(); }
    }
    if (i == argc) return usage();
    for (; i < argc; i++)
        st |= pflag ? makepath(argv[i]) : make(argv[i], 1);
    return st;
}
