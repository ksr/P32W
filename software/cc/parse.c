/* parse.c -- the typed syntax tree (cc/cparse.py): recursive descent over
 * the preprocessed tokens; types worked out as the tree is built (C89, 32-bit
 * integers); constant expressions folded. */
#include "cc.h"
#include <ctype.h>

/* ---------------------------------------------------------------- state */
static struct tok **T;
static int NT, I;
static struct program *prog;
static struct obj *curfn;
static int nlabel;
/* the C library's variadic functions and their headers (an undeclared call
   to one is an error, not a warning) */
static const char *variadic_lib(const char *name)
{
    static const char *const tab[] = { "printf", "stdio.h", "fprintf", "stdio.h", "sprintf", "stdio.h",
        "snprintf", "stdio.h", "scanf", "stdio.h", "fscanf", "stdio.h", "sscanf", "stdio.h", "open", "fcntl.h",
        "execl", "unistd.h", "execlp", "unistd.h", "execle", "unistd.h", 0 };
    int k;
    for (k = 0; tab[k]; k += 2)
        if (!strcmp(name, tab[k])) return tab[k + 1];
    return 0;
}

static int brk_depth, cont_depth, hdr_depth;     /* (hdr: in a loop's condition or increment) */
static struct vec switches;             /* struct node * */
static struct vec fn_labels, fn_gotos;  /* char * / struct node * */

enum { E_VAR, E_TYPEDEF, E_ENUM };
struct entry { char *name; int kind; struct obj *obj; struct type *ty; int val; struct entry *next; };
#define SHASH 64
struct scope { struct entry *h[SHASH]; struct scope *up; struct entry *tags; };
static struct scope *scope, *gscope;

int parser_labels(void) { return nlabel; }

static unsigned sh(const char *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % SHASH;
}

static void push_scope(void)
{
    struct scope *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->up = scope;
    scope = s;
}

static void free_entries(struct entry *e)
{
    while (e) { struct entry *next = e->next; free(e); e = next; }
}

static void pop_scope(void)             /* (nothing points into a closed scope: it is freed) */
{
    struct scope *s = scope;
    int k;
    scope = s->up;
    for (k = 0; k < SHASH; k++) free_entries(s->h[k]);
    free_entries(s->tags);
    free(s);
}

static struct entry *find_in(struct scope *s, const char *name)
{
    struct entry *e;
    for (e = s->h[sh(name)]; e; e = e->next)
        if (e->name == name) return e;
    return 0;
}

static struct entry *lookup(const char *name)
{
    struct scope *s;
    for (s = scope; s; s = s->up) {
        struct entry *e = find_in(s, name);
        if (e) return e;
    }
    return 0;
}

static struct entry *declare_in(struct scope *s, char *name, int kind)
{
    struct entry *e = find_in(s, name);
    if (!e) {
        e = xmalloc(sizeof *e);
        memset(e, 0, sizeof *e);
        e->name = name;
        e->next = s->h[sh(name)];
        s->h[sh(name)] = e;
    }
    e->kind = kind;
    e->obj = 0;
    e->ty = 0;
    e->val = 0;
    return e;
}

/* tags: a separate list per scope */
static struct type *find_tag_in(struct scope *s, const char *name)
{
    struct entry *e;
    for (e = s->tags; e; e = e->next) if (e->name == name) return e->ty;
    return 0;
}

static struct type *lookup_tag(const char *name)
{
    struct scope *s;
    for (s = scope; s; s = s->up) {
        struct type *t = find_tag_in(s, name);
        if (t) return t;
    }
    return 0;
}

static void set_tag(char *name, struct type *t)
{
    struct entry *e;
    for (e = scope->tags; e; e = e->next) if (e->name == name) { e->ty = t; return; }
    e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->name = name;
    e->ty = t;
    e->next = scope->tags;
    scope->tags = e;
}

/* ---------------------------------------------------------------- tokens */
#define TOK (T[I])
static struct tok *peek(int k) { return T[I + k < NT ? I + k : NT - 1]; }
static int at(const char *s) { return (TOK->kind == T_PUNCT || TOK->kind == T_ID) && is(TOK, s); }
static void skip(const char *s)
{
    if (!at(s)) error(TOK, "expected '%s', got '%s'", s, TOK->text[0] ? TOK->text : "end of file");
    I++;
}
static int consume(const char *s) { if (at(s)) { I++; return 1; } return 0; }
static struct tok *ident(void)
{
    if (TOK->kind != T_ID) error(TOK, "expected a name, got '%s'", TOK->text);
    return T[I++];
}

static const char *const type_kw[] = { "void", "char", "short", "int", "long", "signed", "unsigned", "struct",
    "union", "enum", "const", "volatile", "typedef", "extern", "static", "auto", "register", "float",
    "double", "_Bool", "inline", "restrict", 0 };

static int is_typename(struct tok *t)
{
    int i;
    struct entry *e;
    if (t->kind != T_ID) return 0;
    for (i = 0; type_kw[i]; i++) if (is(t, type_kw[i])) return 1;
    e = lookup(t->text);
    return e && e->kind == E_TYPEDEF;
}

static char *newlabel(const char *what)
{
    nlabel++;
    return intern(fmt(".L%s%d", what, nlabel));
}

/* ---------------------------------------------------------------- nodes, constants */
/* A function's nodes come from blocks that are all given back once it has
   been generated (function_done), so a big file needs memory for its
   largest function, not for all of them. Nodes outside functions, and
   string literals (the data section, at the end), are malloc'd. */
void (*function_done)(struct obj *fn);
#define NBLOCK 4088                     /* (with malloc's header, 4 KB: a freed token slab's size, D33) */
struct nblock { struct nblock *next; };
static struct nblock *nblocks;
static char *nfree, *nend;

static void *node_alloc(unsigned n)
{
    n = (n + 7) & ~7u;
    if (nfree + n > nend) {
        struct nblock *b = xmalloc(NBLOCK);
        b->next = nblocks;
        nblocks = b;
        nfree = (char *)b + 8;          /* (8: aligned for the host's pointers too) */
        nend = (char *)b + NBLOCK;
    }
    nfree += n;
    return nfree - n;
}

static void release_nodes(void)
{
    while (nblocks) { struct nblock *next = nblocks->next; free(nblocks); nblocks = next; }
    nfree = nend = 0;
}

/* A node keeps only where it came from (for error messages and -S's
   source lines): a token of its own for each line, never one of the
   program's tokens, so those can be freed as soon as they are parsed */
#define LHASH 256
struct linetok { struct tok t; struct linetok *next; };
static struct linetok *ltab[LHASH];

static struct tok *line_tok(struct tok *t)
{
    unsigned h;
    struct linetok *l;
    if (!t) return 0;
    h = ((unsigned)(size_t)t->file + (unsigned)t->line * 31u) % LHASH;
    for (l = ltab[h]; l; l = l->next)
        if (l->t.line == t->line && l->t.file == t->file) return &l->t;
    l = xmalloc(sizeof *l);
    memset(l, 0, sizeof *l);
    l->t.kind = T_PUNCT;
    l->t.text = intern("");
    l->t.file = t->file;
    l->t.line = t->line;
    l->next = ltab[h];
    ltab[h] = l;
    return &l->t;
}

/* the tokens T[from..to) are done with: freed, and their places point at
   the end-of-file token (string literals stay: their bytes go in the data) */
static void free_tokens(int from, int to)
{
    int k;
    for (k = from; k < to; k++)
        if (T[k] != T[NT - 1] && T[k]->kind != T_STR) {
            tok_free(T[k]);
            T[k] = T[NT - 1];
        }
}

static struct node *mk(int kind, struct tok *t, struct type *ty)
{
    struct node *n = curfn && kind != N_STR ? node_alloc(sizeof *n) : xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->tok = line_tok(t);
    n->ty = ty;
    return n;
}

/* a value as type t holds it (cparse.py's wrap) */
static unsigned wrap(unsigned v, struct type *t)
{
    if (t == ty_bool) return v != 0;
    if (is_integer(t) || t->kind == K_PTR) {
        if (t->size == 1) v = t->uns ? (v & 0xFF) : (unsigned)(int)(signed char)(v & 0xFF);
        else if (t->size == 2) v = t->uns ? (v & 0xFFFF) : (unsigned)(int)(short)(v & 0xFFFF);
    }
    return v;
}

static int unsigned_value(struct type *t) { return t->uns || t->kind == K_PTR; }

static struct node *num(unsigned v, struct type *ty, struct tok *t)
{
    struct node *n = mk(N_NUM, t, ty);
    n->val = wrap(v, ty);
    return n;
}

/* ---------------------------------------------------------------- forward */
static struct node *expr(void);
static struct node *assign(void);
static struct node *conditional(void);
static struct node *cast(void);
static struct node *unary(void);
static struct node *stmt(void);
static struct node *compound(void);
static struct type *declspec(int allow_storage, char **storage);
static struct type *declarator(struct type *ty, int abstract, struct tok **name);
static struct type *suffixes(struct type *ty);
static int const_expr(void);
static struct node *rvalue(struct node *e);
static struct node *convert(struct node *e, struct type *ty, struct tok *t);
static struct node *new_binary(const char *op, struct node *l, struct node *r, struct tok *t);
static struct node *new_assign(struct node *lhs, struct node *rhs, struct tok *t);
static struct node *comma(struct node *a, struct node *b, struct tok *t);
static struct node *var_node(struct obj *o, struct tok *t);
static struct node *new_cast(struct node *e, struct type *ty, struct tok *t);
static struct node *cast_node(struct node *e, struct type *ty, struct tok *t);
static struct node *build_call(struct node *fnode, struct vec *args, struct tok *t);
static struct node *llcast(struct node *e, struct type *ty, struct tok *t);
static struct node *llbinary(const char *op, struct node *l, struct node *r, struct tok *t);
static struct node *llop(const char *op, struct node **args, int nargs, struct type *ty, struct tok *t);
static struct node *truth(struct node *e);
static struct node *dcast(struct node *e, struct type *ty, struct tok *t);
static struct node *dbinary(const char *op, struct node *l, struct node *r, struct tok *t);
static struct node *dop(const char *op, struct node **args, int nargs, struct tok *t);
static int dv(struct node *e, double *v);
static int dfits(double v, struct type *ty);

/* ================================================================ declarations */
static struct type *basic(int *c, struct tok *t)
{
    /* c: void char short int long signed unsigned _Bool float double */
    int u = c[6];
    if (u && c[5]) error(t, "signed and unsigned");
    if (c[0]) return ty_void;
    if (c[8] || c[9]) return ty_double;     /* (float, double, long double: one type) */
    if (c[7]) return ty_bool;
    if (c[1]) return u ? ty_uchar : ty_char;
    if (c[2]) return u ? ty_ushort : ty_short;
    if (c[4] >= 2) return u ? ty_ullong : ty_llong;
    if (c[4]) return u ? ty_ulong : ty_long;
    return u ? ty_uint : ty_int;
}

static struct type *struct_decl(int kind)
{
    char *tag = 0;
    struct type *st = 0;
    struct vec names, tys, widths;     /* (widths: width + 1 of a bit-field, else 0) */
    if (TOK->kind == T_ID) tag = ident()->text;
    if (!at("{")) {
        if (!tag) error(TOK, "%s needs a tag or a body", kind == K_STRUCT ? "struct" : "union");
        st = lookup_tag(tag);
        if (!st) {
            st = struct_type(kind, tag);
            set_tag(tag, st);
        } else if (st->kind != kind)
            error(TOK, "%s is a different kind of tag", tag);
        return st;
    }
    skip("{");
    if (tag) {
        st = find_tag_in(scope, tag);
        if (st && st->members) error(TOK, "%s redefined", tag);
    }
    if (!st) {
        st = struct_type(kind, tag);
        if (tag) set_tag(tag, st);
    }
    memset(&names, 0, sizeof names);
    memset(&tys, 0, sizeof tys);
    memset(&widths, 0, sizeof widths);
    while (!consume("}")) {
        struct type *base = declspec(0, 0);
        if (consume(";")) {             /* an anonymous struct / union member */
            if (base->kind == K_STRUCT || base->kind == K_UNION) { vpush(&names, 0); vpush(&tys, base); vpush(&widths, 0); }
            continue;
        }
        for (;;) {
            struct tok *name = 0;
            struct type *ty = at(":") ? base : declarator(base, 0, &name);     /* (: alone: an unnamed bit-field) */
            int width = -1;
            if (consume(":")) {
                struct tok *t = TOK;
                width = const_expr();
                if (!is_integer(ty)) error(t, "a bit-field needs an integer type");
                if (ty->kind == K_LLONG) error(t, "bit-fields wider than 32 bits are not supported");
                if (width < 0 || width > ty->size * 8) error(t, "bit-field width %d is out of range", width);
                if (width == 0 && name) error(t, "a bit-field of width 0 has no name");
            } else if (ty->kind == K_FUNC || (ty->size == 0 && ty->kind != K_ARRAY))
                error(name ? name : TOK, "member %s has an incomplete type", name ? name->text : "");
            vpush(&names, name ? name->text : 0);
            vpush(&tys, ty);
            vpush(&widths, (void *)(long)(width + 1));
            if (!consume(",")) break;
        }
        skip(";");
    }
    {
        int *w = xmalloc(sizeof(int) * (names.n ? names.n : 1)), k;
        for (k = 0; k < names.n; k++) w[k] = (int)(long)widths.a[k] - 1;
        layout(st, (char **)names.a, (struct type **)tys.a, w, names.n);
    }
    return st;
}

