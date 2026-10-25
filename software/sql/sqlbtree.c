/* sqlbtree.c -- the SQL library's indexes (sqldb(3)): B+trees in files.
 *
 * NAME.idx holds a header page and the tree's nodes, one per page. Every
 * entry is a key and a rowid, all of one size, and they compare with
 * memcmp (sqlint.h says how keys are made), so a node is a sorted array
 * searched by halves. Leaves are chained left to right for range scans.
 * A full node splits in two, and its middle key moves up; a new root is
 * made above a root that splits. A deleted entry just leaves its leaf (no
 * merging): the tree stays correct, though not as small as it could be.
 */
#include "sqlint.h"

static void putbe(unsigned char *p, unsigned v)  /* big-endian: memcmp sorts it */
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

struct pfile *sq_ixfile(struct index *ix)
{
    char fn[40];
    unsigned char *h;
    if (ix->pf) return ix->pf;
    sprintf(fn, "%s.idx", ix->name);
    if (!(ix->pf = sq_pfopen(fn))) fail("index %s is missing", ix->name);
    h = sq_pg(ix->pf, 0);
    if (memcmp(h, "P32I", 4) || get32(h + I_KS) != ix->ks) fail("%s: damaged index", fn);
    return ix->pf;
}

/* the key of a value (not NULL) and rowid */
void sq_key(struct index *ix, val *v, int rid, unsigned char *k)
{
    int l;
    if (ix->ks == 4)
        putbe(k, (unsigned)v->i ^ 0x80000000u);
    else {
        l = strlen(v->s);
        memset(k, 0, TKEY);
        memcpy(k, v->s, l < TKEY ? l : TKEY);
    }
    putbe(k + ix->ks, rid);
}

/* an empty index: its header filled in, a root leaf */
void sq_btinit(struct index *ix)
{
    struct pfile *f = ix->pf;
    unsigned char *h;
    int root = sq_newpage(f);
    sq_touch(f, root)[0] = B_LEAF;
    h = sq_touch(f, 0);
    put32(h + I_ROOT, root);
    put32(h + I_KS, ix->ks);
    put32(h + I_UNIQUE, ix->unique);
    put32(h + I_COL, ix->col);
    strcpy((char *)h + I_NAME, ix->name);
}

static unsigned char *node(struct index *ix, int pg)
{
    unsigned char *p;
    int kl = ix->ks + 4;
    if (pg < 1 || pg >= sq_npages(ix->pf)) fail("index %s is damaged", ix->name);
    p = sq_pg(ix->pf, pg);
    if (!(p[0] == B_LEAF && get16(p + B_N) <= (PGSZ - B_ENT) / kl) &&
        !(p[0] == B_INNER && get16(p + B_N) <= (PGSZ - B_ENT) / (kl + 4)))
        fail("index %s is damaged", ix->name);
    return p;
}

