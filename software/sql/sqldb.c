/* sqldb.c -- a small SQL database for P32W Unix (sqldb(3)): running the
 * statements, and the functions a program calls.
 *
 *   sqldb *db = sql_open("dir", 1);
 *   sql_exec(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");
 *   st = sql_query(db, "SELECT name FROM t WHERE id < 10");
 *   while (sql_next(st) > 0) puts(sql_text(st, 0));
 *   sql_finish(st);
 *
 * INSERT checks each row (types, NOT NULL, lengths, UNIQUE) before it is
 * stored and indexed. UPDATE and DELETE first collect the rowids of the
 * rows to change, then change them (so a row that moves is not met
 * again). SELECT is in sqlselect.c.
 *
 * A statement that changes the database takes the lock, and (outside
 * BEGIN ... COMMIT) commits when it ends; if it fails, what it did is
 * rolled back -- inside a transaction, the whole transaction is.
 */
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "sqlint.h"

static char openerr[240];           /* why sql_open failed */

static int changed(val *a, val *b)
{
    return !sq_vsame(a, b);
}

/* ---------------------------------------------------------------- INSERT, UPDATE, DELETE */

static void begin_write(void)
{
    if (!D->intxn) sq_lock();
    D->stmtw = 1;
}

static int findcol(struct table *t, char *name)
{
    int j;
    for (j = 0; j < t->ncols; j++)
        if (ieq(t->col[j].name, name)) return j;
    fail("no such column: %s.%s", t->name, name);
    return -1;
}

static int pkcol(struct table *t)
{
    int i;
    for (i = 0; i < t->ncols; i++)
        if (t->col[i].flags & C_PK) return i;
    return -1;
}

/* an INTEGER PRIMARY KEY's next number: past the largest given */
static void autonext(struct table *t, int pk, val *v)
{
    if (pk >= 0 && t->col[pk].type == SQL_INTEGER && v[pk].i >= sq_th(t, T_AUTO) && v[pk].i < 2147483647)
        sq_thset(t, T_AUTO, v[pk].i + 1);
}

/* make a row fit its table: types, NOT NULL, lengths */
static void check_row(struct table *t, val *v)
{
    struct column *c;
    int i, x;
    for (i = 0; i < t->ncols; i++) {
        c = &t->col[i];
        if (v[i].t == SQL_NULL) {
            if (c->flags & C_NOTNULL) fail("NOT NULL constraint failed: %s.%s", t->name, c->name);
        } else if (c->type == SQL_INTEGER) {
            if (v[i].t == SQL_TEXT) {
                if (!sq_toint(v[i].s, &x))
                    fail("not a number: '%.30s' (for %s.%s)", v[i].s, t->name, c->name);
                v[i].t = SQL_INTEGER;
                v[i].i = x;
            }
        } else {
            v[i].s = sq_text(&v[i]);
            v[i].t = SQL_TEXT;
            if ((int)strlen(v[i].s) > c->maxlen)
                fail("value too long for %s.%s (%d bytes; the most is %d)",
                     t->name, c->name, (int)strlen(v[i].s), c->maxlen);
        }
    }
}

/* is v already in the unique index ix (in a row other than self)? */
static void unique_check(struct table *t, struct index *ix, val *v, int self)
{
    unsigned char k[TKEY + 8], rec[MAXREC];
    struct btcur c;
    val w[MAXCOL];
    int rid, len;
    if (v->t == SQL_NULL) return;
    sq_ixfile(ix);
    sq_key(ix, v, 0, k);
    sq_btseek(&c, ix, k);
    while (sq_btnext(&c) && !memcmp(c.ent, k, ix->ks)) {
        rid = getbe(c.ent + ix->ks);
        if (rid == self) continue;
        if (ix->ks == 4 || strlen(v->s) < TKEY) goto dup;
        if (!(len = sq_fetch(t, rid, rec))) continue;  /* a long TEXT: the keys are cut short */
        sq_readrec(t, rec, len, w);
        if (w[ix->col].t == SQL_TEXT && !strcmp(w[ix->col].s, v->s)) goto dup;
    }
    return;
dup:
    fail("UNIQUE constraint failed: %s.%s", t->name, t->col[ix->col].name);
}

