/* join -- join the lines of two sorted files on a field:
 *   join [-a 1|2] [-v 1|2] [-e s] [-o list] [-t c] [-1 f] [-2 f] file1 file2
 * For each pair of lines (one from each file) with the same join field
 * (field 1 by default) one line is written: the join field, the rest of
 * file1's line, the rest of file2's. Fields are separated by blanks
 * (leading blanks ignored, written as one space) or, with -t c, by each
 * c. The files must be sorted on the join fields (as by sort). Lines may
 * be of any length; a run of lines with the same field may be as long as
 * memory allows. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct line {
    char *text;                 /* the line, its fields NUL-terminated in place */
    char **f;
    int nf;
};
struct group {                  /* the lines with the same join field */
    struct line *l;
    int n, cap;
};
struct input {
    FILE *fp;
    int jf;                     /* the join field, from 0 */
    struct line next;           /* the line after the group */
    int have;
    struct group g;
};
struct ofield { int file, field; };     /* file 0: the join field */

static int tab = -1;            /* -t: the separator, or -1 for blanks */
static int aflag[3], vflag[3];
static char *estr;
static struct ofield *olist;
static int nolist;
static struct input in[3];

static void nomem(void)
{
    fprintf(stderr, "join: out of memory\n");
    exit(1);
}

static void usage(void)
{
    fprintf(stderr, "usage: join [-a 1|2] [-v 1|2] [-e s] [-o list] [-t c] [-1 f] [-2 f] file1 file2\n");
    exit(1);
}

/* read a line and split it into fields; 0 at the end of the file */
static int readline(FILE *fp, struct line *l)
{
    int c, n = 0, cap = 64, k, nf;
    char *s = malloc(cap), *p;
    if (!s) nomem();
    while ((c = getc(fp)) != EOF && c != '\n') {
        if (n + 1 >= cap) { cap *= 2; if (!(s = realloc(s, cap))) nomem(); }
        s[n++] = c;
    }
    if (c == EOF && n == 0) { free(s); return 0; }
    s[n] = 0;
    nf = 1;                                     /* (at most this many fields) */
    for (k = 0; k < n; k++) if (tab < 0 ? s[k] == ' ' || s[k] == '\t' : (unsigned char)s[k] == tab) nf++;
    if (!(l->f = malloc(nf * sizeof *l->f))) nomem();
    l->text = s;
    l->nf = 0;
    p = s;
    if (tab >= 0) {
        if (!n) return 1;                       /* an empty line: no fields */
        for (;;) {
            l->f[l->nf++] = p;
            while (*p && (unsigned char)*p != tab) p++;
            if (!*p) break;
            *p++ = 0;
        }
        return 1;
    }
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        l->f[l->nf++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (!*p) break;
        *p++ = 0;
    }
    return 1;
}

static void freeline(struct line *l)
{
    free(l->text);
    free(l->f);
}

static char *field(struct line *l, int k)
{
    return k < l->nf ? l->f[k] : "";
}

static char *key(struct input *ip, struct line *l)
{
    return field(l, ip->jf);
}

/* the next group of lines with the same join field: 0 at the end */
static int nextgroup(struct input *ip)
{
    int k;
    for (k = 0; k < ip->g.n; k++) freeline(&ip->g.l[k]);
    ip->g.n = 0;
    if (!ip->have) return 0;
    do {
        if (ip->g.n == ip->g.cap) {
            ip->g.cap = ip->g.cap ? 2 * ip->g.cap : 8;
            if (!(ip->g.l = realloc(ip->g.l, ip->g.cap * sizeof *ip->g.l))) nomem();
        }
        ip->g.l[ip->g.n++] = ip->next;
        ip->have = readline(ip->fp, &ip->next);
    } while (ip->have && !strcmp(key(ip, &ip->next), key(ip, &ip->g.l[0])));
    return 1;
}

static int first;

static void put(const char *s)
{
    if (!first) putchar(tab >= 0 ? tab : ' ');
    first = 0;
    fputs(!*s && estr ? estr : s, stdout);
}

/* one output line: a pair of lines, or one line (a or b 0) not paired */
static void output(struct line *a, struct line *b)
{
    int k, j;
    first = 1;
    if (nolist) {
        for (k = 0; k < nolist; k++) {
            struct line *l = olist[k].file == 1 ? a : b;
            if (olist[k].file == 0)
                put(a ? key(&in[1], a) : key(&in[2], b));
            else
                put(l ? field(l, olist[k].field) : "");
        }
    } else {
        put(a ? key(&in[1], a) : key(&in[2], b));
        for (j = 1; j <= 2; j++) {
            struct line *l = j == 1 ? a : b;
            if (!l) continue;
            for (k = 0; k < l->nf; k++)
                if (k != in[j].jf) put(l->f[k]);
        }
    }
    putchar('\n');
}

