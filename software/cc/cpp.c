/* cpp.c -- the preprocessor (cc/cpp.py): directives, and macro expansion
 * with hidesets (Prosser's algorithm). Token lists are arrays of pointers;
 * expansion reads its input from a stack, so a macro's body goes back in
 * front of the rest to be scanned again. */
#include "cc.h"

struct macro {
    char *name;
    struct tok **body;
    int nbody;
    char **params;                      /* NULL: object-like */
    int nparams, variadic;
    struct macro *next;
};

#define MHASH 512
static struct macro *mtab[MHASH];
static char **incdirs;
static int nincdirs;
static struct vec once;                 /* files with #pragma once */

static unsigned mh(const char *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % MHASH;
}

static struct macro *mfind(const char *name)
{
    struct macro *m;
    for (m = mtab[mh(name)]; m; m = m->next)
        if (m->name == name) return m;              /* (names are interned) */
    return 0;
}

static void mfree(struct macro *m)
{
    int k;
    for (k = 0; k < m->nbody; k++) tok_free(m->body[k]);
    free(m->body);
    free(m->params);
    free(m);
}

static void mundef(const char *name)
{
    struct macro **pp, *m;
    for (pp = &mtab[mh(name)]; *pp; pp = &(*pp)->next)
        if ((*pp)->name == name) {
            m = *pp;
            *pp = m->next;
            mfree(m);
            return;
        }
}

static void mdefine(struct macro *m)
{
    mundef(m->name);
    m->next = mtab[mh(m->name)];
    mtab[mh(m->name)] = m;
}

static void define_text(const char *name, const char *text)
{
    struct macro *m = xmalloc(sizeof *m);
    int n;
    memset(m, 0, sizeof *m);
    m->name = intern(name);
    m->body = tokenize(text, "<command line>", &n);
    m->nbody = n - 1;                               /* (without the eof) */
    mdefine(m);
}

void cpp_init(char **dirs, int ndirs, char **defs, int ndefs)
{
    int i;
    memset(mtab, 0, sizeof mtab);       /* each file starts afresh (as p32cc.py's new Preprocessor) */
    once.n = 0;
    incdirs = dirs;
    nincdirs = ndirs;
    for (i = 0; i < ndefs; i++) {
        char name[256], *eq = strchr(defs[i], '=');
        int n = eq ? eq - defs[i] : (int)strlen(defs[i]);
        if (n > 255) n = 255;
        memcpy(name, defs[i], n);
        name[n] = 0;
        define_text(name, eq ? eq + 1 : "1");      /* (-DX=: X empty) */
    }
    define_text("__P32__", "1");
    define_text("__STDC__", "1");
}

/* ---------------------------------------------------------------- hidesets */
static int hs_has(struct hide *h, const char *name)
{
    for (; h; h = h->next) if (h->name == name) return 1;
    return 0;
}

/* Hidesets are shared: each (name, rest of the list) is made once and
   found again in a table, so the tokens of an expansion all point at one
   list instead of a copy each, and no hideset is ever freed */
#define HHASH 1024
struct hcell { struct hide h; struct hcell *chain; };
static struct hcell *htab[HHASH];

static struct hide *hs_add(struct hide *h, const char *name)
{
    unsigned k = ((unsigned)(size_t)h * 31u + (unsigned)(size_t)name) % HHASH;
    struct hcell *c;
    if (hs_has(h, name)) return h;
    for (c = htab[k]; c; c = c->chain)
        if (c->h.name == name && c->h.next == h) return &c->h;
    c = xmalloc(sizeof *c);
    c->h.name = name;
    c->h.next = h;
    c->chain = htab[k];
    htab[k] = c;
    return &c->h;
}

static struct hide *hs_union(struct hide *a, struct hide *b)
{
    if (!a || a == b) return b;
    for (; b; b = b->next) a = hs_add(a, b->name);
    return a;
}