static void row_insert(struct table *t, val *v)
{
    unsigned char rec[RECBUF];
    int i, rid, pk = pkcol(t), len;
    if (pk >= 0 && t->col[pk].type == SQL_INTEGER && v[pk].t == SQL_NULL) {
        v[pk].t = SQL_INTEGER;          /* INTEGER PRIMARY KEY: the next number */
        v[pk].i = sq_th(t, T_AUTO);
    }
    check_row(t, v);
    for (i = 0; i < t->nidx; i++)
        if (t->idx[i].unique) unique_check(t, &t->idx[i], &v[t->idx[i].col], -1);
    len = sq_mkrec(t, v, rec);
    rid = sq_insrow(t, rec, len);
    for (i = 0; i < t->nidx; i++)
        if (v[t->idx[i].col].t != SQL_NULL) sq_btinsert(&t->idx[i], &v[t->idx[i].col], rid);
    autonext(t, pk, v);
}

static void ex_insert(struct sqlstmt *st, struct stmt *s)
{
    struct table *t;
    struct vec *row;
    struct ctx cx;
    val v[MAXCOL];
    int pos[MAXCOL], n, i, k, r;
    begin_write();
    t = sq_table(s->table, 1);
    n = s->names.n ? s->names.n : t->ncols;
    if (n > t->ncols) fail("%s has %d columns", t->name, t->ncols);
    for (i = 0; i < n; i++) {
        pos[i] = s->names.n ? findcol(t, s->names.v[i]) : i;
        for (k = 0; k < i; k++)
            if (pos[k] == pos[i]) fail("column %s is named twice", t->col[pos[i]].name);
    }
    cx.row = st->row;
    cx.g = 0;
    for (r = 0; r < s->rows.n; r++) {
        row = s->rows.v[r];
        if (row->n != n) fail("%d values for %d columns", row->n, n);
        arelease(&S->t, &st->tm);
        for (i = 0; i < t->ncols; i++) v[i].t = SQL_NULL;
        for (i = 0; i < n; i++) {
            sq_bind(0, 0, 0, row->v[i], 0);
            sq_eval(&cx, row->v[i], &v[pos[i]]);
        }
        row_insert(t, v);
        D->changes++;
    }
}

/* the rowids of the rows of s where the condition holds */
static int *collect(struct sqlstmt *st, struct src *s, struct expr *where, int *n)
{
    struct access a;
    struct tcur *c = zalloc(sizeof *c);
    struct ctx cx;
    val v;
    int *r = 0, *nr, cap = 0;
    sq_bind(0, s, 1, where, 0);
    sq_plan1(s, where, &a);
    sq_tcopen(c, s, &a, st->row);
    cx.row = st->row;
    cx.g = 0;
    *n = 0;
    while (arelease(&S->t, &st->tm), sq_tcnext(c, st->row)) {
        if (where) {
            sq_eval(&cx, where, &v);
            if (!sq_truth(&v)) continue;
        }
        if (*n == cap) {
            nr = aalloc(&S->a, (cap = cap ? 2 * cap : 64) * sizeof *nr);
            if (*n) memcpy(nr, r, *n * sizeof *nr);
            r = nr;
        }
        r[(*n)++] = c->rid;
    }
    return r;
}

static void open1(struct src *s, char *name)
{
    memset(s, 0, sizeof *s);
    s->name = name;
    s->t = sq_table(name, 1);
}