static struct type *enum_decl(void)
{
    char *tag = 0;
    struct type *ty;
    int v = 0;
    if (TOK->kind == T_ID) tag = ident()->text;
    ty = enum_type();
    if (!at("{")) {
        struct type *e = tag ? lookup_tag(tag) : 0;
        return e ? e : ty;
    }
    skip("{");
    while (!consume("}")) {
        struct tok *name = ident();
        struct entry *e;
        if (consume("=")) v = const_expr();
        e = declare_in(scope, name->text, E_ENUM);
        e->val = v;
        v++;
        if (!consume(",")) { skip("}"); break; }
    }
    if (tag) set_tag(tag, ty);
    return ty;
}

static int saw_volatile;                /* (the last declspec had volatile) */

static struct type *declspec(int allow_storage, char **storage)
{
    char *st = 0;
    int c[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, any = 0, cnst = 0;
    static const char *const bnames[] = { "void", "char", "short", "int", "long", "signed", "unsigned", "_Bool",
                                          "float", "double" };
    struct type *ty = 0;
    struct tok *tok0 = TOK;
    saw_volatile = 0;
    for (;;) {
        struct tok *t = TOK;
        int k;
        struct entry *e;
        if (t->kind != T_ID) break;
        if (is(t, "typedef") || is(t, "extern") || is(t, "static") || is(t, "auto") || is(t, "register")) {
            if (!allow_storage) error(t, "storage class not allowed here");
            if (st && strcmp(st, t->text)) error(t, "two storage classes");
            st = t->text;
            I++;
            continue;
        }
        if (is(t, "const") || is(t, "volatile") || is(t, "inline") || is(t, "restrict")) {   /* (restrict: a hint, ignored) */
            cnst = cnst || is(t, "const");
            saw_volatile = saw_volatile || is(t, "volatile");
            I++;
            continue;
        }
        if (is(t, "struct") || is(t, "union")) {
            if (ty || any) error(t, "two types");
            I++;
            ty = struct_decl(is(t, "struct") ? K_STRUCT : K_UNION);
            continue;
        }
        if (is(t, "enum")) {
            if (ty || any) error(t, "two types");
            I++;
            ty = enum_decl();
            continue;
        }
        for (k = 0; k < 10; k++) if (is(t, bnames[k])) break;
        if (k < 10) {
            if (ty) error(t, "two types");
            c[k]++;
            any = 1;
            I++;
            continue;
        }
        e = lookup(t->text);
        if (e && e->kind == E_TYPEDEF && !ty && !any) { ty = e->ty; I++; continue; }
        break;
    }
    if (!ty) {
        if (!any) {
            if (!st && !cnst && TOK == tok0) error(tok0, "expected a type, got '%s'", tok0->text);
            ty = ty_int;                /* C89 implicit int */
        } else
            ty = basic(c, tok0);
    }
    if (storage) *storage = st;
    return ty;
}

static int abstract_params(void)
{
    struct tok *nxt = peek(1);
    return is(nxt, ")") || is_typename(nxt) || is(nxt, "...");
}

static struct type *declarator(struct type *ty, int abstract, struct tok **name)
{
    while (consume("*")) {
        ty = ptr_to(ty);
        while (TOK->kind == T_ID && (is(TOK, "const") || is(TOK, "volatile") || is(TOK, "restrict"))) {
            if (is(TOK, "volatile")) saw_volatile = 1;  /* T *volatile p: p itself is volatile, so never kept */
            I++;                                        /* in a register (setjmp / longjmp would undo it) */
        }
    }
    if (at("(") && !abstract_params() && !(abstract && is_typename(peek(1)))) {
        int start = I, depth = 1, end;
        I++;
        while (depth) {
            if (TOK->kind == T_EOF) error(TOK, "unbalanced parentheses");
            if (at("(")) depth++;
            else if (at(")")) depth--;
            I++;
        }
        ty = suffixes(ty);
        end = I;
        I = start + 1;
        ty = declarator(ty, abstract, name);
        skip(")");
        I = end;
        return ty;
    }
    *name = 0;
    if (TOK->kind == T_ID && !is_typename(TOK)) *name = ident();
    else if (TOK->kind == T_ID) *name = ident();    /* a typedef name redeclared (a parameter too: qstr qstr) */
    else if (!abstract) error(TOK, "expected a name, got '%s'", TOK->text);
    return suffixes(ty);
}

static struct type *params_of(int *nparams, int *variadic, struct tok ***names, int *proto, int **vols)
{
    struct vec ps, ns, vs;
    *variadic = 0;
    *proto = 1;
    *vols = 0;
    if (consume(")")) { *proto = 0; *nparams = 0; *names = 0; return 0; }
    if (at("void") && is(peek(1), ")")) { I += 2; *nparams = 0; *names = 0; return 0; }
    memset(&ps, 0, sizeof ps);
    memset(&ns, 0, sizeof ns);
    memset(&vs, 0, sizeof vs);
    for (;;) {
        struct type *base, *ty;
        struct tok *name = 0;
        if (consume("...")) { *variadic = 1; skip(")"); break; }
        if (!is_typename(TOK) && TOK->kind == T_ID) error(TOK, "old-style (K&R) parameter lists are not supported");
        int vol;
        base = declspec(1, 0);
        vol = saw_volatile;
        saw_volatile = 0;
        ty = declarator(base, 1, &name);
        vol = vol || saw_volatile;      /* (a volatile parameter: kept in memory) */
        if (ty->kind == K_ARRAY) ty = ptr_to(ty->base);
        else if (ty->kind == K_FUNC) ty = ptr_to(ty);
        vpush(&ps, ty);
        vpush(&ns, name);
        vpush(&vs, vol ? (void *)1 : (void *)0);
        if (consume(")")) break;
        skip(",");
    }
    *nparams = ps.n;
    *names = (struct tok **)ns.a;
    if (vs.n) {
        int k;
        *vols = xmalloc(vs.n * sizeof **vols);
        for (k = 0; k < vs.n; k++) (*vols)[k] = vs.a[k] != 0;
        free(vs.a);
    }
    return (struct type *)ps.a;          /* (the array of parameter types, cast back by the caller) */
}

static struct type *suffixes(struct type *ty)
{
    if (consume("[")) {
        int n = -1;
        struct type *inner;
        if (!at("]")) {
            n = const_expr();
            if (n < 0) error(TOK, "negative array size");
        }
        skip("]");
        inner = suffixes(ty);
        if (inner->kind == K_FUNC) error(TOK, "array of functions");
        return array_of(inner, n);
    }
    if (at("(")) {
        int np, var, proto, *vols;
        struct tok **names;
        struct type **ps, *ret, *f;
        I++;
        ps = (struct type **)params_of(&np, &var, &names, &proto, &vols);
        ret = suffixes(ty);
        if (ret->kind == K_FUNC || ret->kind == K_ARRAY) error(TOK, "a function cannot return that");
        if (proto && !ps) ps = xmalloc(sizeof *ps);     /* (f(void): prototyped, no parameters) */
        f = func_type(ret, proto ? ps : 0, np, var);
        f->param_names = names;
        f->param_vol = vols;            /* (which parameters are volatile) */
        return f;
    }
    return ty;
}

static struct type *typename_(void)
{
    struct tok *name;
    struct type *base = declspec(0, 0);
    return declarator(base, 1, &name);
}

/* ---------------------------------------------------------------- globals */
static struct obj *new_obj(char *name, struct type *ty, int local)
{
    struct obj *o = xmalloc(sizeof *o);
    memset(o, 0, sizeof *o);
    o->name = name;
    o->ty = ty;
    o->local = local;
    o->label = name;
    return o;
}

static struct obj *declare_global(struct tok *name, struct type *ty, char *storage)
{
    struct entry *e;
    struct obj *o;
    if (curfn && ty->kind == K_FUNC && ty->param_names) {
        struct type *c = xmalloc(sizeof *c);    /* (declared in a body: never a definition, so */
        *c = *ty;                               /*  no names: the body's tokens are freed) */
        c->param_names = 0;
        ty = c;
    }
    e = find_in(gscope, name->text);
    if (e && e->kind == E_VAR) {
        o = e->obj;
        if (ty->kind == K_FUNC && o->ty->kind == K_FUNC) {
            if (ty->params) o->ty = ty;
        } else if (ty->kind == K_ARRAY && o->ty->kind == K_ARRAY && o->ty->length < 0)
            o->ty = ty;
        if (storage && !strcmp(storage, "static")) o->is_static = 1;
        if (!storage || strcmp(storage, "extern")) o->ext = 0;
        return o;
    }
    o = new_obj(name->text, ty, 0);
    o->is_static = storage && !strcmp(storage, "static");
    o->ext = storage && !strcmp(storage, "extern");
    e = declare_in(gscope, name->text, E_VAR);
    e->obj = o;
    vpush(&prog->globals, o);
    return o;
}

static void function(struct obj *fn, struct tok *name)
{
    struct type *ty = fn->ty;
    int k, body0;
    if (fn->is_def) error(name, "function %s defined twice", name->text);
    fn->is_def = 1;
    fn->ext = 0;
    curfn = fn;
    memset(&fn->locals, 0, sizeof fn->locals);
    memset(&fn->params, 0, sizeof fn->params);
    memset(&fn_labels, 0, sizeof fn_labels);
    memset(&fn_gotos, 0, sizeof fn_gotos);
    push_scope();
    fn->retptr = 0;
    if (is_memval(ty->base)) {          /* where to put the result: argument 0 */
        fn->retptr = new_obj("", ptr_to(ty->base), 1);
        vpush(&fn->params, fn->retptr);
    }
    if (ty->params && ty->param_names)
        for (k = 0; k < ty->nparams; k++) {
            struct tok *pn = ty->param_names[k];
            struct obj *p;
            struct entry *e;
            if (!pn) error(name, "parameter %d of %s has no name", k + 1, name->text);
            p = new_obj(pn->text, ty->params[k], 1);
            if (ty->param_vol && ty->param_vol[k]) p->addr_taken = 1;  /* (volatile: kept in memory, so longjmp cannot undo it) */
            vpush(&fn->params, p);
            e = declare_in(scope, pn->text, E_VAR);
            e->obj = p;
        }
    body0 = I;                          /* (the body's tokens: freed once it is generated) */
    fn->body = compound();
    for (k = 0; k < fn_gotos.n; k++) {
        struct node *g = fn_gotos.a[k];
        int j, found = 0;
        for (j = 0; j < fn_labels.n; j++) if (fn_labels.a[j] == g->label) found = 1;
        if (!found) error(g->tok, "label %s is not defined", g->label);
    }
    pop_scope();
    curfn = 0;
    if (function_done) {                /* generated now, and its nodes and tokens given back */
        function_done(fn);
        fn->body = 0;
        release_nodes();
        free_tokens(body0, I);          /* (what the statements left: the braces) */
    }
}

/* ---------------------------------------------------------------- initialisers */
struct init { int is_list; struct node *e; struct vec items; struct tok *tok;
              struct vec desig; struct init *inner; };    /* (desig.n: a [i] = / .m = item, of inner) */
struct desig { int is_index, index; char *name; struct tok *tok; int is_pos; };   /* (is_pos: anon_path's) */
struct item { int off; struct type *ty; struct node *e; unsigned char *bytes; int nbytes; int bfbit, bfwidth; };

static struct init *initializer(void)
{
    struct init *in = xmalloc(sizeof *in);
    memset(in, 0, sizeof *in);
    if (at("{")) {
        I++;
        in->is_list = 1;
        while (!consume("}")) {
            struct init *d = 0;
            while (at("[") || at(".")) {
                struct desig *g = xmalloc(sizeof *g);
                memset(g, 0, sizeof *g);
                g->tok = TOK;
                if (!d) { d = xmalloc(sizeof *d); memset(d, 0, sizeof *d); }
                if (consume("[")) {
                    g->is_index = 1;
                    g->index = const_expr();
                    skip("]");
                } else {
                    I++;
                    g->name = ident()->text;
                }
                vpush(&d->desig, g);
            }
            if (d) {
                skip("=");
                d->inner = initializer();
                d->tok = ((struct desig *)d->desig.a[0])->tok;
                vpush(&in->items, d);
            } else
                vpush(&in->items, initializer());
            if (!consume(",")) { skip("}"); break; }
        }
        in->tok = TOK;
        return in;
    }
    in->e = assign();
    in->tok = TOK;
    return in;
}

static int fill(struct type *ty, struct vec *items, int *idx, int off, struct vec *out, struct tok *tok, int braced);

/* walk an initialiser against a type: out gets struct items in order (a
   later one for the same place wins; bf: the bit-field's member, or 0); the
   array length used */
static int flatten_init(struct type *ty, struct init *in, int off, struct vec *out, struct tok *tok, struct member *bf)
{
    if (ty->kind == K_ARRAY) {
        struct init *src = in;
        int n, idx = 0;
        if (in->is_list && in->items.n == 1 && !((struct init *)in->items.a[0])->is_list &&
            !((struct init *)in->items.a[0])->desig.n && ((struct init *)in->items.a[0])->e->kind == N_STR)
            src = in->items.a[0];
        if (!src->is_list && src->e->kind == N_STR && ty->base->kind == K_CHAR) {
            struct item *it = xmalloc(sizeof *it);
            n = ty->length >= 0 ? ty->length : src->e->dlen;
            it->off = off;
            it->ty = ty;
            it->e = 0;
            it->bytes = xmalloc(n + 1);
            memset(it->bytes, 0, n + 1);
            memcpy(it->bytes, src->e->data, n < src->e->dlen ? n : src->e->dlen);
            it->nbytes = n;
            vpush(out, it);
            return n;
        }
        if (!in->is_list) error(tok, "array initialiser needs { }");
        n = fill(ty, &in->items, &idx, off, out, tok, 1);
        return ty->length < 0 ? n : ty->length;
    }
    if (ty->kind == K_STRUCT || ty->kind == K_UNION) {
        int idx = 0;
        if (!in->is_list) {
            if (in->e->ty && in->e->ty == ty) {
                struct item *it = xmalloc(sizeof *it);
                memset(it, 0, sizeof *it);
                it->off = off;
                it->ty = ty;
                it->e = in->e;
                vpush(out, it);
                return 1;
            }
            error(tok, "struct initialiser needs { }");
        }
        fill(ty, &in->items, &idx, off, out, tok, 1);
        return 1;
    }
    while (in->is_list) {
        if (in->items.n != 1 || ((struct init *)in->items.a[0])->desig.n)
            error(tok, "scalar initialiser with %d values", in->items.n);
        in = in->items.a[0];
    }
    {
        struct item *it = xmalloc(sizeof *it);
        memset(it, 0, sizeof *it);
        it->off = off;
        it->ty = ty;
        it->e = in->e;
        if (bf) { it->bfbit = bf->bit; it->bfwidth = bf->width; }
        vpush(out, it);
    }
    return 1;
}

/* does the (unbraced) item initialise all of aggregate ty: a string for a
   char array, or an expression of the struct's type? */
static int whole(struct type *ty, struct init *it)
{
    if (ty->kind == K_ARRAY) return it->e->kind == N_STR && ty->base->kind == K_CHAR;
    return it->e->ty == ty;
}

/* the members an initialiser fills (unnamed bit-fields are skipped): how
   many, and the pos'th */
static int ninits(struct type *ty)
{
    int k, n = 0;
    for (k = 0; k < ty->nmembers; k++) if (ty->members[k].name || !ty->members[k].width) n++;
    return n;
}

static struct member *init_member(struct type *ty, int pos)
{
    int k;
    for (k = 0; k < ty->nmembers; k++)
        if (ty->members[k].name || !ty->members[k].width) { if (!pos--) return &ty->members[k]; }
    return 0;
}

/* the type at position pos; *suboff its offset; *bf its member if a bit-field */
static struct type *subobject(struct type *ty, int pos, int off, int *suboff, struct member **bf)
{
    struct member *m;
    *bf = 0;
    if (ty->kind == K_ARRAY) { *suboff = off + pos * ty->base->size; return ty->base; }
    m = init_member(ty, pos);
    *suboff = off + m->offset;
    if (m->width) *bf = m;
    return m->ty;
}

/* the position a designator names in aggregate ty */
/* .name for a member of an anonymous struct or union member (C11): that
   member's position first, then .name inside it */
static void anon_path(struct type *ty, struct vec *ds)
{
    struct desig *d = ds->a[0];
    int k, n;
    if (d->is_index || d->is_pos || (ty->kind != K_STRUCT && ty->kind != K_UNION)) return;
    n = ninits(ty);
    for (k = 0; k < n; k++) {
        struct member *m = init_member(ty, k);
        if (m->name && !strcmp(m->name, d->name)) return;
    }
    for (k = 0; k < n; k++) {
        struct member *m = init_member(ty, k), tmp;
        if (!m->name && (m->ty->kind == K_STRUCT || m->ty->kind == K_UNION) && find_member(m->ty, d->name, &tmp)) {
            struct desig *p = xmalloc(sizeof *p);
            memset(p, 0, sizeof *p);
            p->is_pos = 1;
            p->index = k;
            p->tok = d->tok;
            anon_path(m->ty, ds);                       /* (the path inside, then this member before it) */
            vpush(ds, 0);
            memmove(ds->a + 1, ds->a, (ds->n - 1) * sizeof ds->a[0]);
            ds->a[0] = p;
            return;
        }
    }
}

static int designate(struct type *ty, struct desig *d)
{
    int k;
    if (d->is_pos) return d->index;
    if (d->is_index) {
        if (ty->kind != K_ARRAY) error(d->tok, "[%d] = for a non-array", d->index);
        if (d->index < 0 || (ty->length >= 0 && d->index >= ty->length))
            error(d->tok, "index %d is outside the array", d->index);
        return d->index;
    }
    if (ty->kind != K_STRUCT && ty->kind != K_UNION) error(d->tok, ".%s = for a non-struct", d->name);
    for (k = 0; k < ninits(ty); k++) {
        struct member *m = init_member(ty, k);
        if (m->name && !strcmp(m->name, d->name)) return k;
    }
    error(d->tok, "no member %s", d->name);
    return 0;
}

static int is_aggregate(struct type *ty) { return ty->kind == K_ARRAY || ty->kind == K_STRUCT || ty->kind == K_UNION; }

/* Initialise aggregate ty from items from *idx on, C's "current object" walk
   (as p32cc.py's fill): each item takes the next member or element (a
   designator moves there first); an unbraced item for an aggregate member
   starts filling it from the same list (brace elision). braced: the whole
   list is ty's; else only as many items as ty takes, stopping at a
   designator (it belongs to the list above). The array length used. */
static int fill(struct type *ty, struct vec *items0, int *idx, int off, struct vec *out, struct tok *tok, int braced)
{
    int pos = 0, used = 0, done = 0, nmem, k;
    struct vec items;
    nmem = ty->kind == K_ARRAY ? ty->length : ty->kind == K_STRUCT ? ninits(ty) : (ninits(ty) ? 1 : 0);
    memset(&items, 0, sizeof items);    /* (a copy: a designated item is replaced by its value) */
    for (k = 0; k < items0->n; k++) vpush(&items, items0->a[k]);
    while (*idx < items.n) {
        struct init *it = items.a[*idx];
        struct type *st;
        struct member *bf;
        int so;
        if (it->desig.n) {
            if (!braced) break;
            anon_path(ty, &it->desig);
            pos = designate(ty, it->desig.a[0]);
            if (it->desig.n > 1) {      /* .a.b = / [i].x =: down to the last one */
                struct init *rest = xmalloc(sizeof *rest);
                struct vec one;
                int zero = 0;
                memset(rest, 0, sizeof *rest);
                for (k = 1; k < it->desig.n; k++) vpush(&rest->desig, it->desig.a[k]);
                rest->inner = it->inner;
                rest->tok = it->tok;
                memset(&one, 0, sizeof one);
                vpush(&one, rest);
                st = subobject(ty, pos, off, &so, &bf);
                fill(st, &one, &zero, so, out, tok, 1);
                pos++;
                if (pos > used) used = pos;
                (*idx)++;
                done++;
                continue;
            }
            items.a[*idx] = it = it->inner;
        } else if (nmem >= 0 && (pos >= nmem || (ty->kind == K_UNION && done))) {
            if (braced) {
                warning(tok, "excess initialiser");
                *idx = items.n;
            }
            break;
        }
        st = subobject(ty, pos, off, &so, &bf);
        if (is_aggregate(st) && !it->is_list && !whole(st, it))
            fill(st, &items, idx, so, out, tok, 0);
        else {
            flatten_init(st, it, so, out, tok, bf);
            (*idx)++;
        }
        pos++;
        if (pos > used) used = pos;
        done++;
    }
    if (braced) *idx = items.n;
    return used;
}

static int eval_addr(struct node *e, char **label, int *v);

static void global_bytes(struct obj *o, struct vec *items, struct tok *tok)
{
    int size = o->ty->size, k;
    struct vec rel;
    struct type *ty = o->ty;
    if (ty->kind == K_STRUCT && ty->nmembers && ty->members[ty->nmembers - 1].ty->kind == K_ARRAY &&
        ty->members[ty->nmembers - 1].ty->length < 0) {
        /* a flexible array member given elements (GCC's extension, static
           objects only): the object runs on past sizeof to hold them (D35) */
        for (k = 0; k < items->n; k++) {
            struct item *it = items->a[k];
            int end = it->off + (it->bytes ? it->nbytes : it->ty->size);
            end = (end + ty->align - 1) / ty->align * ty->align;
            if (end > size) size = end;
        }
        if (size > ty->size) o->init_size = size;
    }
    o->init = xmalloc(size + 1);
    memset(o->init, 0, size + 1);
    o->has_init = 1;
    memset(&rel, 0, sizeof rel);
    for (k = 0; k < items->n; k++) {
        struct item *it = items->a[k];
        char *label;
        int val;
        unsigned w;
        int b;
        if (it->bytes) { memcpy(o->init + it->off, it->bytes, it->nbytes); continue; }
        if (it->ty->kind == K_DOUBLE) { /* (8 bytes: a double constant, or an integer one) */
            double d;
            if (!dv(it->e, &d)) error(tok, "not a constant expression");
            memcpy(o->init + it->off, &d, 8);
            continue;
        }
        if (it->ty->kind == K_LLONG && it->e->kind == N_VAR && it->e->obj->is_dconst) {
            double d = it->e->obj->dval;
            unsigned long long w;
            if (!dfits(d, it->ty)) error(tok, "not a constant expression");
            w = it->ty->uns ? (unsigned long long)d : (unsigned long long)(long long)d;
            for (b = 0; b < 8; b++) o->init[it->off + b] = (unsigned char)(w >> (8 * b));
            continue;
        }
        if (eval_addr(it->e, &label, &val) < 0) error(tok, "not a constant expression");
        if (it->bfwidth) {              /* (into its bits of the unit) */
            unsigned mask = (it->bfwidth == 32 ? 0xFFFFFFFFu : (1u << it->bfwidth) - 1) << it->bfbit, cur = 0;
            if (label) error(tok, "an address in a bit-field");
            for (b = 0; b < it->ty->size; b++) cur |= (unsigned)o->init[it->off + b] << (8 * b);
            cur = (cur & ~mask) | (((unsigned)val << it->bfbit) & mask);
            for (b = 0; b < it->ty->size; b++) o->init[it->off + b] = cur >> (8 * b);
            continue;
        }
        if (label) {
            struct reloc_init *r = xmalloc(sizeof *r);
            if (it->ty->size != 4) error(tok, "an address needs a 4-byte object");
            r->off = it->off;
            r->label = label;
            r->add = val;
            vpush(&rel, r);
            continue;
        }
        if (it->ty->kind == K_LLONG) {  /* (8 bytes: a long long constant, or an int extended) */
            unsigned long long w64 = it->e->kind == N_VAR && it->e->obj->is_llconst ? it->e->obj->llval :
                unsigned_value(it->e->ty) ? (unsigned long long)(unsigned)val : (unsigned long long)(long long)val;
            for (b = 0; b < 8; b++) o->init[it->off + b] = (unsigned char)(w64 >> (8 * b));
            continue;
        }
        w = wrap((unsigned)val, it->ty);
        for (b = 0; b < it->ty->size; b++) o->init[it->off + b] = w >> (8 * b);
    }
    o->nirel = rel.n;
    o->irel = xmalloc(sizeof(struct reloc_init) * (rel.n ? rel.n : 1));
    for (k = 0; k < rel.n; k++) o->irel[k] = *(struct reloc_init *)rel.a[k];
}

static void global_init(struct obj *o, struct tok *name)
{
    struct init *in = initializer();
    struct vec out;
    int n;
    memset(&out, 0, sizeof out);
    n = flatten_init(o->ty, in, 0, &out, name, 0);
    if (o->ty->kind == K_ARRAY && o->ty->length < 0) o->ty = array_of(o->ty->base, n);
    global_bytes(o, &out, name);
    o->is_def = 1;
}

static void local_init(struct obj *var, struct tok *tok, struct vec *stmts)
{
    struct init *in = initializer();
    struct vec out;
    int n, k;
    memset(&out, 0, sizeof out);
    n = flatten_init(var->ty, in, 0, &out, tok, 0);
    if (var->ty->kind == K_ARRAY && var->ty->length < 0) var->ty = array_of(var->ty->base, n);
    if (var->ty->kind == K_ARRAY || var->ty->kind == K_STRUCT || var->ty->kind == K_UNION) {
        struct node *z = mk(N_MEMZERO, tok, 0);
        z->var = var;
        vpush(stmts, z);
    }
    for (k = 0; k < out.n; k++) {
        struct item *it = out.a[k];
        struct node *lv, *s;
        if (it->bytes) {
            int j;
            for (j = 0; j < it->nbytes; j++) {
                int b = it->bytes[j];
                if (!b) continue;
                lv = mk(N_VAROFF, tok, ty_char);
                lv->var = var;
                lv->off = it->off + j;
                s = mk(N_EXPRSTMT, tok, 0);
                s->expr = new_assign(lv, num((unsigned)(b > 127 ? b - 256 : b), ty_int, tok), tok);
                vpush(stmts, s);
            }
            continue;
        }
        lv = mk(N_VAROFF, tok, it->ty);
        lv->var = var;
        lv->off = it->off;
        lv->bfbit = it->bfbit;
        lv->bfwidth = it->bfwidth;
        s = mk(N_EXPRSTMT, tok, 0);
        s->expr = new_assign(lv, it->e, tok);
        vpush(stmts, s);
    }
}

static void external(void)
{
    char *storage;
    struct type *base = declspec(1, &storage);
    int first = 1;
    if (consume(";")) return;
    for (;;) {
        struct tok *name;
        struct type *ty = declarator(base, 0, &name);
        if (storage && !strcmp(storage, "typedef")) {
            struct entry *e = declare_in(scope, name->text, E_TYPEDEF);
            e->ty = ty;
        } else if (ty->kind == K_FUNC) {
            struct obj *fn = declare_global(name, ty, storage);
            if (first && at("{")) { function(fn, name); return; }
        } else {
            struct obj *o = declare_global(name, ty, storage);
            if (consume("=")) global_init(o, name);
            else if (!storage || strcmp(storage, "extern")) {
                if (!o->is_def && !o->has_init) o->tentative = 1;
            }
        }
        first = 0;
        if (consume(";")) return;
        skip(",");
    }
}

/* ================================================================ statements */
static void local_decl(struct vec *out)
{
    char *storage;
    struct type *base = declspec(1, &storage);
    int vol = saw_volatile;
    if (consume(";")) return;
    for (;;) {
        struct tok *name;
        struct type *ty;
        int this_vol;
        saw_volatile = 0;
        ty = declarator(base, 0, &name);
        this_vol = vol || saw_volatile;     /* (volatile in the specifiers, or T *volatile name) */
        if (storage && !strcmp(storage, "typedef")) {
            struct entry *e = declare_in(scope, name->text, E_TYPEDEF);
            e->ty = ty;
        } else if (ty->kind == K_FUNC || (storage && !strcmp(storage, "extern"))) {
            struct entry *g = find_in(gscope, name->text), *e;
            struct obj *o = g && g->kind == E_VAR ? g->obj : declare_global(name, ty, "extern");
            e = declare_in(scope, name->text, E_VAR);
            e->obj = o;
        } else if (storage && !strcmp(storage, "static")) {
            struct obj *o = new_obj(name->text, ty, 0);
            struct entry *e;
            o->is_static = 1;
            o->label = newlabel(fmt("s_%s_", name->text));
            vpush(&prog->globals, o);
            e = declare_in(scope, name->text, E_VAR);
            e->obj = o;
            if (consume("=")) global_init(o, name);
            else o->is_def = 1;
        } else {
            struct obj *v = new_obj(name->text, ty, 1);
            struct entry *e;
            v->addr_taken = this_vol;   /* (volatile: kept in memory, so longjmp cannot undo it) */
            vpush(&curfn->locals, v);
            e = declare_in(scope, name->text, E_VAR);
            e->obj = v;
            if (consume("=")) local_init(v, name, out);
            if ((v->ty->kind != K_ARRAY && v->ty->size == 0) || (v->ty->kind == K_ARRAY && v->ty->length < 0))
                error(name, "%s has an incomplete type", name->text);
        }
        if (consume(";")) return;
        skip(",");
    }
}

static struct node *compound(void)
{
    struct node *b;
    skip("{");
    push_scope();
    b = mk(N_BLOCK, 0, 0);
    while (!consume("}")) {
        struct entry *e;
        int from = I;
        if (is_typename(TOK) && !(is(peek(1), ":") && TOK->kind == T_ID && (e = lookup(TOK->text)) != 0 && e->kind != E_TYPEDEF))
            local_decl(&b->list);
        else
            vpush(&b->list, stmt());
        if (function_done) free_tokens(from, I);   /* (the nodes keep line tokens: these are done with) */
    }
    pop_scope();
    return b;
}

static struct node *cond_expr(void)
{
    struct node *e = rvalue(expr());
    if (!is_scalar(e->ty)) error(e->tok, "a condition must be a number or a pointer");
    return truth(e);
}

static struct node *stmt(void)
{
    struct tok *t = TOK;
    struct node *n;
    if (t->kind == T_ID && is(peek(1), ":") && !is(t, "default") && !is_typename(TOK)) {
        int k;
        I += 2;
        for (k = 0; k < fn_labels.n; k++) if (fn_labels.a[k] == t->text) error(t, "label %s defined twice", t->text);
        vpush(&fn_labels, t->text);
        n = mk(N_LABEL, t, 0);
        n->label = t->text;
        n->body = stmt();
        return n;
    }
    if (at("{")) return compound();
    if (consume(";")) return mk(N_BLOCK, t, 0);
    if (t->kind == T_ID) {
        if (is(t, "if")) {
            I++;
            skip("(");
            n = mk(N_IF, t, 0);
            n->cond = cond_expr();
            skip(")");
            n->then = stmt();
            n->els = consume("else") ? stmt() : 0;
            return n;
        }
        if (is(t, "while")) {
            I++;
            skip("(");
            n = mk(N_FOR, t, 0);
            hdr_depth++;
            n->cond = cond_expr();
            hdr_depth--;
            skip(")");
            brk_depth++; cont_depth++;
            n->body = stmt();
            brk_depth--; cont_depth--;
            return n;
        }
        if (is(t, "do")) {
            I++;
            n = mk(N_DO, t, 0);
            brk_depth++; cont_depth++;
            n->body = stmt();
            brk_depth--; cont_depth--;
            if (!consume("while")) error(TOK, "expected while");
            skip("(");
            hdr_depth++;
            n->cond = cond_expr();
            hdr_depth--;
            skip(")");
            skip(";");
            return n;
        }
        if (is(t, "for")) {
            I++;
            skip("(");
            push_scope();
            n = mk(N_FOR, t, 0);
            if (is_typename(TOK)) {
                n->init = mk(N_BLOCK, t, 0);
                local_decl(&n->init->list);
            } else if (!consume(";")) {
                n->init = mk(N_EXPRSTMT, t, 0);
                n->init->expr = expr();
                skip(";");
            }
            hdr_depth++;
            n->cond = at(";") ? 0 : cond_expr();
            skip(";");
            n->inc = at(")") ? 0 : expr();
            hdr_depth--;
            skip(")");
            brk_depth++; cont_depth++;
            n->body = stmt();
            brk_depth--; cont_depth--;
            pop_scope();
            return n;
        }
        if (is(t, "switch")) {
            I++;
            skip("(");
            n = mk(N_SWITCH, t, 0);
            n->expr = rvalue(expr());
            if (!is_integer(n->expr->ty) || n->expr->ty->kind == K_LLONG) error(t, "switch needs an integer (not a long long)");
            skip(")");
            vpush(&switches, n);
            brk_depth++;
            n->body = stmt();
            brk_depth--;
            switches.n--;
            return n;
        }
        if (is(t, "case")) {
            struct node *sw;
            struct casepair *c;
            unsigned v;
            int k;
            I++;
            if (!switches.n) error(t, "case outside switch");
            v = (unsigned)const_expr();
            skip(":");
            sw = switches.a[switches.n - 1];
            v = wrap(v, promote(sw->expr->ty));
            for (k = 0; k < sw->cases.n; k++)
                if (((struct casepair *)sw->cases.a[k])->val == v) error(t, "duplicate case %d", (int)v);
            c = xmalloc(sizeof *c);
            c->val = v;
            c->label = newlabel("case");
            vpush(&sw->cases, c);
            n = mk(N_CASELABEL, t, 0);
            n->label = c->label;
            n->body = stmt();
            return n;
        }
        if (is(t, "default")) {
            struct node *sw;
            I++;
            skip(":");
            if (!switches.n) error(t, "default outside switch");
            sw = switches.a[switches.n - 1];
            if (sw->dflt) error(t, "two defaults");
            sw->dflt = newlabel("dflt");
            n = mk(N_CASELABEL, t, 0);
            n->label = sw->dflt;
            n->body = stmt();
            return n;
        }
        if (is(t, "break")) {
            I++;
            skip(";");
            if (!brk_depth) error(t, "break outside a loop or switch");
            return mk(N_BREAK, t, 0);
        }
        if (is(t, "continue")) {
            I++;
            skip(";");
            if (!cont_depth) error(t, "continue outside a loop");
            return mk(N_CONTINUE, t, 0);
        }
        if (is(t, "return")) {
            struct type *rt = curfn->ty->base;
            struct node *e;
            I++;
            n = mk(N_RETURN, t, 0);
            if (consume(";")) return n;
            e = expr();
            skip(";");
            if (rt->kind == K_VOID) error(t, "return with a value in a void function");
            if (is_memval(rt)) {        /* *ret = e, and ret in r1 */
                struct obj *rp = curfn->retptr;
                struct node *d = mk(N_DEREF, t, rt), *a;
                d->expr = var_node(rp, t);
                a = new_assign(d, e, t);
                n->expr = comma(a, var_node(rp, t), t);
                return n;
            }
            n->expr = convert(e, rt, t);
            return n;
        }
        if (is(t, "goto")) {
            struct tok *name;
            I++;
            name = ident();
            skip(";");
            n = mk(N_GOTO, name, 0);
            n->label = name->text;
            vpush(&fn_gotos, n);
            return n;
        }
    }
    n = mk(N_EXPRSTMT, t, 0);
    n->expr = expr();
    skip(";");
    return n;
}

/* ================================================================ expressions */
static struct node *rvalue(struct node *e)
{
    if (e->ty->kind == K_ARRAY || e->ty->kind == K_FUNC) {
        struct node *a = mk(N_ADDR, e->tok, decay(e->ty));
        a->expr = e;
        return a;
    }
    return e;
}

static int is_null(struct node *e)
{
    return (e->kind == N_NUM && e->val == 0 && is_integer(e->ty)) ||
           (e->kind == N_CAST && e->ty->kind == K_PTR && e->ty->base->kind == K_VOID && is_null(e->expr));
}

static int lvalue_ok(struct node *e)
{
    return e->kind == N_VAR || e->kind == N_DEREF || e->kind == N_VAROFF || (e->kind == N_MEMBER && lvalue_ok(e->expr));
}

static void need_lvalue(struct node *e, struct tok *t, const char *what)
{
    if ((e->kind != N_VAR && e->kind != N_DEREF && e->kind != N_MEMBER && e->kind != N_VAROFF) ||
        (e->kind == N_VAR && e->obj->ty->kind == K_FUNC))
        error(t, "%s needs an lvalue", what);
    if (e->kind == N_MEMBER && !lvalue_ok(e->expr)) error(t, "%s needs an lvalue", what);
}

/* ---------------------------------------------------------------- long long (D30)
   (as p32cc.py) A long long lives in memory (by its address, as a struct):
   constants are objects in the data, operations calls of the library's
   helpers (libc's llong.c) taking and returning long longs by value.
   Constants fold here. */
static struct vec llconsts, helpers;    /* struct obj *: the constants, the helpers called */

/* the bits of a value as type ty holds them */
static unsigned long long llwrap(unsigned long long v, struct type *ty)
{
    if (ty->kind == K_LLONG) return v;
    return (unsigned long long)wrap((unsigned)v, ty) | (unsigned_value(ty) || !(wrap((unsigned)v, ty) >> 31) ? 0 : 0xFFFFFFFF00000000ull);
}

/* a long long constant: an object in the data (one per value and type) */
static struct node *llconst(unsigned long long v, struct type *ty, struct tok *t)
{
    struct obj *o;
    int k;
    for (k = 0; k < llconsts.n; k++) {
        o = llconsts.a[k];
        if (o->llval == v && o->ty->uns == ty->uns) return var_node(o, t);
    }
    o = new_obj("", ty, 0);
    o->is_static = 1;
    o->label = newlabel("ll");
    o->is_def = 1;
    o->has_init = 1;
    o->init = xmalloc(9);
    for (k = 0; k < 8; k++) o->init[k] = (unsigned char)(v >> (8 * k));
    o->irel = xmalloc(sizeof(struct reloc_init));
    o->nirel = 0;
    o->is_llconst = 1;
    o->llval = v;
    vpush(&prog->globals, o);
    vpush(&llconsts, o);
    return var_node(o, t);
}

/* a constant's value (a long long or an int constant): 1, its bits in *v */
static int llv(struct node *e, unsigned long long *v)
{
    if (e->kind == N_VAR && e->obj->is_llconst) { *v = e->obj->llval; return 1; }
    if (e->kind == N_NUM) {
        *v = unsigned_value(e->ty) ? (unsigned long long)e->val : (unsigned long long)(long long)(int)e->val;
        return 1;
    }
    return 0;
}

/* a call of a libc helper: name(args) returning ret */
static struct node *helper(char *name, struct type *ret, struct type **params, int nparams, struct node **args, struct tok *t)
{
    struct obj *o = 0;
    struct vec av;
    int k;
    for (k = 0; k < helpers.n; k++)
        if (!strcmp(((struct obj *)helpers.a[k])->name, name)) o = helpers.a[k];
    if (!o) {
        struct type **ps = xmalloc(sizeof *ps * (nparams ? nparams : 1));
        for (k = 0; k < nparams; k++) ps[k] = params[k];
        o = new_obj(intern(name), func_type(ret, ps, nparams, 0), 0);
        o->ext = 1;
        vpush(&helpers, o);
    }
    memset(&av, 0, sizeof av);
    for (k = 0; k < nparams; k++) vpush(&av, args[k]);
    return build_call(var_node(o, t), &av, t);
}

/* the same long long bits seen as ty (signed / unsigned) */
static struct node *retype(struct node *e, struct type *ty, struct tok *t)
{
    unsigned long long v;
    struct node *a, *d;
    if (e->ty == ty) return e;
    if (llv(e, &v)) return llconst(llwrap(v, e->ty), ty, t);
    a = mk(N_MEMADDR, t, ptr_to(ty));
    a->expr = e;
    d = mk(N_DEREF, t, ty);
    d->expr = a;
    return d;
}

/* a long long operation of type ty (folded when the operands are constants) */
static struct node *llop(const char *op, struct node **args, int nargs, struct type *ty, struct tok *t)
{
    unsigned long long a = 0, b = 0, r = 0;
    struct type *params[2];
    struct node *conv[2];
    char name[16];
    int k, ok = llv(args[0], &a) && (nargs < 2 || llv(args[1], &b)), fold = 0;
    if (ok) {
        long long sa = (long long)a, sb = (long long)b;
        fold = 1;
        if (!strcmp(op, "add")) r = a + b;
        else if (!strcmp(op, "sub")) r = a - b;
        else if (!strcmp(op, "mul")) r = a * b;
        else if ((!strcmp(op, "div") || !strcmp(op, "mod")) && b) {
            if (ty->uns) r = op[0] == 'd' ? a / b : a % b;
            else if (sb == -1) r = op[0] == 'd' ? 0 - a : 0;    /* (no overflow: -2^63 / -1 wraps) */
            else r = op[0] == 'd' ? (unsigned long long)(sa / sb) : (unsigned long long)(sa % sb);
        } else if (!strcmp(op, "and")) r = a & b;
        else if (!strcmp(op, "or")) r = a | b;
        else if (!strcmp(op, "xor")) r = a ^ b;
        else if (!strcmp(op, "neg")) r = 0 - a;
        else if (!strcmp(op, "not")) r = ~a;
        else if (!strcmp(op, "shl")) r = a << (b & 63);
        else if (!strcmp(op, "shr")) r = ty->uns ? a >> (b & 63) : (unsigned long long)(sa >> (b & 63));
        else fold = 0;
        if (fold) return llconst(r, ty, t);
    }
    sprintf(name, "%s%s", ty->uns && (!strcmp(op, "div") || !strcmp(op, "mod") || !strcmp(op, "shr")) ? "__ull" : "__ll", op);
    for (k = 0; k < nargs; k++) params[k] = ty_llong;
    if (!strcmp(op, "shl") || !strcmp(op, "shr")) params[1] = ty_int;
    for (k = 0; k < nargs; k++)
        conv[k] = params[k] == ty_llong ? retype(convert(args[k], ty, t), ty_llong, t) : convert(args[k], ty_int, t);
    return retype(helper(name, ty_llong, params, nargs, conv, t), ty, t);
}

/* l op r where an operand is a long long */
static struct node *llbinary(const char *op, struct node *l, struct node *r, struct tok *t)
{
    static const char *const names[][2] = { { "+", "add" }, { "-", "sub" }, { "*", "mul" }, { "/", "div" },
                                            { "%", "mod" }, { "&", "and" }, { "|", "or" }, { "^", "xor" } };
    struct node *args[2], *c, *n;
    struct type *ty;
    unsigned long long a, b;
    int k;
    if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
        struct type *lt = promote(l->ty);
        if (lt->kind != K_LLONG) return new_binary(op, l, llcast(r, ty_int, t), t);    /* (the count's low word) */
        args[0] = l;
        args[1] = r;
        return llop(op[0] == '<' ? "shl" : "shr", args, 2, lt, t);
    }
    ty = usual(l->ty, r->ty);
    if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
        struct type *ps[2];
        if (llv(l, &a) && llv(r, &b)) {
            int v;
            a = llwrap(a, l->ty);
            b = llwrap(b, r->ty);
            if (ty->uns)
                v = !strcmp(op, "==") ? a == b : !strcmp(op, "!=") ? a != b : !strcmp(op, "<") ? a < b :
                    !strcmp(op, ">") ? a > b : !strcmp(op, "<=") ? a <= b : a >= b;
            else {
                long long x = (long long)a, y = (long long)b;
                v = !strcmp(op, "==") ? x == y : !strcmp(op, "!=") ? x != y : !strcmp(op, "<") ? x < y :
                    !strcmp(op, ">") ? x > y : !strcmp(op, "<=") ? x <= y : x >= y;
            }
            return num((unsigned)v, ty_int, t);
        }
        ps[0] = ps[1] = ty_llong;
        args[0] = retype(convert(l, ty, t), ty_llong, t);
        args[1] = retype(convert(r, ty, t), ty_llong, t);
        c = helper(ty->uns ? "__ullcmp" : "__llcmp", ty_int, ps, 2, args, t);
        n = mk(N_CMP, t, ty_int);
        n->op = (char *)op;
        n->lhs = c;
        n->rhs = num(0, ty_int, t);
        n->uns = 0;
        return n;
    }
    for (k = 0; k < 8; k++)
        if (!strcmp(op, names[k][0])) {
            args[0] = l;
            args[1] = r;
            return llop(names[k][1], args, 2, ty, t);
        }
    error(t, "%s on a long long", op);
    return 0;
}

