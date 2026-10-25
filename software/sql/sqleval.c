/* sqleval.c -- the SQL library's expressions (sqldb(3)): values, operators,
 * functions, column names, and reading tables (the planner chooses an index
 * for a condition; a cursor walks the rows).
 *
 * A value is NULL, INTEGER (32 bits) or TEXT. NULL makes most results NULL,
 * and AND, OR and NOT use three-valued logic. A TEXT holding a whole number
 * counts as that number in arithmetic and when compared with an INTEGER;
 * other TEXT sorts after every INTEGER. TEXT compares byte by byte (LIKE
 * ignores the case of ASCII letters). Arithmetic that overflows 32 bits is
 * an error, and so is division by zero.
 */
#include <limits.h>
#include "sqlint.h"

/* ---------------------------------------------------------------- values */

/* a value as text (an INTEGER is written in the row's temporary memory) */
char *sq_text(val *v)
{
    char *b;
    if (v->t == SQL_TEXT) return v->s;
    if (v->t == SQL_NULL) return "";
    b = aalloc(&S->t, 12);
    sprintf(b, "%d", v->i);
    return b;
}

/* is s a whole number (spaces around it allowed)? its value in *out */
int sq_toint(const char *s, int *out)
{
    unsigned v = 0, lim;
    int neg = 0, nd = 0;
    while (*s == ' ') s++;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    lim = neg ? 2147483648u : 2147483647u;
    for (; *s >= '0' && *s <= '9'; s++, nd++) {
        if (v > (lim - (*s - '0')) / 10) return 0;
        v = v * 10 + (*s - '0');
    }
    while (*s == ' ') s++;
    if (!nd || *s) return 0;
    *out = neg ? (int)(0u - v) : (int)v;
    return 1;
}

int sq_int(val *v)
{
    int x;
    if (v->t == SQL_INTEGER) return v->i;
    if (v->t == SQL_TEXT && !sq_toint(v->s, &x)) fail("not a number: '%.30s'", v->s);
    return v->t == SQL_TEXT ? x : 0;
}

/* compare two values that are not NULL: -1, 0, 1 */
int sq_cmp(val *a, val *b)
{
    int x, y, c;
    if (a->t == SQL_TEXT && b->t == SQL_TEXT) {
        c = strcmp(a->s, b->s);
        return c < 0 ? -1 : c > 0;
    }
    if (a->t == SQL_INTEGER) x = a->i;
    else if (!sq_toint(a->s, &x)) return 1;     /* text after numbers */
    if (b->t == SQL_INTEGER) y = b->i;
    else if (!sq_toint(b->s, &y)) return -1;
    return x < y ? -1 : x > y;
}

/* is a value true (a number not 0)? */
int sq_truth(val *v)
{
    int x;
    if (v->t == SQL_INTEGER) return v->i != 0;
    return v->t == SQL_TEXT && sq_toint(v->s, &x) && x != 0;
}

static void setint(val *r, int i)
{
    r->t = SQL_INTEGER;
    r->i = i;
}

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* s LIKE p: % any run of characters, _ any one; case is ignored */
static int like(const char *s, const char *p)
{
    const char *sp = 0, *pp = 0;        /* where to try again after the last % */
    while (*s) {
        if (*p == '%') {
            while (*p == '%') p++;
            if (!*p) return 1;
            pp = p;
            sp = s;
        } else if (*p && (*p == '_' || lc(*p) == lc(*s))) {
            p++;
            s++;
        } else if (pp) {                /* let the % take one more character */
            p = pp;
            s = ++sp;
        } else
            return 0;
    }
    while (*p == '%') p++;
    return !*p;
}

/* x op y, with overflow checked */
static int arith(int op, int x, int y)
{
    unsigned r;
    switch (op) {
    case E_ADD:
        r = (unsigned)x + (unsigned)y;
        if (((x ^ (int)r) & (y ^ (int)r)) < 0) break;
        return (int)r;
    case E_SUB:
        r = (unsigned)x - (unsigned)y;
        if (((x ^ y) & (x ^ (int)r)) < 0) break;
        return (int)r;
    case E_MUL:
        r = (unsigned)x * (unsigned)y;
        if (x && ((x == -1 && y == INT_MIN) || (int)r / x != y)) break;
        return (int)r;
    case E_DIV:
    case E_MOD:
        if (!y) fail("division by zero");
        if (y == -1) {
            if (op == E_MOD) return 0;
            if (x == INT_MIN) break;
            return -x;
        }
        return op == E_DIV ? x / y : x % y;
    }
    fail("integer overflow");
    return 0;
}