static struct hide *hs_inter(struct hide *a, struct hide *b)
{
    struct hide *r = 0;
    for (; a; a = a->next) if (hs_has(b, a->name)) r = hs_add(r, a->name);
    return r;
}

/* ---------------------------------------------------------------- expansion */
/* the input of one expansion: a stack, its top the next token */
struct input { struct tok **a; int n, cap; };

static void in_push(struct input *s, struct tok *t)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->a = xrealloc(s->a, sizeof *s->a * s->cap);
    }
    s->a[s->n++] = t;
}

static void in_pushlist(struct input *s, struct tok **t, int n)    /* t[0] comes out first */
{
    while (n-- > 0) in_push(s, t[n]);
}

static void expand_list(struct tok **toks, int n, struct vec *out);

static struct tok *paste(struct tok *a, struct tok *b, struct tok *at)
{
    char *text = fmt("%s%s", a->text, b->text);
    int n;
    struct tok **ts = tokenize(text, at->file, &n), *r;
    if (n != 2) error(at, "## makes %s, not one token", text);
    r = ts[0];
    tok_free(ts[1]);                        /* (the end-of-file token) */
    free(ts);
    free(text);
    return r;
}

static int param_index(struct macro *m, struct tok *t)
{
    int i;
    if (t->kind != T_ID || !m->params) return -1;
    for (i = 0; i < m->nparams; i++) if (m->params[i] == t->text) return i;
    return -1;
}

static void push_copies(struct vec *out, struct vec *arg)
{
    int i;
    for (i = 0; i < arg->n; i++) vpush(out, tokcopy(arg->a[i]));
}

/* the macro's body with the arguments put in (cpp.py's subst) */
static void subst(struct macro *m, struct vec *args, struct hide *hide, struct tok *at, struct vec *resp)
{
    struct vec out;
    int i = 0, k;
    memset(&out, 0, sizeof out);
    while (i < m->nbody) {
        struct tok *t = m->body[i];
        int pi;
        if (t->kind == T_PUNCT && is(t, "#") && m->params && i + 1 < m->nbody &&
            (pi = param_index(m, m->body[i + 1])) >= 0) {           /* # param */
            struct vec *a = &args[pi];
            struct vec s;
            struct tok *n;
            char *str, *q;
            int len = 0;
            memset(&s, 0, sizeof s);
            for (k = 0; k < a->n; k++) len += strlen(((struct tok *)a->a[k])->text) + 1;
            str = xmalloc(len + 1);
            str[0] = 0;
            for (k = 0; k < a->n; k++) {
                struct tok *x = a->a[k];
                if (k && x->space) strcat(str, " ");
                strcat(str, x->text);
            }
            q = xmalloc(2 * strlen(str) + 3);
            {
                char *p = str, *d = q;
                *d++ = '"';
                for (; *p; p++) { if (*p == '\\' || *p == '"') *d++ = '\\'; *d++ = *p; }
                *d++ = '"';
                *d = 0;
            }
            n = tok_new();
            n->kind = T_STR;
            n->text = intern(q);
            n->file = at->file;
            n->line = at->line;
            n->space = t->space;
            n->slen = strlen(str);
            n->sval = (unsigned char *)str;
            vpush(&out, n);
            i += 2;
            continue;
        }
        if (i + 1 < m->nbody && is(m->body[i + 1], "##")) {        /* lhs ## rhs, on raw arguments */
            struct vec seq, rhs;
            memset(&seq, 0, sizeof seq);
            if ((pi = param_index(m, t)) >= 0) push_copies(&seq, &args[pi]);
            else vpush(&seq, tokcopy(t));
            i += 2;
            for (;;) {
                struct tok *rt = m->body[i];
                memset(&rhs, 0, sizeof rhs);
                if ((pi = param_index(m, rt)) >= 0) push_copies(&rhs, &args[pi]);
                else vpush(&rhs, tokcopy(rt));
                i += 1;
                if (seq.n && rhs.n) {
                    struct tok *l = seq.a[seq.n - 1];
                    seq.a[seq.n - 1] = paste(l, rhs.a[0], at);
                    tok_free(l);                    /* (both were copies) */
                    tok_free(rhs.a[0]);
                    for (k = 1; k < rhs.n; k++) vpush(&seq, rhs.a[k]);
                } else
                    for (k = 0; k < rhs.n; k++) vpush(&seq, rhs.a[k]);
                free(rhs.a);
                if (i < m->nbody && is(m->body[i], "##")) { i += 1; continue; }
                break;
            }
            for (k = 0; k < seq.n; k++) vpush(&out, seq.a[k]);
            free(seq.a);
            continue;
        }
        if ((pi = param_index(m, t)) >= 0) {
            struct vec cp, exp;
            memset(&cp, 0, sizeof cp);
            push_copies(&cp, &args[pi]);
            expand_list((struct tok **)cp.a, cp.n, &exp);
            for (k = 0; k < exp.n; k++) {       /* (exp's tokens are this call's own: no copies) */
                struct tok *x = exp.a[k];
                if (k == 0) x->space = t->space;
                vpush(&out, x);
            }
            free(cp.a);
            free(exp.a);
            i++;
            continue;
        }
        vpush(&out, tokcopy(t));
        i++;
    }
    for (k = 0; k < out.n; k++) {       /* (out's tokens are its own: changed in place) */
        struct tok *x = out.a[k];
        x->hide = hs_union(x->hide, hide);
        x->file = at->file;
        x->line = at->line;
        x->bol = 0;
        if (k == 0) x->space = at->space;
    }
    *resp = out;
}

