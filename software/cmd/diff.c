/* diff -- differences between two files, or two directory trees:
 *   diff [-u | -U n | -c | -C n | -e] [-b] [-w] [-i] [-q] [-r] [-N] file1 file2
 * The lines are hashed to numbers (equal lines, as -b -w -i see them, get the
 * same number) and compared with Myers' O(ND) algorithm in linear space (the
 * "middle snake", divide and conquer), so the memory used is a few words per
 * line. Output: normal (3c3, < and >), unified (-u), context (-c) or an ed
 * script (-e). Status: 0 the same, 1 different, 2 trouble. - is stdin. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#define NORMAL 0
#define UNIFIED 1
#define CONTEXT 2
#define EDSCRIPT 3

static int format = NORMAL, ctx = 3;
static int bflag, wflag, iflag, qflag, rflag, nflag;
static char opts[128];                  /* the options as given, for "diff -r" lines */
static int status;                      /* the largest status so far */

typedef struct {
    char *name;                         /* as the user named it */
    char *buf;                          /* the whole text */
    char **line;                        /* each line's start */
    int *len;                           /* and length (without the newline) */
    int *id;                            /* and its number */
    char *chg;                          /* 1: not in the common subsequence */
    int n;                              /* lines */
    int noeol;                          /* the last line has no newline */
    long mtime;
} File;

static File f1, f2;

static void nomem(void)
{
    fprintf(stderr, "diff: out of memory\n");
    exit(2);
}

static void *xmalloc(int n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p) nomem();
    return p;
}

/* ---- reading ---- */

/* read a whole file into f; 0 if it could not be read */
static int readfile(File *f, char *name, int absent)
{
    FILE *fp;
    int cap = 1024, n = 0, k, i, nl;
    struct stat st;
    memset(f, 0, sizeof *f);
    f->name = name;
    f->buf = xmalloc(cap);
    if (absent) {
        f->mtime = 0;
    } else {
        if (strcmp(name, "-")) {
            if (!(fp = fopen(name, "r"))) {
                fprintf(stderr, "diff: %s: %s\n", name, strerror(errno));
                return 0;
            }
        } else
            fp = stdin;
        if (fp != stdin && fstat(fileno(fp), &st) == 0) f->mtime = st.st_mtime;
        else f->mtime = time((time_t *)0);
        for (;;) {
            if (n == cap) {
                cap *= 2;
                if (!(f->buf = realloc(f->buf, cap))) nomem();
            }
            k = fread(f->buf + n, 1, cap - n, fp);
            if (k <= 0) break;
            n += k;
        }
        if (fp != stdin) fclose(fp);
    }
    if (n < cap && (f->buf = realloc(f->buf, n > 0 ? n : 1)) == 0) nomem();
    nl = 0;
    for (i = 0; i < n; i++)
        if (f->buf[i] == '\n') nl++;
    f->noeol = n > 0 && f->buf[n - 1] != '\n';
    f->n = nl + f->noeol;
    f->line = xmalloc((f->n + 1) * sizeof(char *));
    f->len = xmalloc((f->n + 1) * sizeof(int));
    f->id = xmalloc((f->n + 1) * sizeof(int));
    f->chg = xmalloc(f->n + 1);
    memset(f->chg, 0, f->n + 1);
    k = 0;
    i = 0;
    while (i < n) {
        int s = i;
        while (i < n && f->buf[i] != '\n') i++;
        f->line[k] = f->buf + s;
        f->len[k++] = i - s;
        i++;
    }
    return 1;
}

static void freefile(File *f)
{
    free(f->buf); free(f->line); free(f->len); free(f->id); free(f->chg);
}

static int binary(File *f)
{
    int i, j;
    for (i = 0; i < f->n; i++)
        for (j = 0; j < f->len[i]; j++)
            if (f->line[i][j] == 0) return 1;
    return 0;
}

/* ---- line numbers ---- */

static char *nb1, *nb2;                 /* scratch for normalised lines */
static int nbcap;

/* the line as -b -w -i see it, into out; its length */
static int normal(char *s, int n, char *out)
{
    int i, k = 0, sp = 0;
    for (i = 0; i < n; i++) {
        int c = s[i] & 255;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            if (wflag) continue;
            if (bflag) { sp = 1; continue; }
        }
        if (sp) { out[k++] = ' '; sp = 0; }
        out[k++] = iflag ? tolower(c) : c;
    }
    return k;
}

