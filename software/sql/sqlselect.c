/* sqlselect.c -- the SQL library's SELECT (sqldb(3)).
 *
 * A SELECT is read one row at a time, as sql_next asks, unless it has to
 * see every row first (GROUP BY, an aggregate, ORDER BY): then the result
 * is made whole at the first sql_next -- rows grouped in a hash table,
 * each group with the first row it saw and its aggregates' state -- sorted
 * with qsort, and handed out from memory. Two tables are joined by nested
 * loops; the inner table is read by an index when a condition allows it
 * (sqleval.c plans that). DISTINCT keeps a hash table of the rows seen.
 */
#include "sqlint.h"

#define NHASH 1024                  /* buckets for GROUP BY and DISTINCT */

static struct select *sortq;        /* the SELECT being sorted (for rowcmp) */

/* ---------------------------------------------------------------- rows, groups */

static unsigned vhash(val *v, int n)
{
    unsigned h = 5381;
    char *s;
    int i;
    for (i = 0; i < n; i++) {
        h = h * 33 + v[i].t;
        if (v[i].t == SQL_INTEGER) h = h * 33 + (unsigned)v[i].i;
        else if (v[i].t == SQL_TEXT)
            for (s = v[i].s; *s; s++) h = h * 33 + (unsigned char)*s;
    }
    return h;
}

int sq_vsame(val *a, val *b)
{
    if (a->t != b->t) return 0;
    if (a->t == SQL_INTEGER) return a->i == b->i;
    return a->t == SQL_NULL || !strcmp(a->s, b->s);
}

/* copy values into the statement's memory, to keep */
static void vkeep(val *d, val *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
        if (s[i].t == SQL_TEXT) d[i].s = astrndup(&S->a, s[i].s, strlen(s[i].s));
    }
}

struct drow { struct drow *next; unsigned h; val v[1]; };

/* DISTINCT: has this row been seen? (it is kept if not) */
static int seen(struct sqlstmt *st, val *v, int n)
{
    unsigned h = vhash(v, n);
    struct drow *d, **b = (struct drow **)&st->dhash[h % NHASH];
    int i;
    for (d = *b; d; d = d->next) {
        if (d->h != h) continue;
        for (i = 0; i < n && sq_vsame(&d->v[i], &v[i]); i++) ;
        if (i == n) return 1;
    }
    d = aalloc(&S->a, sizeof *d + n * sizeof(val));
    d->h = h;
    vkeep(d->v, v, n);
    d->next = *b;
    *b = d;
    return 0;
}

/* add a row to the result kept in memory (with its number, to sort stably) */
void sq_keep(struct sqlstmt *st, val *o)
{
    val **nr, *r;
    if (st->nrows == st->cap) {
        st->cap = st->cap ? 2 * st->cap : 64;
        nr = aalloc(&S->a, st->cap * sizeof *nr);
        if (st->nrows) memcpy(nr, st->rows, st->nrows * sizeof *nr);
        st->rows = nr;
    }
    r = aalloc(&S->a, (st->nout + 1) * sizeof *r);
    vkeep(r, o, st->nout);
    r[st->nout].t = SQL_INTEGER;
    r[st->nout].i = st->nrows;
    st->rows[st->nrows++] = r;
    st->mat = 1;
}

/* the group a row belongs to (made if new) */
static struct group *group_of(struct sqlstmt *st, struct ctx *cx)
{
    struct select *q = st->s->sel;
    struct group *g, **b;
    val *k = st->gkey;
    int i;
    for (i = 0; i < q->ngroup; i++) sq_eval(cx, q->group[i], &k[i]);
    b = &st->ghash[vhash(k, q->ngroup) % NHASH];
    for (g = *b; g; g = g->next) {
        for (i = 0; i < q->ngroup && sq_vsame(&g->key[i], &k[i]); i++) ;
        if (i == q->ngroup) return g;
    }
    g = zalloc(sizeof *g);
    g->key = aalloc(&S->a, (q->ngroup + 1) * sizeof(val));
    vkeep(g->key, k, q->ngroup);
    g->row = aalloc(&S->a, (st->nslot + 1) * sizeof(val));
    vkeep(g->row, st->row, st->nslot);  /* (for columns outside aggregates: the first row's) */
    g->acc = zalloc((q->agg.n + 1) * sizeof(struct acc));
    g->next = *b;
    *b = g;
    *st->gtail = g;
    st->gtail = &g->lnext;
    return g;
}

