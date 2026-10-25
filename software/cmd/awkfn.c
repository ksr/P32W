/* awkfn.c -- awk's built-in functions, print and printf (awk.h). sub and
 * gsub follow the one-true-awk for backslashes in the replacement (& is
 * the match, \& a literal &, \\& a backslash and the match); an empty
 * match next to the previous match is not replaced. substr rounds its
 * arguments as POSIX says. The maths functions are softfp's; rand is a
 * 32-bit linear congruential generator giving 53-bit fractions. */
#include "awk.h"
#include <time.h>

static const fp64 FP_PI = { 0x54442D18, 0x400921FB };
static const fp64 FP_PI2 = { 0x54442D18, 0x3FF921FB };

/* ---------------------------------------------------------------- printf */
/* the formatted text of args (the format first) into out */
void dosprintf(Buf *out, Node *args, const char *what)
{
    Cell t, *c;
    Num x;
    char *fmt, *p, *spec, flags[8], *s;
    int nfl, width, prec, conv, len, i;
    Node *a = args->next;

    fmt = sdup(evalstr(args, &t));
    cfree(&t);
    for (p = fmt; *p; ) {
        if (*p != '%') {
            for (s = p; *s && *s != '%'; s++) ;
            buf_add(out, p, s - p);
            p = s;
            continue;
        }
        if (p[1] == '%') { buf_addc(out, '%'); p += 2; continue; }
        spec = p++;
        nfl = 0;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') { if (nfl < 7) flags[nfl++] = *p; p++; }
        flags[nfl] = 0;
        width = 0;
        if (*p == '*') {
            p++;
            if (!a) fatal("not enough args in %s(%s)", what, fmt);
            width = (evalnum(a, &x), num_toint(&x));
            a = a->next;
            if (width < 0) { width = -width; if (nfl < 7) { flags[nfl++] = '-'; flags[nfl] = 0; } }
        } else
            while (isdigit((unsigned char)*p)) { if (width < 1000000) width = width * 10 + *p - '0'; p++; }
        prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                p++;
                if (!a) fatal("not enough args in %s(%s)", what, fmt);
                evalnum(a, &x);
                prec = num_toint(&x);
                a = a->next;
            } else
                while (isdigit((unsigned char)*p)) { if (prec < 1000000) prec = prec * 10 + *p - '0'; p++; }
        }
        while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'q' || *p == 'j' || *p == 'z' || *p == 't') p++;
        conv = *p;
        if (!conv || !strchr("cdiouxXeEfFgGs", conv)) {      /* (not a conversion: as it is) */
            buf_add(out, spec, p - spec + (*p != 0));
            if (*p) p++;
            continue;
        }
        p++;
        if (!a) fatal("not enough args in %s(%s)", what, fmt);
        if (width > 100000) width = 100000;
        if (conv == 's' || (conv == 'c' && (c = evalref(a, &t), !cisnum(c)))) {
            if (conv == 's') c = evalref(a, &t);
            s = cstr(c);
            len = strlen(s);
            if (conv == 'c') len = len ? 1 : 0;
            else if (prec >= 0 && prec < len) len = prec;
            if (!strchr(flags, '-')) for (i = len; i < width; i++) buf_addc(out, ' ');
            if (len) buf_add(out, s, len);
            if (strchr(flags, '-')) for (i = len; i < width; i++) buf_addc(out, ' ');
            cfree(&t);
        } else if (conv == 'c') {
            cnum(c, &x);
            cfree(&t);
            fmt_number(out, 'c', flags, width, prec, &x);
        } else {
            evalnum(a, &x);
            fmt_number(out, conv, flags, width, prec, &x);
        }
        a = a->next;
    }
    sfree(fmt);
}

/* print and printf, with their redirections */
void doprint(Node *s)
{
    FILE *f = stdout;
    Cell t, *c;
    Node *a;
    Buf b;
    char *name;
    if (s->b) {
        name = sdup(evalstr(s->b, &t));
        cfree(&t);
        f = outstream(name, s->n);
        sfree(name);
    }
    if (s->op == S_PRINTF) {
        buf_init(&b);
        dosprintf(&b, s->a, "printf");
        if (b.len) fwrite(b.s, 1, b.len, f);
        free(b.s);
    } else {
        if (!s->a) fputs(getrec0(), f);
        for (a = s->a; a; a = a->next) {
            c = evalref(a, &t);
            fputs(costr(c), f);
            cfree(&t);
            if (a->next) fputs(cstr(&gvar[V_OFS]), f);
        }
        fputs(cstr(&gvar[V_ORS]), f);
    }
    if (f != stdout && ferror(f)) fatal("write error");
}