typedef struct { File *f; int i; } Rep;
static Rep *reps;                       /* a line of each number */
static int *htab, hsize, nids;

static unsigned hashof(char *s, int n)
{
    unsigned h = 5381;
    while (n-- > 0) h = h * 33 + (*s++ & 255);
    return h;
}

static void need(int n)
{
    if (n + 1 > nbcap) {
        nbcap = n + 64;
        free(nb1); free(nb2);
        nb1 = xmalloc(nbcap);
        nb2 = xmalloc(nbcap);
    }
}

/* give every line of f its number */
static void number(File *f)
{
    int i, n, m;
    unsigned h;
    char *s;
    for (i = 0; i < f->n; i++) {
        int eol = f->noeol && i == f->n - 1;
        if (bflag || wflag || iflag) {
            need(f->len[i]);
            n = normal(f->line[i], f->len[i], nb1);
            s = nb1;
        } else {
            n = f->len[i];
            s = f->line[i];
        }
        h = hashof(s, n) + eol;
        for (h &= hsize - 1;; h = (h + 1) & (hsize - 1)) {
            int r = htab[h];
            Rep *p;
            if (r < 0) {
                reps[nids].f = f;
                reps[nids].i = i;
                htab[h] = nids;
                f->id[i] = nids++;
                break;
            }
            p = &reps[r];
            if ((p->f->noeol && p->i == p->f->n - 1) != eol) continue;
            if (bflag || wflag || iflag) {
                need(p->f->len[p->i]);
                m = normal(p->f->line[p->i], p->f->len[p->i], nb2);
                if (m == n && !memcmp(nb2, s, n)) { f->id[i] = r; break; }
            } else if (p->f->len[p->i] == n && !memcmp(p->f->line[p->i], s, n)) {
                f->id[i] = r;
                break;
            }
        }
    }
}

static void numberboth(void)
{
    int i, t = f1.n + f2.n;
    for (hsize = 64; hsize < 2 * t; hsize *= 2) ;
    htab = xmalloc(hsize * sizeof(int));
    for (i = 0; i < hsize; i++) htab[i] = -1;
    reps = xmalloc((t + 1) * sizeof(Rep));
    nids = 0;
    number(&f1);
    number(&f2);
    free(htab);
    free(reps);
}

/* ---- the comparison (Myers 1986, the linear-space version) ---- */

static int *A, *B;                      /* the two sequences of numbers */
static char *ca, *cb;                   /* 1: not in the common subsequence */
static int *v1, *v2;
static int *stamp, gen;                 /* (numbers seen in a range) */

static void compare(int x0, int x1, int y0, int y1);

/* the middle snake of A[x0..x1) and B[y0..y1) (no common head or tail):
 * split there and compare the two halves */
static void bisect(int x0, int x1, int y0, int y1)
{
    int n = x1 - x0, m = y1 - y0, maxd = (n + m + 1) / 2, off = maxd, vlen = 2 * maxd + 2;
    int delta = n - m, front = delta & 1, d, k1, k2, x, y, xx, i;
    int k1s = 0, k1e = 0, k2s = 0, k2e = 0, *a = A + x0, *b = B + y0;
    for (i = 0; i < vlen; i++) v1[i] = v2[i] = -1;
    v1[off + 1] = 0;
    v2[off + 1] = 0;
    for (d = 0; d < maxd; d++) {
        for (k1 = -d + k1s; k1 <= d - k1e; k1 += 2) {
            int ko = off + k1;
            if (k1 == -d || (k1 != d && v1[ko - 1] < v1[ko + 1])) x = v1[ko + 1];
            else x = v1[ko - 1] + 1;
            y = x - k1;
            while (x < n && y < m && a[x] == b[y]) { x++; y++; }
            v1[ko] = x;
            if (x > n) k1e += 2;
            else if (y > m) k1s += 2;
            else if (front) {
                int k2o = off + delta - k1;
                if (k2o >= 0 && k2o < vlen && v2[k2o] != -1 && x >= n - v2[k2o]) {
                    compare(x0, x0 + x, y0, y0 + y);
                    compare(x0 + x, x1, y0 + y, y1);
                    return;
                }
            }
        }
        for (k2 = -d + k2s; k2 <= d - k2e; k2 += 2) {
            int ko = off + k2;
            if (k2 == -d || (k2 != d && v2[ko - 1] < v2[ko + 1])) xx = v2[ko + 1];
            else xx = v2[ko - 1] + 1;
            y = xx - k2;
            while (xx < n && y < m && a[n - xx - 1] == b[m - y - 1]) { xx++; y++; }
            v2[ko] = xx;
            if (xx > n) k2e += 2;
            else if (y > m) k2s += 2;
            else if (!front) {
                int k1o = off + delta - k2;
                if (k1o >= 0 && k1o < vlen && v1[k1o] != -1) {
                    x = v1[k1o];
                    y = x - (k1o - off);
                    if (x >= n - xx) {
                        compare(x0, x0 + x, y0, y0 + y);
                        compare(x0 + x, x1, y0 + y, y1);
                        return;
                    }
                }
            }
        }
    }
    for (i = x0; i < x1; i++) ca[i] = 1;      /* (nothing in common) */
    for (i = y0; i < y1; i++) cb[i] = 1;
}

