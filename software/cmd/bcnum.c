/* bcnum.c -- bc's numbers: arbitrary-precision decimal arithmetic (bc.h).
 * The scales of the results are POSIX's: a + b and a - b keep the larger
 * scale; a * b keeps min(sa + sb, max(scale, sa, sb)); a / b has scale;
 * a % b is a - (a / b) * b; a ^ n is exact, then cut to
 * min(sa * n, max(scale, sa)) (or 1 / a^-n at scale); sqrt(a) has
 * max(scale, sa). Every result is truncated, never rounded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bc.h"

#define BIG (4 * MAXSCALE + 4)  /* "all the digits" as a scale */

/* Numbers of up to NCLASS * 8 digits are kept when freed, on a list for
 * each size (8 digits a class): malloc and free are slow on long lists. */
#define NCLASS 16
static Num *spare[NCLASS];

Num *n_new(int ilen, int scale)
{
    Num *n;
    int k;
    if (ilen < 0) ilen = 0;
    if (ilen + scale > 1000000) bcerror("number too long");
    k = (ilen + scale) / 8;
    if (k < NCLASS && spare[k]) {
        n = spare[k];
        spare[k] = *(Num **)(n + 1);
    } else {
        n = malloc(sizeof(Num) + (k < NCLASS ? 8 * k + 8 : ilen + scale + 1));
        if (!n) bcerror("out of memory");
    }
    n->cls = k < NCLASS ? k : -1;
    n->ref = 1;
    n->neg = 0;
    n->len = ilen + scale;
    n->scale = scale;
    n->d = (char *)(n + 1);
    memset(n->d, 0, n->len);
    return n;
}

Num *n_ref(Num *n)
{
    n->ref++;
    return n;
}

void n_free(Num *n)
{
    if (!n || --n->ref > 0) return;
    if (n->cls < 0) { free(n); return; }
    *(Num **)(n + 1) = spare[n->cls];
    spare[n->cls] = n;
}

Num *n_zero(void)
{
    return n_new(0, 0);
}

/* no leading zeros in the integer part; zero is not negative */
static Num *norm(Num *n)
{
    int i;
    while (ILEN(n) > 0 && n->d[0] == 0) { n->d++; n->len--; }
    for (i = 0; i < n->len; i++)
        if (n->d[i]) return n;
    n->neg = 0;
    return n;
}

Num *n_int(long v)
{
    char buf[16];
    int k = 0, i;
    unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
    Num *n;
    do { buf[k++] = u % 10; u /= 10; } while (u);
    n = n_new(k, 0);
    for (i = 0; i < k; i++) n->d[i] = buf[k - 1 - i];
    n->neg = v < 0;
    return norm(n);
}

long n_long(Num *n, int *ok)
{
    long v = 0;
    int i;
    *ok = 1;
    for (i = 0; i < ILEN(n); i++) {
        if (v > (2147483647L - n->d[i]) / 10) { *ok = 0; return 0; }
        v = v * 10 + n->d[i];
    }
    return n->neg ? -v : v;
}

int n_iszero(Num *n)
{
    int i;
    for (i = 0; i < n->len; i++)
        if (n->d[i]) return 0;
    return 1;
}

static int ucmp(Num *a, Num *b)
{
    int k, n = a->len < b->len ? a->len : b->len;
    if (ILEN(a) != ILEN(b)) return ILEN(a) > ILEN(b) ? 1 : -1;
    for (k = 0; k < n; k++)             /* (the digits line up) */
        if (a->d[k] != b->d[k]) return a->d[k] > b->d[k] ? 1 : -1;
    for (; k < a->len; k++) if (a->d[k]) return 1;
    for (; k < b->len; k++) if (b->d[k]) return -1;
    return 0;
}

int n_cmp(Num *a, Num *b)
{
    int za = n_iszero(a), zb = n_iszero(b);
    if (za && zb) return 0;
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    return a->neg ? -ucmp(a, b) : ucmp(a, b);
}

