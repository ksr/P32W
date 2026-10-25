/* awkrun.c -- awk's interpreter (awk.h): walks the tree. eval gives a
 * value in a Cell the caller frees; evalref may instead hand back the
 * variable's own Cell (no copy), safe until something with side effects
 * runs, so binary operators copy their left operand only when the right
 * one is not pure; evalnum and evalbool skip the Cells altogether for
 * arithmetic and conditions. Statements return how they ended (X_NEXT,
 * X_BREAK ...); next, nextfile and exit inside a function longjmp back to
 * the rule being run. A function's locals are a frame of Cells; an array
 * argument (or an unset variable, which may become one) is passed as a
 * T_REF to the caller's Cell. */
#include "awk.h"

jmp_buf ruleout;
static Cell *frame;             /* the running function's parameters and locals */
static Cell retval;
static int lastfield;           /* the field lref found (for lhook) */

static void call(Node *n, Cell *r);
static void getline_(Node *n, Cell *r);

/* ---------------------------------------------------------------- helpers */
static int ispure(Node *n)
{
    Node *k;
    int p = 1;
    if (!n) return 1;
    if (n->pure) return n->pure == 1;
    switch (n->op) {
    case N_ASSIGN: case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: case N_CALL: case N_GETLINE:
        p = 0;
        break;
    case N_BUILTIN:
        if (n->n == B_SUB || n->n == B_GSUB || n->n == B_SPLIT || n->n == B_MATCH || n->n == B_CLOSE
            || n->n == B_SYSTEM || n->n == B_SRAND)
            p = 0;
        break;
    }
    if (n->op == N_NUM || n->op == N_STR || n->op == N_REGEX) p = 1;
    else if (p) {
        for (k = n->a; k && p; k = k->next) p = ispure(k);
        for (k = n->b; k && p; k = k->next) p = ispure(k);
        for (k = n->c; k && p; k = k->next) p = ispure(k);
    }
    n->pure = p ? 1 : 2;
    return p;
}

static int evalint(Node *n)
{
    Num x;
    evalnum(n, &x);
    return num_toint(&x);
}

static Array *arrayof(Node *n)
{
    if (n->op == N_VAR) return carray(&gvar[n->n]);
    if (n->op == N_LOCAL) return carray(&frame[n->n]);
    fatal("can't use a value as an array");
    return 0;
}

/* the subscripts joined by SUBSEP (an snew string) */
static char *joinsubs(Node *l)
{
    Buf b;
    Cell t;
    buf_init(&b);
    for (; l; l = l->next) {
        buf_adds(&b, evalstr(l, &t));
        cfree(&t);
        if (l->next) buf_adds(&b, cstr(&gvar[V_SUBSEP]));
    }
    return buf_take(&b);
}

static Cell *elemref(Node *n, int create)
{
    Array *a = arrayof(n->a);
    Cell t, *c;
    char *k;
    if (!n->b->next) {
        k = evalstr(n->b, &t);
        c = aget(a, k, create);
        cfree(&t);
        return c;
    }
    k = joinsubs(n->b);
    c = aget(a, k, create);
    sfree(k);
    return c;
}

/* ---------------------------------------------------------------- assignment */
Cell *lref(Node *n)
{
    Cell *c;
    switch (n->op) {
    case N_VAR:
        if (n->n == V_NF && !fldsplitdone) fldsplit();
        c = &gvar[n->n];
        if (c->t == T_ARR) fatal("can't assign to %s; it's an array name.", gname[n->n]);
        return c;
    case N_LOCAL:
        c = &frame[n->n];
        if (c->t == T_REF) {
            if (deref(c)->t == T_ARR) fatal("can't assign to an array parameter");
            cinit(c);           /* (an unset argument becomes a local scalar) */
        }
        if (c->t == T_ARR) fatal("can't assign to an array parameter");
        return c;
    case N_FIELD:
        lastfield = evalint(n->a);
        return setfieldref(lastfield);
    case N_INDEX:
        return elemref(n, 1);
    }
    fatal("assignment to a non-variable");
    return 0;
}

void lhook(Node *n)
{
    if (n->op == N_FIELD) fieldset(lastfield);
    else if (n->op == N_VAR && n->n == V_NF) nfset();
}

void lassign(Node *n, Cell *v)
{
    Cell *c = lref(n);
    cmove(c, v);
    lhook(n);
}