/* a function's value */
static void func(struct ctx *c, struct expr *e, val *r)
{
    val a[8];
    char *s, *d;
    int i, n, st, len;
    if (e->fn == F_COALESCE || e->fn == F_IFNULL) {    /* the first that is not NULL */
        for (i = 0; i < e->n; i++) {
            sq_eval(c, e->list[i], r);
            if (r->t != SQL_NULL) return;
        }
        return;
    }
    for (i = 0; i < e->n; i++) {
        sq_eval(c, e->list[i], &a[i]);
        if (a[i].t == SQL_NULL) {
            r->t = SQL_NULL;
            return;
        }
    }
    switch (e->fn) {
    case F_UPPER:
    case F_LOWER:
        s = sq_text(&a[0]);
        r->t = SQL_TEXT;
        r->s = d = aalloc(&S->t, strlen(s) + 1);
        for (; *s; s++)
            *d++ = e->fn == F_LOWER ? lc(*s) : *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
        *d = 0;
        return;
    case F_LENGTH:
        setint(r, strlen(sq_text(&a[0])));
        return;
    case F_ABS:
        i = sq_int(&a[0]);
        if (i == INT_MIN) fail("integer overflow");
        setint(r, i < 0 ? -i : i);
        return;
    case F_SUBSTR:                      /* SUBSTR(s, start [, length]), from 1; start < 0 from the end */
        s = sq_text(&a[0]);
        n = strlen(s);
        st = sq_int(&a[1]);
        len = e->n > 2 ? sq_int(&a[2]) : n;
        if (st < 0) st += n + 1;
        if (st < 1) {
            len += st - 1;
            st = 1;
        }
        if (len < 0 || st > n) len = 0;
        else if (len > n - st + 1) len = n - st + 1;
        r->t = SQL_TEXT;
        r->s = astrndup(&S->t, st > n ? s + n : s + st - 1, len);
        return;
    }
}

/* an aggregate's value, for the group being output */
static void aggval(struct ctx *c, struct expr *e, val *r)
{
    struct acc *a;
    if (!c->g) fail("%s() is not allowed here", e->name);
    a = &c->g->acc[e->slot];
    if (e->fn == A_COUNT) setint(r, a->n);
    else if (!a->n) r->t = SQL_NULL;
    else if (e->fn == A_SUM) setint(r, a->sum);
    else if (e->fn == A_AVG) setint(r, a->sum / a->n);     /* (an integer: truncated) */
    else *r = a->m;
}

