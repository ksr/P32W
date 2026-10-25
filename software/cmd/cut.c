/* cut -- select parts of lines:
 *   cut -b list [-n] [file ...]
 *   cut -c list [file ...]
 *   cut -f list [-d delim] [-s] [file ...]
 * A list is numbers and ranges (N, N-M, N-, -M) separated by commas or
 * blanks, numbered from 1; the parts are written in the order they are in
 * the line, each once. Lines may be of any length. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct range { int lo, hi; };   /* hi 0: to the end of the line */
static struct range *rg;
static int nrg;
static int mode;                /* 'b', 'c' or 'f' */
static int delim = '\t', sflag, status;
static char *line;
static int cap;

static void usage(void)
{
    fprintf(stderr, "usage: cut -b list [-n] [file ...]\n"
                    "       cut -c list [file ...]\n"
                    "       cut -f list [-d delim] [-s] [file ...]\n");
    exit(1);
}

static void nomem(void)
{
    fprintf(stderr, "cut: out of memory\n");
    exit(1);
}

static int selected(int k)
{
    int j;
    for (j = 0; j < nrg; j++)
        if (k >= rg[j].lo && (!rg[j].hi || k <= rg[j].hi)) return 1;
    return 0;
}

static int number(const char **pp, const char *list)
{
    const char *p = *pp;
    long n = 0;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + *p++ - '0';
        if (n > 99999999) { fprintf(stderr, "cut: %s: number too large\n", list); exit(1); }
    }
    if (n == 0) { fprintf(stderr, "cut: %s: positions are numbered from 1\n", list); exit(1); }
    *pp = p;
    return (int)n;
}

static void badlist(const char *list)
{
    fprintf(stderr, "cut: invalid list: %s\n", list);
    exit(1);
}

static void parselist(const char *list)
{
    int lo, hi;
    const char *p = list;
    if (!*p) badlist(list);
    while (*p) {
        if (*p == '-') {                    /* -M */
            p++;
            if (*p < '0' || *p > '9') badlist(list);
            lo = 1;
            hi = number(&p, list);
        } else if (*p >= '0' && *p <= '9') {
            hi = lo = number(&p, list);
            if (*p == '-') {                /* N-M or N- (to the end) */
                p++;
                hi = (*p >= '0' && *p <= '9') ? number(&p, list) : 0;
            }
        } else badlist(list);
        if (hi && hi < lo) { fprintf(stderr, "cut: %s: decreasing range\n", list); exit(1); }
        if (!(rg = realloc(rg, (nrg + 1) * sizeof *rg))) nomem();
        rg[nrg].lo = lo;
        rg[nrg++].hi = hi;
        if (*p == ',' || *p == ' ' || *p == '\t') {
            p++;
            if (!*p) badlist(list);
        } else if (*p) badlist(list);
    }
}

static int getln(FILE *f)
{
    int c, n = 0;
    while ((c = getc(f)) != EOF && c != '\n') {
        if (n + 1 >= cap) {
            cap = cap ? 2 * cap : 256;
            if (!(line = realloc(line, cap))) nomem();
        }
        line[n++] = c;
    }
    if (c == EOF && n == 0) return -1;
    return n;
}

static void cutline(int n)
{
    int k, field, start, first = 1;
    if (mode != 'f') {
        for (k = 0; k < n; k++)
            if (selected(k + 1)) putchar(line[k]);
        putchar('\n');
        return;
    }
    if (!memchr(line, delim, n)) {
        if (!sflag) { fwrite(line, 1, n, stdout); putchar('\n'); }
        return;
    }
    field = 1;
    start = 0;
    for (k = 0; k <= n; k++) {
        if (k < n && (unsigned char)line[k] != delim) continue;
        if (selected(field)) {
            if (!first) putchar(delim);
            fwrite(line + start, 1, k - start, stdout);
            first = 0;
        }
        field++;
        start = k + 1;
    }
    putchar('\n');
}

/* an option's argument: the rest of this word or the next word */
static char *optarg_(int argc, char **argv, int *ip, char *p)
{
    if (p[1]) return p + 1;
    if (*ip + 1 >= argc) { fprintf(stderr, "cut: -%c needs an argument\n", *p); usage(); }
    return argv[++*ip];
}

int main(int argc, char **argv)
{
    int i, n;
    char *list = 0, *d = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 'b' || *p == 'c' || *p == 'f') {
                if (list) { fprintf(stderr, "cut: only one of -b, -c and -f may be given\n"); usage(); }
                mode = *p;
                list = optarg_(argc, argv, &i, p);
                break;
            } else if (*p == 'd') {
                d = optarg_(argc, argv, &i, p);
                break;
            } else if (*p == 's') sflag = 1;
            else if (*p == 'n') ;
            else { fprintf(stderr, "cut: unknown option -%c\n", *p); usage(); }
        }
    }
    if (!list) { fprintf(stderr, "cut: one of -b, -c and -f is needed\n"); usage(); }
    if ((d || sflag) && mode != 'f') { fprintf(stderr, "cut: -d and -s go only with -f\n"); usage(); }
    if (d) {
        if (strlen(d) != 1) { fprintf(stderr, "cut: the delimiter must be one character\n"); usage(); }
        delim = (unsigned char)d[0];
    }
    parselist(list);
    if (i == argc) argv[--i] = "-";
    for (; i < argc; i++) {
        FILE *f = stdin;
        if (strcmp(argv[i], "-") && !(f = fopen(argv[i], "r"))) {
            fflush(stdout);
            fprintf(stderr, "cut: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        while ((n = getln(f)) >= 0) cutline(n);
        if (f != stdin) fclose(f);
        else clearerr(stdin);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) { fprintf(stderr, "cut: write error\n"); return 1; }
    return status;
}
