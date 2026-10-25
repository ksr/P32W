/* objfmt.h -- P32W objects and archives for the native as, ld and ar (M7,
 * D24). The binary format of assembler/p32obj.py (its comment is the
 * specification), so the host and native tools read and write the same
 * bytes. Included by as.c, ld.c and ar.c (each a program of its own). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

enum { S_ABS = 0, S_TEXT = 1, S_DATA = 2, S_BSS = 3 };
enum { R_ABS32 = 0, R_HI16 = 1, R_LO16 = 2, R_J24 = 3 };
static const char *const sect_name[4] = { "*ABS*", ".text", ".data", ".bss" };

struct osym { char *name; unsigned value; int sect, global; };
struct oreloc { int sect, kind; unsigned offset; char *sym; int addend; };
struct obj {
    char *source;
    unsigned char *text, *data;
    unsigned tsize, dsize, bsize, talign, dalign, balign;
    int nsym, nrel;
    struct osym *syms;
    struct oreloc *rels;
};

static void *xmalloc(unsigned n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

static int bytecmp(const char *a, const char *b)   /* names in byte order (as Python sorts them) */
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (*x && *x == *y) { x++; y++; }
    return *x - *y;
}

/* ---------------------------------------------------------------- a growing byte buffer */
struct buf { unsigned char *p; unsigned n, cap; };

static void bput(struct buf *b, const void *src, unsigned n)
{
    if (b->n + n > b->cap) {
        unsigned c = b->cap ? b->cap : 256;
        unsigned char *q;
        while (c < b->n + n) c *= 2;
        q = xmalloc(c);
        if (b->n) memcpy(q, b->p, b->n);
        free(b->p);
        b->p = q;
        b->cap = c;
    }
    if (src) memcpy(b->p + b->n, src, n);
    else memset(b->p + b->n, 0, n);
    b->n += n;
}

static void bword(struct buf *b, unsigned v)
{
    unsigned char w[4];
    w[0] = v; w[1] = v >> 8; w[2] = v >> 16; w[3] = v >> 24;
    bput(b, w, 4);
}

static void bpad(struct buf *b) { if (b->n & 3) bput(b, 0, 4 - (b->n & 3)); }