Num *n_neg(Num *a)
{
    Num *r = n_new(ILEN(a), a->scale);
    memcpy(r->d, a->d, a->len);
    r->neg = !a->neg;
    return norm(r);
}

Num *n_trunc(Num *a, int scale)
{
    Num *r;
    if (a->scale <= scale) return n_ref(a);
    r = n_new(ILEN(a), scale);
    memcpy(r->d, a->d, r->len);
    r->neg = a->neg;
    return norm(r);
}

/* r = |a| (aligned for b too), then |b| added or taken away */
static Num *uaddsub(Num *a, Num *b, int sub)
{
    int s = a->scale > b->scale ? a->scale : b->scale;
    int il = (ILEN(a) > ILEN(b) ? ILEN(a) : ILEN(b)) + 1, k, c = 0, v, o;
    Num *r = n_new(il, s);
    char *d;
    memcpy(r->d + il - ILEN(a), a->d, a->len);
    o = il - ILEN(b);
    d = r->d + o;
    for (k = b->len - 1; k >= 0; k--) {
        if (sub) {
            v = d[k] - b->d[k] - c;
            c = v < 0;
            d[k] = c ? v + 10 : v;
        } else {
            v = d[k] + b->d[k] + c;
            c = v >= 10;
            d[k] = c ? v - 10 : v;
        }
    }
    for (k = o - 1; c && k >= 0; k--) {
        if (sub) { c = r->d[k] == 0; r->d[k] = c ? 9 : r->d[k] - 1; }
        else { c = r->d[k] == 9; r->d[k] = c ? 0 : r->d[k] + 1; }
    }
    return r;
}

Num *n_add(Num *a, Num *b)
{
    Num *r;
    if (a->neg == b->neg) {
        r = uaddsub(a, b, 0);
        r->neg = a->neg;
    } else if (ucmp(a, b) >= 0) {
        r = uaddsub(a, b, 1);
        r->neg = a->neg;
    } else {
        r = uaddsub(b, a, 1);
        r->neg = b->neg;
    }
    return norm(r);
}

Num *n_sub(Num *a, Num *b)
{
    Num t;
    t = *b;
    t.neg = !b->neg;
    return n_add(a, &t);
}

/* working space for n_mul and n_div (neither calls the other) */
static char *scratch(int n)
{
    static char *buf;
    static int cap;
    if (n > cap) {
        free(buf);
        cap = n + 256;
        if (!(buf = malloc(cap))) { cap = 0; bcerror("out of memory"); }
    }
    return buf;
}

/* n digits (the most significant first) as limbs of 4, base 10000, the
 * least significant first; and back */
static int tolimbs(char *d, int n, int *l)
{
    int k = 0, r = n & 3;
    char *e = d + n - 4;
    for (; e >= d; e -= 4)
        l[k++] = ((e[0] * 10 + e[1]) * 10 + e[2]) * 10 + e[3];
    if (r) {
        int v = 0;
        for (e = d; e < d + r; e++) v = v * 10 + *e;
        l[k++] = v;
    }
    return k;
}

static void fromlimbs(int *l, char *d, int n)
{
    int v, h, t;
    char *e = d + n - 4;
    for (; e >= d; e -= 4) {
        v = *l++;
        h = v / 100;
        v -= h * 100;
        t = v / 10;
        e[3] = v - t * 10;
        e[2] = t;
        t = h / 10;
        e[1] = h - t * 10;
        e[0] = t;
    }
    for (v = *l, e += 3; e >= d; e--) {
        t = v / 10;
        *e = v - t * 10;
        v = t;
    }
}

static void carry(int *col, int n)
{
    int k, v, c = 0;
    for (k = 0; k < n; k++) {
        v = col[k] + c;
        c = v / 10000;
        col[k] = v - c * 10000;
    }
}