static void compare(int x0, int x1, int y0, int y1)
{
    int i;
    while (x0 < x1 && y0 < y1 && A[x0] == B[y0]) { x0++; y0++; }
    while (x0 < x1 && y0 < y1 && A[x1 - 1] == B[y1 - 1]) { x1--; y1--; }
    if (x0 == x1) { while (y0 < y1) cb[y0++] = 1; return; }
    if (y0 == y1) { while (x0 < x1) ca[x0++] = 1; return; }
    for (gen++, i = y0; i < y1; i++) stamp[B[i]] = gen;
    for (i = x0; i < x1 && stamp[A[i]] != gen; i++) ;
    if (i == x1) {                      /* nothing in common: quickly */
        while (x0 < x1) ca[x0++] = 1;
        while (y0 < y1) cb[y0++] = 1;
        return;
    }
    bisect(x0, x1, y0, y1);
}

/* ---- the changes ---- */

typedef struct { int a0, a1, b0, b1; } Chg;    /* lines [a0,a1) became [b0,b1) */
static Chg *chgs;
static int nchg;

static void changes(void)
{
    int i = 0, j = 0;
    nchg = 0;
    chgs = xmalloc((f1.n + f2.n + 1) * sizeof(Chg));
    while (i < f1.n || j < f2.n) {
        Chg *c;
        if (i < f1.n && j < f2.n && !f1.chg[i] && !f2.chg[j]) { i++; j++; continue; }
        c = &chgs[nchg++];
        c->a0 = i; c->b0 = j;
        while (i < f1.n && f1.chg[i]) i++;
        while (j < f2.n && f2.chg[j]) j++;
        c->a1 = i; c->b1 = j;
    }
}

/* ---- output ---- */

static void putline(File *f, int i, char *pfx)
{
    fputs(pfx, stdout);
    fwrite(f->line[i], 1, f->len[i], stdout);
    putchar('\n');
    if (f->noeol && i == f->n - 1) puts("\\ No newline at end of file");
}

static void range(int a, int b, int sep)        /* lines a..b, 1-based */
{
    if (a >= b) printf("%d", b);
    else printf("%d%c%d", a, sep, b);
}

static void normalout(void)
{
    int k, i;
    for (k = 0; k < nchg; k++) {
        Chg *c = &chgs[k];
        if (c->a0 == c->a1) {
            printf("%d", c->a0);
            putchar('a');
        } else {
            range(c->a0 + 1, c->a1, ',');
            putchar(c->b0 == c->b1 ? 'd' : 'c');
        }
        if (c->b0 == c->b1) printf("%d\n", c->b0);
        else { range(c->b0 + 1, c->b1, ','); putchar('\n'); }
        for (i = c->a0; i < c->a1; i++) putline(&f1, i, "< ");
        if (c->a0 < c->a1 && c->b0 < c->b1) puts("---");
        for (i = c->b0; i < c->b1; i++) putline(&f2, i, "> ");
    }
}

static void edout(void)
{
    int k, i, dots;
    for (k = nchg - 1; k >= 0; k--) {
        Chg *c = &chgs[k];
        if (c->a0 == c->a1) printf("%da\n", c->a0);
        else {
            range(c->a0 + 1, c->a1, ',');
            printf("%c\n", c->b0 == c->b1 ? 'd' : 'c');
        }
        if (c->b0 == c->b1) continue;
        dots = 0;
        for (i = c->b0; i < c->b1; i++) {
            if (f2.len[i] == 1 && f2.line[i][0] == '.') { puts(".."); dots = 1; }
            else { fwrite(f2.line[i], 1, f2.len[i], stdout); putchar('\n'); }
        }
        puts(".");
        if (dots)                       /* lines of one dot went in as two */
            for (i = c->b0; i < c->b1; i++)
                if (f2.len[i] == 1 && f2.line[i][0] == '.')
                    printf("%ds/^\\.//\n", c->a0 + 1 + i - c->b0);
    }
}

