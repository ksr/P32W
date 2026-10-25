/* du [-as] [name ...] -- disk space used, in KiB (1024-byte blocks) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
#define NDIRECT 8                       /* the filesystem's inode (fs(5)) */
#define NINDIRECT 256
#define NSEEN 500
static int aflag, sflag, status;
static struct { int dev; unsigned ino; } seen[NSEEN];
static int nseen;

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

/* the blocks a file of this size takes: its data, rounded up to whole
   blocks, and the indirect blocks that point to them */
static long blocks(struct stat *sp)
{
    long n, k, rest, nn = (long)NINDIRECT * NINDIRECT;
    if (!S_ISREG(sp->st_mode) && !S_ISDIR(sp->st_mode)) return 0;
    n = (sp->st_size + 1023) / 1024;
    k = n;
    if (n > NDIRECT) {
        k++;                                            /* the single-indirect block */
        rest = n - NDIRECT - NINDIRECT;
        if (rest > 0) {
            long dbl = rest < nn ? rest : nn;
            k += 1 + (dbl + NINDIRECT - 1) / NINDIRECT;  /* double-indirect, and its blocks */
            rest -= dbl;
            if (rest > 0)                               /* triple */
                k += 1 + (rest + nn - 1) / nn + (rest + NINDIRECT - 1) / NINDIRECT;
        }
    }
    return k;
}

/* a file with several links: 1 if it was counted already (else note it) */
static int counted(struct stat *sp)
{
    int i;
    if (sp->st_nlink < 2 || S_ISDIR(sp->st_mode)) return 0;
    for (i = 0; i < nseen; i++)
        if (seen[i].dev == sp->st_dev && seen[i].ino == sp->st_ino) return 1;
    if (nseen < NSEEN) {
        seen[nseen].dev = sp->st_dev;
        seen[nseen].ino = sp->st_ino;
        nseen++;
    }
    return 0;
}

static long walk(const char *path, int top)
{
    struct stat st;
    long k;
    char sub[PATHMAX], **v;
    int len, slash, i, nv;
    if (stat(path, &st) < 0) { fprintf(stderr, "du: %s: %s\n", path, strerror(errno)); status = 1; return 0; }
    if (counted(&st)) return 0;
    k = blocks(&st);
    if (!S_ISDIR(st.st_mode)) {
        if (top || (aflag && !sflag)) printf("%ld\t%s\n", k, path);
        return k;
    }
    if (!(v = names(path, &nv))) {
        fprintf(stderr, "du: %s: %s\n", path, strerror(errno));
        status = 1;
    } else {
        len = strlen(path);
        slash = len > 0 && path[len - 1] != '/';
        for (i = 0; i < nv; i++) {
            if (len + slash + strlen(v[i]) >= PATHMAX) {
                fprintf(stderr, "du: %s/%s: name too long\n", path, v[i]);
                status = 1;
                continue;
            }
            strcpy(sub, path);
            if (slash) strcat(sub, "/");
            strcat(sub, v[i]);
            k += walk(sub, 0);
        }
        freenames(v, nv);
    }
    if (top || !sflag) printf("%ld\t%s\n", k, path);
    return k;
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'a') aflag = 1;
            else if (*f == 's') sflag = 1;
            else if (*f == 'k') ;                       /* (KiB is the only unit) */
            else { fprintf(stderr, "du: unknown option -%c\nusage: du [-as] [name ...]\n", *f); return 2; }
    }
    if (i == argc) walk(".", 1);
    for (; i < argc; i++) walk(argv[i], 1);
    return status;
}
