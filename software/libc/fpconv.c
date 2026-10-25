/* fpconv.c -- exact conversion between decimal and IEEE binary64 (fplib.h).
 *
 * Both directions are done with exact integer arithmetic on big numbers,
 * so every result is correctly rounded, whatever the number of digits:
 *
 *   decimal -> binary: the digits D and the power of ten E give the exact
 *     value D * 10^E = D * 5^E * 2^E. For E >= 0 that is the integer
 *     D * 5^E (times 2^E); for E < 0 it is D / 5^-E (times 2^E), divided
 *     out to 64 quotient bits with the remainder as a sticky bit. The top
 *     64 bits and the sticky bit round exactly to a double.
 *
 *   binary -> decimal: x = m * 2^e (m an integer) times 10^t, with t chosen
 *     so the integer part has the digits wanted, is again a ratio of
 *     integers made of m, powers of 5 and powers of 2; its quotient gives
 *     the digits, its remainder (against half the divisor) the rounding.
 *
 * Big numbers are base 2^16 digits ("limbs") held in unsigned ints, so
 * a limb times a limb plus a carry fits 32 bits: p32cc has no long long.
 * Short decimal numbers (at most 15 digits and a power of ten to 22) take
 * a fast path: one correctly rounded softfp multiply or divide of two
 * exact doubles (Clinger). */
#include <softfp.h>
#include <fplib.h>
#include <errno.h>

#define EOF (-1)

#define NL 184                  /* limbs: 2944 bits, more than any conversion needs (~2700) */
#define MAXD 800                /* significant decimal digits kept; the rest only as a sticky digit */

typedef struct {
    int n;                      /* limbs in use (0 for zero); d[n - 1] != 0 */
    unsigned d[NL];             /* 16 bits each, the lowest first */
} big;

static big b1, b2, b3;          /* the work areas (static: big, and p32's stacks are small) */

/* ---------------------------------------------------------------- big numbers */
static void b_trim(big *a) { while (a->n > 0 && !a->d[a->n - 1]) a->n--; }

static void b_set(big *a, unsigned v)
{
    a->n = 0;
    while (v) { a->d[a->n++] = v & 0xFFFF; v >>= 16; }
}

static void b_muladd(big *a, unsigned m, unsigned c)   /* a = a * m + c (m, c < 2^16) */
{
    int i;
    unsigned t;
    for (i = 0; i < a->n; i++) {
        t = a->d[i] * m + c;
        a->d[i] = t & 0xFFFF;
        c = t >> 16;
    }
    if (c) a->d[a->n++] = c;
}

static void b_mulpow5(big *a, int k)                    /* a *= 5^k */
{
    static const unsigned p5[7] = { 1, 5, 25, 125, 625, 3125, 15625 };
    for (; k >= 6; k -= 6) b_muladd(a, 15625, 0);
    if (k) b_muladd(a, p5[k], 0);
}

static void b_shl(big *a, int s)                        /* a <<= s */
{
    int w = s >> 4, i;
    s &= 15;
    if (!a->n) return;
    if (s) {
        a->d[a->n] = 0;
        for (i = a->n; i > 0; i--) a->d[i] = ((a->d[i] << s) | (a->d[i - 1] >> (16 - s))) & 0xFFFF;
        a->d[0] = (a->d[0] << s) & 0xFFFF;
        a->n++;
    }
    if (w) {
        for (i = a->n - 1; i >= 0; i--) a->d[i + w] = a->d[i];
        for (i = 0; i < w; i++) a->d[i] = 0;
        a->n += w;
    }
    b_trim(a);
}

static int bits16(unsigned v)                           /* the bit length of v */
{
    int n = 0;
    while (v) { n++; v >>= 1; }
    return n;
}

static int b_bitlen(const big *a) { return a->n ? (a->n - 1) * 16 + bits16(a->d[a->n - 1]) : 0; }

