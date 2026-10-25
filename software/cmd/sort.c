/* sort -- sort lines: sort [-rnu] [file ...]
 * Lines may be of any length; a last line without a newline gets one.
 * -n compares the numbers as strings of digits, so any length works. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int rflag, nflag, uflag;
static char **lines;
static int n, cap;
static void nomem(void)
{
    fprintf(stderr, "sort: out of memory\n");
    exit(1);
}
/* the number at the start of s: its sign (-1 0 1), its digits less leading zeros */
static int number(const char *s, const char **dp, int *lenp)
{
    int neg = 0, len = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s == '0') s++;
    *dp = s;
    while (s[len] >= '0' && s[len] <= '9') len++;
    *lenp = len;
    if (!len) return 0;
    return neg ? -1 : 1;
}
static int numcmp(const char *x, const char *y)
{
    const char *dx, *dy;
    int lx, ly, sx = number(x, &dx, &lx), sy = number(y, &dy, &ly), r;
    if (sx != sy) return sx < sy ? -1 : 1;
    if (sx == 0) return 0;
    if (lx != ly) r = lx < ly ? -1 : 1;           /* more digits: bigger */
    else { r = strncmp(dx, dy, lx); r = r < 0 ? -1 : r > 0; }
    return sx < 0 ? -r : r;
}
static int cmp(const void *a, const void *b)
{
    const char *x = *(char **)a, *y = *(char **)b;
    int r = nflag ? numcmp(x, y) : 0;
    if (r == 0) r = strcmp(x, y);
    return rflag ? -r : r;
}
static void readall(FILE *f)
{
    char *b;
    int c, len, bcap;
    for (;;) {
        bcap = 64;
        len = 0;
        if (!(b = malloc(bcap))) nomem();
        while ((c = getc(f)) != EOF && c != '\n') {
            if (len + 1 >= bcap) { bcap *= 2; if (!(b = realloc(b, bcap))) nomem(); }
            b[len++] = c;
        }
        if (c == EOF && len == 0) { free(b); return; }
        b[len] = 0;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            if (!(lines = realloc(lines, cap * sizeof *lines))) nomem();
        }
        lines[n++] = b;
    }
}
int main(int argc, char **argv)
{
    int i = 1, k, st = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++)
            if (*p == 'r') rflag = 1; else if (*p == 'n') nflag = 1; else if (*p == 'u') uflag = 1;
            else {
                fprintf(stderr, "sort: unknown option -%c\nusage: sort [-rnu] [file ...]\n", *p);
                return 1;
            }
    }
    if (i == argc) readall(stdin);
    for (; i < argc; i++) {
        FILE *f = strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin;
        if (!f) { fprintf(stderr, "sort: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        readall(f);
        if (f != stdin) fclose(f);
    }
    qsort(lines, n, sizeof *lines, cmp);
    for (k = 0; k < n; k++)
        if (!uflag || k == 0 || strcmp(lines[k], lines[k - 1])) puts(lines[k]);
    return st;
}
