/* comm -- lines in common to two sorted files:  comm [-123] file1 file2
 * Three columns: lines only in file1, lines only in file2, lines in both;
 * each column is indented by a tab for each column before it that is
 * written. -1, -2 and -3 leave those columns out. - is the standard
 * input. Lines may be of any length. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct buf { char *s; int cap; };
static int show[4] = { 0, 1, 1, 1 };

static void nomem(void)
{
    fprintf(stderr, "comm: out of memory\n");
    exit(1);
}

static int getln(FILE *f, struct buf *b)
{
    int c, n = 0;
    if (!f) return 0;
    for (;;) {
        if (n + 1 >= b->cap) {
            b->cap = b->cap ? 2 * b->cap : 256;
            if (!(b->s = realloc(b->s, b->cap))) nomem();
        }
        if ((c = getc(f)) == EOF || c == '\n') break;
        b->s[n++] = c;
    }
    b->s[n] = 0;
    return !(c == EOF && n == 0);
}

static void put(int col, const char *s)
{
    int k;
    if (!show[col]) return;
    for (k = 1; k < col; k++) if (show[k]) putchar('\t');
    fputs(s, stdout);
    putchar('\n');
}

static FILE *open1(const char *name)
{
    FILE *f;
    if (!strcmp(name, "-")) return stdin;
    if (!(f = fopen(name, "r"))) {
        fprintf(stderr, "comm: %s: %s\n", name, strerror(errno));
        exit(1);
    }
    return f;
}

static void usage(void)
{
    fprintf(stderr, "usage: comm [-123] file1 file2\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int i, h1, h2, c;
    struct buf a, b;
    FILE *f1, *f2;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++)
            if (*p >= '1' && *p <= '3') show[*p - '0'] = 0;
            else { fprintf(stderr, "comm: unknown option -%c\n", *p); usage(); }
    }
    if (argc - i != 2) usage();
    f1 = open1(argv[i]);
    f2 = open1(argv[i + 1]);
    a.s = b.s = 0;
    a.cap = b.cap = 0;
    h1 = getln(f1, &a);
    h2 = getln(f2, &b);
    while (h1 || h2) {
        if (!h2) c = -1;
        else if (!h1) c = 1;
        else c = strcmp(a.s, b.s);
        if (c < 0) { put(1, a.s); h1 = getln(f1, &a); }
        else if (c > 0) { put(2, b.s); h2 = getln(f2, &b); }
        else {
            put(3, a.s);
            h1 = getln(f1, &a);
            h2 = getln(f2, &b);
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) { fprintf(stderr, "comm: write error\n"); return 1; }
    return 0;
}