static unsigned rword(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* ---------------------------------------------------------------- a string table, each string once */
#define STHASH 1024
struct stent { char *s; unsigned off; struct stent *next; };
struct strtab { struct buf b; struct stent *h[STHASH]; };

static unsigned strhash(const char *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % STHASH;
}

static unsigned st_add(struct strtab *t, const char *s)
{
    unsigned k = strhash(s);
    struct stent *e;
    for (e = t->h[k]; e; e = e->next)
        if (!strcmp(e->s, s)) return e->off;
    e = xmalloc(sizeof *e);
    e->s = (char *)s;
    e->off = t->b.n;
    e->next = t->h[k];
    t->h[k] = e;
    bput(&t->b, s, strlen(s) + 1);
    return e->off;
}

static void st_free(struct strtab *t)
{
    int i;
    for (i = 0; i < STHASH; i++) {
        struct stent *e = t->h[i], *n;
        for (; e; e = n) { n = e->next; free(e); }
        t->h[i] = 0;
    }
    free(t->b.p);
}

/* ---------------------------------------------------------------- objects */
static int symcmp(const void *a, const void *b)
{
    return bytecmp(((const struct osym *)a)->name, ((const struct osym *)b)->name);
}

/* the object's bytes appended to out (symbols sorted in place) */
static void obj_encode(struct obj *o, struct buf *out)
{
    static struct strtab st;
    unsigned src, padded;
    int i;
    memset(&st, 0, sizeof st);
    qsort(o->syms, o->nsym, sizeof *o->syms, symcmp);
    src = st_add(&st, o->source);
    for (i = 0; i < o->nsym; i++) st_add(&st, o->syms[i].name);
    for (i = 0; i < o->nrel; i++) st_add(&st, o->rels[i].sym);
    padded = (st.b.n + 3) & ~3u;
    bput(out, "P32O", 4);
    bword(out, 2); bword(out, o->tsize); bword(out, o->dsize); bword(out, o->bsize);
    bword(out, o->talign); bword(out, o->dalign); bword(out, o->balign);
    bword(out, o->nsym); bword(out, o->nrel); bword(out, padded); bword(out, src);
    bput(out, o->text, o->tsize); bpad(out);
    bput(out, o->data, o->dsize); bpad(out);
    for (i = 0; i < o->nsym; i++) {
        unsigned char f[4];
        bword(out, st_add(&st, o->syms[i].name));
        bword(out, o->syms[i].value);
        f[0] = o->syms[i].sect; f[1] = o->syms[i].global != 0; f[2] = f[3] = 0;
        bput(out, f, 4);
    }
    for (i = 0; i < o->nrel; i++) {
        unsigned char f[4];
        f[0] = o->rels[i].sect; f[1] = o->rels[i].kind; f[2] = f[3] = 0;
        bput(out, f, 4);
        bword(out, o->rels[i].offset);
        bword(out, st_add(&st, o->rels[i].sym));
        bword(out, (unsigned)o->rels[i].addend);
    }
    bput(out, st.b.p, st.b.n);
    bpad(out);
    st_free(&st);
}

/* an object from its bytes (which it keeps pointing into); 0, or -1 */
static int obj_decode(const unsigned char *b, unsigned len, struct obj *o)
{
    unsigned p, nsym, nrel, stsz;
    const char *tab;
    int i;
    if (len < 48 || memcmp(b, "P32O", 4) || rword(b + 4) != 2) return -1;
    o->tsize = rword(b + 8); o->dsize = rword(b + 12); o->bsize = rword(b + 16);
    o->talign = rword(b + 20); o->dalign = rword(b + 24); o->balign = rword(b + 28);
    nsym = rword(b + 32); nrel = rword(b + 36); stsz = rword(b + 40);
    p = 48;
    o->text = (unsigned char *)b + p; p += (o->tsize + 3) & ~3u;
    o->data = (unsigned char *)b + p; p += (o->dsize + 3) & ~3u;
    if (p + 12 * nsym + 16 * nrel + stsz > len) return -1;
    tab = (const char *)b + p + 12 * nsym + 16 * nrel;
    o->source = (char *)tab + rword(b + 44);
    o->nsym = nsym;
    o->syms = xmalloc(sizeof(struct osym) * (nsym ? nsym : 1));
    for (i = 0; i < (int)nsym; i++, p += 12) {
        o->syms[i].name = (char *)tab + rword(b + p);
        o->syms[i].value = rword(b + p + 4);
        o->syms[i].sect = b[p + 8];
        o->syms[i].global = b[p + 9];
    }
    o->nrel = nrel;
    o->rels = xmalloc(sizeof(struct oreloc) * (nrel ? nrel : 1));
    for (i = 0; i < (int)nrel; i++, p += 16) {
        o->rels[i].sect = b[p];
        o->rels[i].kind = b[p + 1];
        o->rels[i].offset = rword(b + p + 4);
        o->rels[i].sym = (char *)tab + rword(b + p + 8);
        o->rels[i].addend = (int)rword(b + p + 12);
    }
    return 0;
}

/* ---------------------------------------------------------------- files */
static unsigned char *read_file(const char *path, unsigned *len)
{
    int fd = open(path, O_RDONLY), n;
    struct buf b;
    struct stat st;
    if (fd < 0) return 0;
    memset(&b, 0, sizeof b);
    if (fstat(fd, &st) == 0 && st.st_size > 0) {    /* the whole file at once */
        b.p = xmalloc(st.st_size + 1);
        b.cap = st.st_size + 1;
    }
    for (;;) {
        if (b.n == b.cap) bput(&b, 0, 4096), b.n -= 4096;
        n = read(fd, b.p + b.n, b.cap - b.n);
        if (n <= 0) break;
        b.n += n;
    }
    close(fd);
    bput(&b, "", 1);                    /* (a NUL after it: text can be scanned as a string) */
    *len = b.n - 1;
    return b.p;
}

static int write_file(const char *path, const unsigned char *p, unsigned n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    while (n > 0) {
        int k = write(fd, p, n);
        if (k <= 0) { close(fd); return -1; }
        p += k;
        n -= k;
    }
    return close(fd);
}

/* ---------------------------------------------------------------- archives */
struct member { char *name; struct obj o; const unsigned char *raw; unsigned rawlen; };

/* an archive's members (decoded); their count, or -1 */
static int ar_decode(const unsigned char *b, unsigned len, struct member **out)
{
    unsigned nmem, nidx, stsz, p, i;
    const char *tab;
    struct member *m;
    if (len < 20 || memcmp(b, "P32A", 4) || rword(b + 4) != 2) return -1;
    nmem = rword(b + 8); nidx = rword(b + 12); stsz = rword(b + 16);
    p = 20 + 8 * nidx;
    tab = (const char *)b + p;
    p += stsz;
    m = xmalloc(sizeof *m * (nmem ? nmem : 1));
    for (i = 0; i < nmem; i++) {
        unsigned size;
        if (p + 8 > len) return -1;
        m[i].name = (char *)tab + rword(b + p);
        size = rword(b + p + 4);
        p += 8;
        if (p + size > len || obj_decode(b + p, size, &m[i].o) < 0) return -1;
        m[i].raw = b + p;
        m[i].rawlen = size;
        p += (size + 3) & ~3u;
    }
    *out = m;
    return nmem;
}

struct ixent { char *sym; int member; };
static int ixcmp(const void *a, const void *b)
{
    const struct ixent *x = a, *y = b;
    int c = bytecmp(x->sym, y->sym);
    return c ? c : x->member - y->member;
}
static int memcmp_name(const void *a, const void *b)
{
    return bytecmp(((const struct member *)a)->name, ((const struct member *)b)->name);
}

/* members (sorted here by name) to an archive's bytes */
static void ar_encode(struct member *m, int n, struct buf *out)
{
    static struct strtab st;
    struct ixent *ix;
    int nix = 0, i, k;
    memset(&st, 0, sizeof st);
    qsort(m, n, sizeof *m, memcmp_name);
    for (i = 0; i < n; i++)
        for (k = 0; k < m[i].o.nsym; k++) nix += m[i].o.syms[k].global != 0;
    ix = xmalloc(sizeof *ix * (nix ? nix : 1));
    nix = 0;
    for (i = 0; i < n; i++)
        for (k = 0; k < m[i].o.nsym; k++)
            if (m[i].o.syms[k].global) { ix[nix].sym = m[i].o.syms[k].name; ix[nix].member = i; nix++; }
    qsort(ix, nix, sizeof *ix, ixcmp);
    for (i = 0; i < n; i++) st_add(&st, m[i].name);
    for (i = 0; i < nix; i++) st_add(&st, ix[i].sym);
    bput(out, "P32A", 4);
    bword(out, 2); bword(out, n); bword(out, nix); bword(out, (st.b.n + 3) & ~3u);
    for (i = 0; i < nix; i++) { bword(out, st_add(&st, ix[i].sym)); bword(out, ix[i].member); }
    bput(out, st.b.p, st.b.n);
    bpad(out);
    for (i = 0; i < n; i++) {
        struct buf ob;
        memset(&ob, 0, sizeof ob);
        if (m[i].raw) bput(&ob, m[i].raw, m[i].rawlen);     /* (an object as read: its bytes again) */
        else obj_encode(&m[i].o, &ob);
        bword(out, st_add(&st, m[i].name));
        bword(out, ob.n);
        bput(out, ob.p, ob.n);
        bpad(out);
        free(ob.p);
    }
    free(ix);
    st_free(&st);
}