/* ---------------------------------------------------------------- sub and gsub */
/* the replacement for one match m[0..mlen) */
static void addrepl(Buf *o, const char *repl, const char *m, int mlen)
{
    const char *p = repl;
    while (*p) {
        if (*p == '\\') {
            if (p[1] == '\\' && p[2] == '\\' && p[3] == '&') { buf_add(o, "\\&", 2); p += 4; }
            else if (p[1] == '\\' && p[2] == '&') { buf_addc(o, '\\'); p += 2; }
            else if (p[1] == '\\') { buf_add(o, "\\\\", 2); p += 2; }
            else if (p[1] == '&') { buf_addc(o, '&'); p += 2; }
            else buf_addc(o, *p++);
        } else if (*p == '&') {
            buf_add(o, m, mlen);
            p++;
        } else
            buf_addc(o, *p++);
    }
}

static void settarget(Node *tgt, Cell *v)
{
    Cell *c;
    if (tgt) { lassign(tgt, v); return; }
    c = setfieldref(0);
    cmove(c, v);
    fieldset(0);
}

static int dosub(Node *n, int global)
{
    Node *ren = n->a, *repn = ren->next, *tgt = repn->next;
    Cell t, v;
    char *pat = 0, *repl, *s;
    Re *re;
    Buf o;
    int len, pos, ms, me, last = -1, count = 0;
    if (ren->op != N_REGEX) { pat = sdup(evalstr(ren, &t)); cfree(&t); }
    repl = sdup(evalstr(repn, &t));
    cfree(&t);
    if (tgt) {
        s = sdup(evalstr(tgt, &t));
        cfree(&t);
    } else
        s = sdup(getrec0());
    re = pat ? re_dyn(pat) : ren->p;
    len = strlen(s);
    buf_init(&o);
    buf_add(&o, "", 0);
    pos = 0;
    while (pos <= len && re_exec(re, s, len, pos, &ms, &me)) {
        if (me == ms && ms == last) {   /* (empty, just after the last match) */
            if (ms < len) buf_addc(&o, s[ms]);
            pos = ms + 1;
            continue;
        }
        buf_add(&o, s + pos, ms - pos);
        addrepl(&o, repl, s + ms, me - ms);
        count++;
        if (me == ms) {
            if (ms < len) buf_addc(&o, s[ms]);
            pos = ms + 1;
        } else
            pos = me;
        last = me;
        if (!global) break;
    }
    if (count) {
        if (pos < len) buf_add(&o, s + pos, len - pos);
        cinit(&v);
        cset_str(&v, buf_take(&o));
        settarget(tgt, &v);
    }
    free(o.s);
    sfree(s);
    sfree(repl);
    sfree(pat);
    return count;
}

/* ---------------------------------------------------------------- rand */
static unsigned rstate = 1, rseed = 1;

static unsigned rnext(void)
{
    rstate = rstate * 1103515245u + 12345u;
    return rstate;
}

static void dosrand(unsigned seed)
{
    rseed = seed;
    rstate = seed;
    rnext();
}

