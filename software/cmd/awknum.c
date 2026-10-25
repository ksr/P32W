/* awknum.c -- awk's numbers (awk.h): whole numbers that fit are kept as
 * 32-bit ints and the arithmetic tries them first (overflow checked); the
 * rest are IEEE doubles from softfp (fp64). Text to number (with awk's
 * "looks numeric" test), number to text (CONVFMT, OFMT; whole numbers in
 * full, as %.30g would) and printf's numeric conversions %d %i %o %u %x %X
 * %c %e %E %f %F %g %G, from the exact decimal expansion of the double
 * (as many digits as asked for, rounded half to even, like C's printf). */
#include "awk.h"

const fp64 FP_ZERO = { 0, 0 };
const fp64 FP_ONE = { 0, 0x3FF00000 };

/* ---------------------------------------------------------------- basics */
void num_int(Num *n, int i)
{
    n->isint = 1;
    n->i = i;
}

void num_fp(Num *n, const fp64 *f)
{
    int e = (int)((f->hi >> 20) & 0x7FF) - 1023, sh;
    unsigned m;
    if (e < 0) {                        /* |f| < 1: only zeros are ints */
        if (!((f->hi & 0x7FFFFFFF) | f->lo)) { n->isint = 1; n->i = 0; return; }
    } else if (e < 31) {                /* (the bits below the point must be 0) */
        sh = 52 - e;
        if (sh >= 32 ? !f->lo && !(f->hi & ((1u << (sh - 32)) - 1)) : !(f->lo & ((1u << sh) - 1))) {
            m = ((f->hi & 0xFFFFF) | 0x100000);
            m = sh >= 32 ? m >> (sh - 32) : (m << (32 - sh)) | (f->lo >> sh);
            n->isint = 1;
            n->i = f->hi >> 31 ? -(int)m : (int)m;
            return;
        }
    } else if (e == 31 && f->hi == 0xC1E00000 && !f->lo) {  /* -2^31 */
        n->isint = 1;
        n->i = -2147483647 - 1;
        return;
    }
    n->isint = 0;
    n->f = *f;
}

void num_tofp(const Num *n, fp64 *f)
{
    if (n->isint) fp_fromint(f, n->i);
    else *f = n->f;
}

