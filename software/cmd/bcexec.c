/* bcexec.c -- bc's interpreter: walks the tree that bc.c parsed (bc.h).
 * Variables are dynamically scoped, as in every bc: a call saves the
 * values of its parameters' and autos' names, and the return restores
 * them. Arrays are passed by value (copied). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc.h"

#define MAXINDEX 16777215L      /* the largest array index */
#define MAXDEPTH 2000           /* calls within calls */

typedef struct {
    Num **v;
    long n;
} Array;

typedef struct {
    int sym, isarr;
    Num *v;
    Array *a;
} Save;

int scale = 0, ibase = 10, obase = 10;
Func **funcs;
static Num **vars, *last;
static Array **arrays;
static int cap;                         /* of vars arrays funcs */
static Save *saves;
static int nsave, capsave, depth;
static Num *retval;
static int col, linelen = -1;           /* output column; line length */

void growsyms(int n)
{
    int k = cap;
    if (n <= cap) return;
    while (cap < n) cap = cap ? cap * 2 : 32;
    vars = realloc(vars, cap * sizeof(Num *));
    arrays = realloc(arrays, cap * sizeof(Array *));
    funcs = realloc(funcs, cap * sizeof(Func *));
    if (!vars || !arrays || !funcs) { fprintf(stderr, "bc: out of memory\n"); exit(1); }
    for (; k < cap; k++) { vars[k] = 0; arrays[k] = 0; funcs[k] = 0; }
}

/* ---- output ---- */

void outc(int c)
{
    if (linelen < 0) {
        char *e = getenv("BC_LINE_LENGTH");
        linelen = e ? atoi(e) : 70;
        if (linelen < 3 && linelen != 0) linelen = 70;
    }
    if (c == '\n') col = 0;
    else if (++col == linelen - 1 && linelen) {
        putchar('\\');
        putchar('\n');
        col = 1;
    }
    putchar(c);
}

void outs(char *s, int esc)
{
    int c;
    for (; *s; s++) {
        c = *s;
        if (esc && c == '\\' && s[1]) {
            switch (*++s) {
            case 'a': c = 7; break;
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'q': c = '"'; break;
            case '\\': c = '\\'; break;
            default: putchar('\\'); col++; c = *s;
            }
        }
        if (c == '\n') col = 0; else col++;
        putchar(c);
    }
}

/* ---- the tree ---- */

void freenode(Node *n)
{
    Node *next;
    for (; n; n = next) {
        next = n->next;
        freenode(n->a); freenode(n->b); freenode(n->c); freenode(n->d);
        free(n->s);
        if (n->num) n_free(n->num);
        free(n);
    }
}

/* ---- variables ---- */

static Array *newarray(void)
{
    Array *a = malloc(sizeof(Array));
    if (!a) bcerror("out of memory");
    a->v = 0;
    a->n = 0;
    return a;
}

static void freearray(Array *a)
{
    long i;
    if (!a) return;
    for (i = 0; i < a->n; i++) if (a->v[i]) n_free(a->v[i]);
    free(a->v);
    free(a);
}

static Array *copyarray(Array *a)
{
    Array *c = newarray();
    long i;
    if (a && a->n) {
        c->v = malloc(a->n * sizeof(Num *));
        if (!c->v) { free(c); bcerror("out of memory"); }
        c->n = a->n;
        for (i = 0; i < a->n; i++) c->v[i] = a->v[i] ? n_ref(a->v[i]) : 0;
    }
    return c;
}

static Array *arrayof(int sym)
{
    if (!arrays[sym]) arrays[sym] = newarray();
    return arrays[sym];
}

static long tolong(Num *v, char *what)
{
    int ok;
    long n = n_long(v, &ok);
    if (!ok) { n_free(v); bcerror("%s too big", what); }
    return n;
}

static Num *eval(Node *e);

static long subscript(Node *e)
{
    Num *v = eval(e->a);
    long i = tolong(v, "array index");
    n_free(v);
    if (i < 0 || i > MAXINDEX) bcerror("array index %ld out of range", i);
    return i;
}

/* the value of an lvalue (with its index already worked out) */
static Num *lvget(Node *lv, long i)
{
    Array *a;
    switch (lv->t) {
    case E_VAR: return vars[lv->op] ? n_ref(vars[lv->op]) : n_zero();
    case E_ARR:
        a = arrays[lv->op];
        return a && i < a->n && a->v[i] ? n_ref(a->v[i]) : n_zero();
    case E_SCALE: return n_int(scale);
    case E_IBASE: return n_int(ibase);
    case E_OBASE: return n_int(obase);
    default: return last ? n_ref(last) : n_zero();
    }
}