/* in base 10000: a row of 16 adds less than 16 * 9999^2 to a column */
Num *n_mul(Num *a, Num *b, int scale)
{
    int full = a->scale + b->scale, rs, i, j, x, na, nb, nc;
    int *col, *la, *lb, *cp;
    Num *r;
    rs = a->scale > b->scale ? a->scale : b->scale;
    if (scale > rs) rs = scale;
    if (full < rs) rs = full;
    na = (a->len + 3) / 4;
    nb = (b->len + 3) / 4;
    nc = na + nb + 1;
    col = (int *)scratch((nc + na + nb) * sizeof(int));
    la = col + nc;
    lb = la + na;
    memset(col, 0, nc * sizeof(int));
    tolimbs(a->d, a->len, la);
    tolimbs(b->d, b->len, lb);
    for (i = 0; i < na; i++) {
        cp = col + i;
        if ((x = la[i]) != 0)
            for (j = 0; j < nb; j++) cp[j] += x * lb[j];
        if ((i & 15) == 15) carry(col, nc);
    }
    carry(col, nc);
    r = n_new(a->len + b->len - full, full);
    fromlimbs(col, r->d, r->len);
    r->neg = a->neg ^ b->neg;
    r->len -= full - rs;
    r->scale = rs;
    return norm(r);
}

/* the number with these nd digits (an integer) divided by 10^scale */
static Num *scaled(char *d, int nd, int scale)
{
    Num *r = n_new(nd - scale, scale);
    if (nd > 0) memcpy(r->d + r->len - nd, d, nd);
    return norm(r);
}

/* q = u / v in base 10000 (Knuth's algorithm D): u has nu limbs and room
 * for one more, v has n >= 2; u and v are changed */
#define B 10000
static void limbdiv(int *u, int nu, int *v, int n, int *q)
{
    int f = B / (v[n - 1] + 1), i, j, c, t, p, k, qh, rh;
    for (c = 0, i = 0; i < nu; i++) { t = u[i] * f + c; c = t / B; u[i] = t - c * B; }
    u[nu] = c;
    for (c = 0, i = 0; i < n; i++) { t = v[i] * f + c; c = t / B; v[i] = t - c * B; }
    for (j = nu - n; j >= 0; j--) {
        t = u[j + n] * B + u[j + n - 1];
        qh = t / v[n - 1];
        rh = t - qh * v[n - 1];
        while (qh >= B || qh * v[n - 2] > rh * B + u[j + n - 2]) {
            qh--;
            rh += v[n - 1];
            if (rh >= B) break;
        }
        for (k = 0, i = 0; i < n; i++) {        /* u -= qh * v */
            p = qh * v[i];
            t = u[i + j] - k - p % B;
            c = 0;
            if (t < 0) { c = (B - 1 - t) / B; t += c * B; }
            u[i + j] = t;
            k = p / B + c;
        }
        t = u[j + n] - k;
        if (t < 0) {                            /* one too many: add v back */
            qh--;
            for (c = 0, i = 0; i < n; i++) {
                p = u[i + j] + v[i] + c;
                c = p >= B;
                u[i + j] = p - c * B;
            }
            t += c;
        }
        u[j + n] = t;
        q[j] = qh;
    }
}

Num *n_div(Num *a, Num *b, int scale)
{
    int k = scale + b->scale - a->scale, ln = a->len + k, ld, i, j, q, nu, nv;
    char *nd, *dd;
    int *u, *v, *ql;
    Num *r;
    if (n_iszero(b)) return 0;
    if (ln <= 0) return n_new(0, scale);
    dd = b->d;
    ld = b->len;
    while (*dd == 0) { dd++; ld--; }
    nd = scratch(ln + 12 * (ln + ld + 8));
    memset(nd, 0, ln);
    memcpy(nd, a->d, a->len < ln ? a->len : ln);
    if (ld <= 4) {                              /* a short division */
        int dv = 0, rem = 0;
        for (j = 0; j < ld; j++) dv = dv * 10 + dd[j];
        for (i = 0; i < ln; i++) {
            rem = rem * 10 + nd[i];
            q = rem / dv;
            rem -= q * dv;
            nd[i] = q;
        }
    } else {
        u = (int *)(nd + ((ln + 7) & ~7));
        v = u + (ln + 3) / 4 + 2;
        ql = v + (ld + 3) / 4 + 2;
        nu = tolimbs(nd, ln, u);
        nv = tolimbs(dd, ld, v);
        memset(ql, 0, (nu + 1) * sizeof(int));
        if (nu >= nv) limbdiv(u, nu, v, nv, ql);
        fromlimbs(ql, nd, ln);
    }
    r = scaled(nd, ln, scale);
    r->neg = a->neg ^ b->neg;
    return norm(r);
}