/* the arguments of a call, the '(' already taken: args[] (a vec each); the ')' */
static struct tok *collect_args(struct input *s, struct vec *args, int *nargs, int max, struct tok *mtok)
{
    int depth = 0;
    *nargs = 0;
    memset(&args[0], 0, sizeof args[0]);
    for (;;) {
        struct tok *t;
        if (!s->n) error(mtok, "unterminated macro call %s", mtok->text);
        t = s->a[--s->n];
        if (is(t, "(")) depth++;
        else if (is(t, ")")) {
            if (depth == 0) { (*nargs)++; return t; }
            depth--;
        } else if (is(t, ",") && depth == 0) {
            if (*nargs + 1 >= max) error(mtok, "too many arguments to macro %s", mtok->text);
            (*nargs)++;
            memset(&args[*nargs], 0, sizeof args[0]);
            tok_free(t);                            /* (the comma) */
            continue;
        }
        vpush(&args[*nargs], t);
    }
}

static void expand_list(struct tok **toks, int n, struct vec *outp)
{
    struct input s;
    struct vec out;
    memset(&s, 0, sizeof s);
    memset(&out, 0, sizeof out);
    in_pushlist(&s, toks, n);
    while (s.n) {
        struct tok *t = s.a[--s.n];
        struct macro *m;
        if (t->kind != T_ID) { VPUSH(&out, t); continue; }
        if (is(t, "__LINE__")) {
            struct tok *x = tokcopy(t);
            x->kind = T_NUM;
            x->text = intern(fmt("%d", t->line));
            x->hide = 0;
            vpush(&out, x);
            tok_free(t);
            continue;
        }
        if (is(t, "__FILE__")) {
            struct tok *x = tokcopy(t);
            x->kind = T_STR;
            x->text = intern(fmt("\"%s\"", t->file));
            x->sval = (unsigned char *)xstrdup(t->file);
            x->slen = strlen(t->file);
            x->hide = 0;
            vpush(&out, x);
            tok_free(t);
            continue;
        }
        m = mfind(t->text);
        if (!m || hs_has(t->hide, t->text)) { VPUSH(&out, t); continue; }
        if (!m->params) {
            struct vec body;
            subst(m, 0, hs_add(t->hide, m->name), t, &body);
            in_pushlist(&s, (struct tok **)body.a, body.n);         /* rescan */
            free(body.a);
            tok_free(t);                                                /* (the macro's name) */
            continue;
        }
        if (!s.n || !is(s.a[s.n - 1], "(")) { vpush(&out, t); continue; }
        tok_free(s.a[--s.n]);                                          /* the '(' */
        {
            static struct vec args[128];
            struct vec va, *use = args, fixedargs[128], commas;
            int nargs, k;
            struct tok *rparen = collect_args(&s, args, &nargs, 128, t);
            struct vec body, mine[128];
            int nmine = nargs;
            for (k = 0; k < nmine; k++) mine[k] = args[k];          /* (to free: args[] is reused) */
            memset(&va, 0, sizeof va);
            memset(&commas, 0, sizeof commas);
            if (m->nparams == 0 && nargs == 1 && args[0].n == 0) nargs = 0;
            if (m->variadic) {
                int fixed = m->nparams - 1;
                if (nargs < fixed) error(t, "too few arguments to macro %s", m->name);
                for (k = fixed; k < nargs; k++) {
                    int j;
                    if (k > fixed) {
                        struct tok *c = tokcopy(t);
                        c->kind = T_PUNCT;
                        c->text = intern(",");
                        c->hide = 0;
                        c->bol = c->space = 0;
                        vpush(&va, c);
                        vpush(&commas, c);
                    }
                    for (j = 0; j < args[k].n; j++) vpush(&va, args[k].a[j]);
                }
                for (k = 0; k < fixed; k++) fixedargs[k] = args[k];
                fixedargs[fixed] = va;
                use = fixedargs;
                nargs = fixed + 1;
            }
            if (nargs != m->nparams)
                error(t, "macro %s takes %d argument(s), %d given", m->name, m->nparams, nargs);
            {
                struct vec copy[128];
                for (k = 0; k < nargs; k++) copy[k] = use[k];        /* (args[] is reused by inner calls) */
                subst(m, copy, hs_add(hs_inter(t->hide, rparen->hide), m->name), t, &body);
            }
            in_pushlist(&s, (struct tok **)body.a, body.n);
            free(body.a);
            /* the call's own tokens are done with: subst made copies of the
               arguments (this list owns its tokens: each one goes to out or
               is freed here) */
            for (k = 0; k < nmine; k++) {
                int j;
                for (j = 0; j < mine[k].n; j++) tok_free(mine[k].a[j]);
                free(mine[k].a);
            }
            for (k = 0; k < commas.n; k++) tok_free(commas.a[k]);
            free(commas.a);
            free(va.a);
            tok_free(rparen);
            tok_free(t);
        }
    }
    free(s.a);
    *outp = out;
}