static void ex_update(struct sqlstmt *st, struct stmt *s)
{
    unsigned char old[MAXREC], rec[RECBUF];
    struct src src;
    struct table *t;
    struct index *ix;
    struct ctx cx;
    val ov[MAXCOL], nv[MAXCOL];
    int set[MAXCOL], i, j, k, c, n, len, rid, nrid, pk, *rids;
    begin_write();
    open1(&src, s->table);
    t = src.t;
    if (s->names.n > MAXCOL) fail("too many columns in SET");
    for (k = 0; k < s->names.n; k++) {
        set[k] = findcol(t, s->names.v[k]);
        sq_bind(0, &src, 1, s->rows.v[k], 0);
    }
    rids = collect(st, &src, s->where, &n);
    pk = pkcol(t);
    cx.row = ov;
    cx.g = 0;
    for (i = 0; i < n; i++) {
        arelease(&S->t, &st->tm);
        rid = rids[i];
        if (!(len = sq_fetch(t, rid, old))) continue;
        sq_readrec(t, old, len, ov);
        memcpy(nv, ov, t->ncols * sizeof *nv);
        for (k = 0; k < s->names.n; k++) sq_eval(&cx, s->rows.v[k], &nv[set[k]]);
        check_row(t, nv);
        for (j = 0; j < t->nidx; j++) {
            ix = &t->idx[j];
            if (ix->unique && changed(&ov[ix->col], &nv[ix->col])) unique_check(t, ix, &nv[ix->col], rid);
        }
        len = sq_mkrec(t, nv, rec);
        nrid = sq_updrow(t, rid, rec, len);
        for (j = 0; j < t->nidx; j++) {
            ix = &t->idx[j];
            c = ix->col;
            if (nrid == rid && !changed(&ov[c], &nv[c])) continue;
            if (ov[c].t != SQL_NULL) sq_btdelete(ix, &ov[c], rid);
            if (nv[c].t != SQL_NULL) sq_btinsert(ix, &nv[c], nrid);
        }
        autonext(t, pk, nv);
        D->changes++;
    }
}

static void ex_delete(struct sqlstmt *st, struct stmt *s)
{
    unsigned char old[MAXREC];
    struct src src;
    struct table *t;
    val ov[MAXCOL];
    int i, j, n, len, *rids;
    begin_write();
    open1(&src, s->table);
    t = src.t;
    rids = collect(st, &src, s->where, &n);
    for (i = 0; i < n; i++) {
        if (!(len = sq_fetch(t, rids[i], old))) continue;
        sq_readrec(t, old, len, ov);
        for (j = 0; j < t->nidx; j++)
            if (ov[t->idx[j].col].t != SQL_NULL) sq_btdelete(&t->idx[j], &ov[t->idx[j].col], rids[i]);
        sq_delrow(t, rids[i]);
        D->changes++;
    }
}

/* ---------------------------------------------------------------- CREATE, DROP */

static void noquery(void)
{
    if (D->nactive) fail("cannot change the tables while a query is open (sql_finish it first)");
    begin_write();
}

/* make an index of a table, and fill it */
static void mkindex(struct table *t, char *name, int col, int unique)
{
    char fn[40];
    struct index *ix;
    struct tcur *c;
    struct src src;
    val *row;
    if (t->nidx == MAXIDX) fail("too many indexes on %s (the most is %d)", t->name, MAXIDX);
    sprintf(fn, "%s.idx", name);
    ix = &t->idx[t->nidx];
    memset(ix, 0, sizeof *ix);
    strcpy(ix->name, name);
    ix->col = col;
    ix->unique = unique;
    ix->ks = t->col[col].type == SQL_INTEGER ? 4 : TKEY;
    ix->pf = sq_create(fn, "P32I");
    sq_btinit(ix);
    strcpy((char *)sq_touch(ix->pf, 0) + I_TABLE, t->name);
    t->nidx++;
    sq_savetable(t);
    memset(&src, 0, sizeof src);
    src.t = t;
    c = zalloc(sizeof *c);
    row = zalloc(t->ncols * sizeof *row);
    sq_tcopen(c, &src, 0, row);
    while (sq_tcnext(c, row)) {
        if (row[col].t == SQL_NULL) continue;
        if (unique) unique_check(t, ix, &row[col], -1);
        sq_btinsert(ix, &row[col], c->rid);
    }
}