Num *n_mod(Num *a, Num *b, int scale)
{
    Num *q = n_div(a, b, scale), *p, *r;
    if (!q) return 0;
    p = n_mul(q, b, BIG);
    r = n_sub(a, p);
    n_free(q);
    n_free(p);
    return r;
}

Num *n_pow(Num *a, Num *b, int scale)
{
    long e;
    int ok, neg, rs;
    Num *r, *p, *t, *one;
    if (b->scale > 0) {                        /* a fraction is dropped */
        int i;
        for (i = ILEN(b); i < b->len; i++)
            if (b->d[i]) { bcwarn("non-zero scale in exponent"); break; }
    }
    e = n_long(b, &ok);
    if (!ok) bcerror("exponent too large");
    if (e == 0) return n_int(1);
    neg = e < 0;
    if (neg) e = -e;
    if (a->len > 0 && e > 200000L / a->len && !(a->len == 1 && a->d[0] == 1 && a->scale == 0))
        bcerror("exponent too large");
    r = n_int(1);
    p = n_ref(a);
    for (;;) {
        if (e & 1) { t = n_mul(r, p, BIG); n_free(r); r = t; }
        e >>= 1;
        if (!e) break;
        t = n_mul(p, p, BIG); n_free(p); p = t;
    }
    n_free(p);
    if (neg) {
        one = n_int(1);
        t = n_div(one, r, scale);
        n_free(one);
        n_free(r);
        if (!t) bcerror("divide by zero");
        return t;
    }
    rs = a->scale > scale ? a->scale : scale;
    if (r->scale < rs) rs = r->scale;
    t = n_trunc(r, rs);
    n_free(r);
    return t;
}

/* floor(sqrt(x)) of an integer, by Newton's method from above */
static Num *isqrt(Num *x)
{
    Num *g, *y, *q, *s, *two = n_int(2);
    int nd = ILEN(x);
    int t = nd & 1 ? x->d[0] : x->d[0] * 10 + x->d[1], h = 1;
    while (h * h <= t) h++;                     /* h > sqrt of the top digits */
    g = n_new((nd + 1) / 2 + (h == 10), 0);
    if (h == 10) g->d[0] = 1; else g->d[0] = h;
    for (;;) {
        q = n_div(x, g, 0);
        s = n_add(g, q);
        y = n_div(s, two, 0);
        n_free(q);
        n_free(s);
        if (n_cmp(y, g) >= 0) { n_free(y); break; }
        n_free(g);
        g = y;
    }
    n_free(two);
    return g;
}

Num *n_sqrt(Num *a, int scale)
{
    int rs = a->scale > scale ? a->scale : scale, k;
    Num *x, *g, *r;
    if (n_iszero(a)) return n_new(0, rs);
    if (a->neg) return 0;
    k = 2 * rs - a->scale;                     /* the integer a * 10^(2 rs) */
    x = n_new(a->len + k, 0);
    memcpy(x->d, a->d, a->len);
    norm(x);
    g = isqrt(x);
    r = scaled(g->d, g->len, rs);
    n_free(x);
    n_free(g);
    return r;
}

int n_length(Num *n)                    /* the significant digits */
{
    int i = 0;
    if (ILEN(n) > 0) return n->len;
    while (i < n->len && n->d[i] == 0) i++;
    return i < n->len ? n->len - i : 1;
}

static int digval(int c)
{
    return c >= 'A' ? c - 'A' + 10 : c - '0';
}