/* ---------------------------------------------------------------- #if */
struct ifev { struct tok **t; int n, i; struct tok *tok; };
/* #if's values, as C99 says: 64 bits (intmax_t, or uintmax_t when u), the
   usual arithmetic conversions between them (as p32cc.py's IfEval) */
struct ifv { unsigned long long v; int u; };
static struct ifv if_expr(struct ifev *e);
static const char *ifpeek(struct ifev *e) { return e->i < e->n ? e->t[e->i]->text : 0; }

static struct ifv ifv(unsigned long long v, int u)
{
    struct ifv r;
    r.v = v;
    r.u = u;
    return r;
}

static struct ifv if_unary(struct ifev *e)
{
    struct tok *t;
    struct ifv v;
    if (e->i >= e->n) error(e->tok, "#if expression ends early");
    t = e->t[e->i++];
    if (is(t, "-")) { v = if_unary(e); return ifv(0 - v.v, v.u); }
    if (is(t, "+")) return if_unary(e);
    if (is(t, "!")) { v = if_unary(e); return ifv(!v.v, 0); }
    if (is(t, "~")) { v = if_unary(e); return ifv(~v.v, v.u); }
    if (is(t, "(")) {
        v = if_expr(e);
        if (!ifpeek(e) || strcmp(ifpeek(e), ")")) error(e->tok, "missing ) in #if");
        e->i++;
        return v;
    }
    if (t->kind == T_NUM) {
        unsigned long long w;
        int u, l, nd, over;
        if (parse_int64(t, &w, &u, &l, &nd, &over) < 0) error(t, "bad number %s (no floating point)", t->text);
        if (over) error(t, "constant too big for #if");
        return ifv(w, u || w >> 63);
    }
    if (t->kind == T_CHAR) return ifv((unsigned long long)(long long)t->cval, 0);
    error(e->tok, "bad token %s in #if", t->text);
    return ifv(0, 0);
}