/* conversions to and from long long */
static struct node *llcast(struct node *e, struct type *ty, struct tok *t)
{
    unsigned long long v;
    int isc;
    struct node *low, *a;
    e = rvalue(e);
    isc = llv(e, &v);
    if (ty->kind == K_VOID) return cast_node(e, ty_void, t);
    if (ty->kind == K_LLONG) {
        struct type *src, *ps[1];
        struct node *args[1];
        if (e->ty->kind == K_LLONG) return retype(e, ty, t);
        if (!is_scalar(e->ty)) error(t, "cast of a non-scalar value");
        if (isc) return llconst(v, ty, t);
        src = e->ty->uns || e->ty->kind == K_PTR ? ty_uint : ty_int;
        ps[0] = src;
        args[0] = convert(e, src, t);
        return retype(helper(src->uns ? "__llfromuint" : "__llfromint", ty_llong, ps, 1, args, t), ty, t);
    }
    if (!is_scalar(ty)) error(t, "cast to a non-scalar type");
    if (isc && is_integer(ty)) return num((unsigned)v, ty, t);
    a = mk(N_MEMADDR, t, ptr_to(ty_uint));      /* (the low word) */
    a->expr = e;
    low = mk(N_DEREF, t, ty_uint);
    low->expr = a;
    return new_cast(low, ty, t);
}

