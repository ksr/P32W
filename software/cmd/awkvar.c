/* awkvar.c -- awk's values (awk.h): Cells and their conversions, arrays
 * (hash tables), the record and its fields, reading records (RS: a
 * character, "" for paragraphs, or a regular expression), the main input
 * (ARGV's files and assignments), and the files and pipes of print >,
 * getline <, cmd | getline, close, fflush and system. */
#include "awk.h"

extern char **environ;

/* ---------------------------------------------------------------- cells */
void cinit(Cell *c)
{
    c->t = T_UNINIT;
    c->sv = c->nv = 0;
    c->s = 0;
    c->p = 0;
}

void cfree(Cell *c)
{
    if (c->sv == 1) sfree(c->s);
    c->sv = 0;
    c->s = 0;
}

void cset_num(Cell *c, Num *n)
{
    cfree(c);
    c->t = T_NUM;
    c->n = *n;
    c->nv = 1;
}

void cset_int(Cell *c, int i)
{
    cfree(c);
    c->t = T_NUM;
    num_int(&c->n, i);
    c->nv = 1;
}

void cset_str(Cell *c, char *s)
{
    cfree(c);
    c->t = T_STR;
    c->s = s;
    c->sv = 1;
    c->nv = 0;
}

void cset_strcopy(Cell *c, const char *s, int t)
{
    cset_str(c, sdup(s));
    c->t = t;
}

Cell *deref(Cell *c)
{
    while (c->t == T_REF) c = c->p;
    return c;
}

/* d (not holding anything) gets a copy of s's value */
void ccopy(Cell *d, Cell *s)
{
    s = deref(s);
    if (s->t == T_ARR) fatal("can't use array in scalar context");
    *d = *s;
    d->p = 0;
    if (s->sv == 1 || s->sv == 2) {
        d->s = sdup(s->s);
        d->sv = 1;
    }
}

/* d's value is freed and replaced by s's, which s gives up */
void cmove(Cell *d, Cell *s)
{
    if (d->t == T_ARR) fatal("can't assign to an array name");
    cfree(d);
    *d = *s;
    d->p = 0;
    if (s->sv == 2) {
        d->s = sdup(s->s);
        d->sv = 1;
    }
    s->sv = 0;
    s->t = T_UNINIT;
}

static char *fmtvar(int v)
{
    Cell *c = &gvar[v];
    return c->t == T_STR || c->t == T_STRNUM || c->t == T_MAYBE ? c->s : "%.6g";
}

static char nbuf[8][128];
static int nbi;

static char *numstr(Cell *c, int v)
{
    char *b = nbuf[nbi++ & 7];
    if (c->n.isint) return itoa_(c->n.i, b);
    return num_tostr(&c->n, fmtvar(v), b, sizeof nbuf[0]);
}

char *cstr(Cell *c)
{
    switch (c->t) {
    case T_STR: case T_STRNUM: case T_MAYBE: return c->s;
    case T_NUM: return numstr(c, V_CONVFMT);
    case T_UNINIT: return "";
    case T_REF: return cstr(deref(c));
    }
    fatal("can't use array in scalar context");
    return "";
}

char *costr(Cell *c)
{
    if (c->t == T_NUM) return numstr(c, V_OFMT);
    return cstr(c);
}

int cisnum(Cell *c)
{
    Num m;
    int whole;
    switch (c->t) {
    case T_NUM: case T_STRNUM: case T_UNINIT: return 1;
    case T_MAYBE:
        whole = num_parse(c->s, &m, 1);
        c->n = m;
        c->nv = 1;
        c->t = whole ? T_STRNUM : T_STR;
        return whole;
    case T_REF: return cisnum(deref(c));
    }
    return 0;
}

void cnum(Cell *c, Num *n)
{
    switch (c->t) {
    case T_NUM: case T_STRNUM: *n = c->n; return;
    case T_UNINIT: num_int(n, 0); return;
    case T_MAYBE: cisnum(c); *n = c->n; return;
    case T_STR:
        if (!c->nv) { num_parse(c->s, &c->n, 0); c->nv = 1; }
        *n = c->n;
        return;
    case T_REF: cnum(deref(c), n); return;
    }
    fatal("can't use array in scalar context");
}

