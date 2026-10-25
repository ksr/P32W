/* sqlparse.c -- the SQL library's parser (sqldb(3)): statement text to trees.
 *
 * A hand-written recursive-descent parser over a one-token lexer. Keywords
 * are identifiers recognised by their text, in any case; a few are reserved
 * (they cannot name a column or be an alias without "quotes"). Operators,
 * from the loosest:
 *     OR;  AND;  NOT;  = <> != < <= > >= IS LIKE BETWEEN IN;  + -;  * / %;
 *     ||;  unary - +
 * Names of tables and indexes become file names, so they are kept to
 * letters, digits and _, at most MAXNAME long, in lower case.
 */
#include "sqlint.h"

enum { O_NE = 256, O_LE, O_GE, O_CAT };            /* operators of two characters */

static const char *reserved[] = {
    "ALL", "AND", "AS", "ASC", "BETWEEN", "BY", "CREATE", "CROSS", "DELETE", "DESC",
    "DISTINCT", "DROP", "FROM", "GROUP", "HAVING", "IN", "INDEX", "INNER", "INSERT",
    "INTO", "IS", "JOIN", "LEFT", "LIKE", "LIMIT", "NOT", "NULL", "OFFSET", "ON", "OR",
    "ORDER", "OUTER", "PRIMARY", "SELECT", "SET", "TABLE", "UNIQUE", "UPDATE",
    "VALUES", "WHERE", 0
};

static const char *fnames[] = {     /* in the order of F_ and A_ */
    "UPPER", "LOWER", "LENGTH", "SUBSTR", "ABS", "COALESCE", "IFNULL",
    "COUNT", "SUM", "MIN", "MAX", "AVG", 0
};
static const char fmin[] = { 1, 1, 1, 2, 1, 1, 2 }, fmax[] = { 1, 1, 1, 3, 1, 8, 2 };

