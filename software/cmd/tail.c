/* tail -- the last lines: tail [-n [+]count | -count | +count] [file ...]
 * +count starts at that line (1 is the first). Lines may be of any length.
 * With several files each is preceded by a header ==> name <==. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static long n = 10;
static int from;                        /* +count: from line n on */
static void nomem(void)
{
    fprintf(stderr, "tail: out of memory\n");
    exit(1);
}
static int usage(void)
{
    fprintf(stderr, "usage: tail [-n [+]count | -count | +count] [file ...]\n");
    return 1;
}
/* a count: [+]digits */
static int count(const char *s)
{
    long v = 0;
    if (*s == '+') { from = 1; s++; }
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 100000000) return 0;
        v = v * 10 + *s - '0';
    }
    n = v;
    return 1;
}
/* the next line of f with its newline (if it has one), in a new string; 0 at the end */
static char *getln(FILE *f)
{
    int c, len = 0, cap = 64;
    char *b = malloc(cap);
    if (!b) nomem();
    while ((c = getc(f)) != EOF) {
        if (len + 2 >= cap) { cap *= 2; if (!(b = realloc(b, cap))) nomem(); }
        b[len++] = c;
        if (c == '\n') break;
    }
    if (len == 0) { free(b); return 0; }
    b[len] = 0;
    return b;
}
static void tail(FILE *f)
{
    char **ring = 0, *s;
    long cap = 0, have = 0, k;
    int c;
    if (from) {                         /* skip n-1 lines, copy the rest */
        for (k = 1; k < n && (c = getc(f)) != EOF; ) if (c == '\n') k++;
        while ((c = getc(f)) != EOF) putchar(c);
        return;
    }
    if (n == 0) return;
    while ((s = getln(f)) != 0) {
        if (have < n) {                 /* still filling: grow up to n */
            if (have == cap) {
                cap = cap ? cap * 2 : 64;
                if (cap > n) cap = n;
                if (!(ring = realloc(ring, cap * sizeof *ring))) nomem();
            }
            ring[have] = s;
        } else {
            free(ring[have % n]);
            ring[have % n] = s;
        }
        have++;
    }
    for (k = have > n ? have - n : 0; k < have; k++) {
        fputs(ring[k % n], stdout);
        free(ring[k % n]);
    }
    free(ring);
}
int main(int argc, char **argv)
{
    int i = 1, st = 0, many, first = 1;
    FILE *f;
    if (i < argc && !strcmp(argv[i], "-n")) {
        if (i + 1 >= argc || !count(argv[i + 1])) return usage();
        i += 2;
    } else if (i < argc && !strncmp(argv[i], "-n", 2)) {
        if (!count(argv[i] + 2)) return usage();
        i++;
    } else if (i < argc && argv[i][0] == '+') {
        if (!count(argv[i])) return usage();
        i++;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "--")) {
        if (argv[i][1] == '+' || !count(argv[i] + 1)) return usage();
        i++;
    }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    many = argc - i > 1;
    if (i == argc) argv[--i] = "-";     /* no file: the standard input */
    for (; i < argc; i++) {
        f = strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin;
        if (!f) { fprintf(stderr, "tail: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        if (many) { printf("%s==> %s <==\n", first ? "" : "\n", f == stdin ? "standard input" : argv[i]); first = 0; }
        tail(f);
        if (f != stdin) fclose(f);
    }
    return st;
}