static void header(char *mark, File *f)
{
    long t = f->mtime;
    struct tm *tm = localtime((time_t *)&t);
    printf("%s %s\t%d-%02d-%02d %02d:%02d:%02d\n", mark, f->name, tm->tm_year + 1900,
           tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* the changes c[k..e) whose context touches: the end of the group */
static int group(int k)
{
    int e = k + 1;
    while (e < nchg && chgs[e].a0 - chgs[e - 1].a1 <= 2 * ctx) e++;
    return e;
}

static void unifiedout(void)
{
    int k, e, j, i, xs, xe, ys, ye;
    header("---", &f1);
    header("+++", &f2);
    for (k = 0; k < nchg; k = e) {
        e = group(k);
        xs = chgs[k].a0 - ctx; if (xs < 0) xs = 0;
        ys = chgs[k].b0 - (chgs[k].a0 - xs);
        xe = chgs[e - 1].a1 + ctx; if (xe > f1.n) xe = f1.n;
        ye = chgs[e - 1].b1 + (xe - chgs[e - 1].a1);
        printf("@@ -");
        if (xe - xs == 1) printf("%d", xs + 1); else printf("%d,%d", xe > xs ? xs + 1 : xs, xe - xs);
        printf(" +");
        if (ye - ys == 1) printf("%d", ys + 1); else printf("%d,%d", ye > ys ? ys + 1 : ys, ye - ys);
        printf(" @@\n");
        i = xs;
        for (j = k; j < e; j++) {
            Chg *c = &chgs[j];
            for (; i < c->a0; i++) putline(&f1, i, " ");
            for (; i < c->a1; i++) putline(&f1, i, "-");
            for (ys = c->b0; ys < c->b1; ys++) putline(&f2, ys, "+");
        }
        for (; i < xe; i++) putline(&f1, i, " ");
    }
}

static void contextout(void)
{
    int k, e, j, i, xs, xe, ys, ye, any;
    header("***", &f1);
    header("---", &f2);
    for (k = 0; k < nchg; k = e) {
        e = group(k);
        xs = chgs[k].a0 - ctx; if (xs < 0) xs = 0;
        ys = chgs[k].b0 - (chgs[k].a0 - xs);
        xe = chgs[e - 1].a1 + ctx; if (xe > f1.n) xe = f1.n;
        ye = chgs[e - 1].b1 + (xe - chgs[e - 1].a1);
        puts("***************");
        printf("*** "); range(xs + 1, xe, ','); puts(" ****");
        for (any = 0, j = k; j < e; j++) if (chgs[j].a1 > chgs[j].a0) any = 1;
        if (any) {
            i = xs;
            for (j = k; j < e; j++) {
                Chg *c = &chgs[j];
                for (; i < c->a0; i++) putline(&f1, i, "  ");
                for (; i < c->a1; i++) putline(&f1, i, c->b1 > c->b0 ? "! " : "- ");
            }
            for (; i < xe; i++) putline(&f1, i, "  ");
        }
        printf("--- "); range(ys + 1, ye, ','); puts(" ----");
        for (any = 0, j = k; j < e; j++) if (chgs[j].b1 > chgs[j].b0) any = 1;
        if (any) {
            i = ys;
            for (j = k; j < e; j++) {
                Chg *c = &chgs[j];
                for (; i < c->b0; i++) putline(&f2, i, "  ");
                for (; i < c->b1; i++) putline(&f2, i, c->a1 > c->a0 ? "! " : "+ ");
            }
            for (; i < ye; i++) putline(&f2, i, "  ");
        }
    }
}

/* ---- two files ---- */

/* compare files p and q (absent: -N's empty files); 0 1 or 2 */
static int difffiles(char *p, char *q, int pabsent, int qabsent, int recursive)
{
    int r = 0, i, t, na, nb, *ma, *mb;
    if (!readfile(&f1, p, pabsent)) { free(f1.buf); return 2; }
    if (!readfile(&f2, q, qabsent)) { freefile(&f1); free(f2.buf); return 2; }
    if (binary(&f1) || binary(&f2)) {
        if (f1.n != f2.n) r = 1;
        for (i = 0; !r && i < f1.n; i++)
            if (f1.len[i] != f2.len[i] || memcmp(f1.line[i], f2.line[i], f1.len[i])) r = 1;
        if (r || f1.noeol != f2.noeol) { printf("Binary files %s and %s differ\n", p, q); r = 1; }
        goto done;
    }
    numberboth();
    /* A line found in only one file is certainly a change: compare the
     * rest (as GNU diff does). The shortest edit script is the same. */
    stamp = xmalloc((nids + 1) * sizeof(int));
    memset(stamp, 0, (nids + 1) * sizeof(int));
    for (i = 0; i < f1.n; i++) stamp[f1.id[i]] |= 1;
    for (i = 0; i < f2.n; i++) stamp[f2.id[i]] |= 2;
    A = xmalloc((f1.n + 1) * sizeof(int));
    B = xmalloc((f2.n + 1) * sizeof(int));
    ma = xmalloc((f1.n + 1) * sizeof(int));
    mb = xmalloc((f2.n + 1) * sizeof(int));
    for (na = i = 0; i < f1.n; i++)
        if (stamp[f1.id[i]] == 3) { ma[na] = i; A[na++] = f1.id[i]; }
        else f1.chg[i] = 1;
    for (nb = i = 0; i < f2.n; i++)
        if (stamp[f2.id[i]] == 3) { mb[nb] = i; B[nb++] = f2.id[i]; }
        else f2.chg[i] = 1;
    ca = xmalloc(na + 1);
    cb = xmalloc(nb + 1);
    memset(ca, 0, na + 1);
    memset(cb, 0, nb + 1);
    t = na + nb + 4;
    v1 = xmalloc(t * sizeof(int));
    v2 = xmalloc(t * sizeof(int));
    memset(stamp, 0, (nids + 1) * sizeof(int));
    gen = 0;
    compare(0, na, 0, nb);
    for (i = 0; i < na; i++) f1.chg[ma[i]] = ca[i];
    for (i = 0; i < nb; i++) f2.chg[mb[i]] = cb[i];
    free(v1); free(v2); free(stamp); free(A); free(B); free(ma); free(mb); free(ca); free(cb);
    changes();
    if (nchg) {
        r = 1;
        if (qflag) printf("Files %s and %s differ\n", p, q);
        else {
            if (recursive) printf("diff%s %s %s\n", opts, p, q);
            if (format == UNIFIED) unifiedout();
            else if (format == CONTEXT) contextout();
            else if (format == EDSCRIPT) edout();
            else normalout();
        }
    }
    free(chgs);
done:
    freefile(&f1);
    freefile(&f2);
    fflush(stdout);
    return r;
}

/* ---- directories ---- */

static int isdir(char *p, int *exists)
{
    struct stat st;
    if (strcmp(p, "-") == 0) { *exists = 1; return 0; }
    if (stat(p, &st) < 0) { *exists = 0; return 0; }
    *exists = 1;
    return S_ISDIR(st.st_mode);
}

static char *join(char *d, char *n)
{
    char *s = xmalloc(strlen(d) + strlen(n) + 2);
    strcpy(s, d);
    if (*d && d[strlen(d) - 1] != '/') strcat(s, "/");
    strcat(s, n);
    return s;
}

static int scmp(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

/* the names in directory p (less . and ..), sorted; NULL if unreadable */
static char **names(char *p, int *np)
{
    DIR *d = opendir(p);
    struct dirent *e;
    char **v;
    int n = 0, cap = 16;
    if (!d) { fprintf(stderr, "diff: %s: %s\n", p, strerror(errno)); return 0; }
    v = xmalloc(cap * sizeof(char *));
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == cap) { cap *= 2; if (!(v = realloc(v, cap * sizeof(char *)))) nomem(); }
        v[n] = xmalloc(strlen(e->d_name) + 1);
        strcpy(v[n++], e->d_name);
    }
    closedir(d);
    qsort(v, n, sizeof(char *), scmp);
    *np = n;
    return v;
}

static void note(int r)
{
    if (r > status) status = r;
}

static void diffdirs(char *p, char *q);

/* an entry of both trees (either may be absent) */
static void diffentry(char *p, char *q, char *name, int inp, int inq)
{
    char *a = join(p, name), *b = join(q, name);
    int ea, eb, da = inp ? isdir(a, &ea) : 0, db = inq ? isdir(b, &eb) : 0;
    if (!inp || !inq) {
        if (nflag && !(inp ? da : db)) note(difffiles(a, b, !inp, !inq, 1));
        else { printf("Only in %s: %s\n", inp ? p : q, name); note(1); }
    } else if (da && db) {
        if (rflag) diffdirs(a, b);
        else printf("Common subdirectories: %s and %s\n", a, b);
    } else if (da || db) {
        printf("File %s is a %s while file %s is a %s\n", a, da ? "directory" : "regular file",
               b, db ? "directory" : "regular file");
        note(1);
    } else
        note(difffiles(a, b, 0, 0, 1));
    free(a);
    free(b);
}

static void diffdirs(char *p, char *q)
{
    char **u, **v;
    int nu, nv, i = 0, j = 0, c;
    if (!(u = names(p, &nu))) { note(2); return; }
    if (!(v = names(q, &nv))) { note(2); return; }
    while (i < nu || j < nv) {
        c = i == nu ? 1 : j == nv ? -1 : strcmp(u[i], v[j]);
        if (c == 0) { diffentry(p, q, u[i], 1, 1); i++; j++; }
        else if (c < 0) { diffentry(p, q, u[i], 1, 0); i++; }
        else { diffentry(p, q, v[j], 0, 1); j++; }
    }
    for (i = 0; i < nu; i++) free(u[i]);
    for (j = 0; j < nv; j++) free(v[j]);
    free(u);
    free(v);
}

static int usage(void)
{
    fprintf(stderr, "usage: diff [-u | -U n | -c | -C n | -e] [-bwiqrN] file1 file2\n");
    return 2;
}

static int count(char *s)
{
    char *e;
    long n;
    if (!s) return -1;
    n = strtol(s, &e, 10);
    if (*e || e == s || n < 0) return -1;
    return n;
}

int main(int argc, char **argv)
{
    int i = 1, ea, eb, da, db;
    char *p, *q, *base;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (strlen(opts) + strlen(argv[i]) + 2 < sizeof opts) {
            strcat(opts, " ");
            strcat(opts, argv[i]);
        }
        for (p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'u': format = UNIFIED; break;
            case 'c': format = CONTEXT; break;
            case 'e': format = EDSCRIPT; break;
            case 'b': bflag = 1; break;
            case 'w': wflag = 1; break;
            case 'i': iflag = 1; break;
            case 'q': qflag = 1; break;
            case 'r': rflag = 1; break;
            case 'N': nflag = 1; break;
            case 'U': case 'C':
                format = *p == 'U' ? UNIFIED : CONTEXT;
                if (p[1]) ctx = count(p + 1);
                else {
                    ctx = count(argv[++i]);
                    if (i < argc && strlen(opts) + strlen(argv[i]) + 2 < sizeof opts) {
                        strcat(opts, " ");
                        strcat(opts, argv[i]);
                    }
                }
                if (ctx < 0) { fprintf(stderr, "diff: bad context length\n"); return usage(); }
                p = " ";
                break;
            default:
                fprintf(stderr, "diff: unknown option -%c\n", *p);
                return usage();
            }
            if (*p == ' ') break;
        }
    }
    if (argc - i != 2) return usage();
    p = argv[i];
    q = argv[i + 1];
    da = isdir(p, &ea);
    db = isdir(q, &eb);
    if (da && db) { diffdirs(p, q); return status; }
    if (da || db) {                     /* diff file dir: dir/file */
        char *f = da ? q : p, *n;
        if (!strcmp(f, "-")) { fprintf(stderr, "diff: cannot compare - with a directory\n"); return 2; }
        base = strrchr(f, '/') ? strrchr(f, '/') + 1 : f;
        n = join(da ? p : q, base);
        if (da) p = n; else q = n;
        da = isdir(p, &ea);
        db = isdir(q, &eb);
    }
    if (!ea && !nflag) { fprintf(stderr, "diff: %s: %s\n", p, strerror(ENOENT)); return 2; }
    if (!eb && !nflag) { fprintf(stderr, "diff: %s: %s\n", q, strerror(ENOENT)); return 2; }
    if (!ea && !eb) { fprintf(stderr, "diff: %s: %s\n", p, strerror(ENOENT)); return 2; }
    return difffiles(p, q, !ea, !eb, 0);
}