/* the value of an expression for the row in c */
void sq_eval(struct ctx *c, struct expr *e, val *r)
{
    val a, b, h;
    int x, y, i, k;
    char *s, *t;
    switch (e->op) {
    case E_LIT:
        *r = e->v;
        return;
    case E_COL:
        *r = c->row[e->slot];
        return;
    case E_AGG:
        aggval(c, e, r);
        return;
    case E_FUNC:
        func(c, e, r);
        return;
    case E_AND:
    case E_OR:                          /* x, y: 1 true, 0 false, -1 NULL */
        k = e->op == E_OR;
        sq_eval(c, e->l, &a);
        x = a.t == SQL_NULL ? -1 : sq_truth(&a);
        if (x == k) { setint(r, k); return; }
        sq_eval(c, e->r, &b);
        y = b.t == SQL_NULL ? -1 : sq_truth(&b);
        if (y == k) setint(r, k);
        else if (x < 0 || y < 0) r->t = SQL_NULL;
        else setint(r, !k);
        return;
    case E_NOT:
        sq_eval(c, e->l, &a);
        if (a.t == SQL_NULL) *r = a;
        else setint(r, !sq_truth(&a));
        return;
    case E_ISNULL:
    case E_NOTNULL:
        sq_eval(c, e->l, &a);
        setint(r, (a.t == SQL_NULL) == (e->op == E_ISNULL));
        return;
    case E_NEG:
        sq_eval(c, e->l, &a);
        if (a.t == SQL_NULL) *r = a;
        else setint(r, arith(E_SUB, 0, sq_int(&a)));
        return;
    case E_IN:
        sq_eval(c, e->l, &a);
        if (a.t == SQL_NULL) { *r = a; return; }
        for (i = k = x = 0; i < e->n && !k; i++) {
            sq_eval(c, e->list[i], &b);
            if (b.t == SQL_NULL) x = 1;
            else k = !sq_cmp(&a, &b);
        }
        if (!k && x) r->t = SQL_NULL;   /* not found, but a NULL might have matched */
        else setint(r, k ^ e->not);
        return;
    case E_BETWEEN:
        sq_eval(c, e->l, &a);
        sq_eval(c, e->list[0], &b);
        sq_eval(c, e->list[1], &h);
        x = a.t == SQL_NULL || b.t == SQL_NULL ? -1 : sq_cmp(&a, &b) >= 0;
        y = a.t == SQL_NULL || h.t == SQL_NULL ? -1 : sq_cmp(&a, &h) <= 0;
        if (!x || !y) setint(r, e->not);
        else if (x < 0 || y < 0) r->t = SQL_NULL;
        else setint(r, !e->not);
        return;
    }
    /* the binary operators */
    sq_eval(c, e->l, &a);
    sq_eval(c, e->r, &b);
    if (a.t == SQL_NULL || b.t == SQL_NULL) {
        r->t = SQL_NULL;
        return;
    }
    switch (e->op) {
    case E_EQ: setint(r, sq_cmp(&a, &b) == 0); return;
    case E_NE: setint(r, sq_cmp(&a, &b) != 0); return;
    case E_LT: setint(r, sq_cmp(&a, &b) < 0); return;
    case E_LE: setint(r, sq_cmp(&a, &b) <= 0); return;
    case E_GT: setint(r, sq_cmp(&a, &b) > 0); return;
    case E_GE: setint(r, sq_cmp(&a, &b) >= 0); return;
    case E_LIKE:
        setint(r, like(sq_text(&a), sq_text(&b)) ^ e->not);
        return;
    case E_CAT:
        s = sq_text(&a);
        t = sq_text(&b);
        x = strlen(s);
        y = strlen(t);
        if (x + y > MAXSTR) fail("string too long (the most is %d)", MAXSTR);
        r->t = SQL_TEXT;
        r->s = aalloc(&S->t, x + y + 1);
        memcpy(r->s, s, x);
        memcpy(r->s + x, t, y + 1);
        return;
    }
    setint(r, arith(e->op, sq_int(&a), sq_int(&b)));
}

/* ---------------------------------------------------------------- names */

/* find the column an E_COL names, in the tables of src */
static void resolve(struct src *src, int nsrc, struct expr *e)
{
    struct src *s;
    int i, j, found = 0;
    for (i = 0; i < nsrc; i++) {
        s = &src[i];
        if (e->qual && !ieq(e->qual, s->alias ? s->alias : s->name)) continue;
        for (j = 0; j < s->t->ncols; j++)
            if (ieq(e->name, s->t->col[j].name)) {
                if (found) fail("ambiguous column name: %s", e->name);
                found = 1;
                e->tab = i;
                e->col = j;
                e->slot = s->base + j;
            }
    }
    if (!found) fail("no such column: %s%s%s", e->qual ? e->qual : "", e->qual ? "." : "", e->name);
}

/* resolve an expression's column names; collect its aggregates in q
   (aggok: they are allowed here) */
void sq_bind(struct select *q, struct src *src, int nsrc, struct expr *e, int aggok)
{
    int i;
    if (!e) return;
    switch (e->op) {
    case E_COL:
        resolve(src, nsrc, e);
        return;
    case E_STAR:
        fail("* is not allowed here");
    case E_AGG:
        if (!aggok || !q) fail("%s() is not allowed here", e->name);
        sq_bind(q, src, nsrc, e->l, 0);
        e->slot = q->agg.n;
        vpush(&q->agg, e);
        return;
    }
    sq_bind(q, src, nsrc, e->l, aggok);
    sq_bind(q, src, nsrc, e->r, aggok);
    for (i = 0; i < e->n; i++) sq_bind(q, src, nsrc, e->list[i], aggok);
}