/* ---------------------------------------------------------------- the built-ins */
void builtin(Node *n, Cell *r)
{
    Node *a = n->a;
    Cell t, t2, *c;
    Num x, y;
    fp64 f, g;
    char *s, *p;
    Buf b;
    int len, m, k, ms, me;
    Array *arr;
    Re *re;

    switch (n->n) {
    case B_LENGTH:
        if (!a) { cset_int(r, strlen(getrec0())); return; }
        c = evalref(a, &t);
        if (c->t == T_ARR) cset_int(r, ((Array *)c->p)->n);
        else cset_int(r, strlen(cstr(c)));
        cfree(&t);
        return;
    case B_SUBSTR: {
        fp64 fm, fn, lim, one;
        Num xm;
        int start, end;
        s = sdup(evalstr(a, &t));
        cfree(&t);
        len = strlen(s);
        evalnum(a->next, &xm);
        num_tofp(&xm, &fm);
        if (fp_isnan(&fm)) fm = FP_ZERO;
        fp_round(&fm, &fm);
        if (a->next->next) {            /* the end: start + length, rounded */
            evalnum(a->next->next, &x);
            num_tofp(&x, &fn);
            if (fp_isnan(&fn)) fn = FP_ZERO;
            fp_round(&fn, &fn);
            fp_add(&fn, &fm, &fn);
        } else
            fp_fromint(&fn, len + 1);
        fp_fromint(&one, 1);
        if (fp_cmp(&fm, &one) < 0) fm = one;
        fp_fromint(&lim, len + 1);
        if (fp_cmp(&fn, &lim) > 0) fn = lim;
        start = fp_toint(&fm, 0);
        end = fp_cmp(&fn, &fm) <= 0 ? start : fp_toint(&fn, 0);
        if (start > len + 1) start = end = len + 1;
        cset_str(r, sdupn(s + start - 1, end - start));
        sfree(s);
        return;
    }
    case B_INDEX:
        s = sdup(evalstr(a, &t));
        cfree(&t);
        p = evalstr(a->next, &t2);
        if (!*p) m = 0;
        else {
            char *q = strstr(s, p);
            m = q ? q - s + 1 : 0;
        }
        cfree(&t2);
        sfree(s);
        cset_int(r, m);
        return;
    case B_SPLIT:
        s = sdup(evalstr(a, &t));
        cfree(&t);
        arr = carray(evalref(a->next, &t2));
        if (!a->next->next) k = splitstr(s, arr, cstr(&gvar[V_FS]), 0);
        else if (a->next->next->op == N_REGEX) k = splitstr(s, arr, "", a->next->next->p);
        else {
            p = sdup(evalstr(a->next->next, &t));
            cfree(&t);
            k = splitstr(s, arr, p, 0);
            sfree(p);
        }
        sfree(s);
        cset_int(r, k);
        return;
    case B_SUB: case B_GSUB:
        cset_int(r, dosub(n, n->n == B_GSUB));
        return;
    case B_MATCH:
        s = sdup(evalstr(a, &t));
        cfree(&t);
        if (a->next->op == N_REGEX) re = a->next->p;
        else {
            p = sdup(evalstr(a->next, &t));
            cfree(&t);
            re = re_dyn(p);
            sfree(p);
        }
        if (re_exec(re, s, strlen(s), 0, &ms, &me)) {
            cset_int(&gvar[V_RSTART], ms + 1);
            cset_int(&gvar[V_RLENGTH], me - ms);
        } else {
            cset_int(&gvar[V_RSTART], 0);
            cset_int(&gvar[V_RLENGTH], -1);
        }
        sfree(s);
        cset_int(r, gvar[V_RSTART].n.i);
        return;
    case B_SPRINTF:
        buf_init(&b);
        dosprintf(&b, a, "sprintf");
        cset_str(r, buf_take(&b));
        return;
    case B_TOLOWER: case B_TOUPPER:
        s = sdup(evalstr(a, &t));
        cfree(&t);
        for (p = s; *p; p++) *p = n->n == B_TOLOWER ? tolower((unsigned char)*p) : toupper((unsigned char)*p);
        cset_str(r, s);
        return;
    case B_INT:
        evalnum(a, &x);
        num_trunc(&x, &x);
        cset_num(r, &x);
        return;
    case B_SQRT: case B_EXP: case B_LOG: case B_SIN: case B_COS:
        evalnum(a, &x);
        num_tofp(&x, &f);
        switch (n->n) {
        case B_SQRT: fp_sqrt(&f, &f); break;
        case B_EXP: fp_exp(&f, &f); break;
        case B_LOG: fp_log(&f, &f); break;
        case B_SIN: fp_sin(&f, &f); break;
        case B_COS: fp_cos(&f, &f); break;
        }
        num_fp(&x, &f);
        cset_num(r, &x);
        return;
    case B_ATAN2:                       /* atan(y/x), in the right quadrant */
        evalnum(a, &y);
        evalnum(a->next, &x);
        num_tofp(&y, &g);
        num_tofp(&x, &f);
        if (fp_isnan(&f) || fp_isnan(&g)) fp_add(&f, &f, &g);
        else if (fp_iszero(&f)) {
            if (!fp_iszero(&g)) f = FP_PI2;
            else if (f.hi >> 31) f = FP_PI;
            else f = FP_ZERO;
            if (g.hi >> 31) fp_neg(&f, &f);
        } else {
            int xneg = f.hi >> 31, yneg = g.hi >> 31;
            fp_div(&g, &g, &f);
            fp_atan(&f, &g);
            if (xneg) {
                if (yneg) fp_sub(&f, &f, &FP_PI);
                else fp_add(&f, &f, &FP_PI);
            }
        }
        num_fp(&x, &f);
        cset_num(r, &x);
        return;
    case B_RAND: {
        unsigned hi27 = rnext() >> 5, lo26 = rnext() >> 6;
        fp_fromuint(&f, hi27);
        fp_ldexp(&f, &f, 26);
        fp_fromuint(&g, lo26);
        fp_add(&f, &f, &g);
        fp_ldexp(&f, &f, -53);
        num_fp(&x, &f);
        cset_num(r, &x);
        return;
    }
    case B_SRAND:
        m = rseed;
        if (a) { evalnum(a, &x); dosrand((unsigned)num_toint(&x)); }
        else dosrand((unsigned)time(0));
        cset_int(r, m);
        return;
    case B_CLOSE:
        s = sdup(evalstr(a, &t));
        cfree(&t);
        cset_int(r, closestream(s));
        sfree(s);
        return;
    case B_SYSTEM:
        s = sdup(evalstr(a, &t));
        cfree(&t);
        cset_int(r, runsystem(s));
        sfree(s);
        return;
    case B_FFLUSH:
        if (!a) { flushall(); cset_int(r, 0); return; }
        s = sdup(evalstr(a, &t));
        cfree(&t);
        if (!*s) { flushall(); k = 0; }
        else k = flushstream(s);
        cset_int(r, k);
        sfree(s);
        return;
    }
    fatal("internal: unknown built-in %d", n->n);
}