int cbool(Cell *c)
{
    switch (c->t) {
    case T_NUM: case T_STRNUM: return num_true(&c->n);
    case T_UNINIT: return 0;
    case T_STR: return c->s[0] != 0;
    case T_MAYBE: return cisnum(c) ? num_true(&c->n) : c->s[0] != 0;
    case T_REF: return cbool(deref(c));
    }
    fatal("can't use array in scalar context");
    return 0;
}

/* ---------------------------------------------------------------- arrays */
static unsigned hash(const char *s)
{
    unsigned h = 0;
    while (*s) h = h * 31 + (unsigned char)*s++;
    return h;
}

Array *newarray(void)
{
    Array *a = emalloc(sizeof(Array));
    a->n = 0;
    a->size = 8;
    a->tab = emalloc(a->size * sizeof(Elem *));
    memset(a->tab, 0, a->size * sizeof(Elem *));
    return a;
}

Array *carray(Cell *c)
{
    c = deref(c);
    if (c->t == T_UNINIT) {
        c->t = T_ARR;
        c->p = newarray();
    }
    if (c->t != T_ARR) fatal("can't use scalar as array");
    return c->p;
}

static void grow(Array *a)
{
    int ns = a->size * 4, i;
    Elem **nt = emalloc(ns * sizeof(Elem *)), *e, *nx;
    memset(nt, 0, ns * sizeof(Elem *));
    for (i = 0; i < a->size; i++)
        for (e = a->tab[i]; e; e = nx) {
            nx = e->next;
            e->next = nt[hash(e->key) & (ns - 1)];
            nt[hash(e->key) & (ns - 1)] = e;
        }
    free(a->tab);
    a->tab = nt;
    a->size = ns;
}

Cell *aget(Array *a, const char *key, int create)
{
    unsigned h = hash(key) & (a->size - 1);
    Elem *e;
    for (e = a->tab[h]; e; e = e->next)
        if (!strcmp(e->key, key)) return &e->v;
    if (!create) return 0;
    if (a->n >= a->size * 2) {
        grow(a);
        h = hash(key) & (a->size - 1);
    }
    e = anew();
    e->key = sdup(key);
    cinit(&e->v);
    e->next = a->tab[h];
    a->tab[h] = e;
    a->n++;
    return &e->v;
}

void adelete(Array *a, const char *key)
{
    Elem **pe = &a->tab[hash(key) & (a->size - 1)], *e;
    for (; (e = *pe) != 0; pe = &e->next)
        if (!strcmp(e->key, key)) {
            *pe = e->next;
            cfree(&e->v);
            sfree(e->key);
            afree(e);
            a->n--;
            return;
        }
}

void clearray(Array *a)
{
    int i;
    Elem *e, *nx;
    for (i = 0; i < a->size; i++) {
        for (e = a->tab[i]; e; e = nx) {
            nx = e->next;
            cfree(&e->v);
            sfree(e->key);
            afree(e);
        }
        a->tab[i] = 0;
    }
    a->n = 0;
}

void freearray(Array *a)
{
    clearray(a);
    free(a->tab);
    free(a);
}

/* the subscripts, copied (for (k in a) goes over these) */
char **akeys(Array *a, int *n)
{
    char **k = emalloc((a->n + 1) * sizeof(char *));
    int i, m = 0;
    Elem *e;
    for (i = 0; i < a->size; i++)
        for (e = a->tab[i]; e; e = e->next) k[m++] = sdup(e->key);
    *n = m;
    return k;
}

/* ---------------------------------------------------------------- the record and its fields */
Cell *fld;
int nf, fldsplitdone;
int nrec;
static int fldcap;
static Cell fld0;               /* $0: borrows rec.s */
static Buf rec;
static char *fbuf;              /* the fields' text, each ending in a NUL */
static int fbufcap;
static int recdirty;            /* a field was set: $0 must be put together again */
static char *recfs;             /* FS when the record was read */
static Cell emptycell;          /* a field beyond NF */
static int spos, splitstarted;  /* splitting: where it goes on; begun on this record */