/* add a row to its group's aggregates */
static void accumulate(struct sqlstmt *st, struct group *g, struct ctx *cx)
{
    struct select *q = st->s->sel;
    struct expr *e;
    struct acc *a;
    val v;
    int i, x, l;
    for (i = 0; i < q->agg.n; i++) {
        e = q->agg.v[i];
        a = &g->acc[i];
        if (!e->l) {                    /* COUNT(*) */
            a->n++;
            continue;
        }
        sq_eval(cx, e->l, &v);
        if (v.t == SQL_NULL) continue;
        if (e->fn == A_SUM || e->fn == A_AVG) {
            x = sq_int(&v);
            if (((a->sum ^ (int)((unsigned)a->sum + x)) & (x ^ (int)((unsigned)a->sum + x))) < 0)
                fail("integer overflow in %s()", e->name);
            a->sum += x;
        } else if ((e->fn == A_MIN || e->fn == A_MAX) &&
                   (!a->n || (e->fn == A_MIN ? sq_cmp(&v, &a->m) < 0 : sq_cmp(&v, &a->m) > 0))) {
            a->m = v;
            if (v.t == SQL_TEXT) {      /* keep the text (in a buffer that grows) */
                l = strlen(v.s) + 1;
                if (l > a->cap) {
                    a->cap = l > 2 * a->cap ? l : 2 * a->cap;
                    a->buf = aalloc(&S->a, a->cap);
                }
                strcpy(a->buf, v.s);
                a->m.s = a->buf;
            }
        }
        a->n++;
    }
}

/* ---------------------------------------------------------------- SELECT */

static char *srcname(struct src *s) { return s->alias ? s->alias : s->name; }

/* the tables, *, names, the plan */
static void prep_select(struct sqlstmt *st, struct select *q)
{
    struct vec res, alias;
    struct expr *e, *c;
    struct src *s;
    int i, j, k, n, base = 0;
    for (i = 0; i < q->nsrc; i++) {
        q->src[i].t = sq_table(q->src[i].name, 1);
        q->src[i].base = base;
        base += q->src[i].t->ncols;
    }
    st->nslot = base;
    if (q->nsrc == 2 && ieq(srcname(&q->src[0]), srcname(&q->src[1])))
        fail("%s is named twice: give one an alias", srcname(&q->src[0]));
    memset(&res, 0, sizeof res);        /* * and t.* become the columns */
    memset(&alias, 0, sizeof alias);
    for (i = 0; i < q->nres; i++) {
        e = q->res[i];
        if (e->op != E_STAR) {
            vpush(&res, e);
            vpush(&alias, q->alias[i]);
            continue;
        }
        for (k = n = 0; k < q->nsrc; k++) {
            s = &q->src[k];
            if (e->qual && !ieq(e->qual, srcname(s))) continue;
            for (j = 0, n++; j < s->t->ncols; j++) {
                c = zalloc(sizeof *c);
                c->op = E_COL;
                c->name = s->t->col[j].name;
                c->qual = srcname(s);
                vpush(&res, c);
                vpush(&alias, 0);
            }
        }
        if (!n) fail(e->qual ? "no such table: %s" : "* needs a table (FROM)", e->qual);
    }
    q->nres = res.n;
    q->res = (struct expr **)res.v;
    q->alias = (char **)alias.v;
    for (k = 0; k < q->norder; k++) {   /* ORDER BY 2, ORDER BY an alias */
        e = q->order[k];
        q->opos[k] = -1;
        if (e->op == E_LIT && e->v.t == SQL_INTEGER) {
            if (e->v.i < 1 || e->v.i > q->nres)
                fail("ORDER BY %d: not a column number (1 to %d)", e->v.i, q->nres);
            q->opos[k] = e->v.i - 1;
        } else if (e->op == E_COL && !e->qual)
            for (i = 0; i < q->nres; i++)
                if (q->alias[i] && ieq(q->alias[i], e->name)) { q->opos[k] = i; break; }
    }
    for (i = 0; i < q->nres; i++) sq_bind(q, q->src, q->nsrc, q->res[i], 1);
    if (q->nsrc == 2) sq_bind(q, q->src, 2, q->src[1].on, 0);
    sq_bind(q, q->src, q->nsrc, q->where, 0);
    for (i = 0; i < q->ngroup; i++) sq_bind(q, q->src, q->nsrc, q->group[i], 0);
    sq_bind(q, q->src, q->nsrc, q->having, 1);
    for (k = 0; k < q->norder; k++)
        if (q->opos[k] < 0) sq_bind(q, q->src, q->nsrc, q->order[k], 1);
    sq_bind(0, 0, 0, q->limit, 0);
    sq_bind(0, 0, 0, q->offset, 0);
    q->grouped = q->ngroup || q->agg.n;
    if (q->having && !q->grouped) fail("HAVING needs GROUP BY or an aggregate");
    q->cname = zalloc(q->nres * sizeof *q->cname);
    for (i = 0; i < q->nres; i++) {
        e = q->res[i];
        q->cname[i] = q->alias[i] ? q->alias[i] : e->op == E_COL ? e->name :
                      astrndup(&S->a, st->src + e->start, e->end - e->start);
    }
    sq_plan(q, q->where);
}