static void doassign(Node *n, Cell *r)
{
    Cell v, *c;
    Num x, y;
    if (n->n == '=') {
        eval(n->b, &v);
        c = lref(n->a);
        cmove(c, &v);
        lhook(n->a);
        if (r) ccopy(r, n->a->op == N_FIELD ? getfield(lastfield) : c);
        return;
    }
    evalnum(n->b, &y);
    c = n->a->op == N_VAR && n->a->n != V_NF ? &gvar[n->a->n] : 0;
    if (!c || c->t == T_ARR) c = lref(n->a);
    if (c->t == T_NUM) x = c->n;
    else cnum(c, &x);
    if (n->n == N_ADD && x.isint && y.isint) {
        int s = (int)((unsigned)x.i + (unsigned)y.i);
        if (((x.i ^ s) & (y.i ^ s)) >= 0) {
            x.i = s;
            goto store;
        }
    }
    switch (n->n) {
    case N_ADD: num_add(&x, &x, &y); break;
    case N_SUB: num_sub(&x, &x, &y); break;
    case N_MUL: num_mul(&x, &x, &y); break;
    case N_DIV: num_div(&x, &x, &y); break;
    case N_MOD: num_mod(&x, &x, &y); break;
    case N_POW: num_pow(&x, &x, &y); break;
    }
store:
    if (c->sv) cfree(c);
    c->t = T_NUM;
    c->n = x;
    c->nv = 1;
    if (n->a->op == N_FIELD || (n->a->op == N_VAR && n->a->n == V_NF)) lhook(n->a);
    if (r) { cinit(r); cset_num(r, &x); }
}

static void incdec(Node *n, Cell *r)
{
    Cell *c;
    Num old, one, x;
    if (n->a->op == N_VAR && n->a->n != V_NF) {
        c = &gvar[n->a->n];
        if (c->t == T_NUM && c->n.isint && c->n.i < 2147483647 && c->n.i > -2147483647) {
            old = c->n;
            c->n.i += n->op == N_PREINC || n->op == N_POSTINC ? 1 : -1;
            if (r) { cinit(r); cset_num(r, n->op == N_POSTINC || n->op == N_POSTDEC ? &old : &c->n); }
            return;
        }
    }
    c = lref(n->a);
    cnum(c, &old);
    num_int(&one, 1);
    if (n->op == N_PREINC || n->op == N_POSTINC) num_add(&x, &old, &one);
    else num_sub(&x, &old, &one);
    cset_num(c, &x);
    lhook(n->a);
    if (r) {
        cinit(r);
        cset_num(r, n->op == N_POSTINC || n->op == N_POSTDEC ? &old : &x);
    }
}

/* ---------------------------------------------------------------- comparisons and matching */
int cellcmp(Cell *a, Cell *b)
{
    Num x, y;
    int c;
    if (a->t == T_NUM && b->t == T_NUM && a->n.isint && b->n.isint)
        return a->n.i < b->n.i ? -1 : a->n.i > b->n.i;
    if (cisnum(a) && cisnum(b)) {
        cnum(a, &x);
        cnum(b, &y);
        return num_cmp(&x, &y);
    }
    c = strcmp(cstr(a), cstr(b));
    return c < 0 ? -1 : c > 0;
}

static int compare(Node *n)
{
    Cell t1, t2, *a, *b;
    int c;
    if (n->a->op == N_VAR && n->a->n != V_NF) a = &gvar[n->a->n], t1.sv = 0;
    else a = evalref(n->a, &t1);
    if (a != &t1 && n->b->pure != 1 && !ispure(n->b)) { ccopy(&t1, a); a = &t1; }
    if (n->b->op == N_NUM || n->b->op == N_STR) b = n->b->p, t2.sv = 0;
    else b = evalref(n->b, &t2);
    c = cellcmp(a, b);
    if (t1.sv) cfree(&t1);
    if (t2.sv) cfree(&t2);
    if (c == 2) return n->n == C_NE;    /* (a NaN) */
    switch (n->n) {
    case C_LT: return c < 0;
    case C_LE: return c <= 0;
    case C_GT: return c > 0;
    case C_GE: return c >= 0;
    case C_EQ: return c == 0;
    }
    return c != 0;
}

static int domatch(Node *n)
{
    Cell t1, t2, *c;
    Re *re;
    int r;
    c = evalref(n->a, &t1);
    if (c != &t1 && !ispure(n->b)) { ccopy(&t1, c); c = &t1; }
    if (n->b->op == N_REGEX) re = n->b->p;
    else {
        re = re_dyn(evalstr(n->b, &t2));
        cfree(&t2);
    }
    r = re_test(re, cstr(c));
    cfree(&t1);
    return r;
}