static void growfld(int n)
{
    int i;
    if (n < fldcap) return;
    i = fldcap;
    fldcap = n * 2 + 16;
    fld = erealloc(fld, fldcap * sizeof(Cell));
    for (; i < fldcap; i++) cinit(&fld[i]);
}

static void dropfields(void)
{
    int i;
    for (i = 1; i <= nf; i++) {
        if (fld[i].sv == 1) cfree(&fld[i]);
        fld[i].t = T_UNINIT;
        fld[i].sv = 0;
    }
    nf = 0;
}

static void setnfvar(void)
{
    cset_int(&gvar[V_NF], nf);
}

/* a new $0 (s may be rec.s itself), to be split by FS as it is now */
static void newrec(const char *s, int len)
{
    char *fs = cstr(&gvar[V_FS]);
    if (s != rec.s) {
        rec.len = 0;
        buf_add(&rec, s, len);
        if (!rec.s) buf_add(&rec, "", 0);
    }
    fld0.t = T_MAYBE;
    fld0.sv = 2;
    fld0.nv = 0;
    fld0.s = rec.s;
    dropfields();
    fldsplitdone = 0;
    splitstarted = 0;
    recdirty = 0;
    if (!recfs || strcmp(recfs, fs)) {
        sfree(recfs);
        recfs = sdup(fs);
    }
}

void setrecord(const char *s, int len)
{
    newrec(s, len);
}

/* put $0 together from the fields */
static void rebuild(void)
{
    Buf b;
    char *ofs = cstr(&gvar[V_OFS]);
    int i;
    buf_init(&b);
    buf_add(&b, "", 0);
    for (i = 1; i <= nf; i++) {
        if (i > 1) buf_adds(&b, ofs);
        buf_adds(&b, cstr(&fld[i]));
    }
    free(rec.s);
    rec = b;
    fld0.s = rec.s;
    fld0.t = T_MAYBE;
    fld0.nv = 0;
    recdirty = 0;
}

char *getrec0(void)
{
    if (recdirty) rebuild();
    if (!rec.s) buf_add(&rec, "", 0);
    if (!fld0.s) { fld0.t = T_MAYBE; fld0.sv = 2; fld0.s = rec.s; }
    return rec.s;
}

static int fpos;                /* (where the next field goes in fbuf) */

static void addfield(int start, int end)
{
    Cell *c;
    char *d = fbuf + fpos, *src = rec.s + start, *e = rec.s + end;
    if (++nf >= fldcap) growfld(nf);
    c = &fld[nf];
    c->t = T_MAYBE;
    c->sv = 2;
    c->nv = 0;
    c->s = d;
    while (src < e) *d++ = *src++;
    *d = 0;
    fpos = d + 1 - fbuf;
}

/* split by sep: " " (blanks), one character, "" (each character) or a
   regular expression; paragraph mode adds newline. Calls add(start, end). */