static void lvset(Node *lv, long i, Num *v)    /* (v is kept: a reference) */
{
    Array *a;
    long n;
    switch (lv->t) {
    case E_VAR:
        if (vars[lv->op]) n_free(vars[lv->op]);
        vars[lv->op] = n_ref(v);
        return;
    case E_ARR:
        a = arrayof(lv->op);
        if (i >= a->n) {
            Num **nv;
            n = a->n ? a->n : 8;
            while (n <= i) n *= 2;
            if (n > MAXINDEX + 1) n = MAXINDEX + 1;
            nv = realloc(a->v, n * sizeof(Num *));
            if (!nv) bcerror("out of memory");
            a->v = nv;
            while (a->n < n) a->v[a->n++] = 0;
        }
        if (a->v[i]) n_free(a->v[i]);
        a->v[i] = n_ref(v);
        return;
    case E_SCALE:
        n = tolong(v, "scale");
        if (n < 0 || n > MAXSCALE) bcerror("scale must be 0 to %d", MAXSCALE);
        scale = n;
        return;
    case E_IBASE:
        n = tolong(v, "ibase");
        if (n < 2 || n > 16) bcerror("ibase must be 2 to 16");
        ibase = n;
        return;
    case E_OBASE:
        n = tolong(v, "obase");
        if (n < 2 || n > 999) bcerror("obase must be 2 to 999");
        obase = n;
        return;
    default:
        if (last) n_free(last);
        last = n_ref(v);
    }
}

/* push a name's value (to be restored on return) and give it a new one */
static void save(int sym, int isarr, Num *v, Array *a)
{
    Save *s;
    if (nsave == capsave) {
        capsave = capsave ? capsave * 2 : 64;
        saves = realloc(saves, capsave * sizeof(Save));
        if (!saves) { fprintf(stderr, "bc: out of memory\n"); exit(1); }
    }
    s = &saves[nsave++];
    s->sym = sym;
    s->isarr = isarr;
    if (isarr) { s->a = arrays[sym]; arrays[sym] = a; }
    else { s->v = vars[sym]; vars[sym] = v; }
}

static void restore(int mark)
{
    Save *s;
    while (nsave > mark) {
        s = &saves[--nsave];
        if (s->isarr) { freearray(arrays[s->sym]); arrays[s->sym] = s->a; }
        else { if (vars[s->sym]) n_free(vars[s->sym]); vars[s->sym] = s->v; }
    }
}

void unwind(void)
{
    restore(0);
    depth = 0;
}

/* ---- expressions ---- */

static Num *call(Node *e)
{
    Func *f = funcs[e->op];
    Node *p;
    Num **v;
    Array **a;
    int n = 0, i, mark, r;
    Num *res;
    if (!f || !f->defined) bcerror("function %s not defined", symname[e->op]);
    for (p = e->a; p; p = p->next) n++;
    if (n != f->np) bcerror("function %s takes %d argument%s", symname[e->op], f->np, f->np == 1 ? "" : "s");
    if (depth >= MAXDEPTH) bcerror("calls nested too deeply");
    v = malloc((n + 1) * sizeof(Num *));
    a = malloc((n + 1) * sizeof(Array *));
    if (!v || !a) bcerror("out of memory");
    for (i = 0, p = e->a; p; p = p->next, i++) {
        v[i] = 0;
        a[i] = 0;
        if (f->arr[i] != (p->t == E_ARRARG))
            bcerror("function %s: argument %d should be %s", symname[e->op], i + 1,
                    f->arr[i] ? "an array" : "a number");
        if (f->arr[i]) a[i] = copyarray(arrays[p->op]);
        else v[i] = eval(p);
    }
    mark = nsave;
    for (i = 0; i < n; i++) save(f->sym[i], f->arr[i], v[i], a[i]);
    free(v);
    free(a);
    for (; i < f->np + f->na; i++)
        save(f->sym[i], f->arr[i], 0, f->arr[i] ? newarray() : 0);
    depth++;
    r = exec(f->body);
    depth--;
    if (r == RETURN) res = retval;
    else res = n_zero();
    retval = 0;
    restore(mark);
    return res;
}

static int truth(Node *e)
{
    Num *v = eval(e);
    int t = !n_iszero(v);
    n_free(v);
    return t;
}

static Num *arith(int op, Num *x, Num *y)
{
    Num *r;
    switch (op) {
    case '+': return n_add(x, y);
    case '-': return n_sub(x, y);
    case '*': return n_mul(x, y, scale);
    case '/':
        if (!(r = n_div(x, y, scale))) bcerror("divide by zero");
        return r;
    case '%':
        if (!(r = n_mod(x, y, scale))) bcerror("modulo by zero");
        return r;
    default: return n_pow(x, y, scale);
    }
}