static int isin(Node *n)
{
    Array *a = arrayof(n->b);
    Cell t;
    char *k;
    int r;
    if (!n->a->next) {
        k = evalstr(n->a, &t);
        r = aget(a, k, 0) != 0;
        cfree(&t);
        return r;
    }
    k = joinsubs(n->a);
    r = aget(a, k, 0) != 0;
    sfree(k);
    return r;
}

/* ---------------------------------------------------------------- expressions */
Cell *evalref(Node *n, Cell *tmp)
{
    CINIT(tmp);
    switch (n->op) {
    case N_NUM: case N_STR:
        return n->p;
    case N_VAR:
        if (n->n == V_NF && !fldsplitdone) fldsplit();
        return &gvar[n->n];
    case N_LOCAL:
        return deref(&frame[n->n]);
    case N_FIELD:
        return getfield(evalint(n->a));
    case N_INDEX:
        return elemref(n, 1);
    }
    eval(n, tmp);
    return tmp;
}

char *evalstr(Node *n, Cell *tmp)
{
    return cstr(evalref(n, tmp));
}

void evalnum(Node *n, Num *r)
{
    Num a, b;
    Cell t, *c;
    STACKCHECK("expressions");
    switch (n->op) {
    case N_NUM: *r = ((Cell *)n->p)->n; return;
    case N_ADD: evalnum(n->a, &a); evalnum(n->b, &b); num_add(r, &a, &b); return;
    case N_SUB: evalnum(n->a, &a); evalnum(n->b, &b); num_sub(r, &a, &b); return;
    case N_MUL: evalnum(n->a, &a); evalnum(n->b, &b); num_mul(r, &a, &b); return;
    case N_DIV: evalnum(n->a, &a); evalnum(n->b, &b); num_div(r, &a, &b); return;
    case N_MOD: evalnum(n->a, &a); evalnum(n->b, &b); num_mod(r, &a, &b); return;
    case N_POW: evalnum(n->a, &a); evalnum(n->b, &b); num_pow(r, &a, &b); return;
    case N_NEG: evalnum(n->a, &a); num_neg(r, &a); return;
    case N_PLUS: evalnum(n->a, r); return;
    case N_CMP: case N_MATCH: case N_AND: case N_OR: case N_NOT: case N_IN: case N_REGEX:
        num_int(r, evalbool(n));
        return;
    case N_VAR:
        c = &gvar[n->n];
        if (c->t == T_NUM && n->n != V_NF) { *r = c->n; return; }
        c = evalref(n, &t);
        cnum(c, r);
        return;
    case N_LOCAL: case N_FIELD: case N_INDEX:
        c = evalref(n, &t);
        cnum(c, r);
        return;
    case N_CALL:
        call(n, &t);
        cnum(&t, r);
        cfree(&t);
        return;
    }
    eval(n, &t);
    cnum(&t, r);
    cfree(&t);
}

int evalbool(Node *n)
{
    Cell t, *c;
    int b;
    switch (n->op) {
    case N_AND: return evalbool(n->a) && evalbool(n->b);
    case N_OR: return evalbool(n->a) || evalbool(n->b);
    case N_NOT: return !evalbool(n->a);
    case N_CMP: return compare(n);
    case N_MATCH: return domatch(n) ^ n->n;
    case N_REGEX: return re_test(n->p, getrec0());
    case N_IN: return isin(n);
    case N_NUM: return num_true(&((Cell *)n->p)->n);
    }
    c = evalref(n, &t);
    b = cbool(c);
    cfree(&t);
    return b;
}

static void catinto(Node *n, Buf *b)
{
    Cell t;
    STACKCHECK("expressions");
    if (n->op == N_CAT) {
        catinto(n->a, b);
        catinto(n->b, b);
        return;
    }
    buf_adds(b, evalstr(n, &t));
    cfree(&t);
}