static void ex_create(struct stmt *s)
{
    char fn[40], n[40];
    struct table *t;
    struct pfile *f;
    int i;
    noquery();
    if (sq_table(s->table, 0)) {
        if (s->ifex) return;
        fail("table %s already exists", s->table);
    }
    sprintf(fn, "%s.tbl", s->table);
    f = sq_create(fn, "P32T");
    if (!(t = calloc(1, sizeof *t))) fail("out of memory");
    t->next = D->tables;
    D->tables = t;
    strcpy(t->name, s->table);
    t->pf = f;
    t->ncols = s->ncol;
    memcpy(t->col, s->cols, s->ncol * sizeof *t->col);
    sq_thset(t, T_AUTO, 1);
    sq_savetable(t);
    for (i = 0; i < t->ncols; i++)
        if (t->col[i].flags & (C_PK | C_UNIQUE)) {
            if (t->col[i].flags & C_PK) sprintf(n, "%s.pk", t->name);
            else sprintf(n, "%s.u%d", t->name, i);
            mkindex(t, n, i, 1);
        }
}

static void ex_createix(struct stmt *s)
{
    char fn[40];
    struct table *t;
    noquery();
    t = sq_table(s->table, 1);
    sprintf(fn, "%s.idx", s->name);
    if (sq_pfopen(fn)) {
        if (s->ifex) return;
        fail("index %s already exists", s->name);
    }
    mkindex(t, s->name, findcol(t, s->names.v[0]), s->unique);
}

static void ex_dropix(struct stmt *s)
{
    char fn[40], tn[MAXNAME + 1];
    struct pfile *f;
    struct table *t;
    unsigned char *h;
    int i;
    noquery();
    sprintf(fn, "%s.idx", s->name);
    if (!(f = sq_pfopen(fn))) {
        if (s->ifex) return;
        fail("no such index: %s", s->name);
    }
    h = sq_pg(f, 0);
    if (memcmp(h, "P32I", 4)) fail("%s: damaged index", fn);
    memcpy(tn, h + I_TABLE, MAXNAME);
    tn[MAXNAME] = 0;
    if ((t = sq_table(tn, 0))) {
        for (i = 0; i < t->nidx && strcmp(t->idx[i].name, s->name); i++) ;
        if (i < t->nidx) {
            memmove(&t->idx[i], &t->idx[i + 1], (t->nidx - i - 1) * sizeof *t->idx);
            t->nidx--;
            sq_savetable(t);
        }
    }
    sq_dropfile(fn);
}

static void ex_drop(struct stmt *s)
{
    char fn[40];
    struct table *t, **tp;
    int i;
    noquery();
    if (!(t = sq_table(s->table, 0))) {
        if (s->ifex) return;
        fail("no such table: %s", s->table);
    }
    for (i = 0; i < t->nidx; i++) {
        sprintf(fn, "%s.idx", t->idx[i].name);
        sq_dropfile(fn);
    }
    sprintf(fn, "%s.tbl", t->name);
    sq_dropfile(fn);
    for (tp = &D->tables; *tp != t; tp = &(*tp)->next) ;
    *tp = t->next;
    free(t);
}

/* ---------------------------------------------------------------- SHOW, DESCRIBE */

