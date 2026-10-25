/* cp -- copy files: cp [-r] from to, or cp [-r] file ... dir */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
static int rflag;
static int topdev, topino = -1;         /* the directory -r made first (not copied into itself) */

/* the names in a directory but . and .., read all at once and the directory
   closed, so a deep tree does not run out of descriptors; NULL on error */
static char **names(const char *path, int *np)
{
    DIR *d;
    struct dirent *e;
    char **v = malloc(16 * sizeof(char *)), **nv;
    int n = 0, max = 16;
    if (!v) { errno = ENOMEM; return NULL; }
    if (!(d = opendir(path))) { free(v); return NULL; }
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == max && (nv = realloc(v, 2 * max * sizeof(char *))) != NULL) { v = nv; max *= 2; }
        if (n == max || !(v[n] = strdup(e->d_name))) {
            while (n > 0) free(v[--n]);
            free(v);
            closedir(d);
            errno = ENOMEM;
            return NULL;
        }
        n++;
    }
    closedir(d);
    *np = n;
    return v;
}

static void freenames(char **v, int n)
{
    while (n > 0) free(v[--n]);
    free(v);
}

/* dir/name into buf, without a doubled /; 0, or -1 if it would be too long */
static int join(char *buf, const char *dir, const char *name)
{
    int n = strlen(dir);
    int slash = n > 0 && dir[n - 1] != '/';
    if (n + slash + strlen(name) >= PATHMAX) return -1;
    strcpy(buf, dir);
    if (slash) buf[n++] = '/';
    strcpy(buf + n, name);
    return 0;
}

/* the last component of a path, ignoring trailing slashes: "a/b/" gives "b" */
static void lastpart(char *buf, const char *path)
{
    int e = strlen(path), s;
    while (e > 1 && path[e - 1] == '/') e--;
    s = e;
    while (s > 0 && path[s - 1] != '/') s--;
    if (e - s >= PATHMAX) e = s + PATHMAX - 1;
    memcpy(buf, path + s, e - s);
    buf[e - s] = 0;
}

static int copyfile(const char *from, const char *to, struct stat *sp)
{
    char buf[1024];
    int in, out, n;
    struct stat ts;
    if (stat(to, &ts) == 0 && ts.st_dev == sp->st_dev && ts.st_ino == sp->st_ino) {
        fprintf(stderr, "cp: %s and %s are the same file\n", from, to);
        return 1;
    }
    if ((in = open(from, O_RDONLY)) < 0) { fprintf(stderr, "cp: %s: %s\n", from, strerror(errno)); return 1; }
    if ((out = open(to, O_WRONLY | O_CREAT | O_TRUNC, sp->st_mode & 0777)) < 0) {
        fprintf(stderr, "cp: %s: %s\n", to, strerror(errno));
        close(in);
        return 1;
    }
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, n) != n) { fprintf(stderr, "cp: %s: write error\n", to); n = -2; break; }
    if (n == -1) fprintf(stderr, "cp: %s: %s\n", from, strerror(errno));
    close(in);
    close(out);
    return n < 0;
}

static int copy(const char *from, const char *to);

static int copydir(const char *from, const char *to, struct stat *sp)
{
    struct stat ts;
    char f[PATHMAX], t[PATHMAX], **v;
    int r = 0, i, nv;
    if (stat(to, &ts) == 0) {
        if (!S_ISDIR(ts.st_mode)) { fprintf(stderr, "cp: %s: Not a directory\n", to); return 1; }
        if (ts.st_dev == sp->st_dev && ts.st_ino == sp->st_ino) {
            fprintf(stderr, "cp: %s and %s are the same file\n", from, to);
            return 1;
        }
    } else {
        if (mkdir(to, (sp->st_mode & 0777) | 0700) < 0) { fprintf(stderr, "cp: %s: %s\n", to, strerror(errno)); return 1; }
        if (stat(to, &ts) < 0) { fprintf(stderr, "cp: %s: %s\n", to, strerror(errno)); return 1; }
    }
    if (topino < 0) { topdev = ts.st_dev; topino = ts.st_ino; }
    if (!(v = names(from, &nv))) { fprintf(stderr, "cp: %s: %s\n", from, strerror(errno)); return 1; }
    for (i = 0; i < nv; i++) {
        if (join(f, from, v[i]) < 0 || join(t, to, v[i]) < 0) {
            fprintf(stderr, "cp: %s/%s: name too long\n", from, v[i]);
            r = 1;
            continue;
        }
        r |= copy(f, t);
    }
    freenames(v, nv);
    return r;
}

static int copy(const char *from, const char *to)
{
    struct stat st;
    if (stat(from, &st) < 0) { fprintf(stderr, "cp: %s: %s\n", from, strerror(errno)); return 1; }
    if (S_ISDIR(st.st_mode)) {
        if (!rflag) { fprintf(stderr, "cp: %s: is a directory\n", from); return 1; }
        if (st.st_dev == topdev && st.st_ino == topino) return 0;      /* the copy itself */
        return copydir(from, to, &st);
    }
    return copyfile(from, to, &st);
}

static int usage(void)
{
    fprintf(stderr, "usage: cp [-r] from to | cp [-r] file ... dir\n");
    return 2;
}

int main(int argc, char **argv)
{
    struct stat st;
    int i, r = 0;
    char to[PATHMAX], base[PATHMAX];
    const char *dir;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'r' || *f == 'R') rflag = 1;
            else { fprintf(stderr, "cp: unknown option -%c\n", *f); return usage(); }
    }
    argv += i - 1;
    argc -= i - 1;
    if (argc < 3) return usage();
    dir = argv[argc - 1];
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        for (i = 1; i < argc - 1; i++) {
            topino = -1;
            lastpart(base, argv[i]);
            if (join(to, dir, base) < 0) { fprintf(stderr, "cp: %s/%s: name too long\n", dir, base); r = 1; continue; }
            r |= copy(argv[i], to);
        }
        return r;
    }
    if (argc != 3) { fprintf(stderr, "cp: %s is not a directory\n", dir); return 1; }
    return copy(argv[1], argv[2]);
}