/* n's value into r (which holds nothing) */
void eval(Node *n, Cell *r)
{
    Cell t, *c;
    Num x;
    Buf b;
    STACKCHECK("expressions");
top:
    cinit(r);
    switch (n->op) {
    case N_NUM: case N_STR:
        *r = *(Cell *)n->p;
        return;
    case N_VAR: case N_LOCAL: case N_FIELD: case N_INDEX:
        c = evalref(n, &t);
        ccopy(r, c);
        return;
    case N_CMP: case N_MATCH: case N_AND: case N_OR: case N_NOT: case N_IN: case N_REGEX:
        cset_int(r, evalbool(n));
        return;
    case N_ADD: case N_SUB: case N_MUL: case N_DIV: case N_MOD: case N_POW: case N_NEG: case N_PLUS:
        evalnum(n, &x);
        cset_num(r, &x);
        return;
    case N_CAT:
        buf_init(&b);
        catinto(n, &b);
        cset_str(r, buf_take(&b));
        return;
    case N_ASSIGN:
        doassign(n, r);
        return;
    case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
        incdec(n, r);
        return;
    case N_COND:                        /* (no recursion: a function's return a ? b : f(x) goes deep) */
        n = evalbool(n->a) ? n->b : n->c;
        goto top;
    case N_CALL:
        call(n, r);
        return;
    case N_BUILTIN:
        builtin(n, r);
        return;
    case N_GETLINE:
        getline_(n, r);
        return;
    }
    fatal("internal: unknown node %d", n->op);
}

/* an expression whose value is not wanted */
static void evalvoid(Node *n)
{
    Cell t;
    switch (n->op) {
    case N_ASSIGN: doassign(n, 0); return;
    case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: incdec(n, 0); return;
    }
    eval(n, &t);
    cfree(&t);
}

/* ---------------------------------------------------------------- getline */
static void getline_(Node *n, Cell *r)
{
    Buf b;
    Cell t, v;
    char *name;
    int res;
    buf_init(&b);
    if (n->n == G_SIMPLE) {
        res = mainrec(n->a ? &b : 0);
    } else {
        name = sdup(evalstr(n->b, &t));
        cfree(&t);
        res = getlinefrom(name, n->n, &b);
        sfree(name);
        if (res > 0 && n->n == G_CMD) {
            Num x, one;
            cnum(&gvar[V_NR], &x);
            num_int(&one, 1);
            num_add(&x, &x, &one);
            cset_num(&gvar[V_NR], &x);
            nrec = num_toint(&x);
        }
        if (res > 0 && !n->a) setrecord(b.s, b.len);
    }
    if (res > 0 && n->a) {
        cinit(&v);
        cset_str(&v, sdupn(b.s, b.len));
        v.t = T_MAYBE;
        lassign(n->a, &v);
    }
    free(b.s);
    cset_int(r, res);
}

/* ---------------------------------------------------------------- functions */
static void call(Node *n, Cell *r)
{
    Func *f = n->p;
    Cell *loc, *save, *src;
    Node *a;
    int i, x, line = curline;
    if (n->n > f->nparam)
        fatal("function %s called with %d args, uses only %d", f->name, n->n, f->nparam);
    STACKCHECK("function calls");
    loc = emalloc((f->nparam + 1) * sizeof(Cell));
    for (i = 0, a = n->a; i < f->nparam; i++) {
        cinit(&loc[i]);
        if (!a) continue;
        if (a->op == N_VAR || a->op == N_LOCAL) {
            src = a->op == N_VAR ? &gvar[a->n] : deref(&frame[a->n]);
            if (src->t == T_ARR || src->t == T_UNINIT) {
                loc[i].t = T_REF;
                loc[i].p = src;
                a = a->next;
                continue;
            }
        }
        eval(a, &loc[i]);
        a = a->next;
    }
    save = frame;
    frame = loc;
    x = exec(f->body);
    frame = save;
    curline = line;
    for (i = 0; i < f->nparam; i++) {
        if (loc[i].t == T_ARR) freearray(loc[i].p);
        else cfree(&loc[i]);
    }
    free(loc);
    if (x == X_NEXT || x == X_NEXTFILE || x == X_EXIT) longjmp(ruleout, x);
    if (x == X_RETURN) {
        *r = retval;
        cinit(&retval);
    } else
        cinit(r);
}

