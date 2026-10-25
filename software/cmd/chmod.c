/* chmod -- change the mode of files: chmod [-R] mode file ...
   mode: octal (755), or symbolic: [ugoa]*[+-=][rwxXst]*... or [ugoa]*[+-=][ugo],
   clauses separated by commas (u+x,go-w) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
static char *modestr;
static int octal = -1, mask, Rflag;

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

/* the new mode for a file of mode m under the symbolic mode s; -1 if s is not one */
static int symbolic(const char *s, int m, int isdir)
{
    for (;;) {
        int who = 0, op, bits, k;
        for (; *s == 'u' || *s == 'g' || *s == 'o' || *s == 'a'; s++)
            who |= *s == 'u' ? 04700 : *s == 'g' ? 02070 : *s == 'o' ? 01007 : 07777;
        if (!who) who = 07777 & ~mask;          /* no who: all, except the umask's bits */
        if (*s != '+' && *s != '-' && *s != '=') return -1;
        while (*s == '+' || *s == '-' || *s == '=') {
            op = *s++;
            bits = 0;
            if (*s == 'u' || *s == 'g' || *s == 'o') {         /* copy another class's bits */
                k = *s == 'u' ? (m >> 6) & 7 : *s == 'g' ? (m >> 3) & 7 : m & 7;
                bits = k << 6 | k << 3 | k;
                s++;
            } else
                for (; *s && strchr("rwxXst", *s); s++)
                    switch (*s) {
                    case 'r': bits |= 0444; break;
                    case 'w': bits |= 0222; break;
                    case 'x': bits |= 0111; break;
                    case 'X': if (isdir || (m & 0111)) bits |= 0111; break;
                    case 's': bits |= 06000; break;
                    case 't': bits |= 01000; break;
                    }
            if (op == '+') m |= bits & who;
            else if (op == '-') m &= ~(bits & who);
            else m = (m & ~who) | (bits & who);
        }
        if (!*s) return m;
        if (*s++ != ',') return -1;
    }
}

static int change(const char *path)
{
    struct stat st;
    int m, r = 0;
    if (stat(path, &st) < 0) { fprintf(stderr, "chmod: %s: %s\n", path, strerror(errno)); return 1; }
    m = octal >= 0 ? octal : symbolic(modestr, st.st_mode & 07777, S_ISDIR(st.st_mode));
    if (chmod(path, m) < 0) { fprintf(stderr, "chmod: %s: %s\n", path, strerror(errno)); r = 1; }
    if (Rflag && S_ISDIR(st.st_mode)) {
        char sub[PATHMAX], **v;
        int n = strlen(path), slash = n > 0 && path[n - 1] != '/', i, nv;
        if (!(v = names(path, &nv))) { fprintf(stderr, "chmod: %s: %s\n", path, strerror(errno)); return 1; }
        for (i = 0; i < nv; i++) {
            if (n + slash + strlen(v[i]) >= PATHMAX) {
                fprintf(stderr, "chmod: %s/%s: name too long\n", path, v[i]);
                r = 1;
                continue;
            }
            strcpy(sub, path);
            if (slash) strcat(sub, "/");
            strcat(sub, v[i]);
            r |= change(sub);
        }
        freenames(v, nv);
    }
    return r;
}

int main(int argc, char **argv)
{
    int i = 1, st = 0;
    char *end;
    if (i < argc && !strcmp(argv[i], "-R")) { Rflag = 1; i++; }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    if (argc - i < 2) { fprintf(stderr, "usage: chmod [-R] mode file ...\n"); return 2; }
    modestr = argv[i++];
    mask = umask(0);
    umask(mask);
    if (*modestr >= '0' && *modestr <= '7') {
        octal = strtol(modestr, &end, 8);
        if (*end || octal > 07777) { fprintf(stderr, "chmod: invalid mode: %s\n", modestr); return 2; }
    } else if (symbolic(modestr, 0, 0) < 0) { fprintf(stderr, "chmod: invalid mode: %s\n", modestr); return 2; }
    for (; i < argc; i++) st |= change(argv[i]);
    return st;
}
