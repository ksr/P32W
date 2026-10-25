/* rm -- remove files: rm [-fir] [--] name ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
static int fflag, iflag, rflag;

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

/* -i: ask on the error output; a reply starting with y or Y is yes */
static int ask(const char *what, const char *path)
{
    char c, first = 0;
    int n, any = 0;
    fprintf(stderr, "rm: remove %s%s? ", what, path);
    fflush(stderr);
    while ((n = read(0, &c, 1)) == 1 && c != '\n')
        if (!any++) first = c;
    return first == 'y' || first == 'Y';
}

/* the last component, ignoring trailing slashes, is . or .. */
static int dotname(const char *path)
{
    int e = strlen(path), s;
    while (e > 1 && path[e - 1] == '/') e--;
    s = e;
    while (s > 0 && path[s - 1] != '/') s--;
    return (e - s == 1 && path[s] == '.') || (e - s == 2 && path[s] == '.' && path[s + 1] == '.');
}

static int rm(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) {
        if (fflag && errno == ENOENT) return 0;
        fprintf(stderr, "rm: %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        char sub[PATHMAX], **v;
        int r = 0, i, nv, n = strlen(path), slash = n > 0 && path[n - 1] != '/';
        if (!rflag) { fprintf(stderr, "rm: %s is a directory\n", path); return 1; }
        if (iflag && !ask("directory ", path)) return 0;
        if (!(v = names(path, &nv))) { fprintf(stderr, "rm: %s: %s\n", path, strerror(errno)); return 1; }
        for (i = 0; i < nv; i++) {
            if (n + slash + strlen(v[i]) >= PATHMAX) {
                fprintf(stderr, "rm: %s/%s: name too long\n", path, v[i]);
                r = 1;
                continue;
            }
            strcpy(sub, path);
            if (slash) strcat(sub, "/");
            strcat(sub, v[i]);
            r |= rm(sub);
        }
        freenames(v, nv);
        if (r) return r;                        /* (it cannot be empty) */
        if (rmdir(path) < 0) { fprintf(stderr, "rm: %s: %s\n", path, strerror(errno)); return 1; }
        return 0;
    }
    if (iflag && !ask("", path)) return 0;
    if (unlink(path) < 0) { fprintf(stderr, "rm: %s: %s\n", path, strerror(errno)); return 1; }
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: rm [-fir] [--] name ...\n");
    return 2;
}

int main(int argc, char **argv)
{
    int i = 1, st = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'f') { fflag = 1; iflag = 0; }
            else if (*f == 'i') { iflag = 1; fflag = 0; }
            else if (*f == 'r' || *f == 'R') rflag = 1;
            else { fprintf(stderr, "rm: unknown option -%c\n", *f); return usage(); }
    }
    if (i == argc) return fflag ? 0 : usage();
    for (; i < argc; i++) {
        if (dotname(argv[i])) { fprintf(stderr, "rm: %s: will not remove . or ..\n", argv[i]); st = 1; continue; }
        st |= rm(argv[i]);
    }
    return st;
}