/* EXPLAIN: the plan as rows of text */
static void explain(struct sqlstmt *st, struct select *q)
{
    static char *plan[] = { "plan" };
    char b[120], *t;
    struct access *a;
    struct src *s;
    val v;
    int l, n;
    st->ncols = st->nout = 1;
    st->cname = plan;
    v.t = SQL_TEXT;
    v.s = b;
    for (l = 0; l < q->nsrc; l++) {
        s = &q->src[q->ord[l]];
        a = &q->acc[q->ord[l]];
        n = sprintf(b, "%s%s %s", l ? (s->join == 2 ? "LEFT JOIN " : "JOIN ") : "",
                    a->ix ? "SEARCH" : "SCAN", s->name);
        if (s->alias) n += sprintf(b + n, " AS %s", s->alias);
        if (a->ix) {
            t = s->t->col[a->ix->col].name;
            n += sprintf(b + n, " USING INDEX %s (", a->ix->name);
            if (a->lo == a->hi) sprintf(b + n, "%s = ?)", t);
            else if (a->lo && a->hi) sprintf(b + n, "%s BETWEEN ? AND ?)", t);
            else sprintf(b + n, "%s %s ?)", t, a->lo ? ">" : "<");
        }
        sq_keep(st, &v);
    }
    if (q->grouped) { v.s = q->ngroup ? "GROUP BY" : "AGGREGATE"; sq_keep(st, &v); }
    if (q->distinct) { v.s = "DISTINCT"; sq_keep(st, &v); }
    if (q->norder) { v.s = "SORT"; sq_keep(st, &v); }
    if (!st->nrows) { v.s = "ONE ROW"; sq_keep(st, &v); }
}

void sq_startsel(struct sqlstmt *st)
{
    struct select *q = st->s->sel;
    struct ctx cx;
    val v;
    prep_select(st, q);
    if (q->explain) {
        explain(st, q);
        return;
    }
    st->ncols = q->nres;
    st->cname = q->cname;
    st->nout = q->nres + q->norder;
    st->out = zalloc((st->nout + 1) * sizeof(val));
    st->cur = zalloc(2 * sizeof *st->cur);
    cx.row = st->row;
    cx.g = 0;
    st->limit = -1;
    if (q->limit) {
        sq_eval(&cx, q->limit, &v);
        if (v.t != SQL_NULL) st->limit = sq_int(&v);
    }
    if (q->offset) {
        sq_eval(&cx, q->offset, &v);
        if (v.t != SQL_NULL) st->skip = sq_int(&v);
    }
    if (q->distinct) st->dhash = zalloc(NHASH * sizeof *st->dhash);
    if (q->nsrc) sq_tcopen(&st->cur[0], &q->src[q->ord[0]], &q->acc[q->ord[0]], st->row);
}

/* the next row of the tables (joined): 1, or 0 at the end */
static int scan_next(struct sqlstmt *st)
{
    struct select *q = st->s->sel;
    struct src *in = &q->src[q->ord[1]];
    struct ctx cx;
    val v;
    int i;
    if (!q->nsrc) {                     /* SELECT without FROM: one row */
        if (st->state) return 0;
        return st->state = 1;
    }
    cx.row = st->row;
    cx.g = 0;
    for (;;) {
        if (!st->state) {
            if (!sq_tcnext(&st->cur[0], st->row)) return 0;
            if (q->nsrc == 1) return 1;
            sq_tcopen(&st->cur[1], in, &q->acc[q->ord[1]], st->row);
            st->state = 1;
            st->matched = 0;
        }
        if (sq_tcnext(&st->cur[1], st->row)) {
            if (q->src[1].on) {
                sq_eval(&cx, q->src[1].on, &v);
                if (!sq_truth(&v)) continue;
            }
            st->matched = 1;
            return 1;
        }
        st->state = 0;
        if (q->src[1].join == 2 && !st->matched) {     /* LEFT JOIN: the left row with NULLs */
            for (i = 0; i < q->src[1].t->ncols; i++) st->row[q->src[1].base + i].t = SQL_NULL;
            return 1;
        }
    }
}