static int isid0(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int isdig(int c) { return c >= '0' && c <= '9'; }
static int isid(int c) { return isid0(c) || isdig(c); }

/* ---------------------------------------------------------------- the lexer */

void sq_lexinit(struct parser *p, const char *src)
{
    memset(p, 0, sizeof *p);
    p->src = src;
    P = p;
    sq_lex();
}

/* the next token */
void sq_lex(void)
{
    const char *s = P->src;
    int i = P->pos, j, n, q;
    unsigned v;
    char *b;
    P->lastend = P->end;
    for (;;) {                          /* blanks and comments */
        while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
        if (s[i] == '-' && s[i + 1] == '-')
            while (s[i] && s[i] != '\n') i++;
        else if (s[i] == '/' && s[i + 1] == '*') {
            for (i += 2; s[i] && !(s[i] == '*' && s[i + 1] == '/'); i++) ;
            if (s[i]) i += 2;
        } else
            break;
    }
    P->start = i;
    if (!s[i])
        P->t = T_EOF;
    else if (isid0(s[i])) {
        for (j = i; isid(s[i]); i++) ;
        P->t = T_ID;
        P->s = astrndup(&S->a, s + j, i - j);
    } else if (isdig(s[i])) {
        for (v = 0; isdig(s[i]); i++) {     /* (2147483648 is let through for unary -) */
            if (v > 214748364 || (v == 214748364 && s[i] > '8'))
                fail("number too large: %.12s", s + P->start);
            v = v * 10 + (s[i] - '0');
        }
        if (isid(s[i]) || s[i] == '.') fail("bad number: %.12s", s + P->start);
        P->t = T_INT;
        P->ival = v;
    } else if (s[i] == '\'' || s[i] == '"') {   /* 'string' or "name"; a doubled quote is one */
        q = s[i];
        for (j = i + 1, n = 0; s[j] != q || s[j + 1] == q; j += s[j] == q ? 2 : 1, n++)
            if (!s[j]) fail("unterminated %s", q == '"' ? "name" : "string");
        if (n > MAXSTR) fail("string too long (the most is %d)", MAXSTR);
        P->s = b = aalloc(&S->a, n + 1);
        for (i++; s[i] != q || s[i + 1] == q; i += s[i] == q ? 2 : 1) *b++ = s[i];
        *b = 0;
        i++;
        P->t = q == '"' ? T_QID : T_STR;
    } else {
        P->t = T_OP;
        P->op = s[i];
        n = 2;
        if ((s[i] == '<' && s[i + 1] == '>') || (s[i] == '!' && s[i + 1] == '=')) P->op = O_NE;
        else if (s[i] == '<' && s[i + 1] == '=') P->op = O_LE;
        else if (s[i] == '>' && s[i + 1] == '=') P->op = O_GE;
        else if (s[i] == '|' && s[i + 1] == '|') P->op = O_CAT;
        else if (s[i] == '=' && s[i + 1] == '=') P->op = '=';
        else if (!strchr("(),;.*+-/%=<>", s[i])) fail("unexpected character '%c'", s[i]);
        else n = 1;                     /* (n: the operator's length) */
        i += n;
    }
    P->end = i;
    P->pos = i;
}

/* the current token, for messages */
static const char *near(void)
{
    static char b[40];
    int n = P->end - P->start;
    if (P->t == T_EOF) return "the end";
    sprintf(b, "\"%.*s\"", n > 30 ? 30 : n, P->src + P->start);
    return b;
}

static int iskw(const char *k) { return P->t == T_ID && ieq(P->s, k); }
int sq_isop(int o) { return P->t == T_OP && P->op == o; }

static int kw(const char *k)
{
    if (!iskw(k)) return 0;
    sq_lex();
    return 1;
}

static void need(const char *k)
{
    if (!kw(k)) fail("expected %s near %s", k, near());
}

int sq_op(int o)
{
    if (!sq_isop(o)) return 0;
    sq_lex();
    return 1;
}

static void needop(int o)
{
    if (!sq_op(o)) fail("expected \"%c\" near %s", o, near());
}

static int isreserved(const char *s)
{
    int i;
    for (i = 0; reserved[i]; i++)
        if (ieq(s, reserved[i])) return 1;
    return 0;
}

/* is the current token a name (not a reserved word)? */
static int isname(void)
{
    return P->t == T_QID || (P->t == T_ID && !isreserved(P->s));
}

static char *ident(const char *what)
{
    char *s = P->s;
    if (!isname()) fail("expected %s near %s", what, near());
    sq_lex();
    return s;
}

/* a table or index name: a file name, so in lower case and plain */
static char *objname(const char *what)
{
    char *s = ident(what), *p;
    if (strlen(s) > MAXNAME) fail("name too long: %s (the most is %d characters)", s, MAXNAME);
    for (p = s; *p; p++) {
        if (!isid(*p)) fail("bad name: %s", s);
        if (*p >= 'A' && *p <= 'Z') *p += 32;
    }
    return s;
}

/* ---------------------------------------------------------------- expressions */

static struct expr *p_expr(void);

static struct expr *mk(int op, struct expr *l, struct expr *r)
{
    struct expr *e = zalloc(sizeof *e);
    e->op = op;
    e->l = l;
    e->r = r;
    e->start = l ? l->start : P->start;
    e->end = P->lastend;
    return e;
}

/* items, separated by commas, up to ')' */
static struct expr **p_list(int *n)
{
    struct vec v;
    memset(&v, 0, sizeof v);
    do vpush(&v, p_expr()); while (sq_op(','));
    needop(')');
    *n = v.n;
    return (struct expr **)v.v;
}

static struct expr *p_call(char *name, int st)
{
    struct expr *e = mk(E_FUNC, 0, 0);
    int f;
    for (f = 0; fnames[f] && !ieq(name, fnames[f]); f++) ;
    if (!fnames[f]) fail("no such function: %s", name);
    e->fn = f;
    e->name = name;
    if (f >= A_COUNT) {                 /* an aggregate: one argument, or COUNT(*) */
        e->op = E_AGG;
        if (sq_op('*')) {
            if (f != A_COUNT) fail("%s(*) is not allowed", fnames[f]);
        } else {
            if (kw("DISTINCT")) fail("DISTINCT in an aggregate is not supported");
            kw("ALL");
            e->l = p_expr();
        }
        needop(')');
    } else {
        if (sq_op(')')) e->n = 0;
        else e->list = p_list(&e->n);
        if (e->n < fmin[f] || e->n > fmax[f]) fail("wrong number of arguments to %s", fnames[f]);
    }
    e->start = st;
    return e;
}

static struct expr *p_primary(void)
{
    struct expr *e = 0;
    int st = P->start;
    char *n;
    if (P->t == T_INT) {
        if (P->ival > 2147483647u) fail("number too large: %u", P->ival);
        e = mk(E_LIT, 0, 0);
        e->v.t = SQL_INTEGER;
        e->v.i = P->ival;
        sq_lex();
    } else if (P->t == T_STR) {
        e = mk(E_LIT, 0, 0);
        e->v.t = SQL_TEXT;
        e->v.s = P->s;
        sq_lex();
    } else if (kw("NULL"))
        e = mk(E_LIT, 0, 0);
    else if (sq_op('(')) {
        e = p_expr();
        needop(')');
    } else if (isname()) {
        n = ident("a name");
        if (sq_op('(')) e = p_call(n, st);
        else {
            e = mk(E_COL, 0, 0);
            if (sq_op('.')) {
                e->qual = n;
                n = ident("a column name");
            }
            e->name = n;
        }
    } else
        fail("expected an expression near %s", near());
    e->start = st;
    e->end = P->lastend;
    return e;
}

static struct expr *p_unary(void)
{
    struct expr *e;
    int st = P->start;
    if (sq_op('+')) return p_unary();
    if (!sq_op('-')) return p_primary();
    if (P->t == T_INT) {                /* a negative number (down to -2147483648) */
        e = mk(E_LIT, 0, 0);
        e->v.t = SQL_INTEGER;
        e->v.i = (int)(0u - P->ival);
        sq_lex();
    } else
        e = mk(E_NEG, p_unary(), 0);
    e->start = st;
    e->end = P->lastend;
    return e;
}

static struct expr *p_cat(void)
{
    struct expr *e = p_unary();
    while (sq_op(O_CAT)) e = mk(E_CAT, e, p_unary());
    return e;
}

static struct expr *p_mul(void)
{
    struct expr *e = p_cat();
    for (;;) {
        if (sq_op('*')) e = mk(E_MUL, e, p_cat());
        else if (sq_op('/')) e = mk(E_DIV, e, p_cat());
        else if (sq_op('%')) e = mk(E_MOD, e, p_cat());
        else return e;
        e->end = P->lastend;
    }
}

static struct expr *p_add(void)
{
    struct expr *e = p_mul();
    for (;;) {
        if (sq_op('+')) e = mk(E_ADD, e, p_mul());
        else if (sq_op('-')) e = mk(E_SUB, e, p_mul());
        else return e;
        e->end = P->lastend;
    }
}

/* NOT LIKE, NOT BETWEEN, NOT IN: is the word after NOT one of them? */
static int notnext(void)
{
    struct parser save;
    int r;
    if (!iskw("NOT")) return 0;
    save = *P;
    sq_lex();
    r = iskw("LIKE") || iskw("BETWEEN") || iskw("IN");
    *P = save;
    return r;
}

static struct expr *p_cmp(void)
{
    struct expr *e = p_add(), *x;
    int o, neg;
    for (;;) {
        o = -1;
        if (P->t == T_OP)
            switch (P->op) {
            case '=': o = E_EQ; break;
            case O_NE: o = E_NE; break;
            case '<': o = E_LT; break;
            case O_LE: o = E_LE; break;
            case '>': o = E_GT; break;
            case O_GE: o = E_GE; break;
            }
        if (o >= 0) {
            sq_lex();
            e = mk(o, e, p_add());
        } else if (kw("IS")) {
            neg = kw("NOT");
            need("NULL");
            e = mk(neg ? E_NOTNULL : E_ISNULL, e, 0);
        } else {
            neg = notnext();
            if (neg) sq_lex();
            if (kw("LIKE"))
                e = mk(E_LIKE, e, p_add());
            else if (kw("BETWEEN")) {
                x = mk(E_BETWEEN, e, 0);
                x->list = zalloc(2 * sizeof *x->list);
                x->n = 2;
                x->list[0] = p_add();
                need("AND");
                x->list[1] = p_add();
                e = x;
            } else if (kw("IN")) {
                x = mk(E_IN, e, 0);
                needop('(');
                x->list = p_list(&x->n);
                e = x;
            } else
                return e;
            e->not = neg;
        }
        e->end = P->lastend;
    }
}

static struct expr *p_not(void)
{
    int st = P->start;
    struct expr *e;
    if (!kw("NOT")) return p_cmp();
    e = mk(E_NOT, p_not(), 0);
    e->start = st;
    return e;
}

static struct expr *p_and(void)
{
    struct expr *e = p_not();
    while (kw("AND")) e = mk(E_AND, e, p_not());
    return e;
}

static struct expr *p_expr(void)
{
    struct expr *e = p_and();
    while (kw("OR")) e = mk(E_OR, e, p_and());
    return e;
}

/* ---------------------------------------------------------------- statements */

static void p_src(struct src *s)
{
    s->name = objname("a table name");
    if (kw("AS")) s->alias = ident("an alias");
    else if (isname()) s->alias = ident("an alias");
}

static struct select *p_select(void)
{
    struct select *q = zalloc(sizeof *q);
    struct vec res, alias, grp;
    struct expr *e;
    struct parser save;
    char *a;
    int j;
    memset(&res, 0, sizeof res);
    memset(&alias, 0, sizeof alias);
    memset(&grp, 0, sizeof grp);
    need("SELECT");
    if (kw("DISTINCT")) q->distinct = 1;
    else kw("ALL");
    do {
        a = 0;
        save = *P;
        if (sq_isop('*')) {
            e = mk(E_STAR, 0, 0);
            sq_lex();
        } else if (isname() && (sq_lex(), sq_op('.')) && sq_isop('*')) {
            e = mk(E_STAR, 0, 0);       /* table.* */
            e->qual = save.s;
            sq_lex();
        } else {
            *P = save;
            e = p_expr();
            if (kw("AS")) a = ident("an alias");
            else if (isname()) a = ident("an alias");
        }
        vpush(&res, e);
        vpush(&alias, a);
    } while (sq_op(','));
    q->nres = res.n;
    q->res = (struct expr **)res.v;
    q->alias = (char **)alias.v;
    if (kw("FROM")) {
        p_src(&q->src[0]);
        q->nsrc = 1;
        for (;;) {
            if (sq_op(',') || kw("JOIN")) j = 1;
            else if (kw("INNER") || kw("CROSS")) { need("JOIN"); j = 1; }
            else if (kw("LEFT")) { kw("OUTER"); need("JOIN"); j = 2; }
            else break;
            if (q->nsrc == 2) fail("a join of more than two tables is not supported");
            p_src(&q->src[1]);
            q->src[1].join = j;
            q->nsrc = 2;
            if (kw("ON")) q->src[1].on = p_expr();
            else if (j == 2) fail("LEFT JOIN needs ON");
        }
    }
    if (kw("WHERE")) q->where = p_expr();
    if (kw("GROUP")) {
        need("BY");
        do vpush(&grp, p_expr()); while (sq_op(','));
        q->ngroup = grp.n;
        q->group = (struct expr **)grp.v;
    }
    if (kw("HAVING")) q->having = p_expr();
    if (kw("ORDER")) {
        need("BY");
        do {
            if (q->norder == MAXORDER) fail("too many ORDER BY terms");
            q->order[q->norder] = p_expr();
            q->desc[q->norder++] = kw("DESC") ? 1 : (kw("ASC"), 0);
        } while (sq_op(','));
    }
    if (kw("LIMIT")) {
        q->limit = p_expr();
        if (kw("OFFSET")) q->offset = p_expr();
        else if (sq_op(',')) {          /* LIMIT skip, count */
            q->offset = q->limit;
            q->limit = p_expr();
        }
    }
    return q;
}

/* a column of CREATE TABLE */
static void p_coldef(struct column *c)
{
    char *n = ident("a column name");
    int len = -1;
    if (strlen(n) > MAXNAME) fail("name too long: %s (the most is %d characters)", n, MAXNAME);
    strcpy(c->name, n);
    if (kw("INTEGER") || kw("INT") || kw("SMALLINT") || kw("BIGINT") || kw("TINYINT"))
        c->type = SQL_INTEGER;
    else if (kw("TEXT") || kw("VARCHAR") || kw("CHAR") || kw("STRING") ||
             (kw("CHARACTER") && (kw("VARYING"), 1)))
        c->type = SQL_TEXT;
    else if (P->t == T_ID && !iskw("PRIMARY") && !iskw("NOT") && !iskw("UNIQUE"))
        fail("unknown type %s (INTEGER or TEXT)", P->s);
    else
        fail("column %s needs a type (INTEGER or TEXT)", n);
    if (sq_op('(')) {                   /* CHAR(n), VARCHAR(n) */
        if (P->t != T_INT || P->ival < 1) fail("expected a length near %s", near());
        len = P->ival;
        sq_lex();
        needop(')');
    }
    c->maxlen = c->type == SQL_INTEGER ? 0 : len > 0 && len < MAXTEXT ? len : MAXTEXT;
    for (;;) {
        if (kw("PRIMARY")) { need("KEY"); c->flags |= C_PK | C_NOTNULL; kw("ASC"); }
        else if (kw("NOT")) { need("NULL"); c->flags |= C_NOTNULL; }
        else if (kw("NULL")) ;
        else if (kw("UNIQUE")) c->flags |= C_UNIQUE;
        else if (P->t == T_ID && !sq_isop(',')) fail("%s is not supported (in column %s)", P->s, n);
        else break;
    }
}

/* PRIMARY KEY (col) or UNIQUE (col) after the columns */
static void p_tablekey(struct stmt *s, int flag)
{
    char *n;
    int i;
    needop('(');
    n = ident("a column name");
    if (sq_isop(',')) fail("a key of more than one column is not supported");
    needop(')');
    for (i = 0; i < s->ncol && !ieq(s->cols[i].name, n); i++) ;
    if (i == s->ncol) fail("no such column: %s", n);
    s->cols[i].flags |= flag;
}

static void p_create(struct stmt *s)
{
    int i, j, npk = 0;
    s->unique = kw("UNIQUE");
    if (kw("INDEX")) {
        s->kind = K_CREATEIX;
        if (kw("IF")) { need("NOT"); need("EXISTS"); s->ifex = 1; }
        s->name = objname("an index name");
        need("ON");
        s->table = objname("a table name");
        needop('(');
        vpush(&s->names, ident("a column name"));
        if (sq_isop(',')) fail("an index of more than one column is not supported");
        needop(')');
        return;
    }
    if (s->unique || !kw("TABLE")) fail("expected TABLE or INDEX after CREATE");
    s->kind = K_CREATE;
    if (kw("IF")) { need("NOT"); need("EXISTS"); s->ifex = 1; }
    s->table = objname("a table name");
    s->cols = zalloc(MAXCOL * sizeof *s->cols);
    needop('(');
    do {
        if (kw("PRIMARY")) { need("KEY"); p_tablekey(s, C_PK | C_NOTNULL); }
        else if (kw("UNIQUE")) p_tablekey(s, C_UNIQUE);
        else {
            if (s->ncol == MAXCOL) fail("too many columns (the most is %d)", MAXCOL);
            p_coldef(&s->cols[s->ncol++]);
        }
    } while (sq_op(','));
    needop(')');
    for (i = 0; i < s->ncol; i++) {
        if (s->cols[i].flags & C_PK) npk++;
        for (j = 0; j < i; j++)
            if (ieq(s->cols[i].name, s->cols[j].name)) fail("column %s is named twice", s->cols[i].name);
    }
    if (npk > 1) fail("a table has one PRIMARY KEY (of one column)");
}

static void p_insert(struct stmt *s)
{
    struct vec *row;
    int n;
    s->kind = K_INSERT;
    need("INTO");
    s->table = objname("a table name");
    if (sq_op('(')) {
        do vpush(&s->names, ident("a column name")); while (sq_op(','));
        needop(')');
    }
    need("VALUES");
    do {
        needop('(');
        row = zalloc(sizeof *row);
        row->v = (void **)p_list(&n);
        row->n = n;
        vpush(&s->rows, row);
    } while (sq_op(','));
}

/* one statement (its ; is read too) */
struct stmt *sq_parse(void)
{
    struct stmt *s = zalloc(sizeof *s);
    if (iskw("SELECT")) {
        s->kind = K_SELECT;
        s->sel = p_select();
    } else if (kw("EXPLAIN")) {
        if (kw("QUERY")) need("PLAN");
        if (!iskw("SELECT")) fail("EXPLAIN shows how a SELECT is done");
        s->kind = K_SELECT;
        s->sel = p_select();
        s->sel->explain = 1;
    } else if (kw("INSERT"))
        p_insert(s);
    else if (kw("UPDATE")) {
        s->kind = K_UPDATE;
        s->table = objname("a table name");
        need("SET");
        do {
            vpush(&s->names, ident("a column name"));
            needop('=');
            vpush(&s->rows, p_expr());
        } while (sq_op(','));
        if (kw("WHERE")) s->where = p_expr();
    } else if (kw("DELETE")) {
        s->kind = K_DELETE;
        need("FROM");
        s->table = objname("a table name");
        if (kw("WHERE")) s->where = p_expr();
    } else if (kw("CREATE"))
        p_create(s);
    else if (kw("DROP")) {
        if (kw("TABLE")) s->kind = K_DROP;
        else if (kw("INDEX")) s->kind = K_DROPIX;
        else fail("expected TABLE or INDEX after DROP");
        if (kw("IF")) { need("EXISTS"); s->ifex = 1; }
        s->name = s->table = objname("a name");
    } else if (kw("BEGIN")) {
        s->kind = K_BEGIN;
        kw("TRANSACTION");
    } else if (kw("COMMIT") || kw("END")) {
        s->kind = K_COMMIT;
        kw("TRANSACTION");
    } else if (kw("ROLLBACK")) {
        s->kind = K_ROLLBACK;
        kw("TRANSACTION");
    } else if (kw("SHOW")) {
        if (kw("TABLES")) s->kind = K_TABLES;
        else if (kw("INDEXES") || kw("INDEX")) {
            s->kind = K_INDEXES;
            if (kw("FROM") || kw("ON")) s->table = objname("a table name");
        } else if (kw("COLUMNS")) {
            s->kind = K_DESCRIBE;
            if (!kw("FROM")) need("IN");
            s->table = objname("a table name");
        } else
            fail("expected TABLES, INDEXES or COLUMNS after SHOW");
    } else if (kw("DESCRIBE") || kw("DESC")) {
        s->kind = K_DESCRIBE;
        s->table = objname("a table name");
    } else
        fail("unknown statement near %s", near());
    if (!sq_op(';') && P->t != T_EOF) fail("unexpected %s", near());
    return s;
}
