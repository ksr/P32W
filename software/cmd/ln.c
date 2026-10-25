/* ln -- make hard links: ln [-f] old new, or ln [-f] file ... dir */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
static int fflag;

/* dir/(last component of path) into buf, without a doubled /; -1 if too long */
static int into(char *buf, const char *dir, const char *path)
{
    int e = strlen(path), s, n = strlen(dir), slash;
    while (e > 1 && path[e - 1] == '/') e--;
    s = e;
    while (s > 0 && path[s - 1] != '/') s--;
    slash = n > 0 && dir[n - 1] != '/';
    if (n + slash + (e - s) >= PATHMAX) return -1;
    strcpy(buf, dir);
    if (slash) buf[n++] = '/';
    memcpy(buf + n, path + s, e - s);
    buf[n + e - s] = 0;
    return 0;
}

static int ln(const char *old, const char *new_)
{
    struct stat os, ns;
    if (stat(old, &os) < 0) { fprintf(stderr, "ln: %s: %s\n", old, strerror(errno)); return 1; }
    if (S_ISDIR(os.st_mode)) { fprintf(stderr, "ln: %s: is a directory\n", old); return 1; }
    if (stat(new_, &ns) == 0) {
        if (ns.st_dev == os.st_dev && ns.st_ino == os.st_ino) {
            fprintf(stderr, "ln: %s and %s are the same file\n", old, new_);
            return 1;
        }
        if (!fflag) { fprintf(stderr, "ln: %s: %s\n", new_, strerror(EEXIST)); return 1; }
        if (S_ISDIR(ns.st_mode)) { fprintf(stderr, "ln: %s: is a directory\n", new_); return 1; }
        if (ns.st_dev != os.st_dev) { fprintf(stderr, "ln: %s: %s\n", new_, strerror(EXDEV)); return 1; }
        if (unlink(new_) < 0) { fprintf(stderr, "ln: %s: %s\n", new_, strerror(errno)); return 1; }
    }
    if (link(old, new_) < 0) { fprintf(stderr, "ln: %s: %s\n", new_, strerror(errno)); return 1; }
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: ln [-f] old new | ln [-f] file ... dir\n");
    return 2;
}

int main(int argc, char **argv)
{
    char to[PATHMAX];
    struct stat st;
    int i, r = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'f') fflag = 1;
            else { fprintf(stderr, "ln: unknown option -%c\n", *f); return usage(); }
    }
    argv += i - 1;
    argc -= i - 1;
    if (argc < 3) return usage();
    if (stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode)) {
        for (i = 1; i < argc - 1; i++) {
            if (into(to, argv[argc - 1], argv[i]) < 0) { fprintf(stderr, "ln: %s: name too long\n", argv[i]); r = 1; continue; }
            r |= ln(argv[i], to);
        }
        return r;
    }
    if (argc != 3) { fprintf(stderr, "ln: %s is not a directory\n", argv[argc - 1]); return 1; }
    return ln(argv[1], argv[2]);
}