/* ---------------------------------------------------------------- the planner */

/* the tables an expression reads: bit 0, bit 1 (bit 2: an aggregate) */
static int tmask(struct expr *e)
{
    int m, i;
    if (!e) return 0;
    if (e->op == E_COL) return 1 << e->tab;
    if (e->op == E_AGG) return 4;
    m = tmask(e->l) | tmask(e->r);
    for (i = 0; i < e->n; i++) m |= tmask(e->list[i]);
    return m;
}

static int colof(struct expr *e, int tab)
{
    return e->op == E_COL && e->tab == tab ? e->col : -1;
}

struct index *sq_findix(struct table *t, int col)
{
    int i;
    for (i = 0; i < t->nidx; i++)
        if (t->idx[i].col == col) return &t->idx[i];
    return 0;
}

/* the conditions ANDed together in e */
static void conj(struct expr *e, struct vec *v)
{
    if (!e) return;
    if (e->op == E_AND) {
        conj(e->l, v);
        conj(e->r, v);
    } else
        vpush(v, e);
}

/* how to read table tab of s, given the conditions cj, when the tables in
   outer have their row already: an index for col = x (best) or a range
   col > x, col < x, col BETWEEN x AND y, where x reads only outer tables */
static void plantab(struct src *s, int tab, int outer, struct vec *cj, struct access *a)
{
    struct expr *e, *x;
    struct index *ix;
    int i, c, o;
    a->ix = 0;
    a->lo = a->hi = 0;
    for (i = 0; i < cj->n; i++) {
        e = cj->v[i];
        if (e->op == E_BETWEEN && !e->not) {
            if ((c = colof(e->l, tab)) < 0 || (tmask(e->list[0]) | tmask(e->list[1])) & ~outer) continue;
            if (!(ix = sq_findix(s->t, c)) || (a->ix && a->ix != ix)) continue;
            a->ix = ix;
            a->lo = e->list[0];
            a->hi = e->list[1];
            continue;
        }
        if (e->op != E_EQ && (e->op < E_LT || e->op > E_GE)) continue;
        o = e->op;
        if ((c = colof(e->l, tab)) >= 0 && !(tmask(e->r) & ~outer))
            x = e->r;
        else if ((c = colof(e->r, tab)) >= 0 && !(tmask(e->l) & ~outer)) {
            x = e->l;                   /* 5 < col is col > 5 */
            o = o == E_LT ? E_GT : o == E_GT ? E_LT : o == E_LE ? E_GE : o == E_GE ? E_LE : o;
        } else
            continue;
        if (!(ix = sq_findix(s->t, c))) continue;
        if (o == E_EQ) {
            a->ix = ix;
            a->lo = a->hi = x;
            return;
        }
        if (a->ix && a->ix != ix) continue;
        a->ix = ix;
        if (o == E_GT || o == E_GE) a->lo = x;
        else a->hi = x;
    }
}

/* plan a SELECT: an access for each table, and the order of the loops */
void sq_plan(struct select *q, struct expr *where)
{
    struct vec w, all;
    struct access other;
    int i;
    memset(&w, 0, sizeof w);
    memset(&all, 0, sizeof all);
    conj(where, &w);
    q->ord[0] = 0;
    q->ord[1] = 1;
    if (q->nsrc == 1)
        plantab(&q->src[0], 0, 0, &w, &q->acc[0]);
    else if (q->nsrc == 2 && q->src[1].join == 2) {    /* LEFT JOIN: ON finds the right rows */
        plantab(&q->src[0], 0, 0, &w, &q->acc[0]);
        conj(q->src[1].on, &all);
        plantab(&q->src[1], 1, 1, &all, &q->acc[1]);
    } else if (q->nsrc == 2) {
        for (i = 0; i < w.n; i++) vpush(&all, w.v[i]);
        conj(q->src[1].on, &all);
        plantab(&q->src[1], 1, 1, &all, &q->acc[1]);
        plantab(&q->src[0], 0, 2, &all, &other);
        if (!q->acc[1].ix && other.ix) {    /* the second table first is better */
            q->ord[0] = 1;
            q->ord[1] = 0;
            q->acc[0] = other;
            plantab(&q->src[1], 1, 0, &all, &q->acc[1]);
        } else
            plantab(&q->src[0], 0, 0, &all, &q->acc[0]);
    }
}