static const char *const if_levels[][5] = {
    { "||" }, { "&&" }, { "|" }, { "^" }, { "&" }, { "==", "!=" }, { "<", ">", "<=", ">=" },
    { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" } };

static struct ifv if_binary(struct ifev *e, int lvl)
{
    struct ifv v, r;
    int k, u;
    if (lvl == 10) return if_unary(e);
    v = if_binary(e, lvl + 1);
    for (;;) {
        const char *p = ifpeek(e), *op = 0;
        unsigned long long a, b;
        long long sa, sb;
        if (!p) break;
        for (k = 0; k < 5 && if_levels[lvl][k]; k++) if (!strcmp(p, if_levels[lvl][k])) op = if_levels[lvl][k];
        if (!op) break;
        e->i++;
        r = if_binary(e, lvl + 1);
        if ((!strcmp(op, "/") || !strcmp(op, "%")) && r.v == 0) error(e->tok, "division by zero in #if");
        if (!strcmp(op, "||")) { v = ifv(v.v || r.v, 0); continue; }
        if (!strcmp(op, "&&")) { v = ifv(v.v && r.v, 0); continue; }
        if (!strcmp(op, "<<") || !strcmp(op, ">>")) {      /* (the left operand's type) */
            int n = (int)(r.v & 63);
            v.v = op[0] == '<' ? v.v << n : v.u ? v.v >> n : (unsigned long long)((long long)v.v >> n);
            continue;
        }
        u = v.u || r.u;
        a = v.v;
        b = r.v;
        sa = (long long)a;
        sb = (long long)b;
        if (!strcmp(op, "==")) v = ifv(a == b, 0);
        else if (!strcmp(op, "!=")) v = ifv(a != b, 0);
        else if (!strcmp(op, "<")) v = ifv(u ? a < b : sa < sb, 0);
        else if (!strcmp(op, ">")) v = ifv(u ? a > b : sa > sb, 0);
        else if (!strcmp(op, "<=")) v = ifv(u ? a <= b : sa <= sb, 0);
        else if (!strcmp(op, ">=")) v = ifv(u ? a >= b : sa >= sb, 0);
        else if (!strcmp(op, "|")) v = ifv(a | b, u);
        else if (!strcmp(op, "^")) v = ifv(a ^ b, u);
        else if (!strcmp(op, "&")) v = ifv(a & b, u);
        else if (!strcmp(op, "+")) v = ifv(a + b, u);
        else if (!strcmp(op, "-")) v = ifv(a - b, u);
        else if (!strcmp(op, "*")) v = ifv(a * b, u);
        else if (u) v = ifv(op[0] == '/' ? a / b : a % b, 1);
        else if (sb == -1) v = ifv(op[0] == '/' ? 0 - a : 0, 0);     /* (no overflow: -2^63 / -1 wraps) */
        else v = ifv(op[0] == '/' ? (unsigned long long)(sa / sb) : (unsigned long long)(sa % sb), 0);
    }
    return v;
}

static struct ifv if_expr(struct ifev *e)
{
    struct ifv c = if_binary(e, 0);
    if (ifpeek(e) && !strcmp(ifpeek(e), "?")) {
        struct ifv a, b;
        e->i++;
        a = if_expr(e);
        if (!ifpeek(e) || strcmp(ifpeek(e), ":")) error(e->tok, "bad ?: in #if");
        e->i++;
        b = if_expr(e);
        return ifv(c.v ? a.v : b.v, a.u || b.u);
    }
    return c;
}

static int eval_if(struct tok **toks, int n, struct tok *tok)
{
    struct vec pre, exp;
    struct ifev e;
    int i = 0, v;
    memset(&pre, 0, sizeof pre);
    while (i < n) {
        struct tok *t = toks[i];
        if (t->kind == T_ID && is(t, "defined")) {
            const char *name;
            struct tok *x;
            if (i + 1 < n && is(toks[i + 1], "(")) { name = toks[i + 2]->text; i += 4; }
            else { name = toks[i + 1]->text; i += 2; }
            x = tokcopy(t);
            x->kind = T_NUM;
            x->text = intern(mfind(name) ? "1" : "0");
            vpush(&pre, x);
            continue;
        }
        vpush(&pre, tokcopy(t));            /* (expansion takes the copies: the line is freed after) */
        i++;
    }
    expand_list((struct tok **)pre.a, pre.n, &exp);
    for (i = 0; i < exp.n; i++) {
        struct tok *t = exp.a[i];
        if (t->kind == T_ID) {              /* an identifier left: 0 */
            t->kind = T_NUM;
            t->text = intern("0");
        }
    }
    e.t = (struct tok **)exp.a;
    e.n = exp.n;
    e.i = 0;
    e.tok = tok;
    v = if_expr(&e).v != 0;
    if (e.i != e.n) error(tok, "bad #if expression");
    for (i = 0; i < exp.n; i++) tok_free(exp.a[i]);
    free(pre.a);
    free(exp.a);
    return v != 0;
}

/* ---------------------------------------------------------------- directives */
static void process(struct tok **toks, int ntoks, struct vec *out);

static char *find_include(const char *name, const char *cur, int quoted, struct tok *tok)
{
    int i;
    FILE *f;
    if (quoted) {
        const char *slash = strrchr(cur, '/');
        char *p = slash ? fmt("%.*s/%s", (int)(slash - cur), cur, name) : fmt("%s", name);
        if ((f = fopen(p, "r")) != 0) { fclose(f); return p; }
    }
    for (i = 0; i < nincdirs; i++) {
        char *p = fmt("%s/%s", incdirs[i], name);
        if ((f = fopen(p, "r")) != 0) { fclose(f); return p; }
    }
    error(tok, "cannot find include file %s", name);
    return 0;
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "r");
    char *buf;
    int n = 0, cap = 4096, k;
    if (!f) return 0;
    buf = xmalloc(cap);
    while ((k = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += k;
        if (n + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void do_include(struct tok **rest, int n, struct tok *tok, struct vec *out)
{
    char name[512];
    int quoted, i;
    char *path, *src;
    struct tok **t;
    int nt;
    if (!n) error(tok, "#include needs a file");
    if (rest[0]->kind == T_STR) {
        int len = strlen(rest[0]->text) - 2;
        if (len > 511) len = 511;
        memcpy(name, rest[0]->text + 1, len);
        name[len] = 0;
        quoted = 1;
    } else if (is(rest[0], "<")) {
        name[0] = 0;
        for (i = 1; i < n && !is(rest[i], ">"); i++)
            if (strlen(name) + strlen(rest[i]->text) < 511) strcat(name, rest[i]->text);
        quoted = 0;
    } else {                            /* a macro: expanded (copies; the line is freed after) */
        struct vec c, e;
        memset(&c, 0, sizeof c);
        for (i = 0; i < n; i++) vpush(&c, tokcopy(rest[i]));
        expand_list((struct tok **)c.a, c.n, &e);
        do_include((struct tok **)e.a, e.n, tok, out);
        for (i = 0; i < e.n; i++) tok_free(e.a[i]);
        free(c.a);
        free(e.a);
        return;
    }
    path = find_include(name, tok->file, quoted, tok);
    for (i = 0; i < once.n; i++) if (!strcmp(once.a[i], path)) return;
    if (!(src = read_text(path))) error(tok, "cannot read %s", path);
    t = tokenize(src, path, &nt);
    free(src);
    process(t, nt, out);
    free(t);                            /* (the tokens live on in out; the array is done with) */
}

static void do_define(struct tok **rest, int n, struct tok *tok)
{
    struct macro *m;
    int j, k;
    if (!n || rest[0]->kind != T_ID) error(tok, "#define needs a name");
    m = xmalloc(sizeof *m);
    memset(m, 0, sizeof *m);
    m->name = rest[0]->text;
    if (n > 1 && is(rest[1], "(") && !rest[1]->space) {        /* function-like */
        struct vec ps;
        j = 2;
        memset(&ps, 0, sizeof ps);
        for (;;) {
            struct tok *t;
            if (j >= n) error(tok, "bad macro parameters");
            t = rest[j];
            if (is(t, ")")) { j++; break; }
            if (is(t, "...")) { m->variadic = 1; vpush(&ps, intern("__VA_ARGS__")); }
            else if (t->kind == T_ID) vpush(&ps, t->text);
            else error(t, "bad macro parameter %s", t->text);
            j++;
            if (j < n && is(rest[j], ",")) j++;
        }
        m->params = (char **)(ps.a ? ps.a : xmalloc(sizeof(char *)));
        m->nparams = ps.n;
        j--;                            /* (the body starts after rest[j]) */
    } else
        j = 0;
    m->nbody = n - j - 1;               /* the body, copied: the line's tokens are freed after it */
    m->body = xmalloc(sizeof *m->body * (m->nbody + 1));
    for (k = 0; k < m->nbody; k++) m->body[k] = tokcopy(rest[j + 1 + k]);
    mdefine(m);
}

struct cond { int taking, taken, saw_else; };

static void directive(struct tok **line, int n, struct cond *cond, int *ncond, int skipping, struct vec *out)
{
    struct tok *d;
    struct tok **rest = line + 1;
    int nrest = n - 1;
    if (!n) return;                                  /* the null directive */
    d = line[0];
    if (is(d, "if") || is(d, "ifdef") || is(d, "ifndef")) {
        int v;
        if (*ncond >= 64) error(d, "#if nested too deeply");
        if (skipping) { cond[*ncond].taking = 0; cond[*ncond].taken = 1; cond[*ncond].saw_else = 0; (*ncond)++; return; }
        if (is(d, "if")) v = eval_if(rest, nrest, d);
        else {
            if (!nrest || rest[0]->kind != T_ID) error(d, "#%s needs a name", d->text);
            v = (mfind(rest[0]->text) != 0) == is(d, "ifdef");
        }
        cond[*ncond].taking = v; cond[*ncond].taken = v; cond[*ncond].saw_else = 0;
        (*ncond)++;
        return;
    }
    if (is(d, "elif") || is(d, "else") || is(d, "endif")) {
        struct cond *c;
        int outer_skip;
        if (!*ncond) error(d, "#%s without #if", d->text);
        c = &cond[*ncond - 1];
        outer_skip = *ncond > 1 && !cond[*ncond - 2].taking;
        if (is(d, "endif")) (*ncond)--;
        else if (is(d, "else")) {
            if (c->saw_else) error(d, "#else after #else");
            c->saw_else = 1;
            c->taking = !c->taken && !outer_skip;
            c->taken = 1;
        } else {
            if (c->saw_else) error(d, "#elif after #else");
            if (c->taken || outer_skip) c->taking = 0;
            else { c->taking = eval_if(rest, nrest, d); c->taken = c->taking; }
        }
        return;
    }
    if (skipping) return;
    if (is(d, "define")) do_define(rest, nrest, d);
    else if (is(d, "undef")) { if (nrest) mundef(rest[0]->text); }
    else if (is(d, "include")) do_include(rest, nrest, d, out);
    else if (is(d, "error")) {
        char msg[1024];
        int i;
        strcpy(msg, "#error");
        for (i = 0; i < nrest && strlen(msg) + strlen(rest[i]->text) < 1000; i++) { strcat(msg, " "); strcat(msg, rest[i]->text); }
        error(d, "%s", msg);
    } else if (is(d, "pragma") || is(d, "line") || is(d, "ident")) {
        if (is(d, "pragma") && nrest && is(rest[0], "once")) vpush(&once, d->file);
    } else
        error(d, "unknown directive #%s", d->text);
}

static void process(struct tok **toks, int ntoks, struct vec *out)
{
    struct cond cond[64];
    int ncond = 0, i = 0, k;
    struct vec pending;
    memset(&pending, 0, sizeof pending);
    while (toks[i]->kind != T_EOF) {
        struct tok *t = toks[i];
        if (t->bol && t->kind == T_PUNCT && is(t, "#")) {
            int j = i + 1;
            if (pending.n) {
                struct vec e;
                expand_list((struct tok **)pending.a, pending.n, &e);
                for (k = 0; k < e.n; k++) VPUSH(out, e.a[k]);
                free(e.a);
                pending.n = 0;
            }
            while (toks[j]->kind != T_EOF && (!toks[j]->bol || j == i + 1)) j++;
            directive(toks + i + 1, j - (i + 1), cond, &ncond, ncond && !cond[ncond - 1].taking, out);
            for (k = i; k < j; k++) tok_free(toks[k]);      /* (the directive's line, '#' and all) */
            i = j;
            continue;
        }
        if (ncond && !cond[ncond - 1].taking) { tok_free(t); i++; continue; }      /* (skipped) */
        VPUSH(&pending, t);
        i++;
    }
    if (pending.n) {
        struct vec e;
        expand_list((struct tok **)pending.a, pending.n, &e);
        for (k = 0; k < e.n; k++) VPUSH(out, e.a[k]);
        free(e.a);
    }
    free(pending.a);
    if (ncond) error(toks[i], "#if without #endif");
    (void)ntoks;
}

/* preprocessing is over: the macros and the hidesets go back to the heap
   (the parser and the code generator need neither) */
static void cpp_done(struct tok **toks, int n)
{
    int k;
    for (k = 0; k < n; k++) toks[k]->hide = 0;
    for (k = 0; k < MHASH; k++)
        while (mtab[k]) {
            struct macro *m = mtab[k];
            mtab[k] = m->next;
            mfree(m);
        }
    for (k = 0; k < HHASH; k++)
        while (htab[k]) {
            struct hcell *c = htab[k];
            htab[k] = c->chain;
            free(c);
        }
}

struct tok **preprocess(const char *path, int *n)
{
    char *src = read_text(path);
    struct tok **t;
    int nt;
    struct vec out;
    if (!src) { fprintf(stderr, "cc: cannot read %s\n", path); exit(1); }
    t = tokenize(src, path, &nt);
    free(src);
    memset(&out, 0, sizeof out);
    process(t, nt, &out);
    free(t);
    cpp_done((struct tok **)out.a, out.n);
    *n = out.n;
    return (struct tok **)out.a;
}

/* the -E text (cc/p32cc.py's detok) */
char *detok(struct tok **t, int n)
{
    struct vec parts;
    int i, len = 2, line = -1, k;
    char *s;
    memset(&parts, 0, sizeof parts);
    for (i = 0; i < n; i++) {
        if (t[i]->line != line && parts.n) vpush(&parts, "\n");
        else if (t[i]->space || (parts.n && strcmp(parts.a[parts.n - 1], "\n"))) vpush(&parts, " ");
        line = t[i]->line;
        vpush(&parts, t[i]->text);
    }
    for (k = 0; k < parts.n; k++) len += strlen(parts.a[k]);
    s = xmalloc(len);
    s[0] = 0;
    {
        char *p = s;
        for (k = 0; k < parts.n; k++) { strcpy(p, parts.a[k]); p += strlen(p); }
        strcpy(p, "\n");
    }
    return s;
}