static void unpaired(int j)
{
    int k;
    if (!aflag[j] && !vflag[j]) return;
    for (k = 0; k < in[j].g.n; k++)
        output(j == 1 ? &in[1].g.l[k] : 0, j == 2 ? &in[2].g.l[k] : 0);
}

static int number(const char *s, const char *what)
{
    int n = 0;
    const char *p = s;
    while (*p >= '0' && *p <= '9' && n < 100000) n = n * 10 + *p++ - '0';
    if (*p || p == s || n < 1) {
        fprintf(stderr, "join: bad %s: %s\n", what, s);
        exit(1);
    }
    return n;
}

static void parseo(const char *s)
{
    const char *p = s;
    if (!(olist = realloc(olist, (nolist + strlen(s) / 2 + 2) * sizeof *olist))) nomem();
    while (*p) {
        int f = 0, n = 0;
        if (p[0] == '0' && (!p[1] || p[1] == ',' || p[1] == ' ' || p[1] == '\t')) p++;
        else if ((p[0] == '1' || p[0] == '2') && p[1] == '.' && p[2] >= '1' && p[2] <= '9') {
            f = p[0] - '0';
            p += 2;
            while (*p >= '0' && *p <= '9' && n < 100000) n = n * 10 + *p++ - '0';
        } else break;
        olist[nolist].file = f;
        olist[nolist++].field = n - 1;
        if (!*p) return;
        if (*p != ',' && *p != ' ' && *p != '\t') break;
        p++;
        if (!*p) break;
    }
    fprintf(stderr, "join: bad -o list: %s\n", s);
    exit(1);
}

static FILE *open1(const char *name)
{
    FILE *f;
    if (!strcmp(name, "-")) return stdin;
    if (!(f = fopen(name, "r"))) {
        fprintf(stderr, "join: %s: %s\n", name, strerror(errno));
        exit(1);
    }
    return f;
}

int main(int argc, char **argv)
{
    int i, c, j;
    in[1].jf = in[2].jf = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char opt = argv[i][1], *arg;
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strchr("av12jeot", opt)) { fprintf(stderr, "join: unknown option -%c\n", opt); usage(); }
        if (argv[i][2]) arg = argv[i] + 2;
        else if (i + 1 < argc) arg = argv[++i];
        else { fprintf(stderr, "join: -%c needs an argument\n", opt); usage(); }
        switch (opt) {
        case 'a':
        case 'v':
            if (strcmp(arg, "1") && strcmp(arg, "2")) {
                fprintf(stderr, "join: -%c takes 1 or 2, not %s\n", opt, arg);
                usage();
            }
            if (opt == 'a') aflag[arg[0] - '0'] = 1;
            else vflag[arg[0] - '0'] = 1;
            break;
        case '1': in[1].jf = number(arg, "field number") - 1; break;
        case '2': in[2].jf = number(arg, "field number") - 1; break;
        case 'j': in[1].jf = in[2].jf = number(arg, "field number") - 1; break;
        case 'e': estr = arg; break;
        case 'o': parseo(arg); break;
        case 't':
            if (strlen(arg) != 1) { fprintf(stderr, "join: -t takes one character\n"); usage(); }
            tab = (unsigned char)arg[0];
            break;
        }
    }
    if (argc - i != 2) usage();
    if (!strcmp(argv[i], "-") && !strcmp(argv[i + 1], "-")) {
        fprintf(stderr, "join: both files cannot be the standard input\n");
        exit(1);
    }
    for (j = 1; j <= 2; j++) {
        in[j].fp = open1(argv[i + j - 1]);
        in[j].have = readline(in[j].fp, &in[j].next);
    }
    nextgroup(&in[1]);
    nextgroup(&in[2]);
    while (in[1].g.n && in[2].g.n) {
        c = strcmp(key(&in[1], &in[1].g.l[0]), key(&in[2], &in[2].g.l[0]));
        if (c < 0) { unpaired(1); nextgroup(&in[1]); }
        else if (c > 0) { unpaired(2); nextgroup(&in[2]); }
        else {
            if (!vflag[1] && !vflag[2]) {
                int k, m;
                for (k = 0; k < in[1].g.n; k++)
                    for (m = 0; m < in[2].g.n; m++)
                        output(&in[1].g.l[k], &in[2].g.l[m]);
            }
            nextgroup(&in[1]);
            nextgroup(&in[2]);
        }
    }
    for (j = 1; j <= 2; j++)
        while (in[j].g.n) { unpaired(j); nextgroup(&in[j]); }
    if (fflush(stdout) != 0 || ferror(stdout)) { fprintf(stderr, "join: write error\n"); return 1; }
    return 0;
}