static void splitby(const char *s, int len, const char *sep, Re *re, int para, void (*add)(int, int))
{
    int i, start, ms, me, pos;
    char c;
    Buf b;
    if (!len) return;
    if (!re && !strcmp(sep, " ")) {
        i = 0;
        for (;;) {
            while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
            if (i >= len) break;
            start = i;
            while (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n') i++;
            add(start, i);
        }
        return;
    }
    if (!re && !*sep) {
        for (i = 0; i < len; i++) add(i, i + 1);
        return;
    }
    if (!re && !sep[1]) {
        c = sep[0];
        start = 0;
        for (i = 0; i < len; i++)
            if (s[i] == c || (para && s[i] == '\n')) { add(start, i); start = i + 1; }
        add(start, len);
        return;
    }
    if (!re) {
        if (para) {
            buf_init(&b);
            buf_adds(&b, "(");
            buf_adds(&b, sep);
            buf_adds(&b, ")|\n");
            re = re_dyn(b.s);
            free(b.s);
        } else
            re = re_dyn(sep);
    }
    start = pos = 0;
    while (pos <= len && re_exec(re, s, len, pos, &ms, &me)) {
        if (me == ms) { pos = ms + 1; continue; }
        add(start, ms);
        start = pos = me;
    }
    add(start, len);
}

static int paramode(void)
{
    Cell *c = &gvar[V_RS];
    return (c->t == T_STR || c->t == T_MAYBE || c->t == T_STRNUM) && !c->s[0];
}

/* Fields are split as far as they are needed: $1 needs only the first.
   (NF needs them all.) Once started on a record, the field array is not
   reallocated before the record is done, so Cells handed out stay put:
   records too long for that are split all at once. */

#define isblankfs(c) ((c) == ' ' || (c) == '\t' || (c) == '\n')

/* split $0 until there are want fields (or all of them) */
static void splitmore(int want)
{
    char *s, *fs = recfs ? recfs : " ", c;
    int len, i, start, ms, me, pos, para;
    Re *re;
    Buf b;
    if (fldsplitdone) return;
    getrec0();
    s = rec.s;
    len = rec.len;
    if (!splitstarted) {
        dropfields();
        if (2 * len + 2 > fbufcap) {    /* (each field and a NUL: at most twice the record) */
            fbufcap = 2 * len + 64;
            free(fbuf);
            fbuf = emalloc(fbufcap);
        }
        fpos = 0;
        spos = 0;
        splitstarted = 1;
        if (len + 2 > fldcap) want = len + 2;   /* (all at once: see above) */
    }
    i = spos;
    para = paramode();
    if (!strcmp(fs, " ")) {
        for (;;) {
            while (i < len && isblankfs(s[i])) i++;
            if (i >= len) goto done;
            if (nf >= want) break;
            start = i;
            while (i < len && !isblankfs(s[i])) i++;
            addfield(start, i);
        }
    } else if (!*fs) {
        for (; i < len && nf < want; i++) addfield(i, i + 1);
        if (i >= len) goto done;
    } else if (!fs[1]) {
        c = fs[0];
        if (!len) goto done;
        while (nf < want) {
            start = i;
            while (i < len && s[i] != c && !(para && s[i] == '\n')) i++;
            addfield(start, i);
            if (i >= len) goto done;
            i++;
        }
    } else {
        if (para) {
            buf_init(&b);
            buf_adds(&b, "(");
            buf_adds(&b, fs);
            buf_adds(&b, ")|\n");
            re = re_dyn(b.s);
            free(b.s);
        } else
            re = re_dyn(fs);
        if (!len) goto done;
        start = pos = i;
        while (nf < want) {
            if (pos <= len && re_exec(re, s, len, pos, &ms, &me)) {
                if (me == ms) { pos = ms + 1; continue; }
                addfield(start, ms);
                start = pos = me;
            } else {
                addfield(start, len);
                goto done;
            }
        }
        i = start;
    }
    spos = i;
    return;
done:
    spos = len;
    fldsplitdone = 1;
    setnfvar();
}

void fldsplit(void)
{
    splitmore(2147483647);
}

Cell *getfield(int k)
{
    if (k == 0) { getrec0(); return &fld0; }
    if (k < 0) fatal("trying to access out of range field %d", k);
    if (k > nf && !fldsplitdone) splitmore(k);
    return k <= nf ? &fld[k] : &emptycell;
}

Cell *setfieldref(int k)
{
    int i;
    if (k == 0) { getrec0(); return &fld0; }
    if (k < 0 || k > 1000000) fatal("trying to access out of range field %d", k);
    if (!fldsplitdone) fldsplit();
    if (k > nf) {
        growfld(k);
        for (i = nf + 1; i <= k; i++) { cfree(&fld[i]); cinit(&fld[i]); }
        nf = k;
        setnfvar();
    }
    return &fld[k];
}

/* after an assignment to $k */
void fieldset(int k)
{
    char *s;
    if (k) { recdirty = 1; return; }
    s = sdup(cstr(&fld0));      /* $0 = ...: a new record */
    cfree(&fld0);
    fld0.s = 0;
    newrec(s, strlen(s));
    sfree(s);
}

/* after an assignment to NF */
void nfset(void)
{
    Num n;
    int k, i;
    cnum(&gvar[V_NF], &n);
    k = num_toint(&n);
    if (k < 0) fatal("cannot set NF to a negative value");
    if (k > 1000000) fatal("NF set to %d: too many fields", k);
    if (k > nf) {
        growfld(k);
        for (i = nf + 1; i <= k; i++) { cfree(&fld[i]); cinit(&fld[i]); }
    } else
        for (i = k + 1; i <= nf; i++) { cfree(&fld[i]); fld[i].t = T_UNINIT; }
    nf = k;
    setnfvar();
    recdirty = 1;
}

static Array *splitarr;
static const char *splitsrc;
static int splitn;

static void addelem(int start, int end)
{
    char key[16];
    Cell *c;
    itoa_(++splitn, key);
    c = aget(splitarr, key, 1);
    cset_str(c, sdupn(splitsrc + start, end - start));
    c->t = T_MAYBE;
}

/* split(s, a, fs): the number of elements */
int splitstr(const char *s, Array *a, const char *fs, Re *re)
{
    char *copy = sdup(s);       /* (s may be an element of a) */
    clearray(a);
    splitarr = a;
    splitsrc = copy;
    splitn = 0;
    splitby(copy, strlen(copy), fs, re, re ? 0 : paramode(), addelem);
    sfree(copy);
    return splitn;
}

/* ---------------------------------------------------------------- reading records */
typedef struct Reader { int fd; char *buf; int pos, len, cap, eof; } Reader;

static Reader *newreader(int fd)
{
    Reader *r = emalloc(sizeof(Reader));
    r->fd = fd;
    r->cap = 4096;
    r->buf = emalloc(r->cap);
    r->pos = r->len = r->eof = 0;
    return r;
}

static void freereader(Reader *r)
{
    if (r->fd > 0) close(r->fd);
    free(r->buf);
    free(r);
}

/* more input after what is there; 0 at the end */
static int fill(Reader *r)
{
    int n;
    if (r->eof) return 0;
    if (r->pos > 0) {
        memmove(r->buf, r->buf + r->pos, r->len - r->pos);
        r->len -= r->pos;
        r->pos = 0;
    }
    if (r->len == r->cap) {
        r->cap *= 2;
        r->buf = erealloc(r->buf, r->cap);
    }
    n = read(r->fd, r->buf + r->len, r->cap - r->len);
    if (n <= 0) { r->eof = 1; return 0; }
    r->len += n;
    return 1;
}

static int rdgetc(Reader *r)
{
    if (r->pos >= r->len && !fill(r)) return -1;
    return (unsigned char)r->buf[r->pos++];
}

static int rdpeek(Reader *r)
{
    if (r->pos >= r->len && !fill(r)) return -1;
    return (unsigned char)r->buf[r->pos];
}

/* the next record into b (emptied first): 1, or 0 at the end */
static int rdrecord(Reader *r, Buf *b)
{
    Cell *rsc = &gvar[V_RS];
    char *rs = cstr(rsc), *f;
    int c, got = 0, ms, me;
    b->len = 0;
    buf_add(b, "", 0);
    if (!rs[0]) {                       /* paragraphs */
        while ((c = rdgetc(r)) == '\n') ;
        if (c < 0) return 0;
        for (;;) {
            buf_addc(b, c);
            if (c == '\n') {
                c = rdpeek(r);
                if (c == '\n' || c < 0) {
                    while (rdpeek(r) == '\n') r->pos++;
                    b->s[--b->len] = 0;
                    return 1;
                }
            }
            c = rdgetc(r);
            if (c < 0) {
                if (b->len && b->s[b->len - 1] == '\n') b->s[--b->len] = 0;
                return 1;
            }
        }
    }
    if (!rs[1]) {                       /* one character */
        c = (unsigned char)rs[0];
        for (;;) {
            if (r->pos < r->len) {
                got = 1;
                f = memchr(r->buf + r->pos, c, r->len - r->pos);
                if (f) {
                    buf_add(b, r->buf + r->pos, f - (r->buf + r->pos));
                    r->pos = f - r->buf + 1;
                    return 1;
                }
                buf_add(b, r->buf + r->pos, r->len - r->pos);
                r->pos = r->len;
            }
            if (!fill(r)) return got;
        }
    }
    /* a regular expression */
    {
        Re *re = re_dyn(rs);
        for (;;) {
            if (r->pos < r->len && re_exec(re, r->buf + r->pos, r->len - r->pos, 0, &ms, &me)) {
                while (me == ms && ms < r->len - r->pos &&
                       re_exec(re, r->buf + r->pos, r->len - r->pos, ms + 1, &ms, &me)) ;
                if (me > ms && (r->pos + me < r->len || r->eof)) {
                    buf_add(b, r->buf + r->pos, ms);
                    r->pos += me;
                    return 1;
                }
            }
            if (!fill(r)) break;
        }
        if (r->pos < r->len) {
            buf_add(b, r->buf + r->pos, r->len - r->pos);
            r->pos = r->len;
            return 1;
        }
        return 0;
    }
}

/* ---------------------------------------------------------------- the main input */
static Reader *mainrd;
static int argi = 1, usedstdin, anyfile;
static Buf mbuf;

static void incvar(int v)
{
    Num n, one;
    Cell *c = &gvar[v];
    if (c->t == T_NUM && c->n.isint && c->n.i < 2147483647) { c->n.i++; return; }
    cnum(&gvar[v], &n);
    num_int(&one, 1);
    num_add(&n, &n, &one);
    cset_num(&gvar[v], &n);
}

static int openmain(void)
{
    Num n;
    Cell *c;
    char key[16], *s;
    int argc, fd;
    cnum(&gvar[V_ARGC], &n);
    argc = num_toint(&n);
    while (argi < argc) {
        itoa_(argi++, key);
        c = gvar[V_ARGV].t == T_ARR ? aget(gvar[V_ARGV].p, key, 0) : 0;
        if (!c) continue;
        s = cstr(c);
        if (!*s) continue;
        if (isassign(s)) { cmdassign(s); continue; }
        anyfile = 1;
        if (!strcmp(s, "-") || !strcmp(s, "/dev/stdin")) fd = 0;
        else if ((fd = open(s, O_RDONLY)) < 0)
            fatal("can't open file %s", s);
        mainrd = newreader(fd);
        cset_strcopy(&gvar[V_FILENAME], s, T_STR);
        cset_int(&gvar[V_FNR], 0);
        return 1;
    }
    if (anyfile || usedstdin) return 0;
    usedstdin = 1;
    mainrd = newreader(0);
    cset_int(&gvar[V_FNR], 0);
    return 1;
}

void nextfile(void)
{
    if (mainrd) { freereader(mainrd); mainrd = 0; }
}

/* the next record of the main input as $0 (b: 0) or into b; 1, or 0 at the end */
int mainrec(Buf *b)
{
    for (;;) {
        if (!mainrd && !openmain()) return 0;
        if (rdrecord(mainrd, b ? b : &mbuf)) break;
        nextfile();
    }
    incvar(V_NR);
    incvar(V_FNR);
    nrec = num_toint(&gvar[V_NR].n);
    if (!b) {                           /* (swapped: $0 survives the end of the input) */
        Buf t;
        t = rec;
        rec = mbuf;
        mbuf = t;
        newrec(rec.s, rec.len);
    }
    return 1;
}

/* ---------------------------------------------------------------- files and pipes */
#define NSTREAM 16
enum { ST_FILE = 1, ST_APPEND, ST_PIPEOUT, ST_IN, ST_PIPEIN };
static struct stream {
    char *name;
    int kind;                   /* 0: free */
    FILE *fp;
    Reader *rd;
    int pid, status, reaped;
} st[NSTREAM];

/* wait for pid; the status of others that end meanwhile is kept */
static int reap(int pid)
{
    int s, w, i;
    for (i = 0; i < NSTREAM; i++)
        if (st[i].kind && st[i].pid == pid && st[i].reaped) return st[i].status;
    for (;;) {
        w = wait(&s);
        if (w < 0) return -1;
        if (w == pid) return s;
        for (i = 0; i < NSTREAM; i++)
            if (st[i].kind && st[i].pid == w) { st[i].reaped = 1; st[i].status = s; }
    }
}

static int exitcode(int s)
{
    if (s < 0) return -1;
    if (WIFEXITED(s)) return WEXITSTATUS(s);
    return 256 + WTERMSIG(s);
}

void flushall(void)
{
    int i;
    fflush(stdout);
    for (i = 0; i < NSTREAM; i++)
        if (st[i].fp) fflush(st[i].fp);
}

/* sh -c cmd with its input (w) or output (!w) on a pipe; our end's fd */
static int startcmd(const char *cmd, int w, int *pid)
{
    int p[2], fd;
    flushall();
    if (pipe(p) < 0) return -1;
    if ((*pid = fork()) < 0) { close(p[0]); close(p[1]); return -1; }
    if (*pid == 0) {
        dup2(w ? p[0] : p[1], w ? 0 : 1);
        for (fd = 3; fd < 20; fd++) close(fd);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    close(w ? p[0] : p[1]);
    return w ? p[1] : p[0];
}

static int findstream(const char *name, int in)
{
    int i;
    for (i = 0; i < NSTREAM; i++)
        if (st[i].kind && !strcmp(st[i].name, name) && (in ? st[i].rd != 0 : st[i].fp != 0)) return i;
    return -1;
}

static int freeslot(const char *name)
{
    int i;
    for (i = 0; i < NSTREAM; i++)
        if (!st[i].kind) return i;
    fatal("%s makes too many open files", name);
    return -1;
}

FILE *outstream(const char *name, int how)
{
    int i, fd, pid = 0;
    FILE *fp;
    if (!strcmp(name, "/dev/stdout") || !strcmp(name, "-")) return stdout;
    if (!strcmp(name, "/dev/stderr")) { fflush(stdout); return stderr; }
    if ((i = findstream(name, 0)) >= 0) return st[i].fp;
    i = freeslot(name);
    if (how == R_PIPE) {
        fd = startcmd(name, 1, &pid);
        fp = fd < 0 ? 0 : fdopen(fd, "w");
    } else
        fp = fopen(name, how == R_APPEND ? "a" : "w");
    if (!fp) fatal("can't redirect to %s", name);
    st[i].name = sdup(name);
    st[i].kind = how == R_PIPE ? ST_PIPEOUT : how == R_APPEND ? ST_APPEND : ST_FILE;
    st[i].fp = fp;
    st[i].rd = 0;
    st[i].pid = pid;
    st[i].reaped = 0;
    return fp;
}

/* getline < name (G_FILE) or name | getline (G_CMD) into b: 1, 0 at the end, -1 */
int getlinefrom(const char *name, int how, Buf *b)
{
    int i, fd, pid = 0;
    if ((i = findstream(name, 1)) < 0) {
        if (how == G_CMD) fd = startcmd(name, 0, &pid);
        else if (!strcmp(name, "-") || !strcmp(name, "/dev/stdin")) fd = 0;
        else fd = open(name, O_RDONLY);
        if (fd < 0) return -1;
        i = freeslot(name);
        st[i].name = sdup(name);
        st[i].kind = how == G_CMD ? ST_PIPEIN : ST_IN;
        st[i].fp = 0;
        st[i].rd = newreader(fd);
        st[i].pid = pid;
        st[i].reaped = 0;
    }
    return rdrecord(st[i].rd, b);
}

static int closeone(int i)
{
    int r = 0;
    if (st[i].fp && fclose(st[i].fp) == EOF) r = -1;
    if (st[i].rd) freereader(st[i].rd);
    if (st[i].kind == ST_PIPEOUT || st[i].kind == ST_PIPEIN) r = exitcode(reap(st[i].pid));
    sfree(st[i].name);
    st[i].kind = 0;
    st[i].fp = 0;
    st[i].rd = 0;
    return r;
}

int closestream(const char *name)
{
    int i, r = -1;
    for (i = 0; i < NSTREAM; i++)
        if (st[i].kind && !strcmp(st[i].name, name)) r = closeone(i);
    return r;
}

void closeall(void)
{
    int i;
    fflush(stdout);
    for (i = 0; i < NSTREAM; i++)
        if (st[i].kind) closeone(i);
    fflush(stdout);
}

int flushstream(const char *name)
{
    int i;
    if (!strcmp(name, "/dev/stdout") || !strcmp(name, "-")) return fflush(stdout);
    if (!strcmp(name, "/dev/stderr")) return 0;
    if ((i = findstream(name, 0)) < 0) return -1;
    return fflush(st[i].fp);
}

int runsystem(const char *cmd)
{
    int pid, fd;
    flushall();
    if ((pid = fork()) < 0) return -1;
    if (pid == 0) {
        for (fd = 3; fd < 20; fd++) close(fd);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    return exitcode(reap(pid));
}

/* ---------------------------------------------------------------- variables at the start */
static void setconst(int v, char *s)
{
    cfree(&gvar[v]);
    gvar[v].t = T_STR;
    gvar[v].s = s;
    gvar[v].sv = 3;
}

void initvars(int argc, char **argv, int first)
{
    Array *a;
    Cell *c;
    char key[16], *eq, **e;
    int i;
    setconst(V_FS, " ");
    setconst(V_OFS, " ");
    setconst(V_ORS, "\n");
    setconst(V_RS, "\n");
    setconst(V_SUBSEP, "\034");
    setconst(V_CONVFMT, "%.6g");
    setconst(V_OFMT, "%.6g");
    setconst(V_FILENAME, "");
    cset_int(&gvar[V_NR], 0);
    cset_int(&gvar[V_FNR], 0);
    cset_int(&gvar[V_NF], 0);
    cset_int(&gvar[V_RSTART], 0);
    cset_int(&gvar[V_RLENGTH], -1);
    a = carray(&gvar[V_ENVIRON]);
    for (e = environ; e && *e; e++)
        if ((eq = strchr(*e, '=')) != 0) {
            char *name = sdupn(*e, eq - *e);
            c = aget(a, name, 1);
            cset_strcopy(c, eq + 1, T_MAYBE);
            sfree(name);
        }
    a = carray(&gvar[V_ARGV]);
    cset_strcopy(aget(a, "0", 1), argv[0], T_STR);
    for (i = first; i < argc; i++) {
        itoa_(i - first + 1, key);
        cset_strcopy(aget(a, key, 1), argv[i], T_MAYBE);
    }
    cset_int(&gvar[V_ARGC], argc - first + 1);
    growfld(60);                        /* (records up to 120 characters are split lazily) */
    newrec("", 0);
}

int isassign(const char *s)
{
    if (!isalpha((unsigned char)*s) && *s != '_') return 0;
    while (isalnum((unsigned char)*s) || *s == '_') s++;
    return *s == '=';
}

/* var=value (from -v or the operands): value's escapes done, a strnum maybe */
void cmdassign(const char *arg)
{
    const char *eq = strchr(arg, '=');
    char *name = sdupn(arg, eq - arg), *val;
    int v = findglobal(name);
    sfree(name);
    if (v < 0) return;                  /* (the program never uses it) */
    if (gvar[v].t == T_ARR) fatal("can't assign to %s; it's an array name.", gname[v]);
    val = snew(strlen(eq + 1));
    unescape(val, eq + 1);
    cset_str(&gvar[v], val);
    gvar[v].t = T_MAYBE;
}