static int isblank_(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static int ncaseeq(const char *s, const char *w)
{
    while (*w) { if (tolower((unsigned char)*s) != *w) return 0; s++; w++; }
    return 1;
}

/* the number at the start of s (after blanks); with whole, 1 only if
   nothing but blanks follows it, else 1 if there was a number at all */
/* m / 10^k (m > 0 and below 10^9, 1 <= k <= 22), correctly rounded: the
   quotient's 56-58 top bits by long division in 16-bit pieces, the rest
   kept as a sticky bit. (softfp's fp_parse is exact too, but its 64-bit
   division is slow, and decimals like 4.25 are what awk reads most.) */
static void decimal(fp64 *r, unsigned m, int k)
{
    static const unsigned p10[4] = { 1, 10, 100, 1000 };
    unsigned limb[12], rem, cur, d, a, x0, x1, sum, hi, lo, mh, ml, rb, half;
    int bl = 0, s, ws, bs, nl, i, j, sticky = 0, qb, sh, E;
    for (a = m; a; a >>= 1) bl++;
    s = 57 + k * 3322 / 1000 + 1 - bl;  /* (m << s) / 10^k has 56 to 58 bits */
    ws = s / 16;
    bs = s % 16;
    for (i = 0; i < 12; i++) limb[i] = 0;
    x0 = (m & 0xFFFF) << bs;
    x1 = (m >> 16) << bs;
    limb[ws] = x0 & 0xFFFF;
    sum = (x0 >> 16) + (x1 & 0xFFFF);
    limb[ws + 1] = sum & 0xFFFF;
    limb[ws + 2] = (x1 >> 16) + (sum >> 16);
    nl = ws + 3;
    for (i = k; i > 0; i -= 4) {
        d = i >= 4 ? 10000 : p10[i];
        rem = 0;
        for (j = nl - 1; j >= 0; j--) {
            cur = (rem << 16) | limb[j];
            limb[j] = cur / d;
            rem = cur % d;
        }
        if (rem) sticky = 1;
        while (nl > 0 && !limb[nl - 1]) nl--;
    }
    hi = limb[3] << 16 | limb[2];
    lo = limb[1] << 16 | limb[0];
    for (qb = 64; qb > 0 && !((qb > 32 ? hi >> (qb - 33) : lo >> (qb - 1)) & 1); qb--) ;
    sh = qb - 53;                       /* 3 to 5 bits to round off */
    rb = lo & ((1u << sh) - 1);
    half = 1u << (sh - 1);
    ml = (lo >> sh) | (hi << (32 - sh));
    mh = hi >> sh;
    if (rb > half || (rb == half && (sticky || (ml & 1)))) {
        if (++ml == 0) mh++;
        if (mh >> 21) { ml = (ml >> 1) | (mh << 31); mh >>= 1; sh++; }
    }
    E = 52 + sh - s;                    /* the value is (mh:ml) * 2^(E - 52) */
    r->hi = ((unsigned)(E + 1023) << 20) | (mh & 0xFFFFF);
    r->lo = ml;
}

/* the number at the start of s (after blanks); with whole, 1 only if
   nothing but blanks follows it, else 1 if there was a number at all */
int num_parse(const char *s, Num *n, int whole)
{
    const char *p = s, *q, *e;
    int neg = 0, v = 0, nd = 0, k;
    unsigned m;
    fp64 f;
    while (isblank_((unsigned char)*p)) p++;
    q = p;
    if (*q == '+' || *q == '-') neg = *q++ == '-';
    while (*q >= '0' && *q <= '9' && nd < 9) { v = v * 10 + (*q++ - '0'); nd++; }
    if (nd && !(*q >= '0' && *q <= '9') && *q != '.' && *q != 'e' && *q != 'E') {
        num_int(n, neg ? -v : v);        /* (the usual case: a small whole number) */
        e = q;
    } else if (*q == '.' && q[1] >= '0' && q[1] <= '9') {      /* ddd.ddd: up to 9 digits? */
        m = v;
        if (!m) nd = 0;
        k = 0;
        for (e = q + 1; *e >= '0' && *e <= '9' && nd < 9 && k < 22; e++, k++) {
            m = m * 10 + (*e - '0');
            if (m) nd++;
        }
        if ((*e >= '0' && *e <= '9') || *e == 'e' || *e == 'E') goto slow;
        if (!m) num_int(n, 0);
        else {
            decimal(&f, m, k);
            if (neg) fp_neg(&f, &f);
            num_fp(n, &f);
        }
    } else {
    slow:
        e = fp_parse(&f, p);
        if (!e) {                       /* inf, infinity, nan */
            q = p + (*p == '+' || *p == '-');
            if (ncaseeq(q, "inf")) {
                e = q + (ncaseeq(q, "infinity") ? 8 : 3);
                f.hi = 0x7FF00000; f.lo = 0;
            } else if (ncaseeq(q, "nan")) {
                e = q + 3;
                f.hi = 0x7FF80000; f.lo = 0;
            } else {
                num_int(n, 0);
                return 0;
            }
            if (*p == '-') fp_neg(&f, &f);
        }
        num_fp(n, &f);
    }
    if (!whole) return 1;
    while (isblank_((unsigned char)*e)) e++;
    return *e == 0;
}

/* i in decimal into buf (12 bytes); buf */
char *itoa_(int i, char *buf)
{
    char t[12], *o = buf;
    unsigned u = i < 0 ? 0u - (unsigned)i : (unsigned)i;
    int k = 0;
    do { t[k++] = '0' + u % 10; u /= 10; } while (u);
    if (i < 0) *o++ = '-';
    while (k) *o++ = t[--k];
    *o = 0;
    return buf;
}

int num_isnum(const char *s, Num *n)
{
    return num_parse(s, n, 1);
}

/* ---------------------------------------------------------------- arithmetic */
void num_add(Num *r, const Num *a, const Num *b)
{
    fp64 x, y;
    if (a->isint && b->isint) {
        int s = (int)((unsigned)a->i + (unsigned)b->i);
        if (((a->i ^ s) & (b->i ^ s)) >= 0) { num_int(r, s); return; }
    }
    num_tofp(a, &x);
    num_tofp(b, &y);
    fp_add(&x, &x, &y);
    num_fp(r, &x);
}

void num_sub(Num *r, const Num *a, const Num *b)
{
    fp64 x, y;
    if (a->isint && b->isint) {
        int s = (int)((unsigned)a->i - (unsigned)b->i);
        if (((a->i ^ b->i) & (a->i ^ s)) >= 0) { num_int(r, s); return; }
    }
    num_tofp(a, &x);
    num_tofp(b, &y);
    fp_sub(&x, &x, &y);
    num_fp(r, &x);
}

void num_mul(Num *r, const Num *a, const Num *b)
{
    fp64 x, y;
    if (a->isint && b->isint && a->i <= 46340 && a->i >= -46340 && b->i <= 46340 && b->i >= -46340) {
        num_int(r, a->i * b->i);
        return;
    }
    num_tofp(a, &x);                    /* (exact whenever the product fits an int) */
    num_tofp(b, &y);
    fp_mul(&x, &x, &y);
    num_fp(r, &x);
}

void num_div(Num *r, const Num *a, const Num *b)
{
    fp64 x, y;
    if (b->isint ? b->i == 0 : fp_iszero(&b->f)) fatal("division by zero");
    if (a->isint && b->isint && !(a->i == (-2147483647 - 1) && b->i == -1) && a->i % b->i == 0) {
        num_int(r, a->i / b->i);
        return;
    }
    num_tofp(a, &x);
    num_tofp(b, &y);
    fp_div(&x, &x, &y);
    num_fp(r, &x);
}

/* the remainder with a's sign (C's fmod), exactly: subtract b * 2^k */
static void fp_fmod(fp64 *r, const fp64 *a, const fp64 *b)
{
    fp64 x, y, t;
    int k, neg = a->hi >> 31;
    if (fp_isnan(a) || fp_isnan(b) || fp_isinf(a)) { r->hi = 0x7FF80000; r->lo = 0; return; }
    fp_abs(&x, a);
    fp_abs(&y, b);
    if (fp_isinf(&y) || fp_cmp(&x, &y) < 0) { *r = *a; return; }
    while (fp_cmp(&x, &y) >= 0) {
        k = fp_ilogb(&x) - fp_ilogb(&y);
        fp_ldexp(&t, &y, k);
        if (fp_cmp(&t, &x) > 0) fp_ldexp(&t, &y, k - 1);
        fp_sub(&x, &x, &t);
        if (fp_iszero(&x)) break;
    }
    if (neg) fp_neg(&x, &x);
    *r = x;
}

void num_mod(Num *r, const Num *a, const Num *b)
{
    fp64 x, y;
    if (b->isint ? b->i == 0 : fp_iszero(&b->f)) fatal("division by zero in %%");
    if (a->isint && b->isint) {
        num_int(r, b->i == -1 ? 0 : a->i % b->i);
        return;
    }
    num_tofp(a, &x);
    num_tofp(b, &y);
    fp_fmod(&x, &x, &y);
    num_fp(r, &x);
}

void num_pow(Num *r, const Num *a, const Num *b)
{
    fp64 x, y;
    Num base, acc;
    int e;
    if (b->isint && b->i >= 0) {        /* by squaring: ints while they fit */
        num_int(&acc, 1);
        base = *a;
        e = b->i;
        while (e) {
            if (e & 1) num_mul(&acc, &acc, &base);
            e >>= 1;
            if (e) num_mul(&base, &base, &base);
        }
        *r = acc;
        return;
    }
    num_tofp(a, &x);
    num_tofp(b, &y);
    fp_pow(&x, &x, &y);
    num_fp(r, &x);
}

void num_neg(Num *r, const Num *a)
{
    fp64 x;
    if (a->isint && a->i != (-2147483647 - 1)) { num_int(r, -a->i); return; }
    num_tofp(a, &x);
    fp_neg(&x, &x);
    num_fp(r, &x);
}

int num_cmp(const Num *a, const Num *b)
{
    fp64 x, y;
    if (a->isint && b->isint) return a->i < b->i ? -1 : a->i > b->i;
    num_tofp(a, &x);
    num_tofp(b, &y);
    return fp_cmp(&x, &y);
}

int num_true(const Num *n)
{
    return n->isint ? n->i != 0 : !fp_iszero(&n->f);
}

int num_toint(const Num *n)
{
    if (n->isint) return n->i;
    if (fp_isnan(&n->f)) return 0;
    return fp_toint(&n->f, 0);
}

void num_trunc(Num *r, const Num *a)
{
    fp64 x;
    if (a->isint) { *r = *a; return; }
    if (fp_isnan(&a->f) || fp_isinf(&a->f)) { *r = *a; return; }
    fp_trunc(&x, &a->f);
    num_fp(r, &x);
}

/* ---------------------------------------------------------------- whole numbers */
/* (hi:lo) /= base; the remainder */
static unsigned div64(unsigned *hi, unsigned *lo, unsigned base)
{
    unsigned p[4], rem = 0, cur;
    int j;
    p[3] = *hi >> 16; p[2] = *hi & 0xFFFF; p[1] = *lo >> 16; p[0] = *lo & 0xFFFF;
    for (j = 3; j >= 0; j--) { cur = (rem << 16) | p[j]; p[j] = cur / base; rem = cur % base; }
    *hi = p[3] << 16 | p[2];
    *lo = p[1] << 16 | p[0];
    return rem;
}

/* |x| truncated to a 64-bit magnitude (hi:lo), saturated at 2^64 - 1 */
static void fp_to64(const fp64 *x, unsigned *hi, unsigned *lo)
{
    int e, s;
    unsigned h, l;
    *hi = *lo = 0;
    if (fp_isnan(x) || fp_iszero(x)) return;
    e = fp_isinf(x) ? 64 : fp_ilogb(x);
    if (e < 0) return;
    if (e >= 64) { *hi = *lo = 0xFFFFFFFF; return; }
    h = (x->hi & 0xFFFFF) | 0x100000;
    l = x->lo;
    s = e - 52;
    if (s > 0) {
        h = (h << s) | (l >> (32 - s));
        l <<= s;
    } else if (s < 0) {
        s = -s;
        if (s >= 32) { l = h >> (s - 32); h = 0; if (s == 32) l = (x->hi & 0xFFFFF) | 0x100000; }
        else { l = (l >> s) | (h << (32 - s)); h >>= s; }
    }
    *hi = h;
    *lo = l;
}

/* ---------------------------------------------------------------- exact decimal digits */
/* A double is M * 2^E exactly, so its decimal expansion is finite: the
   whole part's digits come from dividing it by 10000, the fraction's from
   multiplying it by 10000 (in 16-bit limbs, the binary point above the top
   limb), four digits at a time, only as many as are needed. Rounding is
   then done on exact digits, half to even, as C's printf does. */
#define MAXDIG 1500
static char dg[MAXDIG + 8];
static unsigned fr[80];         /* the fraction, 16-bit limbs, least significant first */

static void put4(char *d, unsigned v)
{
    d[0] = '0' + v / 1000;
    d[1] = '0' + v / 100 % 10;
    d[2] = '0' + v / 10 % 10;
    d[3] = '0' + v % 10;
}

/* the digits of finite, nonzero |x| into dg, from the first nonzero one
   (*k: its power), down to the power lim at least (sig: lim digits at
   least); *sticky: nonzero digits follow those. Their count. */
static int expand(const fp64 *x, int lim, int sig, int *k, int *sticky)
{
    unsigned w[34], grp[80], mh, ml, fh = 0, fl = 0, a, b, rem, cur, c, v0, v1, v2;
    int be = (x->hi >> 20) & 0x7FF, E, n = 0, i, j, nw, ng = 0, L = 0, lo, top, hi, s, fb, pw;
    char q[4];
    mh = x->hi & 0xFFFFF;
    ml = x->lo;
    if (be) mh |= 0x100000;
    else be = 1;
    E = be - 1075;                      /* |x| = (mh:ml) * 2^E */
    nw = E >= 0 ? E / 32 + 3 : 2;       /* (the words the whole part may use) */
    for (i = 0; i < nw; i++) w[i] = 0;
    fb = E < 0 ? -E : 0;                /* bits after the point */
    if (E >= 0) {
        int ws = E / 32, bs = E % 32;
        w[ws] = ml << bs;
        w[ws + 1] = bs ? (mh << bs) | (ml >> (32 - bs)) : mh;
        if (bs) w[ws + 2] = mh >> (32 - bs);
    } else if (fb < 53) {
        if (fb >= 32) { w[0] = mh >> (fb - 32); fh = mh & ((1u << (fb - 32)) - 1); fl = ml; }
        else { w[0] = (ml >> fb) | (fb ? mh << (32 - fb) : 0); w[1] = mh >> fb; fl = ml & ((1u << fb) - 1); }
        if (fb == 0) { w[0] = ml; w[1] = mh; fl = 0; }
    } else {
        fh = mh;
        fl = ml;
    }
    /* the whole part, 4 digits at a time from the bottom */
    for (;;) {
        while (nw > 0 && !w[nw - 1]) nw--;
        if (!nw) break;
        rem = 0;
        for (j = nw - 1; j >= 0; j--) {
            cur = (rem << 16) | (w[j] >> 16); a = cur / 10000; rem = cur % 10000;
            cur = (rem << 16) | (w[j] & 0xFFFF); b = cur / 10000; rem = cur % 10000;
            w[j] = a << 16 | b;
        }
        grp[ng++] = rem;
    }
    if (ng) {
        put4(q, grp[ng - 1]);
        for (i = 0; q[i] == '0'; i++) ;
        for (; i < 4; i++) dg[n++] = q[i];
        for (j = ng - 2; j >= 0; j--) { put4(dg + n, grp[j]); n += 4; }
        *k = n - 1;
    }
    /* the fraction: (fh:fl) / 2^fb, shifted so that the point is above limb L-1 */
    *sticky = 0;
    if (!(fh | fl)) { dg[n] = 0; return n; }
    L = (fb + 15) / 16;
    s = 16 * L - fb;
    for (i = 0; i < L; i++) fr[i] = 0;
    v0 = fl << s;
    v1 = s ? (fh << s) | (fl >> (32 - s)) : fh;
    v2 = s ? fh >> (32 - s) : 0;
    fr[0] = v0 & 0xFFFF;
    if (L > 1) fr[1] = v0 >> 16;
    if (L > 2) fr[2] = v1 & 0xFFFF;
    if (L > 3) fr[3] = v1 >> 16;
    if (L > 4) fr[4] = v2 & 0xFFFF;
    lo = 0;
    top = L - 1;
    while (top >= 0 && !fr[top]) top--;
    while (!fr[lo]) lo++;
    pw = -1;                            /* the power of the next digit */
    while (lo <= top) {
        if (n && (sig ? n >= lim : *k - n + 1 <= lim)) break;
        if (!n && !sig && pw < lim) break;      /* (all zeros down to lim) */
        if (n >= MAXDIG - 4) break;
        hi = top + 1 < L ? top + 1 : L - 1;
        c = 0;
        for (i = lo; i <= hi; i++) {
            c = fr[i] * 10000 + c;
            fr[i] = c & 0xFFFF;
            c >>= 16;
        }
        if (hi < L - 1) c = 0;          /* (the carry stayed inside) */
        put4(q, c);
        for (i = 0; i < 4; i++, pw--) {
            if (!n && q[i] == '0') continue;
            if (!n) *k = pw;
            dg[n++] = q[i];
        }
        top = hi;
        while (top >= lo && !fr[top]) top--;
        while (lo <= top && !fr[lo]) lo++;
    }
    *sticky = lo <= top;
    if (!n) *k = pw;
    dg[n] = 0;
    return n;
}

static int allzero(const char *s)
{
    while (*s) if (*s++ != '0') return 0;
    return 1;
}

/* exact digits dg[0..L) of power k (sticky: more nonzero ones follow):
   rounded half to even to nd digits; their count, and their power in *e */
static int round_digits(int L, int nd, int k, int *e, int sticky)
{
    int i;
    *e = k;
    if (nd > MAXDIG) nd = MAXDIG;
    if (nd < 0) return 0;
    if (nd >= L) {
        for (i = L; i < nd; i++) dg[i] = '0';
        dg[nd] = 0;
        return nd;
    }
    if (dg[nd] > '5' || (dg[nd] == '5' && (sticky || !allzero(dg + nd + 1) || (nd > 0 && (dg[nd - 1] - '0') & 1)))) {
        for (i = nd - 1; i >= 0; i--) {
            if (dg[i] < '9') { dg[i]++; break; }
            dg[i] = '0';
        }
        if (i < 0) {                    /* 999 -> 1000 */
            dg[0] = '1';
            for (i = 1; i <= nd; i++) dg[i] = '0';
            dg[nd + 1] = 0;
            *e = k + 1;
            return nd + 1;
        }
    } else if (nd == 0)
        return 0;
    dg[nd] = 0;
    return nd;
}

/* finite, nonzero x rounded at the digit of power pos: its digits in dg
   (from the first nonzero one, of power *e); 0 if it rounds to 0 */
static int digits_at(const fp64 *x, int pos, int *e)
{
    int k, st, n = expand(x, pos - 1, 0, &k, &st);
    if (!n) return 0;
    return round_digits(n, k - pos + 1, k, e, st);
}

/* finite, nonzero x rounded to P significant digits */
static int digits_sig(const fp64 *x, int P, int *e)
{
    int k, st, n = expand(x, P + 1, 1, &k, &st);
    return round_digits(n, P, k, e, st);
}

/* the text of one conversion is put together in bd */
static char bd[MAXDIG + 360];
static int bn;

/* the body of %e (prec digits after the point) from n digits of power e */
static void put_e(int n, int e, int prec, int alt, int upper)
{
    int i;
    bd[bn++] = n ? dg[0] : '0';
    if (prec > 0 || alt) bd[bn++] = '.';
    for (i = 1; i <= prec; i++) bd[bn++] = i < n ? dg[i] : '0';
    bd[bn++] = upper ? 'E' : 'e';
    if (!n) e = 0;
    bd[bn++] = e < 0 ? '-' : '+';
    if (e < 0) e = -e;
    if (e >= 100) bd[bn++] = '0' + e / 100;
    bd[bn++] = '0' + e / 10 % 10;
    bd[bn++] = '0' + e % 10;
}

/* the body of %f: digits dg[0..n) of power e, prec after the point */
static void put_f(int n, int e, int prec, int alt)
{
    int j, i;
    if (!n || e < 0) bd[bn++] = '0';
    else
        for (j = e; j >= 0; j--) { i = e - j; bd[bn++] = i < n ? dg[i] : '0'; }
    if (prec > 0 || alt) bd[bn++] = '.';
    for (j = -1; j >= -prec; j--) {
        i = e - j;
        bd[bn++] = n && i >= 0 && i < n ? dg[i] : '0';
    }
}

static void fill(Buf *out, int c, int n)
{
    char t[32];
    int k;
    memset(t, c, sizeof t);
    while (n > 0) {
        k = n < 32 ? n : 32;
        buf_add(out, t, k);
        n -= k;
    }
}

/* sign, prefix and the body bd[0..bn), padded to width */
static void pad(Buf *out, const char *sign, const char *pre, int width, int left, int zero)
{
    int ls = strlen(sign), lp = strlen(pre), len = ls + lp + bn;
    if (!left && !zero && len < width) fill(out, ' ', width - len);
    if (ls) buf_add(out, sign, ls);
    if (lp) buf_add(out, pre, lp);
    if (!left && zero && len < width) fill(out, '0', width - len);
    buf_add(out, bd, bn);
    if (left && len < width) fill(out, ' ', width - len);
}

/* one numeric conversion of printf: conv, its flags (- + space # 0), width and precision (-1: none) */
void fmt_number(Buf *out, int conv, const char *flags, int width, int prec, const Num *n)
{
    char tmp[80], *sign = "", *pre = "";
    const char *f;
    int left = 0, zero = 0, alt = 0, plus = 0, space = 0;
    int neg, nd, e = 0, upper = conv == 'E' || conv == 'G' || conv == 'X' || conv == 'F';
    unsigned hi, lo;
    fp64 x;

    for (f = flags; *f; f++)
        switch (*f) {
        case '-': left = 1; break;
        case '0': zero = 1; break;
        case '#': alt = 1; break;
        case '+': plus = 1; break;
        case ' ': space = 1; break;
        }
    bn = 0;
    if (conv == 'c') {
        bd[bn++] = (char)num_toint(n);  /* (a NUL byte too) */
        pad(out, "", "", width, left, 0);
        return;
    }
    if (conv == 'd' || conv == 'i' || conv == 'o' || conv == 'u' || conv == 'x' || conv == 'X') {
        int base = conv == 'o' ? 8 : conv == 'x' || conv == 'X' ? 16 : 10, i, k = 0;
        if (n->isint) {
            neg = n->i < 0;
            hi = 0;
            lo = neg ? 0u - (unsigned)n->i : (unsigned)n->i;
        } else {
            neg = !fp_isnan(&n->f) && n->f.hi >> 31;
            fp_to64(&n->f, &hi, &lo);
            if (!hi && !lo) neg = 0;
        }
        if ((conv == 'd' || conv == 'i') && hi >> 31) {        /* (saturated) */
            hi = neg ? 0x80000000 : 0x7FFFFFFF;
            lo = neg ? 0 : 0xFFFFFFFF;
        }
        if (conv == 'd' || conv == 'i') {
            if (neg) sign = "-";
            else if (plus) sign = "+";
            else if (space) sign = " ";
        } else if (neg) {               /* two's complement, 64 bits */
            hi = ~hi; lo = ~lo;
            if (++lo == 0) hi++;
        }
        if (!hi && base == 10)          /* (the usual case: one division at a time) */
            while (lo) { tmp[k++] = '0' + lo % 10; lo /= 10; }
        while (hi || lo) {
            int r = div64(&hi, &lo, base);
            tmp[k++] = r < 10 ? '0' + r : (upper ? 'A' : 'a') + r - 10;
        }
        if (prec < 0 && !k) tmp[k++] = '0';
        while (k < prec && k < 70) tmp[k++] = '0';
        if (alt && conv == 'o' && (!k || tmp[k - 1] != '0')) tmp[k++] = '0';
        if (alt && (conv == 'x' || conv == 'X') && k && !(k == 1 && tmp[0] == '0')) pre = conv == 'x' ? "0x" : "0X";
        for (i = k - 1; i >= 0; i--) bd[bn++] = tmp[i];
        pad(out, sign, pre, width, left, zero && prec < 0 && !left);
        return;
    }
    /* e f g */
    num_tofp(n, &x);
    neg = x.hi >> 31;
    if (neg && !fp_isnan(&x)) sign = "-";
    else if (plus) sign = "+";
    else if (space) sign = " ";
    if (fp_isnan(&x) || fp_isinf(&x)) {
        strcpy(bd, fp_isnan(&x) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"));
        bn = 3;
        pad(out, sign, "", width, left, 0);
        return;
    }
    if (prec < 0) prec = 6;
    if (prec > MAXDIG - 500) prec = MAXDIG - 500;
    fp_abs(&x, &x);
    if (conv == 'e' || conv == 'E') {
        if (fp_iszero(&x)) nd = 0, e = 0;
        else nd = digits_sig(&x, prec + 1, &e);
        put_e(nd, e, prec, alt, upper);
    } else if (conv == 'f' || conv == 'F') {
        nd = fp_iszero(&x) ? 0 : digits_at(&x, -prec, &e);
        put_f(nd, e, prec, alt);
    } else {                            /* g G */
        int P = prec ? prec : 1, X, ep = -1, i;
        if (fp_iszero(&x)) nd = 0, X = 0;
        else nd = digits_sig(&x, P, &X);
        if (X < -4 || X >= P) put_e(nd, X, P - 1, alt, upper);
        else put_f(nd, X, P - 1 - X, alt);
        if (!alt) {                     /* no trailing zeros, nor a bare point */
            for (i = 0; i < bn; i++) if (bd[i] == 'e' || bd[i] == 'E') ep = i;
            if (memchr(bd, '.', bn)) {
                int end = ep >= 0 ? ep : bn, t = end;
                while (t > 0 && bd[t - 1] == '0') t--;
                if (t > 0 && bd[t - 1] == '.') t--;
                if (ep >= 0) { memmove(bd + t, bd + ep, bn - ep); bn -= ep - t; }
                else bn = t;
            }
        }
    }
    pad(out, sign, "", width, left, zero && !left);
}

/* ---------------------------------------------------------------- number to text */
/* one number by a printf-style format (CONVFMT, OFMT) */
static void fmt_one(Buf *out, const char *fmt, const Num *n)
{
    const char *p = fmt;
    char flags[8];
    int nfl, width, prec, done = 0;
    while (*p) {
        if (*p != '%') { buf_addc(out, *p++); continue; }
        if (p[1] == '%') { buf_addc(out, '%'); p += 2; continue; }
        p++;
        nfl = 0;
        while (*p && strchr("-+ #0", *p)) { if (nfl < 7) flags[nfl++] = *p; p++; }
        flags[nfl] = 0;
        width = 0;
        while (isdigit((unsigned char)*p)) { if (width < 10000) width = width * 10 + *p - '0'; p++; }
        prec = -1;
        if (*p == '.') { p++; prec = 0; while (isdigit((unsigned char)*p)) { if (prec < 10000) prec = prec * 10 + *p - '0'; p++; } }
        while (*p && strchr("hlLqjzt", *p)) p++;
        if (*p && strchr("diouxXeEfFgGc", *p) && !done) {
            fmt_number(out, *p, flags, width, prec, n);
            done = 1;
        }
        if (*p) p++;
    }
}

/* n as text: ints and whole numbers in full (up to 30 digits), others by fmt */
char *num_tostr(const Num *n, const char *fmt, char *buf, int size)
{
    Buf b;
    int L, e, i, k;
    char *o;
    if (n->isint) return itoa_(n->i, buf);
    if (fp_isnan(&n->f)) { strcpy(buf, n->f.hi >> 31 ? "-nan" : "nan"); return buf; }
    if (fp_isinf(&n->f)) { strcpy(buf, n->f.hi >> 31 ? "-inf" : "inf"); return buf; }
    if (fp_isint(&n->f)) {              /* (like %.30g) */
        o = buf;
        if (n->f.hi >> 31) *o++ = '-';
        L = expand(&n->f, 31, 1, &e, &i);
        if (L <= 30) { strcpy(o, dg); return buf; }
        k = round_digits(L, 30, L - 1, &e, 0);
        while (k > 1 && dg[k - 1] == '0') k--;
        *o++ = dg[0];
        if (k > 1) { *o++ = '.'; for (i = 1; i < k; i++) *o++ = dg[i]; }
        sprintf(o, "e+%02d", e);
        return buf;
    }
    buf_init(&b);
    if (!strcmp(fmt, "%.6g")) fmt_number(&b, 'g', "", 0, 6, n);
    else fmt_one(&b, fmt, n);
    if (b.len >= size) b.len = size - 1;
    if (b.len) memcpy(buf, b.s, b.len);
    buf[b.len] = 0;
    free(b.s);
    return buf;
}