/* the first entry >= k (le: > k) in a node of entries es bytes apart */
static int search(unsigned char *p, int es, int kl, unsigned char *k, int le)
{
    int lo = 0, hi = get16(p + B_N), mid, c;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        c = memcmp(p + B_ENT + mid * es, k, kl);
        if (c < 0 || (le && c == 0)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* the child of an inner node to look in for k */
static int child(unsigned char *p, int kl, unsigned char *k)
{
    int i = search(p, kl + 4, kl, k, 1);
    return i ? get32(p + B_ENT + (i - 1) * (kl + 4) + kl) : get32(p + B_LINK);
}

/* add entry k below page pg; if pg splits: 1, with the new page's first
   key in up and its number in *uppg */
static int bt_ins(struct index *ix, int pg, unsigned char *k, unsigned char *up, int *uppg, int depth)
{
    unsigned char tmp[PGSZ + TKEY + 16], ent[TKEY + 12], *p, *q;
    int kl = ix->ks + 4, es, n, pos, cap, np, half, c;
    if (depth > 16) fail("index %s is damaged", ix->name);
    p = node(ix, pg);
    if (p[0] == B_LEAF) {
        es = kl;
        memcpy(ent, k, kl);
    } else {                            /* the entry to add here is a split child's */
        if (!bt_ins(ix, child(p, kl, k), k, ent, &c, depth + 1)) return 0;
        es = kl + 4;
        put32(ent + kl, c);
        p = node(ix, pg);
    }
    n = get16(p + B_N);
    pos = search(p, es, kl, ent, 0);
    cap = (PGSZ - B_ENT) / es;
    if (n < cap) {
        p = sq_touch(ix->pf, pg);
        memmove(p + B_ENT + (pos + 1) * es, p + B_ENT + pos * es, (n - pos) * es);
        memcpy(p + B_ENT + pos * es, ent, es);
        put16(p + B_N, n + 1);
        return 0;
    }
    /* full: the n + 1 entries in order, then half to a new page */
    memcpy(tmp, p + B_ENT, pos * es);
    memcpy(tmp + pos * es, ent, es);
    memcpy(tmp + (pos + 1) * es, p + B_ENT + pos * es, (n - pos) * es);
    n++;
    half = n / 2;
    np = sq_newpage(ix->pf);
    q = sq_touch(ix->pf, np);
    p = sq_touch(ix->pf, pg);
    q[0] = p[0];
    put16(p + B_N, half);
    memcpy(p + B_ENT, tmp, half * es);
    if (p[0] == B_LEAF) {               /* the new leaf gets the upper half */
        put16(q + B_N, n - half);
        memcpy(q + B_ENT, tmp + half * es, (n - half) * es);
        put32(q + B_LINK, get32(p + B_LINK));
        put32(p + B_LINK, np);
        memcpy(up, q + B_ENT, kl);
    } else {                            /* the middle entry moves up */
        put16(q + B_N, n - half - 1);
        memcpy(q + B_ENT, tmp + (half + 1) * es, (n - half - 1) * es);
        put32(q + B_LINK, get32(tmp + half * es + kl));
        memcpy(up, tmp + half * es, kl);
    }
    *uppg = np;
    return 1;
}

void sq_btinsert(struct index *ix, val *v, int rid)
{
    unsigned char k[TKEY + 8], up[TKEY + 8], *p;
    struct pfile *f = sq_ixfile(ix);
    int root = get32(sq_pg(f, 0) + I_ROOT), np, nr;
    sq_key(ix, v, rid, k);
    if (!bt_ins(ix, root, k, up, &np, 0)) return;
    nr = sq_newpage(f);                 /* the root split: a new root above */
    p = sq_touch(f, nr);
    p[0] = B_INNER;
    put16(p + B_N, 1);
    put32(p + B_LINK, root);
    memcpy(p + B_ENT, up, ix->ks + 4);
    put32(p + B_ENT + ix->ks + 4, np);
    put32(sq_touch(f, 0) + I_ROOT, nr);
}

/* the leaf where key k is or would be */
static int leaf(struct index *ix, unsigned char *k)
{
    int pg = get32(sq_pg(sq_ixfile(ix), 0) + I_ROOT), depth = 0;
    unsigned char *p;
    while ((p = node(ix, pg))[0] == B_INNER) {
        if (++depth > 16) fail("index %s is damaged", ix->name);
        pg = child(p, ix->ks + 4, k);
    }
    return pg;
}

/* remove an entry (nodes are not merged: a leaf may become empty) */
void sq_btdelete(struct index *ix, val *v, int rid)
{
    unsigned char k[TKEY + 8], *p;
    int kl = ix->ks + 4, pg, i, n;
    sq_key(ix, v, rid, k);
    pg = leaf(ix, k);
    p = node(ix, pg);
    i = search(p, kl, kl, k, 0);
    n = get16(p + B_N);
    if (i >= n || memcmp(p + B_ENT + i * kl, k, kl)) return;
    p = sq_touch(ix->pf, pg);
    memmove(p + B_ENT + i * kl, p + B_ENT + (i + 1) * kl, (n - i - 1) * kl);
    put16(p + B_N, n - 1);
}

/* place a cursor before the first entry >= key */
void sq_btseek(struct btcur *c, struct index *ix, unsigned char *key)
{
    c->ix = ix;
    c->pg = leaf(ix, key);
    c->pos = search(node(ix, c->pg), ix->ks + 4, ix->ks + 4, key, 0);
    c->steps = 0;
}

/* the next entry into c->ent: 1, or 0 at the end */
int sq_btnext(struct btcur *c)
{
    unsigned char *p;
    int kl = c->ix->ks + 4;
    while (c->pg) {
        p = node(c->ix, c->pg);
        if (p[0] != B_LEAF) fail("index %s is damaged", c->ix->name);
        if (c->pos < get16(p + B_N)) {
            memcpy(c->ent, p + B_ENT + c->pos++ * kl, kl);
            return 1;
        }
        if (++c->steps > sq_npages(c->ix->pf)) fail("index %s is damaged (a loop)", c->ix->name);
        c->pg = get32(p + B_LINK);
        c->pos = 0;
    }
    return 0;
}