/* ---------------------------------------------------------------- statements */
int exec(Node *s)
{
    int x, i, nk;
    char **keys;
    Array *a;
    Cell t, v;
    for (; s; s = s->next) {
    again:
        curline = s->line;
        switch (s->op) {
        case S_EXPR:
            switch (s->a->op) {
            case N_ASSIGN: doassign(s->a, 0); break;
            case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: incdec(s->a, 0); break;
            case N_CALL: call(s->a, &t); cfree(&t); break;
            default: evalvoid(s->a);
            }
            break;
        case S_PRINT: case S_PRINTF:
            doprint(s);
            break;
        case S_IF:
            if (!s->next) {             /* (the last statement: no recursion) */
                s = evalbool(s->a) ? s->b : s->c;
                if (!s) return 0;
                goto again;
            }
            if (evalbool(s->a)) x = exec(s->b);
            else x = s->c ? exec(s->c) : 0;
            if (x) return x;
            break;
        case S_WHILE:
            while (evalbool(s->a)) {
                x = exec(s->b);
                if (x == X_BREAK) break;
                if (x && x != X_CONTINUE) return x;
            }
            break;
        case S_DO:
            do {
                x = exec(s->b);
                if (x == X_BREAK) break;
                if (x && x != X_CONTINUE) return x;
            } while (evalbool(s->a));
            break;
        case S_FOR:
            if (s->a) exec(s->a);
            for (;;) {
                if (s->b && !(s->b->op == N_CMP ? compare(s->b) : evalbool(s->b))) break;
                x = exec(s->d);
                if (x == X_BREAK) break;
                if (x && x != X_CONTINUE) return x;
                if (s->c) {
                    if (s->c->op == S_EXPR && (s->c->a->op == N_POSTINC || s->c->a->op == N_PREINC)) incdec(s->c->a, 0);
                    else exec(s->c);
                }
            }
            break;
        case S_FORIN:
            a = arrayof(s->b);
            keys = akeys(a, &nk);
            x = 0;
            for (i = 0; i < nk; i++) {
                if (x == 0 || x == X_CONTINUE) {
                    if (!aget(a, keys[i], 0)) { sfree(keys[i]); continue; }     /* (deleted meanwhile) */
                    cinit(&v);
                    cset_str(&v, keys[i]);
                    lassign(s->a, &v);
                    x = exec(s->d);
                } else
                    sfree(keys[i]);
            }
            free(keys);
            if (x && x != X_BREAK && x != X_CONTINUE) return x;
            break;
        case S_BLOCK:
            if (!s->next) {             /* (the last statement: no recursion) */
                s = s->a;
                if (!s) return 0;
                goto again;
            }
            if ((x = exec(s->a)) != 0) return x;
            break;
        case S_NEXT: return X_NEXT;
        case S_NEXTFILE: return X_NEXTFILE;
        case S_EXIT:
            if (s->a) exitstatus = evalint(s->a);
            return X_EXIT;
        case S_RETURN:
            if (s->a) {
                eval(s->a, &t);
                cfree(&retval);
                retval = t;
            } else {
                cfree(&retval);
                cinit(&retval);
            }
            return X_RETURN;
        case S_BREAK: return X_BREAK;
        case S_CONTINUE: return X_CONTINUE;
        case S_DELETE:
            a = arrayof(s->a);
            if (!s->b) clearray(a);
            else if (!s->b->next) {
                adelete(a, evalstr(s->b, &t));
                cfree(&t);
            } else {
                char *k = joinsubs(s->b);
                adelete(a, k);
                sfree(k);
            }
            break;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- the rules */
static int matches(Rule *r)
{
    if (!r->pat1) return 1;
    if (!r->pat2) return evalbool(r->pat1);
    if (!r->inrange) {
        if (!evalbool(r->pat1)) return 0;
        r->inrange = !evalbool(r->pat2);
        return 1;
    }
    if (evalbool(r->pat2)) r->inrange = 0;
    return 1;
}

/* a rule: its pattern and its action (next and exit from a function land here) */
static int rule(Rule *r)
{
    int x;
    if ((x = setjmp(ruleout)) != 0) {
        frame = 0;
        return x;
    }
    if (!matches(r)) return 0;
    if (!r->action) {
        FILE *f = stdout;
        fputs(getrec0(), f);
        fputs(cstr(&gvar[V_ORS]), f);
        return 0;
    }
    return exec(r->action);
}

void run(void)
{
    Rule *r;
    int x = 0;
    for (r = begins; r; r = r->next)
        if ((x = rule(r)) == X_EXIT) break;
    if (x != X_EXIT && (rules || ends)) {
        while (x != X_EXIT && mainrec(0)) {
            for (r = rules; r; r = r->next) {
                x = rule(r);
                if (x == X_NEXT) break;
                if (x == X_NEXTFILE) { nextfile(); break; }
                if (x == X_EXIT) break;
            }
        }
    }
    for (r = ends; r; r = r->next)
        if (rule(r) == X_EXIT) break;
}