/* digits . digits, . digits, or digits e digits, an exponent, a suffix f F l L
   (as p32cc.py's FLOAT_RE); or C99's hexadecimal 0x1.8p3, whose p exponent
   is required (HEXFLOAT_RE). strtod reads both. */
static int is_float_lit(const char *s)
{
    int d = 0, point = 0, e = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (s += 2; isxdigit((unsigned char)*s); s++) d++;
        if (*s == '.') for (s++; isxdigit((unsigned char)*s); s++) d++;
        if (!d || (*s != 'p' && *s != 'P')) return 0;
        s++;
        if (*s == '+' || *s == '-') s++;
        if (!isdigit((unsigned char)*s)) return 0;
        for (; isdigit((unsigned char)*s); s++) ;
        if (*s && strchr("fFlL", *s)) s++;
        return !*s;
    }
    for (; isdigit((unsigned char)*s); s++) d++;
    if (*s == '.') { point = 1; s++; for (; isdigit((unsigned char)*s); s++) d++; }
    if (!d) return 0;
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') s++;
        if (!isdigit((unsigned char)*s)) return 0;
        for (; isdigit((unsigned char)*s); s++) ;
        e = 1;
    }
    if (*s && strchr("fFlL", *s)) s++;
    return !*s && (point || e);
}

