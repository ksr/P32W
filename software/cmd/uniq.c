/* uniq -- drop repeated adjacent lines: uniq [-cdu] [input [output]]
 * Lines may be of any length; a last line without a newline gets one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int cflag, dflag, uflag;
static FILE *out;
struct buf { char *s; int cap; };
static void nomem(void)
{
    fprintf(stderr, "uniq: out of memory\n");
    exit(1);
}
/* read the next line (without its newline) into b; 0 at the end */
static int getln(FILE *f, struct buf *b)
{
    int c, n = 0;
    if (!b->s) { b->cap = 256; if (!(b->s = malloc(b->cap))) nomem(); }
    while ((c = getc(f)) != EOF && c != '\n') {
        if (n + 1 >= b->cap) { b->cap *= 2; if (!(b->s = realloc(b->s, b->cap))) nomem(); }
        b->s[n++] = c;
    }
    if (c == EOF && n == 0) return 0;
    b->s[n] = 0;
    return 1;
}
static void put(const char *s, int count)
{
    if (dflag && count == 1) return;
    if (uflag && count > 1) return;
    if (cflag) fprintf(out, "%4d ", count);
    fputs(s, out);
    putc('\n', out);
}
int main(int argc, char **argv)
{
    struct buf a, b, t;
    FILE *in = stdin;
    int i = 1, count = 0, have = 0;
    out = stdout;
    a.s = b.s = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        for (p = argv[i] + 1; *p; p++)
            if (*p == 'c') cflag = 1; else if (*p == 'd') dflag = 1; else if (*p == 'u') uflag = 1;
            else {
                fprintf(stderr, "uniq: unknown option -%c\nusage: uniq [-cdu] [input [output]]\n", *p);
                return 1;
            }
    }
    if (argc - i > 2) { fprintf(stderr, "usage: uniq [-cdu] [input [output]]\n"); return 1; }
    if (i < argc && strcmp(argv[i], "-") && !(in = fopen(argv[i], "r"))) {
        fprintf(stderr, "uniq: %s: %s\n", argv[i], strerror(errno));
        return 1;
    }
    if (i + 1 < argc && !(out = fopen(argv[i + 1], "w"))) {
        fprintf(stderr, "uniq: %s: %s\n", argv[i + 1], strerror(errno));
        return 1;
    }
    while (getln(in, &b)) {
        if (have && !strcmp(a.s, b.s)) { count++; continue; }
        if (have) put(a.s, count);
        t = a; a = b; b = t;            /* keep this line in a */
        have = 1;
        count = 1;
    }
    if (have) put(a.s, count);
    if (fflush(out) != 0 || ferror(out)) { fprintf(stderr, "uniq: write error\n"); return 1; }
    return 0;
}