static int b_cmp(const big *a, const big *b)
{
    int i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

static unsigned b_divsmall(big *a, unsigned v)          /* a /= v (v < 2^16); the remainder */
{
    unsigned r = 0, t;
    int i;
    for (i = a->n - 1; i >= 0; i--) {
        t = (r << 16) | a->d[i];
        a->d[i] = t / v;
        r = t % v;
    }
    b_trim(a);
    return r;
}

/* q = u / v, u = u mod v (Knuth's algorithm D, as in Hacker's Delight's
   divmnu). v > 0 is left shifted to have its top limb's top bit set, and u
   with it: the remainder stays shifted by the same amount, which is what
   the comparisons that use it need. Returns that shift. */
static int b_div(big *q, big *u, big *v)
{
    int n = v->n, m, j, i, s;
    unsigned qh, rh, p, t, br, c;
    s = 16 - bits16(v->d[n - 1]);
    b_shl(v, s);
    b_shl(u, s);
    if (b_cmp(u, v) < 0) { q->n = 0; return s; }
    if (n == 1) {                       /* a one-limb divisor */
        unsigned r = 0, dv = v->d[0];
        for (i = u->n - 1; i >= 0; i--) {
            t = (r << 16) | u->d[i];
            q->d[i] = t / dv;
            r = t % dv;
        }
        q->n = u->n;
        b_trim(q);
        b_set(u, r);
        return s;
    }
    m = u->n - n;
    u->d[u->n] = 0;
    for (j = m; j >= 0; j--) {
        t = (u->d[j + n] << 16) | u->d[j + n - 1];
        qh = t / v->d[n - 1];           /* the estimate: at most 2 too big */
        rh = t % v->d[n - 1];
        while (qh > 0xFFFF || qh * v->d[n - 2] > ((rh << 16) | u->d[j + n - 2])) {
            qh--;
            rh += v->d[n - 1];
            if (rh > 0xFFFF) break;
        }
        br = 0;                         /* u[j .. j+n] -= qh * v */
        c = 0;
        for (i = 0; i < n; i++) {
            p = qh * v->d[i] + c;
            c = p >> 16;
            t = u->d[i + j] - (p & 0xFFFF) - br;
            u->d[i + j] = t & 0xFFFF;
            br = (t >> 16) != 0;
        }
        t = u->d[j + n] - c - br;
        u->d[j + n] = t & 0xFFFF;
        if (t >> 16) {                  /* (rarely) one too many: add v back */
            qh--;
            c = 0;
            for (i = 0; i < n; i++) {
                t = u->d[i + j] + v->d[i] + c;
                u->d[i + j] = t & 0xFFFF;
                c = t >> 16;
            }
            u->d[j + n] = (u->d[j + n] + c) & 0xFFFF;
        }
        q->d[j] = qh;
    }
    q->n = m + 1;
    b_trim(q);
    u->n = n;
    b_trim(u);
    return s;
}

static unsigned b_win(const big *a, int p)           /* the 16 bits from bit p up (p may be < 0) */
{
    int i, b;
    if (p < 0) return p <= -16 || !a->n ? 0 : (a->d[0] << -p) & 0xFFFF;
    i = p >> 4;
    b = p & 15;
    if (i >= a->n) return 0;
    if (!b) return a->d[i];
    return ((a->d[i] >> b) | (i + 1 < a->n ? a->d[i + 1] << (16 - b) : 0)) & 0xFFFF;
}

static int b_anybelow(const big *a, int p)              /* a bit below bit p set? */
{
    int i;
    for (i = 0; i < a->n && i * 16 < p; i++) {
        unsigned d = a->d[i];
        if ((i + 1) * 16 > p) d &= (1u << (p - i * 16)) - 1;
        if (d) return 1;
    }
    return 0;
}

/* the top 64 bits of a (> 0) as h:l; *sticky |= the rest != 0; returns the bit length */
static int b_top64(const big *a, unsigned *h, unsigned *l, int *sticky)
{
    int len = b_bitlen(a), p = len - 64;
    *h = (b_win(a, p + 48) << 16) | b_win(a, p + 32);
    *l = (b_win(a, p + 16) << 16) | b_win(a, p);
    if (p > 0 && b_anybelow(a, p)) *sticky = 1;
    return len;
}

/* a >>= s (s > 0), returning what was dropped against a half: 0 nothing,
   1 less than a half, 2 a half, 3 more */
static int b_shr(big *a, int s)
{
    int w = s >> 4, i, half, rest;
    half = b_win(a, s - 1) & 1;
    rest = b_anybelow(a, s - 1);
    for (i = 0; i + w < a->n; i++) a->d[i] = b_win(a, s + 16 * i);
    a->n = a->n > w ? a->n - w : 0;
    b_trim(a);
    return half ? (rest ? 3 : 2) : (rest ? 1 : 0);
}

/* ---------------------------------------------------------------- rounding to a double */
/* r = +-(h:l + sticky) * 2^(e2 - 63), h's top bit set, rounded to nearest even.
   Returns 1 if inexact. (Also fpmath.c's.) */
int __fp_pack64(fp64 *r, unsigned h, unsigned l, int sticky, int e2, int neg)
{
    int sh, half, rest;
    unsigned mh, ml, rb_h, rb_l;
    unsigned sign = neg ? 0x80000000 : 0;
    if (e2 > 1023) { r->hi = sign | 0x7FF00000; r->lo = 0; return 1; }
    sh = e2 >= -1022 ? 11 : 11 + (-1022 - e2);    /* bits to drop */
    if (sh > 64) { r->hi = sign; r->lo = 0; return 1; }
    if (sh == 64) {                     /* only the rounding bit (the top bit) is left */
        rest = l || (h & 0x7FFFFFFF) || sticky;
        r->hi = sign; r->lo = rest;     /* a tie goes to 0 (even) */
        return 1;
    }
    /* m = (h:l) >> sh; the dropped bits rb with the half point */
    if (sh >= 32) {
        mh = 0; ml = h >> (sh - 32);
        rb_h = sh == 32 ? 0 : h & ((1u << (sh - 32)) - 1); rb_l = l;
        half = sh - 1 >= 32 ? (int)(rb_h >> (sh - 33)) & 1 : (int)(rb_l >> 31) & 1;
        if (sh - 1 >= 32) { rb_h &= (1u << (sh - 33)) - 1; } else rb_l &= 0x7FFFFFFF;
    } else {
        mh = h >> sh; ml = (l >> sh) | (h << (32 - sh));
        rb_h = 0; rb_l = l & ((1u << sh) - 1);
        half = (rb_l >> (sh - 1)) & 1;
        rb_l &= (1u << (sh - 1)) - 1;
    }
    rest = rb_h || rb_l || sticky;
    if (half && (rest || (ml & 1))) { ml++; if (!ml) mh++; }
    if (e2 >= -1022) r->hi = ((unsigned)(e2 + 1022) << 20) + mh;   /* (the hidden bit adds the 1, or carries) */
    else r->hi = mh;                    /* subnormal (rounding up may make it the smallest normal) */
    r->lo = ml;
    if (r->hi >= 0x7FF00000) { r->hi = 0x7FF00000; r->lo = 0; }
    r->hi |= sign;
    return half || rest;
}

/* ---------------------------------------------------------------- decimal -> binary */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static const char *match(const char *s, const char *w)  /* a case-insensitive prefix: the end, or 0 */
{
    while (*w) if (lower(*s++) != *w++) return 0;
    return s;
}

static const fp64 TEN[23] = {          /* 10^0 .. 10^22: exact doubles */
    { 0, 0x3FF00000 }, { 0, 0x40240000 }, { 0, 0x40590000 }, { 0, 0x408F4000 },
    { 0, 0x40C38800 }, { 0, 0x40F86A00 }, { 0, 0x412E8480 }, { 0, 0x416312D0 },
    { 0, 0x4197D784 }, { 0, 0x41CDCD65 }, { 0x20000000, 0x4202A05F }, { 0xE8000000, 0x42374876 },
    { 0xA2000000, 0x426D1A94 }, { 0xE5400000, 0x42A2309C }, { 0x1E900000, 0x42D6BCC4 },
    { 0x26340000, 0x430C6BF5 }, { 0x37E08000, 0x4341C379 }, { 0x85D8A000, 0x43763457 },
    { 0x674EC800, 0x43ABC16D }, { 0x60913D00, 0x43E158E4 }, { 0x78B58C40, 0x4415AF1D },
    { 0xD6E2EF50, 0x444B1AE4 }, { 0x064DD592, 0x4480F0CF }
};

static int clampadd(int e, int d) { e += d; return e > 100000 ? 100000 : e < -100000 ? -100000 : e; }

/* [sign] digits [exponent]: the end, *ev the value (clamped), or 0 if no digits */
static const char *exponent(const char *s, int *ev)
{
    int neg = 0, v = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    if (!is_digit(*s)) return 0;
    while (is_digit(*s)) { if (v < 100000) v = v * 10 + (*s - '0'); s++; }
    *ev = neg ? -v : v;
    return s;
}

/* nan(...): the Mac takes everything to the first ')'; a payload when the
   inside is a C integer (decimal, 0 octal or 0x hexadecimal), modulo 2^64,
   whose low 51 bits go in the fraction below the quiet bit */
static const char *nanpayload(const char *s, unsigned *ph, unsigned *pl)
{
    const char *e = s, *p = s;
    unsigned h = 0, l = 0, base = 10, d;
    int ok = 1;
    *ph = *pl = 0;
    while (*e && *e != ')') e++;
    if (!*e) return 0;
    if (p == e) return e + 1;
    if (*p == '0') {
        base = 8; p++;
        if (*p == 'x') { base = 16; p++; if (p == e) ok = 0; }
    }
    for (; ok && p < e; p++) {
        d = hexval(*p);
        if (d >= base) { ok = 0; break; }
        /* (h:l) = (h:l) * base + d, modulo 2^64, in 16-bit pieces */
        {
            unsigned w[4], t, c = d;
            int k;
            w[0] = l & 0xFFFF; w[1] = l >> 16; w[2] = h & 0xFFFF; w[3] = h >> 16;
            for (k = 0; k < 4; k++) { t = w[k] * base + c; w[k] = t & 0xFFFF; c = t >> 16; }
            l = w[0] | (w[1] << 16); h = w[2] | (w[3] << 16);
        }
    }
    if (ok) { *ph = h & 0xFFFFF; *pl = l; }
    return e + 1;
}

static const char *hexfloat(fp64 *r, const char *s, int neg)  /* after "0x": the end, or 0 */
{
    unsigned h = 0, l = 0;
    int any = 0, nd = 0, sticky = 0, bexp = 0, ev, z, d, inexact;
    const char *t;
    for (; (d = hexval(*s)) >= 0; s++) {
        any = 1;
        if (nd < 16) { if (nd || d) { h = (h << 4) | (l >> 28); l = (l << 4) | d; nd++; } }
        else { bexp = clampadd(bexp, 4); if (d) sticky = 1; }
    }
    if (*s == '.') {
        for (s++; (d = hexval(*s)) >= 0; s++) {
            any = 1;
            if (nd < 16) { if (nd || d) { h = (h << 4) | (l >> 28); l = (l << 4) | d; nd++; } bexp = clampadd(bexp, -4); }
            else if (d) sticky = 1;
        }
    }
    if (!any) return 0;
    if ((*s == 'p' || *s == 'P') && (t = exponent(s + 1, &ev)) != 0) { bexp = clampadd(bexp, ev); s = t; }
    if (!(h | l)) { r->hi = neg ? 0x80000000 : 0; r->lo = 0; return s; }
    for (z = 0; !(h & 0x80000000); z++) { h = (h << 1) | (l >> 31); l <<= 1; }
    inexact = __fp_pack64(r, h, l, sticky, bexp + 63 - z, neg);
    if ((r->hi & 0x7FF00000) == 0x7FF00000 || ((r->hi & 0x7FF00000) == 0 && inexact)) errno = ERANGE;
    return s;
}

static char dig[MAXD + 2];

const char *__fp_strtod(fp64 *r, const char *s)
{
    const char *p = s, *t;
    int neg = 0, nd = 0, e10 = 0, any = 0, sticky = 0, i, ev;
    unsigned h, l;
    r->hi = r->lo = 0;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if ((t = match(p, "inf")) != 0) {
        if ((p = match(t, "inity")) != 0) t = p;
        r->hi = (neg ? 0x80000000 : 0) | 0x7FF00000;
        return t;
    }
    if ((t = match(p, "nan")) != 0) {
        r->hi = (neg ? 0x80000000 : 0) | 0x7FF80000;
        if (*t == '(' && (p = nanpayload(t + 1, &h, &l)) != 0) { r->hi |= h; r->lo = l; t = p; }
        return t;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        if ((t = hexfloat(r, p + 2, neg)) != 0) return t;
        r->hi = neg ? 0x80000000 : 0;
        return p + 1;                   /* just the 0 */
    }
    /* the significant digits (no leading zeros) into dig[], value = D * 10^e10 */
    for (; is_digit(*p); p++) {
        any = 1;
        if (nd < MAXD) { if (nd || *p != '0') dig[nd++] = *p - '0'; }
        else { e10++; if (*p != '0') sticky = 1; }
    }
    if (*p == '.') {
        for (p++; is_digit(*p); p++) {
            any = 1;
            if (nd < MAXD) { if (nd || *p != '0') dig[nd++] = *p - '0'; e10--; }
            else if (*p != '0') sticky = 1;
        }
    }
    if (!any) return s;
    if ((*p == 'e' || *p == 'E') && (t = exponent(p + 1, &ev)) != 0) { e10 = clampadd(e10, ev); p = t; }
    if (sticky) { dig[nd++] = 1; e10--; }      /* digits beyond MAXD: a final nonzero digit stands for them */
    while (nd > 0 && !dig[nd - 1]) { nd--; e10++; }
    if (!nd) { r->hi = neg ? 0x80000000 : 0; return p; }
    if (nd + e10 > 310) {               /* >= 10^309: too big */
        r->hi = (neg ? 0x80000000 : 0) | 0x7FF00000;
        errno = ERANGE;
        return p;
    }
    if (nd + e10 < -323) {              /* < 10^-324: rounds to 0 */
        r->hi = neg ? 0x80000000 : 0;
        errno = ERANGE;
        return p;
    }
    if (nd <= 15 && e10 >= -22 && e10 <= 22) {  /* exact D and 10^|e10|: one rounding */
        fp64 a, b;
        h = l = 0;
        for (i = 0; i < nd; i++) {      /* (h:l) = D, < 10^15 < 2^50 */
            unsigned l8 = l << 3, l2 = l << 1, nl = l8 + l2;
            h = (h << 3) + (h << 1) + (l >> 29) + (l >> 31) + (nl < l8);
            l = nl + dig[i];
            if (l < nl) h++;
        }
        fp_fromuint(&a, h);
        fp_ldexp(&a, &a, 32);
        fp_fromuint(&b, l);
        fp_add(&a, &a, &b);
        if (e10 >= 0) fp_mul(r, &a, &TEN[e10]);
        else fp_div(r, &a, &TEN[-e10]);
        if (neg) r->hi |= 0x80000000;
        return p;
    }
    /* D as a big number, four digits at a time */
    b1.n = 0;
    for (i = 0; i + 4 <= nd; i += 4)
        b_muladd(&b1, 10000, dig[i] * 1000 + dig[i + 1] * 100 + dig[i + 2] * 10 + dig[i + 3]);
    for (; i < nd; i++) b_muladd(&b1, 10, dig[i]);
    sticky = 0;
    if (e10 >= 0) {                     /* D * 5^e10 * 2^e10 */
        b_mulpow5(&b1, e10);
        ev = b_top64(&b1, &h, &l, &sticky);
        ev += e10 - 1;                  /* the top bit's exponent */
    } else {                            /* D / 5^k * 2^-k: 64+ quotient bits */
        int k = -e10, sh;
        b_set(&b2, 1);
        b_mulpow5(&b2, k);
        sh = b_bitlen(&b2) - b_bitlen(&b1) + 66;
        if (sh < 0) sh = 0;
        b_shl(&b1, sh);
        b_div(&b3, &b1, &b2);
        sticky = b1.n != 0;
        ev = b_top64(&b3, &h, &l, &sticky);
        ev += -sh - k - 1;
    }
    __fp_pack64(r, h, l, sticky, ev, neg);
    if ((r->hi & 0x7FF00000) == 0x7FF00000 || (r->hi & 0x7FF00000) == 0) errno = ERANGE;
    return p;
}

/* ---------------------------------------------------------------- binary -> decimal */
static int floor_log10_2(int e)         /* floor(e * log10 2), |e| < 1100 */
{
    int v = e * 78913;                  /* (log10 2 = 78913 / 2^18 closely enough) */
    return v >= 0 ? v >> 18 : -((-v + 262143) >> 18);
}

static char tmpd[FP_DTOA_BUF + 8];

int __fp_dtoa(const fp64 *x, int mode, int ndig, char *digits, int *decpt, int *neg)
{
    unsigned mh, ml;
    int e, ex, len, t, te, k, nq, keep, cls, up, i, j, sh;
    *neg = x->hi >> 31;
    ex = (x->hi >> 20) & 0x7FF;
    mh = x->hi & 0xFFFFF;
    ml = x->lo;
    digits[0] = 0;
    if (ex == 0x7FF) return (mh | ml) ? FP_DTOA_NAN : FP_DTOA_INF;
    if (!(mh | ml) && !ex) { *decpt = 1; return 0; }
    if (ex) { mh |= 0x100000; e = ex - 1075; } else e = -1074;
    while (!(ml & 1)) { ml = (ml >> 1) | (mh << 31); mh >>= 1; e++; }   /* x = m * 2^e, m odd */
    len = mh ? 32 + (mh >> 16 ? 16 + bits16(mh >> 16) : bits16(mh)) : (ml >> 16 ? 16 + bits16(ml >> 16) : bits16(ml));
    te = e < 0 ? -e : 0;                /* x * 10^te is a whole number */
    if (mode == 'e') {
        if (ndig < 1) ndig = 1;
        if (ndig > FP_DTOA_BUF - 20) ndig = FP_DTOA_BUF - 20;   /* (beyond 767 they are zeros anyway) */
        k = floor_log10_2(e + len - 1);  /* 10^k <= x < 10^(k+2) */
        t = ndig - 1 - k;
    } else {
        if (ndig < 0) ndig = 0;
        t = ndig;
    }
    if (t > te) t = te;                 /* (exact: no rounding) */
    /* q = x * 10^t = m * 5^t * 2^(e+t), or m * 2^(e+t) / 5^-t; cls = the remainder against half the divisor */
    b1.d[0] = ml & 0xFFFF; b1.d[1] = ml >> 16; b1.d[2] = mh & 0xFFFF; b1.d[3] = mh >> 16;
    b1.n = 4;
    b_trim(&b1);
    cls = 0;
    sh = e + t;
    if (t >= 0) {
        b_mulpow5(&b1, t);
        if (sh >= 0) b_shl(&b1, sh);
        else cls = b_shr(&b1, -sh);
    } else {
        b_set(&b2, 1);
        b_mulpow5(&b2, -t);
        if (sh >= 0) b_shl(&b1, sh); else b_shl(&b2, -sh);
        b_div(&b3, &b1, &b2);           /* b3 = q, b1 = remainder, both scaled with b2 */
        if (b1.n) {
            b_shl(&b1, 1);
            j = b_cmp(&b1, &b2);
            cls = j < 0 ? 1 : j == 0 ? 2 : 3;
        }
        b1 = b3;
    }
    /* q's digits, most significant first, into tmpd */
    nq = 0;
    while (b1.n) {
        unsigned g = b_divsmall(&b1, 10000);
        for (j = 0; j < 4; j++) { tmpd[nq++] = '0' + g % 10; g /= 10; }
    }
    while (nq > 0 && tmpd[nq - 1] == '0') nq--;     /* (the leading zeros of the last group) */
    for (i = 0, j = nq - 1; i < j; i++, j--) { char c = tmpd[i]; tmpd[i] = tmpd[j]; tmpd[j] = c; }
    *decpt = nq - t;
    keep = mode == 'e' && nq > ndig ? ndig : nq;
    if (nq > keep) {                    /* (one digit too many: round it off) */
        int rd = tmpd[keep] - '0', rest = cls != 0;
        for (i = keep + 1; i < nq; i++) if (tmpd[i] != '0') rest = 1;
        up = rd > 5 || (rd == 5 && (rest || ((tmpd[keep - 1] - '0') & 1)));
    } else
        up = cls == 3 || (cls == 2 && nq > 0 && ((tmpd[nq - 1] - '0') & 1));
    nq = keep;
    if (up) {
        for (i = nq - 1; i >= 0 && tmpd[i] == '9'; i--) tmpd[i] = '0';
        if (i >= 0) tmpd[i]++;
        else {                          /* 99..9 -> 100..0 (or nothing -> 1) */
            tmpd[0] = '1';
            for (i = 1; i < nq; i++) tmpd[i] = '0';
            if (!nq) nq = 1;
            (*decpt)++;
        }
    }
    while (nq > 0 && tmpd[nq - 1] == '0') nq--;
    for (i = 0; i < nq; i++) digits[i] = tmpd[i];
    digits[nq] = 0;
    if (!nq) *decpt = mode == 'e' ? 1 : -ndig;
    return nq;
}

/* ---------------------------------------------------------------- scanf's float input */
/* The Mac's (FreeBSD's) parsefloat: read while the text could still become a
   number, remembering the last point where it was one (commit); give back
   what follows that point. */
enum { S_START, S_SIGN, S_INF, S_NAN, S_DONE, S_HEX, S_DIGITS, S_POINT, S_FRAC, S_EXP, S_EXPDIG };

static int alnum(int c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

int __fp_scan(int (*get)(void *), void (*unget)(int, void *), void *src, char *buf, int size, int width)
{
    int state = S_START, n = 0, commit = 0, pos = 0, gotdig = 0, hex = 0, c, more;
    if (width <= 0 || width > size - 1) width = size - 1;
    while (n < width) {
        c = get(src);
        if (c == EOF) break;
        more = 1;
    again:
        switch (state) {
        case S_START:
            state = S_SIGN;
            if (c == '-' || c == '+') break;
            goto again;
        case S_SIGN:
            if (c == '0') { state = S_HEX; commit = n + 1; }
            else if (c == 'i' || c == 'I') state = S_INF;
            else if (c == 'n' || c == 'N') state = S_NAN;
            else { state = S_DIGITS; goto again; }
            break;
        case S_INF:
            if (pos > 6 || lower(c) != "nfinity"[pos]) { more = 0; break; }
            if (pos == 1 || pos == 6) commit = n + 1;      /* inf, infinity */
            pos++;
            break;
        case S_NAN:
            if (pos == 0) { if (lower(c) != 'a') more = 0; }
            else if (pos == 1) { if (lower(c) != 'n') more = 0; else commit = n + 1; }
            else if (pos == 2) { if (c != '(') more = 0; }
            else if (c == ')') { commit = n + 1; state = S_DONE; }
            else if (!alnum(c) && c != '_') more = 0;
            pos++;
            break;
        case S_DONE:
            more = 0;
            break;
        case S_HEX:
            state = S_DIGITS;
            if (c == 'x' || c == 'X') { hex = 1; break; }
            gotdig = 1;
            goto again;
        case S_DIGITS:
            if (is_digit(c) || (hex && hexval(c) >= 0)) { gotdig = 1; commit = n + 1; break; }
            state = S_POINT;
            goto again;
        case S_POINT:
            state = S_FRAC;
            if (c == '.') { if (gotdig) commit = n + 1; break; }
            goto again;
        case S_FRAC:
            if ((!hex && (c == 'e' || c == 'E')) || (hex && (c == 'p' || c == 'P'))) {
                if (!gotdig) more = 0;
                else state = S_EXP;
            } else if (is_digit(c) || (hex && hexval(c) >= 0)) { commit = n + 1; gotdig = 1; }
            else more = 0;
            break;
        case S_EXP:
            state = S_EXPDIG;
            if (c == '-' || c == '+') break;
            goto again;
        case S_EXPDIG:
            if (is_digit(c)) commit = n + 1;
            else more = 0;
            break;
        }
        if (!more) { unget(c, src); break; }
        buf[n++] = c;
    }
    while (n > commit) unget((unsigned char)buf[--n], src);
    buf[n] = 0;
    return n;
}