/* ---------------------------------------------------------------- double (D30)
   (as p32cc.py) A double (float is the same type) lives in memory, as a long
   long: constants are objects in the data, operations calls of libc's
   helpers (dbl.c, on softfp). Constants fold, except a division by zero or a
   NaN result (NaNs' bits differ between machines). */
static struct vec dconsts;              /* struct obj *: the double constants */

static struct node *dconst(double v, struct tok *t)
{
    struct obj *o;
    unsigned char b[8];
    int k;
    memcpy(b, &v, 8);
    for (k = 0; k < dconsts.n; k++) {
        o = dconsts.a[k];
        if (!memcmp(o->init, b, 8)) return var_node(o, t);
    }
    o = new_obj("", ty_double, 0);
    o->is_static = 1;
    o->label = newlabel("fl");
    o->is_def = 1;
    o->has_init = 1;
    o->init = xmalloc(9);
    memcpy(o->init, b, 8);
    o->irel = xmalloc(sizeof(struct reloc_init));
    o->nirel = 0;
    o->is_dconst = 1;
    o->dval = v;
    vpush(&prog->globals, o);
    vpush(&dconsts, o);
    return var_node(o, t);
}

/* a constant's value as a double: 1, the value in *v */
static int dv(struct node *e, double *v)
{
    unsigned long long w;
    if (e->kind == N_VAR && e->obj->is_dconst) { *v = e->obj->dval; return 1; }
    if (!llv(e, &w)) return 0;
    *v = unsigned_value(e->ty) ? (double)w : (double)(long long)w;
    return 1;
}

static struct node *dop(const char *op, struct node **args, int nargs, struct tok *t)
{
    struct type *ps[2];
    struct node *conv[2];
    double a, b = 0, r = 0;
    char name[16];
    int k, fold = 0;
    if (dv(args[0], &a) && (nargs < 2 || dv(args[1], &b))) {
        fold = 1;
        if (!strcmp(op, "add")) r = a + b;
        else if (!strcmp(op, "sub")) r = a - b;
        else if (!strcmp(op, "mul")) r = a * b;
        else if (!strcmp(op, "div") && b != 0) r = a / b;
        else if (!strcmp(op, "neg")) r = -a;
        else fold = 0;
        if (fold && r == r) return dconst(r, t);
    }
    sprintf(name, "__d%s", op);
    for (k = 0; k < nargs; k++) {
        ps[k] = ty_double;
        conv[k] = convert(args[k], ty_double, t);
    }
    return helper(name, ty_double, ps, nargs, conv, t);
}

/* l op r where an operand is a double */
static struct node *dbinary(const char *op, struct node *l, struct node *r, struct tok *t)
{
    static const char *const names[][2] = { { "+", "add" }, { "-", "sub" }, { "*", "mul" }, { "/", "div" } };
    struct node *args[2], *c, *n;
    struct type *ps[2];
    double a, b;
    int k;
    if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
        if (dv(l, &a) && dv(r, &b)) {
            int v = !strcmp(op, "==") ? a == b : !strcmp(op, "!=") ? a != b : !strcmp(op, "<") ? a < b :
                    !strcmp(op, ">") ? a > b : !strcmp(op, "<=") ? a <= b : a >= b;
            return num((unsigned)v, ty_int, t);
        }
        ps[0] = ps[1] = ty_double;
        args[0] = convert(l, ty_double, t);
        args[1] = convert(r, ty_double, t);
        c = helper("__dcmp", ty_int, ps, 2, args, t);
        /* (__dcmp: -1, 0, 1, or 2 unordered: every comparison with a NaN is false but !=) */
        n = mk(N_CMP, t, ty_int);
        n->uns = 0;
        if (!strcmp(op, "==") || !strcmp(op, "!=")) {
            n->op = (char *)op;
            n->lhs = c;
            n->rhs = num(0, ty_int, t);
            return n;
        }
        if (!strcmp(op, "<") || !strcmp(op, ">")) {
            n->op = "==";
            n->lhs = c;
            n->rhs = num(op[0] == '<' ? (unsigned)-1 : 1, ty_int, t);
            return n;
        }
        if (!strcmp(op, "<=")) {        /* c is -1 or 0: (unsigned)(c + 1) <= 1 */
            struct node *s = mk(N_BINOP, t, ty_int);
            s->op = "+";
            s->lhs = c;
            s->rhs = num(1, ty_int, t);
            c = s;
        }
        n->op = "<=";
        n->lhs = c;
        n->rhs = num(1, ty_int, t);
        n->uns = 1;
        return n;
    }
    for (k = 0; k < 4; k++)
        if (!strcmp(op, names[k][0])) {
            args[0] = l;
            args[1] = r;
            return dop(names[k][1], args, 2, t);
        }
    error(t, "%s on a double", op);
    return 0;
}

/* does a double, truncated, fit integer type ty? */
static int dfits(double v, struct type *ty)
{
    int n = ty->size * 8;
    double top = n == 64 ? 18446744073709551616.0 : (double)(1ull << n);
    if (v != v) return 0;
    if (unsigned_value(ty)) return v > -1.0 && v < top;
    if (n == 64) return v >= -9223372036854775808.0 && v < 9223372036854775808.0;
    return v > -top / 2 - 1.0 && v < top / 2;
}