static int namecmp(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

/* the tables' names, sorted */
static void tablenames(struct vec *v)
{
    struct dirent *de;
    DIR *d;
    int n;
    memset(v, 0, sizeof *v);
    if (!(d = opendir(D->dir))) fail("%s: %s", D->dir, strerror(errno));
    while ((de = readdir(d)))
        if ((n = strlen(de->d_name)) > 4 && n - 4 <= MAXNAME && !strcmp(de->d_name + n - 4, ".tbl"))
            vpush(v, astrndup(&S->a, de->d_name, n - 4));
    closedir(d);
    if (v->n) qsort(v->v, v->n, sizeof *v->v, namecmp);
}

static void show(struct sqlstmt *st, struct stmt *s)
{
    static char *ctab[] = { "table", "rows" };
    static char *cdesc[] = { "column", "type", "null", "key" };
    static char *cidx[] = { "index", "table", "column", "unique" };
    struct table *t;
    struct index *ix;
    struct column *c;
    struct vec names;
    char type[20];
    val v[4];
    int i, j;
    for (i = 0; i < 4; i++) v[i].t = SQL_TEXT;
    st->mat = 1;
    if (s->kind == K_DESCRIBE) {
        st->ncols = st->nout = 4;
        st->cname = cdesc;
        t = sq_table(s->table, 1);
        for (i = 0; i < t->ncols; i++) {
            c = &t->col[i];
            ix = sq_findix(t, i);
            if (c->type == SQL_INTEGER) strcpy(type, "INTEGER");
            else if (c->maxlen == MAXTEXT) strcpy(type, "TEXT");
            else sprintf(type, "VARCHAR(%d)", c->maxlen);
            for (j = 0; j < t->nidx && !(t->idx[j].col == i && t->idx[j].unique); j++) ;
            v[0].s = c->name;
            v[1].s = type;
            v[2].s = c->flags & C_NOTNULL ? "NO" : "YES";
            v[3].s = c->flags & C_PK ? "PRI" : j < t->nidx ? "UNI" : ix ? "MUL" : "";
            sq_keep(st, v);
        }
        return;
    }
    st->ncols = st->nout = s->kind == K_TABLES ? 2 : 4;
    st->cname = s->kind == K_TABLES ? ctab : cidx;
    if (s->table) {
        memset(&names, 0, sizeof names);
        vpush(&names, s->table);
        sq_table(s->table, 1);
    } else
        tablenames(&names);
    for (i = 0; i < names.n; i++) {
        if (!(t = sq_table(names.v[i], 0))) continue;
        if (s->kind == K_TABLES) {
            v[0].s = t->name;
            v[1].t = SQL_INTEGER;
            v[1].i = sq_th(t, T_NROWS);
            sq_keep(st, v);
        } else
            for (j = 0; j < t->nidx; j++) {
                v[0].s = t->idx[j].name;
                v[1].s = t->name;
                v[2].s = t->col[t->idx[j].col].name;
                v[3].t = SQL_INTEGER;
                v[3].i = t->idx[j].unique;
                sq_keep(st, v);
            }
    }
}

/* ---------------------------------------------------------------- running statements */

/* forget the last statement's state */
static void qreset(struct sqlstmt *st, struct stmt *s)
{
    st->s = s;
    st->ncols = st->nout = st->nslot = 0;
    st->cname = 0;
    st->out = 0;
    st->cur = 0;
    st->state = st->matched = st->skip = st->mat = st->emitted = st->started = 0;
    st->limit = -1;
    st->ghash = 0;
    st->glist = 0;
    st->dhash = 0;
    st->rows = 0;
    st->nrows = st->cap = st->pos = 0;
    D->stmtw = D->stmtmod = 0;
    if (!D->locked) sq_recover(0);      /* a crashed writer's journal */
}

/* run one statement (from sql_exec) */
static void exec1(struct sqlstmt *st, struct stmt *s)
{
    qreset(st, s);
    switch (s->kind) {
    case K_SELECT:
        sq_startsel(st);
        while (sq_nextrow(st)) ;
        break;
    case K_TABLES:
    case K_INDEXES:
    case K_DESCRIBE:
        show(st, s);
        break;
    case K_INSERT: D->changes = 0; ex_insert(st, s); break;
    case K_UPDATE: D->changes = 0; ex_update(st, s); break;
    case K_DELETE: D->changes = 0; ex_delete(st, s); break;
    case K_CREATE: ex_create(s); break;
    case K_CREATEIX: ex_createix(s); break;
    case K_DROP: ex_drop(s); break;
    case K_DROPIX: ex_dropix(s); break;
    case K_BEGIN:
        if (D->intxn) fail("a transaction is already open");
        sq_lock();
        D->intxn = 1;
        break;
    case K_COMMIT:
    case K_ROLLBACK:
        if (!D->intxn) fail("no transaction is open");
        D->stmtw = D->stmtmod = 1;      /* (if the commit fails, roll back) */
        if (s->kind == K_COMMIT) sq_commit();
        else sq_rollback();
        D->intxn = D->stmtw = 0;
        sq_unlock();
        break;
    }
    if (D->stmtw && !D->intxn) {        /* the end of a statement that wrote */
        sq_commit();
        sq_unlock();
    }
    D->stmtw = 0;
    if (!D->intxn && !D->nactive) sq_reset();
}

/* after an error: undo what the statement wrote */
static void on_error(void)
{
    if (D->stmtw && D->stmtmod) {
        if (D->intxn && strlen(D->err) < sizeof D->err - 40)
            strcat(D->err, " (the transaction was rolled back)");
        D->intxn = 0;
        sq_rollback();
    }
    if (!D->intxn) sq_unlock();
    D->stmtw = 0;
}

/* call fn with the database's state set up; an error comes back as -1 */
static int run(sqldb *db, struct sqlstmt *st, int (*fn)(struct sqlstmt *))
{
    jmp_buf jb, *savejb = db->jb;
    sqldb *saveD = D;
    struct sqlstmt *saveS = S;
    struct parser *saveP = P;
    int r;
    D = db;
    S = st;
    db->jb = &jb;
    if (setjmp(jb)) {
        on_error();
        r = -1;
    } else {
        if (st && !st->tm.c) {          /* where the temporaries start */
            aalloc(&st->t, 8);
            amark(&st->t, &st->tm);
        }
        r = fn(st);
    }
    if (!db->intxn && !db->nactive) sq_reset();
    db->jb = savejb;
    D = saveD;
    S = saveS;
    P = saveP;
    return r;
}

static int exec_all(struct sqlstmt *st)
{
    struct parser p;
    struct stmt *s;
    struct mark m;
    sq_lexinit(&p, st->src);
    for (;;) {
        while (sq_op(';')) ;
        if (p.t == T_EOF) return 0;
        amark(&st->a, &m);
        s = sq_parse();
        exec1(st, s);
        arelease(&st->a, &m);           /* the statement's memory, the next token's too: */
        p.pos = p.start;
        sq_lex();                       /* read it again */
    }
}

static int query_prep(struct sqlstmt *st)
{
    struct parser p;
    struct stmt *s;
    sq_lexinit(&p, st->src);
    while (sq_op(';')) ;
    if (p.t == T_EOF) fail("no statement");
    s = sq_parse();
    while (sq_op(';')) ;
    if (p.t != T_EOF) fail("sql_query runs one statement");
    qreset(st, s);
    if (s->kind == K_SELECT) sq_startsel(st);
    else if (s->kind == K_TABLES || s->kind == K_INDEXES || s->kind == K_DESCRIBE) show(st, s);
    else fail("not a query (use sql_exec)");
    st->active = 1;
    st->gen = D->gen;
    D->nactive++;
    return 0;
}

static int next_fn(struct sqlstmt *st)
{
    return sq_nextrow(st);
}

static int open_fn(struct sqlstmt *st)
{
    sq_recover(1);
    return 0;
}

/* ---------------------------------------------------------------- the API */

sqldb *sql_open(const char *dir, int create)
{
    struct stat sb;
    sqldb *db;
    int i;
    openerr[0] = 0;
    if (!dir || !*dir || strlen(dir) > sizeof db->dir - 40) {
        strcpy(openerr, "bad database name");
        return 0;
    }
    if (stat(dir, &sb) < 0) {
        if (!create) {
            snprintf(openerr, sizeof openerr, "%s: no such database", dir);
            return 0;
        }
        if (mkdir(dir, 0755) < 0) {
            snprintf(openerr, sizeof openerr, "cannot make %s: %s", dir, strerror(errno));
            return 0;
        }
    } else if (!S_ISDIR(sb.st_mode)) {
        snprintf(openerr, sizeof openerr, "%s: not a directory", dir);
        return 0;
    }
    if (!(db = calloc(1, sizeof *db)) || !(db->mem = malloc(NFRAME * PGSZ))) {
        free(db);
        strcpy(openerr, "out of memory");
        return 0;
    }
    strcpy(db->dir, dir);
    db->jfd = -1;
    for (i = 0; i < NFRAME; i++) db->fr[i].data = db->mem + i * PGSZ;
    if (run(db, 0, open_fn) < 0) {
        strcpy(openerr, db->err);
        free(db->mem);
        free(db);
        return 0;
    }
    return db;
}

void sql_close(sqldb *db)
{
    sqldb *save = D;
    if (!db) return;
    D = db;
    if (db->intxn) sq_rollback();
    db->intxn = 0;
    sq_unlock();
    sq_reset();
    free(db->mem);
    free(db);
    D = save == db ? 0 : save;
}

static struct sqlstmt *st_new(sqldb *db, const char *sql)
{
    struct sqlstmt *st = calloc(1, sizeof *st);
    if (!st) {
        strcpy(db->err, "out of memory");
        return 0;
    }
    st->db = db;
    st->src = sql ? sql : "";
    db->err[0] = 0;
    return st;
}

static void st_free(struct sqlstmt *st)
{
    afree(&st->a);
    afree(&st->t);
    free(st->txt);
    free(st);
}

int sql_exec(sqldb *db, const char *sql)
{
    struct sqlstmt *st;
    int r;
    if (!db || !(st = st_new(db, sql))) return -1;
    r = run(db, st, exec_all);
    st_free(st);
    return r;
}

sqlstmt *sql_query(sqldb *db, const char *sql)
{
    struct sqlstmt *st;
    if (!db || !(st = st_new(db, sql))) return 0;
    if (run(db, st, query_prep) < 0) {
        st_free(st);
        return 0;
    }
    return st;
}

int sql_next(sqlstmt *st)
{
    int r;
    if (!st || st->err) return -1;
    if (st->done) return 0;
    if (st->gen != st->db->gen) {
        strcpy(st->db->err, "the query was cancelled by a rollback");
        st->err = 1;
        return -1;
    }
    st->db->err[0] = 0;
    r = run(st->db, st, next_fn);
    if (r < 0) st->err = 1;
    else if (!r) st->done = 1;
    if (r <= 0) st->out = 0;
    return r;
}

void sql_finish(sqlstmt *st)
{
    sqldb *db, *save = D;
    if (!st) return;
    db = st->db;
    if (st->active) db->nactive--;
    st_free(st);
    D = db;
    if (!db->intxn && !db->nactive) sq_reset();
    D = save;
}

int sql_ncols(sqlstmt *st) { return st ? st->ncols : 0; }

const char *sql_colname(sqlstmt *st, int i)
{
    return st && i >= 0 && i < st->ncols ? st->cname[i] : "";
}

static val *colval(sqlstmt *st, int i)
{
    return st && st->out && i >= 0 && i < st->ncols ? &st->out[i] : 0;
}

int sql_isnull(sqlstmt *st, int i)
{
    val *v = colval(st, i);
    return !v || v->t == SQL_NULL;
}

int sql_coltype(sqlstmt *st, int i)
{
    val *v = colval(st, i);
    return v ? v->t : SQL_NULL;
}

int sql_int(sqlstmt *st, int i)
{
    val *v = colval(st, i);
    int x;
    if (!v || v->t == SQL_NULL) return 0;
    if (v->t == SQL_INTEGER) return v->i;
    return sq_toint(v->s, &x) ? x : 0;
}

const char *sql_text(sqlstmt *st, int i)
{
    val *v = colval(st, i);
    if (!v || v->t == SQL_NULL) return "";
    if (v->t == SQL_TEXT) return v->s;
    if (!st->txt && !(st->txt = malloc(st->ncols * 12))) return "";
    sprintf(st->txt + 12 * i, "%d", v->i);
    return st->txt + 12 * i;
}

int sql_changes(sqldb *db) { return db ? db->changes : 0; }

const char *sql_errmsg(sqldb *db) { return db ? db->err : openerr; }