static Num *eval(Node *e)
{
    Num *x, *y, *r;
    long i = 0;
    int c;
    switch (e->t) {
    case E_NUM:
        if (!e->num || e->base != ibase) {
            if (e->num) n_free(e->num);
            e->num = 0;
            e->num = n_parse(e->s, ibase);
            e->base = ibase;
        }
        return n_ref(e->num);
    case E_ARR:
        i = subscript(e);
        /* fall through */
    case E_VAR: case E_SCALE: case E_IBASE: case E_OBASE: case E_LAST:
        return lvget(e, i);
    case E_CALL:
        return call(e);
    case E_ARRARG:
        bcerror("array %s[] used as a number", symname[e->op]);
    case E_PAREN:
        return eval(e->a);
    case E_NEG:
        x = eval(e->a);
        r = n_neg(x);
        n_free(x);
        return r;
    case E_NOT:
        return n_int(!truth(e->a));
    case E_AND:
        return n_int(truth(e->a) && truth(e->b));
    case E_OR:
        return n_int(truth(e->a) || truth(e->b));
    case E_BIN:
        x = eval(e->a);
        y = eval(e->b);
        r = arith(e->op, x, y);
        n_free(x);
        n_free(y);
        return r;
    case E_REL:
        x = eval(e->a);
        y = eval(e->b);
        c = n_cmp(x, y);
        n_free(x);
        n_free(y);
        switch (e->op) {
        case '<': return n_int(c < 0);
        case '>': return n_int(c > 0);
        case 'L': return n_int(c <= 0);
        case 'G': return n_int(c >= 0);
        case 'E': return n_int(c == 0);
        default: return n_int(c != 0);
        }
    case E_ASSIGN:
        if (e->a->t == E_ARR) i = subscript(e->a);
        y = eval(e->b);
        if (e->op != '=') {
            x = lvget(e->a, i);
            r = arith(e->op, x, y);
            n_free(x);
            n_free(y);
            y = r;
        }
        lvset(e->a, i, y);
        return y;
    case E_PRE: case E_POST:
        if (e->a->t == E_ARR) i = subscript(e->a);
        x = lvget(e->a, i);
        y = n_int(1);
        r = e->op == '+' ? n_add(x, y) : n_sub(x, y);
        n_free(y);
        lvset(e->a, i, r);
        if (e->t == E_PRE) { n_free(x); return r; }
        n_free(r);
        return x;
    case E_FSCALE:
        x = eval(e->a);
        r = n_int(x->scale);
        n_free(x);
        return r;
    case E_SQRT:
        x = eval(e->a);
        r = n_sqrt(x, scale);
        n_free(x);
        if (!r) bcerror("square root of a negative number");
        return r;
    case E_LENGTH:
        x = eval(e->a);
        r = n_int(n_length(x));
        n_free(x);
        return r;
    }
    bcerror("bad expression");
    return 0;
}

/* ---- statements ---- */

int exec(Node *s)
{
    Node *p;
    Num *v;
    int r;
    switch (s->t) {
    case S_EXPR:
        v = eval(s->a);
        if (s->a->t == E_ASSIGN) { n_free(v); return 0; }
        n_print(v, obase);
        outc('\n');
        if (last) n_free(last);
        last = v;
        return 0;
    case S_STR:
        outs(s->s, 0);
        return 0;
    case S_PRINT:
        for (p = s->a; p; p = p->next) {
            if (p->t == S_STR) outs(p->s, 1);
            else {
                v = eval(p);
                n_print(v, obase);
                if (last) n_free(last);
                last = v;
            }
        }
        return 0;
    case S_BLOCK:
        for (p = s->a; p; p = p->next)
            if ((r = exec(p)) != 0) return r;
        return 0;
    case S_IF:
        if (truth(s->a)) return exec(s->b);
        if (s->c) return exec(s->c);
        return 0;
    case S_WHILE:
        while (truth(s->a)) {
            r = exec(s->b);
            if (r == BREAK) break;
            if (r == RETURN) return r;
        }
        return 0;
    case S_FOR:
        if (s->a) n_free(eval(s->a));
        for (;;) {
            if (s->b && !truth(s->b)) break;
            r = exec(s->d);
            if (r == BREAK) break;
            if (r == RETURN) return r;
            if (s->c) n_free(eval(s->c));
        }
        return 0;
    case S_BREAK:
        return BREAK;
    case S_CONT:
        return CONTINUE;
    case S_RETURN:
        if (retval) n_free(retval);
        retval = s->a ? eval(s->a) : n_zero();
        return RETURN;
    case S_HALT:
        fflush(stdout);
        exit(0);
    }
    return 0;
}