/* plan the one table of an UPDATE or DELETE */
void sq_plan1(struct src *s, struct expr *where, struct access *a)
{
    struct vec w;
    memset(&w, 0, sizeof w);
    conj(where, &w);
    plantab(s, 0, 0, &w, a);
}

/* ---------------------------------------------------------------- cursors */

/* can v bound index ix? (converted to its type) */
static int keyable(struct index *ix, val *v)
{
    int x;
    if (ix->ks != 4) return v->t == SQL_TEXT;
    if (v->t == SQL_TEXT) {
        if (!sq_toint(v->s, &x)) return 0;
        v->t = SQL_INTEGER;
        v->i = x;
    }
    return 1;
}

/* start reading a table, by a (if not 0), with the outer row in row */
void sq_tcopen(struct tcur *c, struct src *s, struct access *a, val *row)
{
    struct ctx cx;
    val lo, hi;
    unsigned char k[TKEY + 8];
    c->t = s->t;
    c->base = s->base;
    c->mode = 1;
    c->pg = 1;
    c->slot = 0;
    if (!a || !a->ix) return;
    cx.row = row;
    cx.g = 0;
    if (a->lo) sq_eval(&cx, a->lo, &lo);
    if (a->hi) sq_eval(&cx, a->hi, &hi);
    if ((a->lo && lo.t == SQL_NULL) || (a->hi && hi.t == SQL_NULL)) {
        c->mode = 0;                    /* col = NULL is never true */
        return;
    }
    if ((a->lo && !keyable(a->ix, &lo)) || (a->hi && !keyable(a->ix, &hi)))
        return;                         /* other types: every row is read */
    sq_ixfile(a->ix);
    if (a->lo) sq_key(a->ix, &lo, 0, k);
    else memset(k, 0, sizeof k);
    sq_btseek(&c->bc, a->ix, k);
    if ((c->hashi = a->hi != 0)) sq_key(a->ix, &hi, 0, c->hik);
    c->mode = 2;
}

/* the next row into row (from c->base): 1, or 0 at the end. An index's
   bounds are inclusive and TEXT keys are cut short, so the caller still
   tests its conditions on every row. */
int sq_tcnext(struct tcur *c, val *row)
{
    unsigned char *p;
    int n, len, off, ks;
    if (c->mode == 1) {
        while (c->pg < sq_npages(c->t->pf)) {
            p = sq_dpage(c->t, c->pg);
            n = get16(p);
            while (c->slot < n) {
                len = get16(p + D_SLOTS + 4 * c->slot + 2);
                off = get16(p + D_SLOTS + 4 * c->slot);
                c->rid = c->pg << 8 | c->slot++;
                if (!len) continue;
                if (off < D_SLOTS + 4 * n || off + len > PGSZ)
                    fail("%s.tbl: page %d is damaged", c->t->name, c->pg);
                memcpy(c->rec, p + off, len);
                sq_readrec(c->t, c->rec, len, row + c->base);
                return 1;
            }
            c->pg++;
            c->slot = 0;
        }
    } else if (c->mode == 2) {
        ks = c->bc.ix->ks;
        if (sq_btnext(&c->bc) && !(c->hashi && memcmp(c->bc.ent, c->hik, ks) > 0)) {
            c->rid = getbe(c->bc.ent + ks);
            if (!(len = sq_fetch(c->t, c->rid, c->rec)))
                fail("index %s is damaged (row %d is missing)", c->bc.ix->name, c->rid);
            sq_readrec(c->t, c->rec, len, row + c->base);
            return 1;
        }
    }
    c->mode = 0;
    return 0;
}