/* conversions to and from double */
static struct node *dcast(struct node *e, struct type *ty, struct tok *t)
{
    struct type *ps[1];
    struct node *args[1], *f;
    double v;
    e = rvalue(e);
    if (ty->kind == K_VOID) return cast_node(e, ty_void, t);
    if (ty->kind == K_DOUBLE) {
        struct type *src;
        if (e->ty->kind == K_DOUBLE) return e;
        if (!is_integer(e->ty)) error(t, "%s to double", type_str(e->ty));
        if (dv(e, &v)) return dconst(v, t);
        if (e->ty->kind == K_LLONG) {
            ps[0] = e->ty;
            args[0] = e;
            return helper(e->ty->uns ? "__dfromull" : "__dfromll", ty_double, ps, 1, args, t);
        }
        src = e->ty->uns ? ty_uint : ty_int;
        ps[0] = src;
        args[0] = convert(e, src, t);
        return helper(src->uns ? "__dfromuint" : "__dfromint", ty_double, ps, 1, args, t);
    }
    if (!is_integer(ty)) error(t, "double to %s", type_str(ty));
    if (dv(e, &v) && dfits(v, ty)) {    /* (toward zero; folded only if it fits) */
        if (ty->kind == K_LLONG)
            return llconst(ty->uns ? (unsigned long long)v : (unsigned long long)(long long)v, ty, t);
        return num(unsigned_value(ty) ? (unsigned)(unsigned long long)v : (unsigned)(long long)v, ty, t);
    }
    ps[0] = ty_double;
    args[0] = e;
    if (ty->kind == K_LLONG) return helper(ty->uns ? "__dtoull" : "__dtoll", ty, ps, 1, args, t);
    f = helper(ty->uns ? "__dtouint" : "__dtoint", ty->uns ? ty_uint : ty_int, ps, 1, args, t);
    return new_cast(f, ty, t);
}

/* e used as a condition: a long long through __lltest (1 if not 0), a double
   through __dtest (1 if not 0: a NaN too) */
static struct node *truth(struct node *e)
{
    unsigned long long v;
    struct type *ps[1];
    struct node *args[1];
    if (e->ty->kind == K_DOUBLE) {
        double d;
        if (dv(e, &d)) return num(d != 0, ty_int, e->tok);
        ps[0] = ty_double;
        args[0] = e;
        return helper("__dtest", ty_int, ps, 1, args, e->tok);
    }
    if (e->ty->kind != K_LLONG) return e;
    if (llv(e, &v)) return num(v != 0, ty_int, e->tok);
    ps[0] = ty_llong;
    args[0] = retype(e, ty_llong, e->tok);
    return helper("__lltest", ty_int, ps, 1, args, e->tok);
}

static struct node *cast_node(struct node *e, struct type *ty, struct tok *t)
{
    struct node *c = mk(N_CAST, t, ty);
    c->expr = e;
    return c;
}

/* e converted to _Bool: 1 if it is not 0 (or not a null pointer), else 0
   -- not its low byte (D35) */
static struct node *to_bool(struct node *e, struct tok *t)
{
    if (e->ty == ty_bool) return e;
    if (e->ty->kind == K_DOUBLE || e->ty->kind == K_LLONG) return cast_node(truth(e), ty_bool, t);
    if (e->kind == N_NUM) return num(e->val, ty_bool, t);
    return cast_node(new_binary("!=", e, num(0, ty_int, t), t), ty_bool, t);
}

static struct node *new_cast(struct node *e, struct type *ty, struct tok *t)
{
    if (ty == ty_bool && is_scalar(e->ty)) return to_bool(e, t);
    if (ty->kind == K_DOUBLE || e->ty->kind == K_DOUBLE) return dcast(e, ty, t);
    if (ty->kind == K_LLONG || e->ty->kind == K_LLONG) return llcast(e, ty, t);
    if (is_integer(ty) && e->ty->kind == K_PTR) {   /* a constant address with no symbol (offsetof's */
        char *lab;                                  /* &((T *)0)->m): a number, so it folds further */
        int v;
        if (eval_addr(e, &lab, &v) == 0 && !lab) return num((unsigned)v, ty, t);
    }
    if (e->kind == N_NUM && is_scalar(ty)) return num(e->val, ty, t);
    if (ty->kind == K_VOID) return cast_node(e, ty_void, t);
    return cast_node(e, ty, t);
}

static struct node *convert(struct node *e, struct type *ty, struct tok *t)
{
    e = rvalue(e);
    if (ty->kind == K_STRUCT || ty->kind == K_UNION) {
        if (e->ty != ty) error(t, "incompatible struct");
        return e;
    }
    if (ty->kind == K_VOID) return e;
    if (ty == ty_bool && is_scalar(e->ty)) return to_bool(e, t);
    if ((ty->kind == K_DOUBLE || e->ty->kind == K_DOUBLE) && is_arith(ty) && is_arith(e->ty)) return dcast(e, ty, t);
    if (is_integer(ty) && is_integer(e->ty)) {
        if (e->ty->size == ty->size && e->ty->uns == ty->uns) {
            if (e->ty->kind == ty->kind) return e;
            return e->kind != N_NUM ? cast_node(e, ty, t) : num(e->val, ty, t);
        }
        return new_cast(e, ty, t);
    }
    if (ty->kind == K_PTR) {
        if (e->ty->kind == K_PTR) {
            if (!(ty->base->kind == K_VOID || e->ty->base->kind == K_VOID || compatible(ty->base, e->ty->base)))
                warning(t, "incompatible pointer types (%s, %s)", type_str(e->ty), type_str(ty));
            return e->ty == ty ? e : cast_node(e, ty, t);
        }
        if (is_integer(e->ty)) {
            if (!is_null(e)) warning(t, "integer converted to pointer");
            return new_cast(e, ty, t);
        }
    }
    if (is_integer(ty) && e->ty->kind == K_PTR) {
        warning(t, "pointer converted to integer");
        return cast_node(e, ty, t);
    }
    error(t, "cannot convert %s to %s", type_str(e->ty), type_str(ty));
    return 0;
}

static struct node *new_assign(struct node *lhs, struct node *rhs, struct tok *t)
{
    struct node *n;
    need_lvalue(lhs, t, "assignment");
    if (lhs->ty->kind == K_ARRAY) error(t, "assignment to an array");
    n = mk(N_ASSIGN, t, lhs->ty);
    n->lhs = lhs;
    n->rhs = convert(rhs, lhs->ty, t);
    return n;
}

static struct node *scale(struct node *e, int size, struct tok *t)
{
    struct node *b;
    e = convert(e, promote(e->ty), t);
    if (size == 1) return e;
    if (e->kind == N_NUM) return num(e->val * (unsigned)size, e->ty, t);
    b = mk(N_BINOP, t, e->ty);
    b->op = "*";
    b->lhs = e;
    b->rhs = num((unsigned)size, ty_int, t);
    return b;
}

/* can e be evaluated twice (no side effects, cheap)? */
static int simple(struct node *e)
{
    if (e->kind == N_VAR || e->kind == N_VAROFF) return 1;
    if (e->kind == N_MEMBER) return simple(e->expr);
    return e->kind == N_DEREF && e->expr->kind == N_VAR;
}

static struct node *comma(struct node *a, struct node *b, struct tok *t)
{
    struct node *n = mk(N_COMMA, t, b->ty);
    n->lhs = a;
    n->rhs = b;
    return n;
}

static struct node *var_node(struct obj *o, struct tok *t)
{
    struct node *v = mk(N_VAR, t, o->ty);
    v->obj = o;
    return v;
}

/* lhs op= rhs (post: the old value is the result, as x++) on a bit-field, as
   plain assignments (p32cc.py's bf_update): through a pointer temporary
   unless the struct's place is simple, so it is evaluated once */
static struct node *bf_update(const char *op, struct node *lhs, struct node *rhs, struct tok *t, int post)
{
    struct node *pre = 0, *place = lhs, *e;
    if (!lhs->bfwidth) {                /* (a long long: the place itself, or through a pointer temporary) */
        if (!simple(lhs)) {
            struct obj *p = new_obj("", ptr_to(lhs->ty), 1);
            struct node *pv, *a;
            vpush(&curfn->locals, p);
            pv = var_node(p, t);
            a = mk(N_ADDR, t, p->ty);
            a->expr = lhs;
            pre = mk(N_ASSIGN, t, p->ty);
            pre->lhs = pv;
            pre->rhs = a;
            place = mk(N_DEREF, t, lhs->ty);
            place->expr = pv;
        }
    } else if (!simple(lhs->expr)) {
        struct node *base = lhs->expr, *pv, *a, *d;
        struct obj *p = new_obj("", ptr_to(base->ty), 1);
        vpush(&curfn->locals, p);
        pv = var_node(p, t);
        a = mk(N_ADDR, t, p->ty);
        a->expr = base;
        pre = mk(N_ASSIGN, t, p->ty);
        pre->lhs = pv;
        pre->rhs = a;
        d = mk(N_DEREF, t, base->ty);
        d->expr = pv;
        place = mk(N_MEMBER, t, lhs->ty);
        place->expr = d;
        place->mem = lhs->mem;
        place->bfbit = lhs->bfbit;
        place->bfwidth = lhs->bfwidth;
    }
    if (post) {
        struct obj *old = new_obj("", promote(lhs->ty), 1);
        struct node *ov, *a1, *a2, *res;
        vpush(&curfn->locals, old);
        ov = var_node(old, t);
        a1 = mk(N_ASSIGN, t, ov->ty);                               /* old = place, */
        a1->lhs = ov;
        a1->rhs = convert(place, ov->ty, t);
        a2 = new_assign(place, new_binary(op, ov, rhs, t), t);      /* place = old op rhs, */
        res = convert(ov, lhs->ty, t);                              /* old */
        e = comma(a1, comma(a2, res, t), t);
    } else
        e = new_assign(place, new_binary(op, place, rhs, t), t);
    return pre ? comma(pre, e, t) : e;
}

static struct node *new_opassign(const char *op, struct node *lhs, struct node *rhs, struct tok *t)
{
    struct node *n;
    struct type *lt = lhs->ty, *opty;
    int shift = !strcmp(op, "<<") || !strcmp(op, ">>");
    need_lvalue(lhs, t, fmt("%s=", op));
    rhs = rvalue(rhs);
    if (lhs->bfwidth || lhs->ty->kind == K_LLONG || lhs->ty->kind == K_DOUBLE ||
        ((rhs->ty->kind == K_LLONG || rhs->ty->kind == K_DOUBLE) && is_arith(lhs->ty)))
        return bf_update(op, lhs, rhs, t, 0);
    if (lt->kind == K_PTR && (!strcmp(op, "+") || !strcmp(op, "-"))) {
        if (!is_integer(rhs->ty)) error(t, "pointer %s= needs an integer", op);
        if (lt->base->size == 0) error(t, "arithmetic on a pointer to an incomplete type");
        n = mk(N_OPASSIGN, t, lt);
        n->op = (char *)op;
        n->lhs = lhs;
        n->rhs = scale(rhs, lt->base->size, t);
        n->opty = lt;
        return n;
    }
    if (!(is_integer(lt) && is_integer(rhs->ty))) error(t, "%s= needs integers", op);
    opty = shift ? promote(lt) : usual(lt, rhs->ty);
    n = mk(N_OPASSIGN, t, lt);
    n->op = (char *)op;
    n->lhs = lhs;
    n->rhs = convert(rhs, shift ? promote(rhs->ty) : opty, t);
    n->opty = opty;
    return n;
}

static struct node *new_incdec(int kind, struct node *e, struct tok *t)
{
    struct node *n;
    int step = 1;
    need_lvalue(e, t, kind == N_PREINC || kind == N_POSTINC ? "++" : "--");
    if (e->bfwidth || e->ty->kind == K_LLONG || e->ty->kind == K_DOUBLE)
        return bf_update(kind == N_PREINC || kind == N_POSTINC ? "+" : "-", e, num(1, ty_int, t), t,
                         kind == N_POSTINC || kind == N_POSTDEC);
    if (e->ty->kind == K_PTR) {
        if (e->ty->base->size == 0) error(t, "++/-- on a pointer to an incomplete type");
        step = e->ty->base->size;
    } else if (!is_integer(e->ty))
        error(t, "++/-- needs a number or a pointer");
    n = mk(kind, t, e->ty);
    n->expr = e;
    n->step = step;
    return n;
}

static struct node *ptr_add(struct node *p, struct node *i, struct tok *t, int sign)
{
    struct node *off, *b;
    if (p->ty->base->size == 0) error(t, "arithmetic on a pointer to an incomplete type");
    off = scale(i, p->ty->base->size, t);
    if (off->kind == N_NUM && off->val == 0) return p;
    b = mk(N_BINOP, t, p->ty);
    b->op = sign > 0 ? "+" : "-";
    b->lhs = p;
    b->rhs = off;
    return b;
}

/* a < b etc. on values of type ct */
static int less(unsigned a, unsigned b, int uns) { return uns ? a < b : (int)a < (int)b; }

static unsigned divide(unsigned a, unsigned b, int uns, int mod)
{
    if (uns) return mod ? a % b : a / b;
    if ((int)a == (int)0x80000000 && (int)b == -1) return mod ? 0 : a;
    return mod ? (unsigned)((int)a % (int)b) : (unsigned)((int)a / (int)b);
}