/* the result columns (and ORDER BY values) for the row or group in cx */
static void make_out(struct sqlstmt *st, struct ctx *cx, val *o)
{
    struct select *q = st->s->sel;
    int i;
    for (i = 0; i < q->nres; i++) sq_eval(cx, q->res[i], &o[i]);
    for (i = 0; i < q->norder; i++) {
        if (q->opos[i] >= 0) o[q->nres + i] = o[q->opos[i]];
        else sq_eval(cx, q->order[i], &o[q->nres + i]);
    }
}

static int rowcmp(const void *x, const void *y)
{
    val *a = *(val **)x, *b = *(val **)y, *u, *v;
    int k, c, n = sortq->nres;
    for (k = 0; k < sortq->norder; k++) {
        u = &a[n + k];
        v = &b[n + k];
        if (u->t == SQL_NULL || v->t == SQL_NULL) c = (u->t != SQL_NULL) - (v->t != SQL_NULL);
        else c = sq_cmp(u, v);          /* (NULL first) */
        if (c) return sortq->desc[k] ? -c : c;
    }
    n += sortq->norder;
    return a[n].i - b[n].i;
}

/* the whole result, grouped and sorted, into st->rows */
static void materialize(struct sqlstmt *st)
{
    struct select *q = st->s->sel;
    struct group *g;
    struct ctx cx;
    val v;
    int i;
    cx.row = st->row;
    cx.g = 0;
    st->mat = 1;
    if (q->grouped) {
        st->ghash = zalloc(NHASH * sizeof *st->ghash);
        st->gtail = &st->glist;
        st->gkey = zalloc((q->ngroup + 1) * sizeof(val));
    }
    for (;;) {
        arelease(&S->t, &st->tm);
        if (!scan_next(st)) break;
        if (q->where) {
            sq_eval(&cx, q->where, &v);
            if (!sq_truth(&v)) continue;
        }
        if (q->grouped) accumulate(st, group_of(st, &cx), &cx);
        else {
            make_out(st, &cx, st->out);
            if (!q->distinct || !seen(st, st->out, q->nres)) sq_keep(st, st->out);
        }
    }
    if (q->grouped) {
        if (!q->ngroup && !st->glist) { /* an aggregate of no rows: one row still */
            for (i = 0; i < st->nslot; i++) st->row[i].t = SQL_NULL;
            group_of(st, &cx);
        }
        for (g = st->glist; g; g = g->lnext) {
            arelease(&S->t, &st->tm);
            cx.row = g->row;
            cx.g = g;
            if (q->having) {
                sq_eval(&cx, q->having, &v);
                if (!sq_truth(&v)) continue;
            }
            make_out(st, &cx, st->out);
            if (!q->distinct || !seen(st, st->out, q->nres)) sq_keep(st, st->out);
        }
    }
    if (q->norder && st->nrows > 1) {
        sortq = q;
        qsort(st->rows, st->nrows, sizeof *st->rows, rowcmp);
    }
    arelease(&S->t, &st->tm);
}

/* the next result row into st->out: 1, or 0 at the end */
int sq_nextrow(struct sqlstmt *st)
{
    struct select *q = st->s->sel;
    struct ctx cx;
    val v;
    if (!st->started) {
        st->started = 1;
        if (q && !st->mat && (q->grouped || q->norder)) materialize(st);
    }
    if (st->limit >= 0 && st->emitted >= st->limit) return 0;
    if (st->mat) {
        if (st->skip > 0) st->pos += st->skip;
        st->skip = 0;
        if (st->pos >= st->nrows) return 0;
        st->out = st->rows[st->pos++];
        st->emitted++;
        return 1;
    }
    cx.row = st->row;
    cx.g = 0;
    for (;;) {
        arelease(&S->t, &st->tm);
        if (!scan_next(st)) return 0;
        if (q->where) {
            sq_eval(&cx, q->where, &v);
            if (!sq_truth(&v)) continue;
        }
        make_out(st, &cx, st->out);
        if (q->distinct && seen(st, st->out, q->nres)) continue;
        if (st->skip > 0) {
            st->skip--;
            continue;
        }
        st->emitted++;
        return 1;
    }
}