/* a constant of the program, digits 0-9 A-F, in base */
Num *n_parse(char *s, int base)
{
    char *dot = strchr(s, '.');
    int il = dot ? dot - s : strlen(s), fl = dot ? strlen(dot + 1) : 0, i, v;
    Num *r, *t, *u, *nb, *f, *m;
    if (strlen(s) == 1) return n_int(digval(*s));
    if (base == 10) {
        r = n_new(il, fl);
        for (i = 0; i < il; i++) r->d[i] = (v = digval(s[i])) > 9 ? 9 : v;
        for (i = 0; i < fl; i++) r->d[il + i] = (v = digval(dot[1 + i])) > 9 ? 9 : v;
        return norm(r);
    }
    nb = n_int(base);
    r = n_zero();
    for (i = 0; i < il; i++) {
        v = digval(s[i]);
        if (v >= base) v = base - 1;
        t = n_mul(r, nb, 0);
        u = n_int(v);
        n_free(r);
        r = n_add(t, u);
        n_free(t);
        n_free(u);
    }
    if (fl) {
        f = n_zero();
        m = n_int(1);
        for (i = 0; i < fl; i++) {
            v = digval(dot[1 + i]);
            if (v >= base) v = base - 1;
            t = n_mul(f, nb, 0);
            u = n_int(v);
            n_free(f);
            f = n_add(t, u);
            n_free(t);
            n_free(u);
            t = n_mul(m, nb, 0);
            n_free(m);
            m = t;
        }
        t = n_div(f, m, fl);
        u = n_add(r, t);
        n_free(r);
        n_free(t);
        n_free(f);
        n_free(m);
        r = u;
    }
    n_free(nb);
    return r;
}

/* a digit of a base over 16: a space and decimal digits, as wide as base - 1 */
static void bigdigit(long d, int width, int space)
{
    char buf[16];
    if (space) outc(' ');
    sprintf(buf, "%0*ld", width, d);
    for (width = 0; buf[width]; width++) outc(buf[width]);
}

void n_print(Num *n, int base)
{
    int i, k, cap, width, ok, space;
    long *stk, d;
    Num *ip, *fp, *nb, *q, *t, *u, *mag;
    if (n_iszero(n)) { outc('0'); return; }
    if (n->neg) outc('-');
    if (base == 10) {
        for (i = 0; i < n->len; i++) {
            if (i == ILEN(n)) outc('.');
            outc('0' + n->d[i]);
        }
        return;
    }
    mag = n_new(ILEN(n), n->scale);
    memcpy(mag->d, n->d, n->len);
    nb = n_int(base);
    ip = n_trunc(mag, 0);
    fp = n_sub(mag, ip);
    for (width = 0, d = base - 1; d; d /= 10) width++;
    cap = 16;
    k = 0;
    stk = malloc(cap * sizeof(long));
    while (stk && !n_iszero(ip)) {      /* the integer part, last digit first */
        q = n_div(ip, nb, 0);
        t = n_mul(q, nb, 0);
        u = n_sub(ip, t);
        if (k == cap) stk = realloc(stk, (cap *= 2) * sizeof(long));
        if (stk) stk[k++] = n_long(u, &ok);
        n_free(t); n_free(u); n_free(ip);
        ip = q;
    }
    if (!stk) bcerror("out of memory");
    while (k > 0) {
        d = stk[--k];
        if (base <= 16) outc("0123456789ABCDEF"[d]);
        else bigdigit(d, width, 1);
    }
    free(stk);
    if (n->scale > 0) {                 /* the fraction: digits while base^i fits */
        outc('.');
        space = 0;
        t = n_int(1);
        while (ILEN(t) <= n->scale) {
            u = n_mul(fp, nb, n->scale);
            n_free(fp);
            q = n_trunc(u, 0);
            d = n_long(q, &ok);
            fp = n_sub(u, q);
            n_free(u); n_free(q);
            if (base <= 16) outc("0123456789ABCDEF"[d]);
            else { bigdigit(d, width, space); space = 1; }
            u = n_mul(t, nb, 0);
            n_free(t);
            t = u;
        }
        n_free(t);
    }
    n_free(ip); n_free(fp); n_free(nb); n_free(mag);
}