static struct node *new_binary(const char *op, struct node *l, struct node *r, struct tok *t)
{
    struct type *lt, *rt, *ty;
    struct node *n;
    l = rvalue(l);
    r = rvalue(r);
    if (!strcmp(op, "&&") || !strcmp(op, "||")) {
        l = truth(l);
        r = truth(r);
    }
    if ((!strcmp(op, "+") || !strcmp(op, "-")) && (l->ty->kind == K_PTR) != (r->ty->kind == K_PTR)) {
        if (l->ty->kind == K_LLONG) l = llcast(l, ty_int, t);      /* (a long long index: its low word) */
        if (r->ty->kind == K_LLONG) r = llcast(r, ty_int, t);
    }
    lt = l->ty;
    rt = r->ty;
    if (is_arith(lt) && is_arith(rt) && (lt->kind == K_DOUBLE || rt->kind == K_DOUBLE) && strcmp(op, "&&") && strcmp(op, "||"))
        return dbinary(op, l, r, t);
    if (is_integer(lt) && is_integer(rt) && (lt->kind == K_LLONG || rt->kind == K_LLONG) && strcmp(op, "&&") && strcmp(op, "||"))
        return llbinary(op, l, r, t);
    if (!strcmp(op, "&&") || !strcmp(op, "||")) {
        if (!(is_scalar(lt) && is_scalar(rt))) error(t, "%s needs scalars", op);
        if (l->kind == N_NUM && r->kind == N_NUM) {
            int v = op[0] == '&' ? (l->val != 0 && r->val != 0) : (l->val != 0 || r->val != 0);
            return num((unsigned)v, ty_int, t);
        }
        n = mk(op[0] == '&' ? N_LOGAND : N_LOGOR, t, ty_int);
        n->lhs = l;
        n->rhs = r;
        return n;
    }
    if (!strcmp(op, "+")) {
        if (lt->kind == K_PTR && is_integer(rt)) return ptr_add(l, r, t, 1);
        if (rt->kind == K_PTR && is_integer(lt)) return ptr_add(r, l, t, 1);
    }
    if (!strcmp(op, "-")) {
        if (lt->kind == K_PTR && is_integer(rt)) return ptr_add(l, r, t, -1);
        if (lt->kind == K_PTR && rt->kind == K_PTR) {
            int size = lt->base->size;
            struct node *d;
            if (size == 0) error(t, "subtracting pointers to incomplete types");
            d = mk(N_BINOP, t, ty_int);
            d->op = "-";
            d->lhs = cast_node(l, ty_int, t);
            d->rhs = cast_node(r, ty_int, t);
            if (size == 1) return d;
            n = mk(N_BINOP, t, ty_int);
            n->op = "/";
            n->lhs = d;
            n->rhs = num((unsigned)size, ty_int, t);
            return n;
        }
    }
    if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") ||
        !strcmp(op, "<=") || !strcmp(op, ">=")) {
        struct type *ct;
        if (is_integer(lt) && is_integer(rt)) ct = usual(lt, rt);
        else if (lt->kind == K_PTR && rt->kind == K_PTR) ct = ty_uint;
        else if (lt->kind == K_PTR && is_integer(rt)) {
            if (!is_null(r)) warning(t, "comparing a pointer with an integer");
            ct = ty_uint;
        } else if (rt->kind == K_PTR && is_integer(lt)) {
            if (!is_null(l)) warning(t, "comparing a pointer with an integer");
            ct = ty_uint;
        } else {
            error(t, "cannot compare %s with %s", type_str(lt), type_str(rt));
            ct = 0;
        }
        if (l->kind == N_NUM && r->kind == N_NUM) {
            unsigned a = wrap(l->val, ct), b = wrap(r->val, ct);
            int u = unsigned_value(ct), v;
            if (!strcmp(op, "==")) v = a == b;
            else if (!strcmp(op, "!=")) v = a != b;
            else if (!strcmp(op, "<")) v = less(a, b, u);
            else if (!strcmp(op, ">")) v = less(b, a, u);
            else if (!strcmp(op, "<=")) v = !less(b, a, u);
            else v = !less(a, b, u);
            return num((unsigned)v, ty_int, t);
        }
        n = mk(N_CMP, t, ty_int);
        n->op = (char *)op;
        n->lhs = l;
        n->rhs = r;
        n->uns = ct->uns;
        return n;
    }
    if (!(is_integer(lt) && is_integer(rt)))
        error(t, "%s needs integers (got %s, %s)", op, type_str(lt), type_str(rt));
    if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
        ty = promote(lt);
        r = convert(r, promote(rt), t);
    } else {
        ty = usual(lt, rt);
        r = convert(r, ty, t);
    }
    l = convert(l, ty, t);
    if (l->kind == N_NUM && r->kind == N_NUM) {
        int shift = !strcmp(op, "<<") || !strcmp(op, ">>");
        unsigned a = wrap(l->val, ty), b = wrap(r->val, shift ? promote(rt) : ty), v = 0;
        int u = unsigned_value(ty);
        if ((op[0] == '/' || op[0] == '%') && b == 0) error(t, "division by zero");
        switch (op[0]) {
        case '+': v = a + b; break;
        case '-': v = a - b; break;
        case '*': v = a * b; break;
        case '/': v = divide(a, b, u, 0); break;
        case '%': v = divide(a, b, u, 1); break;
        case '&': v = a & b; break;
        case '|': v = a | b; break;
        case '^': v = a ^ b; break;
        case '<': v = a << (b & 31); break;
        case '>': v = u ? a >> (b & 31) : (unsigned)((int)a >> (b & 31)); break;
        }
        return num(v, ty, t);
    }
    n = mk(N_BINOP, t, ty);
    n->op = (char *)op;
    n->lhs = l;
    n->rhs = r;
    return n;
}

static struct node *member(struct node *e, struct tok *name)
{
    struct member tmp, *m;
    struct node *n;
    if (!e->ty->members) error(name, "use of an incomplete struct or union");
    m = find_member(e->ty, name->text, &tmp);
    if (!m) error(name, "no member %s in %s", name->text, type_str(e->ty));
    if (m == &tmp) { m = xmalloc(sizeof *m); *m = tmp; }
    n = mk(N_MEMBER, name, m->ty);
    n->expr = e;
    n->mem = m;
    n->bfbit = m->bit;
    n->bfwidth = m->width;
    return n;
}

static struct node *call(struct node *fnode, struct tok *t)
{
    struct vec args;
    memset(&args, 0, sizeof args);
    if (!consume(")")) {
        for (;;) {
            vpush(&args, rvalue(assign()));
            if (consume(")")) break;
            skip(",");
        }
    }
    return build_call(fnode, &args, t);
}

static struct node *build_call(struct node *fnode, struct vec *argv, struct tok *t)
{
    struct vec args = *argv;
    struct node *f, *n;
    struct type *ft;
    int k;
    f = rvalue(fnode);
    if (f->ty->kind != K_PTR || f->ty->base->kind != K_FUNC) error(t, "call of a non-function");
    ft = f->ty->base;
    if (ft->params && (args.n < ft->nparams || (args.n > ft->nparams && !ft->variadic)))
        error(t, "%d argument(s) to a function taking %d", args.n, ft->nparams);
    n = mk(N_CALL, t, ft->base);
    for (k = 0; k < args.n; k++) {
        struct node *a = args.a[k];
        struct tok *at_ = a->tok ? a->tok : t;
        if (is_memval(a->ty) && !ft->params) error(at_, "a struct passed to a function without a prototype");
        if (ft->params && k < ft->nparams) vpush(&n->list, convert(a, ft->params[k], at_));
        else vpush(&n->list, convert(a, is_integer(a->ty) ? promote(a->ty) : a->ty, at_));
    }
    if (is_memval(ft->base)) {          /* the result into a temporary: *f(&tmp, ...), made
                                           after the arguments' own temporaries (as p32cc) */
        struct obj *tmp = new_obj("", ft->base, 1);
        struct node *a = mk(N_ADDR, t, ptr_to(ft->base));
        tmp->addr_taken = 1;
        vpush(&curfn->locals, tmp);
        a->expr = var_node(tmp, t);
        vpush(&n->list, a);
        memmove(n->list.a + 1, n->list.a, (n->list.n - 1) * sizeof n->list.a[0]);
        n->list.a[0] = a;
        n->ty = a->ty;
    }
    n->fn = f;
    n->ftype = ft;
    n->direct = fnode->kind == N_VAR && fnode->obj->ty->kind == K_FUNC ? fnode->obj : 0;
    if (is_memval(ft->base)) {
        struct node *d = mk(N_DEREF, t, ft->base);
        d->expr = n;
        return d;
    }
    return n;
}

static struct node *primary(void)
{
    struct tok *t = TOK;
    if (consume("(")) {
        struct node *e = expr();
        skip(")");
        return e;
    }
    if (t->kind == T_NUM) {
        unsigned v;
        int uns, lng, nondec;
        struct type *ty;
        I++;
        unsigned long long w;
        int over;
        if (is_float_lit(t->text)) {    /* a floating constant (f, l: the same double) */
            char buf[256];
            int n = strlen(t->text);
            if (n >= (int)sizeof buf) error(t, "floating constant too long");
            strcpy(buf, t->text);
            if (strchr("fFlL", buf[n - 1])) buf[n - 1] = 0;
            return dconst(strtod(buf, 0), t);
        }
        if (parse_int64(t, &w, &uns, &lng, &nondec, &over) < 0) error(t, "bad number %s (no floating point)", t->text);
        if (over) error(t, "constant too big for 64 bits");
        if (lng == 2 || w >> 32)                /* (a 32-bit one keeps its C89 type) */
            return llconst(w, uns || w >> 63 ? ty_ullong : ty_llong, t);
        v = (unsigned)w;
        if (uns) ty = lng ? ty_ulong : ty_uint;
        else if (v >= 0x80000000u) ty = ty_uint;
        else ty = lng ? ty_long : ty_int;
        return num(v, ty, t);
    }
    if (t->kind == T_CHAR) { I++; return num((unsigned)t->cval, ty_int, t); }
    if (t->kind == T_STR) {
        struct node *n;
        int len = 0;
        unsigned char *d;
        int k;
        for (k = I; T[k]->kind == T_STR; k++) len += T[k]->slen;
        d = xmalloc(len + 1);
        len = 0;
        while (TOK->kind == T_STR) {
            memcpy(d + len, TOK->sval, TOK->slen);
            len += TOK->slen;
            I++;
        }
        d[len++] = 0;
        n = mk(N_STR, t, array_of(ty_char, len));
        n->label = newlabel("str");
        n->data = d;
        n->dlen = len;
        vpush(&prog->strings, n);
        return n;
    }
    if (t->kind == T_ID) {
        struct entry *e;
        struct obj *o;
        struct node *n;
        I++;
        e = lookup(t->text);
        if (!e && (!strcmp(t->text, "__builtin_nan") || !strcmp(t->text, "__builtin_inf")) && at("(")) {
            /* math.h's NAN and INFINITY as constants (the NaN is 0x7FF8000000000000);
               the argument (nan's tag) is ignored */
            int depth = 1;
            double d;
            unsigned long long bits = !strcmp(t->text, "__builtin_nan") ? 0x7FF8000000000000ULL : 0x7FF0000000000000ULL;
            I++;
            while (depth) {
                if (TOK->kind == T_EOF) error(t, "unbalanced parentheses");
                depth += at("(") - at(")");
                I++;
            }
            memcpy(&d, &bits, 8);
            return dconst(d, t);
        }
        if (!e && curfn && !strcmp(t->text, "__func__")) {     /* (C99: the function's name) */
            int len = strlen(curfn->name) + 1;
            unsigned char *d = xmalloc(len);
            memcpy(d, curfn->name, len);
            n = mk(N_STR, t, array_of(ty_char, len));
            n->label = newlabel("str");
            n->data = d;
            n->dlen = len;
            vpush(&prog->strings, n);
            return n;
        }
        if (!e) {
            if (at("(")) {
                const char *h = variadic_lib(t->text);
                /* (a variadic function takes every argument on the stack: called
                   undeclared, it would read the wrong ones and crash) */
                if (h) error(t, "%s() takes a variable number of arguments: #include <%s>", t->text, h);
                warning(t, "implicit declaration of %s()", t->text);
                o = declare_global(t, func_type(ty_int, 0, 0, 0), "extern");
                n = mk(N_VAR, t, o->ty);
                n->obj = o;
                return n;
            }
            error(t, "undeclared %s", t->text);
        }
        if (e->kind == E_ENUM) return num((unsigned)e->val, ty_int, t);
        if (e->kind == E_TYPEDEF) error(t, "%s is a type, not a value", t->text);
        o = e->obj;
        o->used = 1;
        o->uses += 1 + 7 * (brk_depth + hdr_depth);    /* (a use inside a loop or its header counts more) */
        n = mk(N_VAR, t, o->ty);
        n->obj = o;
        return n;
    }
    error(t, "expected an expression, got '%s'", t->text[0] ? t->text : "end of file");
    return 0;
}

/* (type){ ... }: an object without a name. Outside functions a static one;
   inside, a local one whose initialisation runs where the literal is
   evaluated: *(N_STMTEXPR: the initialising statements, then &object) */
static struct node *compound_literal(struct type *ty, struct tok *t)
{
    struct obj *var;
    struct node *v, *a, *s, *d;
    if (!curfn) {
        struct obj *o = new_obj("", ty, 0);
        o->is_static = 1;
        o->label = newlabel("cl");
        vpush(&prog->globals, o);
        global_init(o, t);
        v = mk(N_VAR, t, o->ty);
        v->obj = o;
        return v;
    }
    var = new_obj("", ty, 1);
    var->addr_taken = 1;
    vpush(&curfn->locals, var);
    s = mk(N_STMTEXPR, t, 0);
    local_init(var, t, &s->list);
    v = mk(N_VAR, t, var->ty);
    v->obj = var;
    a = mk(N_ADDR, t, ptr_to(var->ty));
    a->expr = v;
    s->ty = a->ty;
    s->expr = a;
    d = mk(N_DEREF, t, var->ty);
    d->expr = s;
    return d;
}

static struct node *postfix_on(struct node *e);
static struct node *postfix(void) { return postfix_on(primary()); }

static struct node *postfix_on(struct node *e)
{
    for (;;) {
        struct tok *t = TOK;
        if (consume("[")) {
            struct node *idx = expr(), *s, *d;
            skip("]");
            s = new_binary("+", e, idx, t);
            if (s->ty->kind != K_PTR) error(t, "subscript of a non-pointer");
            d = mk(N_DEREF, t, s->ty->base);
            d->expr = s;
            e = d;
        } else if (consume("(")) {
            e = call(e, t);
        } else if (consume(".")) {
            struct tok *name = ident();
            if (e->ty->kind != K_STRUCT && e->ty->kind != K_UNION) error(t, ". needs a struct");
            e = member(e, name);
        } else if (consume("->")) {
            struct tok *name = ident();
            struct node *p = rvalue(e), *d;
            if (p->ty->kind != K_PTR || (p->ty->base->kind != K_STRUCT && p->ty->base->kind != K_UNION))
                error(t, "-> needs a struct pointer");
            d = mk(N_DEREF, t, p->ty->base);
            d->expr = p;
            e = member(d, name);
        } else if (at("++") || at("--")) {
            I++;
            e = new_incdec(is(t, "++") ? N_POSTINC : N_POSTDEC, e, t);
        } else
            return e;
    }
}

static struct node *unary(void)
{
    struct tok *t = TOK;
    if (t->kind == T_PUNCT) {
        if (is(t, "++") || is(t, "--")) {
            I++;
            return new_incdec(is(t, "++") ? N_PREINC : N_PREDEC, unary(), t);
        }
        if (is(t, "&")) {
            struct node *e, *base, *a;
            I++;
            e = cast();
            if (e->kind == N_VAR && e->obj->ty->kind == K_FUNC) {
                a = mk(N_ADDR, t, ptr_to(e->ty));
                a->expr = e;
                return a;
            }
            need_lvalue(e, t, "&");
            if (e->bfwidth) error(t, "& of a bit-field");
            base = e;
            while (base->kind == N_MEMBER) base = base->expr;
            if (base->kind == N_VAR) base->obj->addr_taken = 1;
            a = mk(N_ADDR, t, ptr_to(e->ty));
            a->expr = e;
            return a;
        }
        if (is(t, "*")) {
            struct node *e, *d;
            I++;
            e = rvalue(cast());
            if (e->ty->kind != K_PTR) error(t, "* needs a pointer");
            if (e->ty->base->kind == K_FUNC) return e;
            if (e->ty->base->kind == K_VOID) error(t, "dereferencing a void pointer");
            d = mk(N_DEREF, t, e->ty->base);
            d->expr = e;
            return d;
        }
        if (is(t, "+") || is(t, "-") || is(t, "~") || is(t, "!")) {
            struct node *e, *n;
            struct type *ty;
            I++;
            e = rvalue(cast());
            if (is(t, "!")) {
                if (!is_scalar(e->ty)) error(t, "! needs a scalar");
                e = truth(e);
                if (e->kind == N_NUM) return num(e->val == 0, ty_int, t);
                n = mk(N_NOT, t, ty_int);
                n->expr = e;
                return n;
            }
            if (e->ty->kind == K_DOUBLE && (is(t, "+") || is(t, "-"))) return is(t, "+") ? e : dop("neg", &e, 1, t);
            if (!is_integer(e->ty)) error(t, "%s needs an integer", t->text);
            ty = promote(e->ty);
            if (ty->kind == K_LLONG) {
                if (is(t, "+")) return e;
                return llop(is(t, "-") ? "neg" : "not", &e, 1, ty, t);
            }
            if (e->kind == N_NUM)
                return num(is(t, "+") ? e->val : is(t, "-") ? (unsigned)-(int)e->val : ~e->val, ty, t);
            if (is(t, "+")) return convert(e, ty, t);
            n = mk(is(t, "-") ? N_NEG : N_BITNOT, t, ty);
            n->expr = convert(e, ty, t);
            return n;
        }
    }
    if (t->kind == T_ID && is(t, "sizeof")) {
        struct type *ty;
        I++;
        if (at("(") && is_typename(peek(1))) {
            I++;
            ty = typename_();
            skip(")");
        } else {
            struct node *e = unary();
            if (e->bfwidth) error(t, "sizeof a bit-field");
            ty = e->ty;
        }
        if (ty->kind == K_FUNC || ((ty->kind == K_STRUCT || ty->kind == K_UNION) && !ty->members) || ty->kind == K_VOID)
            error(t, "sizeof an incomplete type");
        return num((unsigned)ty->size, ty_uint, t);
    }
    return postfix();
}

static struct node *cast(void)
{
    if (at("(") && is_typename(peek(1))) {
        struct tok *t = TOK;
        struct type *ty;
        struct node *e;
        I++;
        ty = typename_();
        skip(")");
        if (at("{")) return postfix_on(compound_literal(ty, t));
        e = rvalue(cast());
        if (ty->kind != K_VOID && !is_scalar(ty)) error(t, "cast to a non-scalar type");
        if (ty->kind != K_VOID && !is_scalar(e->ty)) error(t, "cast of a non-scalar value");
        return new_cast(e, ty, t);
    }
    return unary();
}

static const char *const bin_levels[][5] = {
    { "||" }, { "&&" }, { "|" }, { "^" }, { "&" }, { "==", "!=" }, { "<", ">", "<=", ">=" },
    { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" } };

static struct node *binary(int lvl)
{
    struct node *e;
    if (lvl == 10) return cast();
    e = binary(lvl + 1);
    for (;;) {
        int k;
        const char *op = 0;
        if (TOK->kind != T_PUNCT) break;
        for (k = 0; k < 5 && bin_levels[lvl][k]; k++) if (is(TOK, bin_levels[lvl][k])) op = bin_levels[lvl][k];
        if (!op) break;
        {
            struct tok *t = TOK;
            struct node *r;
            I++;
            r = binary(lvl + 1);
            e = new_binary(op, e, r, t);
        }
    }
    return e;
}

static struct node *conditional(void)
{
    struct node *c = binary(0), *a, *b, *n;
    struct tok *t;
    struct type *ty = 0;
    if (!at("?")) return c;
    t = TOK;
    I++;
    c = truth(rvalue(c));
    a = rvalue(expr());
    skip(":");
    b = rvalue(conditional());
    c = rvalue(c);
    if (is_arith(a->ty) && is_arith(b->ty)) ty = usual(a->ty, b->ty);
    else if (a->ty->kind == K_PTR && b->ty->kind == K_PTR) ty = a->ty->base->kind != K_VOID ? a->ty : b->ty;
    else if (a->ty->kind == K_PTR && is_null(b)) ty = a->ty;
    else if (b->ty->kind == K_PTR && is_null(a)) ty = b->ty;
    else if (a->ty->kind == K_VOID || b->ty->kind == K_VOID) ty = ty_void;
    else if ((a->ty->kind == K_STRUCT || a->ty->kind == K_UNION) && a->ty == b->ty) ty = a->ty;     /* (by address) */
    else error(t, "?: operands of different types");
    if (c->kind == N_NUM) return convert(c->val ? a : b, ty, t);
    n = mk(N_COND, t, ty);
    n->cond = c;
    n->a = convert(a, ty, t);
    n->b = convert(b, ty, t);
    return n;
}

static const char *const assign_ops[][2] = {
    { "=", 0 }, { "+=", "+" }, { "-=", "-" }, { "*=", "*" }, { "/=", "/" }, { "%=", "%" }, { "&=", "&" },
    { "|=", "|" }, { "^=", "^" }, { "<<=", "<<" }, { ">>=", ">>" } };

static struct node *assign(void)
{
    struct node *e = conditional();
    struct tok *t = TOK;
    int k;
    if (t->kind == T_PUNCT)
        for (k = 0; k < 11; k++)
            if (is(t, assign_ops[k][0])) {
                struct node *r;
                I++;
                r = assign();
                if (!assign_ops[k][1]) return new_assign(e, r, t);
                return new_opassign(assign_ops[k][1], e, r, t);
            }
    return e;
}

static struct node *expr(void)
{
    struct node *e = assign();
    while (at(",")) {
        struct tok *t = TOK;
        struct node *r, *n;
        I++;
        r = assign();
        n = mk(N_COMMA, t, rvalue(r)->ty);
        n->lhs = e;
        n->rhs = rvalue(r);
        e = n;
    }
    return e;
}

/* ---------------------------------------------------------------- constants */
static int eval_lvalue(struct node *e, char **label, int *v);

/* a constant initialiser: *label (or NULL) and *v; -1 if it is not one */
static int eval_addr(struct node *e, char **label, int *v)
{
    char *l1, *l2;
    int a, b;
    if (e->kind == N_VAR && e->obj->is_dconst) {       /* (a double for an integer: truncated, if it fits 32 bits) */
        double d = e->obj->dval;
        if (!(d == d && d >= -2147483648.0 && d < 4294967296.0)) return -1;
        *label = 0;
        *v = (int)(long long)d;
        return 0;
    }
    if (e->kind == N_VAR && e->obj->is_llconst) {      /* (a long long constant: its low word here) */
        *label = 0;
        *v = (int)e->obj->llval;
        return 0;
    }
    switch (e->kind) {
    case N_NUM: *label = 0; *v = (int)e->val; return 0;
    case N_STR: *label = e->label; *v = 0; return 0;
    case N_ADDR: return eval_lvalue(e->expr, label, v);
    case N_CAST:
        if (eval_addr(e->expr, label, v) < 0) return -1;
        if (!*label) *v = (int)wrap((unsigned)*v, e->ty);
        return 0;
    case N_BINOP:
        if (strcmp(e->op, "+") && strcmp(e->op, "-")) return -1;
        if (eval_addr(e->lhs, &l1, &a) < 0 || eval_addr(e->rhs, &l2, &b) < 0) return -1;
        if (l1 && l2) return -1;
        if (l2) {
            if (e->op[0] == '-') return -1;
            *label = l2;
            *v = a + b;
            return 0;
        }
        *label = l1;
        *v = e->op[0] == '+' ? a + b : a - b;
        if (!l1) *v = (int)wrap((unsigned)*v, e->ty);
        return 0;
    case N_VAR:
        if (e->ty->kind == K_ARRAY || e->ty->kind == K_FUNC) return eval_lvalue(e, label, v);
        return -1;
    case N_COND:
        if (eval_addr(e->cond, label, &a) < 0 || *label) return -1;
        return eval_addr(a ? e->a : e->b, label, v);
    }
    return -1;
}

static int eval_lvalue(struct node *e, char **label, int *v)
{
    if (e->kind == N_VAR && !e->obj->local) { *label = e->obj->label; *v = 0; return 0; }
    if (e->kind == N_STR) { *label = e->label; *v = 0; return 0; }
    if (e->kind == N_DEREF) return eval_addr(e->expr, label, v);
    if (e->kind == N_MEMBER) {
        if (eval_lvalue(e->expr, label, v) < 0) return -1;
        *v += e->mem->offset;
        return 0;
    }
    return -1;
}

static int const_expr(void)
{
    struct tok *t = TOK;
    struct node *e = conditional();
    char *label;
    int v;
    if (eval_addr(e, &label, &v) < 0) error(t, "not a constant expression");
    if (label) error(t, "not an integer constant");
    return v;
}

/* ================================================================ the program */
struct program *parse(struct tok **toks, int n)
{
    struct tok *eof = tok_new();
    eof->kind = T_EOF;
    eof->text = intern("");
    eof->file = n ? toks[n - 1]->file : "?";
    eof->line = n ? toks[n - 1]->line : 0;
    T = xmalloc(sizeof *T * (n + 2));
    if (n) memcpy(T, toks, sizeof *T * n);
    free(toks);                         /* (preprocess's array: T is the copy used from here) */
    T[n] = eof;
    NT = n + 1;
    I = 0;
    prog = xmalloc(sizeof *prog);
    memset(prog, 0, sizeof *prog);
    nlabel = 0;                         /* each file afresh (as p32cc.py's new Parser) */
    llconsts.n = 0;
    helpers.n = 0;
    dconsts.n = 0;
    brk_depth = cont_depth = hdr_depth = 0;
    switches.n = 0;
    curfn = 0;
    scope = 0;
    push_scope();
    gscope = scope;
    while (TOK->kind != T_EOF) {
        if (consume(";")) continue;
        external();
    }
    return prog;
}
