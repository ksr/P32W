/* softfp.c -- IEEE 754 binary64 arithmetic in software (D27; softfp.h).
 *
 * Inside, a finite nonzero number is unpacked into a sign, an exponent e
 * and a 64-bit significand m (hi:lo) with its leading 1 at bit 61:
 *     value = m / 2^61 * 2^e
 * The 9 bits below the 53 that a double keeps, and a sticky bit (bit 0:
 * "something nonzero was shifted out"), let pack() round exactly to the
 * nearest double, ties to even. Everything is 32-bit integer work: p32cc
 * has no long long, so 64-bit values are word pairs and products are
 * built from 16-bit pieces. */
#include <softfp.h>
#include <string.h>

struct un { int s, e; unsigned h, l; };
enum { ZERO, FINITE, INF, NAN_ };

static const fp64 ONE = { 0, 0x3FF00000 };
static const fp64 HALF = { 0, 0x3FE00000 };
static const fp64 QNAN = { 0, 0x7FF80000 };
static const fp64 LN2_HI = { 0xFEE00000, 0x3FE62E42 };     /* ln 2, in two parts */
static const fp64 LN2_LO = { 0x35793C76, 0x3DEA39EF };
static const fp64 INV_LN2 = { 0x652B82FE, 0x3FF71547 };
static const fp64 PIO2_1 = { 0x54400000, 0x3FF921FB };     /* pi/2, in three parts */
static const fp64 PIO2_2 = { 0x1A600000, 0x3DD0B461 };
static const fp64 PIO2_3 = { 0x2E000000, 0x3BA3198A };
static const fp64 TWO_OVER_PI = { 0x6DC9C883, 0x3FE45F30 };
static const fp64 SQRT2 = { 0x667F3BCD, 0x3FF6A09E };
static const fp64 EXP_MAX = { 0xFEFA39EF, 0x40862E42 };    /* 709.782712893384 */
static const fp64 EXP_MIN = { 0xD52D3051, 0xC0874910 };    /* -745.1332191019411 */

/* ---------------------------------------------------------------- 64-bit pieces */
static int top_bit(unsigned h, unsigned l)              /* 63..0, or -1 for 0 */
{
    int n = 32;
    unsigned x = h;
    if (!h) { x = l; n = 0; if (!l) return -1; }
    if (x >= 0x10000) { n += 16; x >>= 16; }
    if (x >= 0x100) { n += 8; x >>= 8; }
    if (x >= 0x10) { n += 4; x >>= 4; }
    if (x >= 0x4) { n += 2; x >>= 2; }
    if (x >= 0x2) n += 1;
    return n;
}

static void shr(struct un *u, int n)                    /* right, keeping a sticky bit */
{
    unsigned st;
    if (n <= 0) return;
    if (n >= 64) { st = (u->h | u->l) != 0; u->h = 0; u->l = st; return; }
    if (n >= 32) {
        st = u->l != 0 || (n > 32 && (u->h & ((1u << (n - 32)) - 1)));
        u->l = n == 32 ? u->h : u->h >> (n - 32);
        u->h = 0;
        u->l |= st;
        return;
    }
    st = (u->l & ((1u << n) - 1)) != 0;
    u->l = (u->l >> n) | (u->h << (32 - n));
    u->h >>= n;
    u->l |= st;
}

static void shl(struct un *u, int n)                    /* left (n < 64; nothing is lost) */
{
    if (n <= 0) return;
    if (n >= 32) { u->h = u->l << (n - 32); u->l = 0; return; }
    u->h = (u->h << n) | (u->l >> (32 - n));
    u->l <<= n;
}

static void normalize(struct un *u)                     /* leading 1 to bit 61 */
{
    int t = top_bit(u->h, u->l);
    if (t < 0) return;
    if (t > 61) { shr(u, t - 61); u->e += t - 61; }
    else if (t < 61) { shl(u, 61 - t); u->e -= 61 - t; }
}

static int unpack(const fp64 *a, struct un *u)
{
    unsigned hi = a->hi, fh = hi & 0xFFFFF;
    int ex = (hi >> 20) & 0x7FF;
    u->s = hi >> 31;
    if (ex == 0x7FF) return (fh | a->lo) ? NAN_ : INF;
    if (ex == 0) {
        if (!(fh | a->lo)) { u->h = u->l = 0; u->e = 0; return ZERO; }
        u->h = fh; u->l = a->lo; u->e = 61 - 1074;      /* value = frac * 2^-1074 = frac / 2^61 * 2^e */
        normalize(u);
        return FINITE;
    }
    u->h = fh | 0x100000;
    u->l = a->lo;
    shl(u, 9);
    u->e = ex - 1023;
    return FINITE;
}

static void pack(fp64 *r, struct un *u)
{
    int b;
    unsigned rb;
    if (!(u->h | u->l)) { r->hi = (unsigned)u->s << 31; r->lo = 0; return; }
    normalize(u);
    b = u->e + 1023;
    if (b >= 2047) { r->hi = ((unsigned)u->s << 31) | 0x7FF00000; r->lo = 0; return; }
    if (b <= 0) {                       /* subnormal: down to the smallest exponent */
        shr(u, b < -70 ? 70 : 1 - b);
        b = 0;
    }
    rb = u->l & 0x1FF;                  /* round to nearest, ties to even */
    u->l = (u->l >> 9) | (u->h << 23);
    u->h >>= 9;
    if (rb > 0x100 || (rb == 0x100 && (u->l & 1))) { u->l++; if (!u->l) u->h++; }
    if (b == 0) r->hi = u->h;           /* (rounding up may make it the smallest normal) */
    else r->hi = ((unsigned)(b - 1) << 20) + u->h;
    r->lo = u->l;
    if ((r->hi >> 20) >= 0x7FF) { r->hi = 0x7FF00000; r->lo = 0; }
    r->hi |= (unsigned)u->s << 31;
}

/* m = a.m * b.m / 2^61, e = a.e + b.e (both finite, nonzero) */
static void u_mul(struct un *r, const struct un *a, const struct un *b)
{
    unsigned x[4], y[4], p[8], w0, w1, w2, w3, t, carry;
    int i, j;
    x[0] = a->l & 0xFFFF; x[1] = a->l >> 16; x[2] = a->h & 0xFFFF; x[3] = a->h >> 16;
    y[0] = b->l & 0xFFFF; y[1] = b->l >> 16; y[2] = b->h & 0xFFFF; y[3] = b->h >> 16;
    for (i = 0; i < 8; i++) p[i] = 0;
    for (i = 0; i < 4; i++) {
        carry = 0;
        for (j = 0; j < 4; j++) {
            t = x[i] * y[j] + p[i + j] + carry;
            p[i + j] = t & 0xFFFF;
            carry = t >> 16;
        }
        p[i + 4] = carry;
    }
    w0 = p[0] | p[1] << 16; w1 = p[2] | p[3] << 16; w2 = p[4] | p[5] << 16; w3 = p[6] | p[7] << 16;
    r->l = (w1 >> 29) | (w2 << 3);
    r->h = (w2 >> 29) | (w3 << 3);
    if (w0 || (w1 & 0x1FFFFFFF)) r->l |= 1;
    r->e = a->e + b->e;
    r->s = a->s ^ b->s;
    normalize(r);
}

/* m = a.m / b.m * 2^61, e = a.e - b.e (both finite, nonzero, normalized) */
static void u_div(struct un *r, const struct un *a, const struct un *b)
{
    unsigned rh = a->h, rl = a->l, qh = 0, ql = 0;
    int i;
    for (i = 61; i >= 0; i--) {
        if (rh > b->h || (rh == b->h && rl >= b->l)) {
            unsigned borrow = rl < b->l;
            rl -= b->l;
            rh -= b->h + borrow;
            if (i >= 32) qh |= 1u << (i - 32); else ql |= 1u << i;
        }
        rh = (rh << 1) | (rl >> 31);
        rl <<= 1;
    }
    r->h = qh;
    r->l = ql | ((rh | rl) != 0);
    r->e = a->e - b->e;
    r->s = a->s ^ b->s;
    normalize(r);
}

/* ---------------------------------------------------------------- arithmetic */
static void addsub(fp64 *r, const fp64 *a, const fp64 *b, int negate_b)
{
    struct un x, y, t;
    int ca = unpack(a, &x), cb = unpack(b, &y), d;
    y.s ^= negate_b;
    if (ca == NAN_ || cb == NAN_) { *r = QNAN; return; }
    if (ca == INF || cb == INF) {
        if (ca == INF && cb == INF && x.s != y.s) { *r = QNAN; return; }
        r->lo = 0;
        r->hi = 0x7FF00000 | ((unsigned)(ca == INF ? x.s : y.s) << 31);
        return;
    }
    if (ca == ZERO && cb == ZERO) { r->lo = 0; r->hi = (unsigned)(x.s & y.s) << 31; return; }
    if (cb == ZERO) { *r = *a; return; }
    if (ca == ZERO) { r->lo = b->lo; r->hi = (b->hi & 0x7FFFFFFF) | ((unsigned)y.s << 31); return; }
    if (x.e < y.e || (x.e == y.e && (x.h < y.h || (x.h == y.h && x.l < y.l)))) { t = x; x = y; y = t; }
    d = x.e - y.e;
    shr(&y, d);
    if (x.s == y.s) {
        unsigned l = x.l + y.l;
        x.h += y.h + (l < x.l);
        x.l = l;
    } else {
        unsigned borrow = x.l < y.l;
        x.l -= y.l;
        x.h -= y.h + borrow;
        if (!(x.h | x.l)) { r->hi = r->lo = 0; return; }   /* (an exact zero is +0) */
    }
    pack(r, &x);
}

void fp_add(fp64 *r, const fp64 *a, const fp64 *b) { addsub(r, a, b, 0); }
void fp_sub(fp64 *r, const fp64 *a, const fp64 *b) { addsub(r, a, b, 1); }

void fp_mul(fp64 *r, const fp64 *a, const fp64 *b)
{
    struct un x, y, z;
    int ca = unpack(a, &x), cb = unpack(b, &y);
    unsigned s = (unsigned)(x.s ^ y.s) << 31;
    if (ca == NAN_ || cb == NAN_ || (ca == INF && cb == ZERO) || (ca == ZERO && cb == INF)) { *r = QNAN; return; }
    if (ca == INF || cb == INF) { r->hi = 0x7FF00000 | s; r->lo = 0; return; }
    if (ca == ZERO || cb == ZERO) { r->hi = s; r->lo = 0; return; }
    u_mul(&z, &x, &y);
    pack(r, &z);
}

void fp_div(fp64 *r, const fp64 *a, const fp64 *b)
{
    struct un x, y, z;
    int ca = unpack(a, &x), cb = unpack(b, &y);
    unsigned s = (unsigned)(x.s ^ y.s) << 31;
    if (ca == NAN_ || cb == NAN_ || (ca == INF && cb == INF) || (ca == ZERO && cb == ZERO)) { *r = QNAN; return; }
    if (ca == INF || cb == ZERO) { r->hi = 0x7FF00000 | s; r->lo = 0; return; }
    if (ca == ZERO || cb == INF) { r->hi = s; r->lo = 0; return; }
    u_div(&z, &x, &y);
    pack(r, &z);
}

/* 128-bit words for the square root (w[0] lowest) */
static int w4_ge(const unsigned *a, const unsigned *b)
{
    int k;
    for (k = 3; k >= 0; k--) if (a[k] != b[k]) return a[k] > b[k];
    return 1;
}
static void w4_add(unsigned *a, const unsigned *b)
{
    unsigned c = 0, t;
    int k;
    for (k = 0; k < 4; k++) { t = a[k] + b[k]; unsigned c2 = t < a[k]; t += c; c2 |= t < c; a[k] = t; c = c2; }
}
static void w4_sub(unsigned *a, const unsigned *b)
{
    unsigned br = 0, t;
    int k;
    for (k = 0; k < 4; k++) { t = a[k] - b[k]; unsigned b2 = a[k] < b[k]; b2 |= t < br; a[k] = t - br; br = b2; }
}
static void w4_shr(unsigned *a, int n)                  /* n = 1 or 2 */
{
    int k;
    for (k = 0; k < 3; k++) a[k] = (a[k] >> n) | (a[k + 1] << (32 - n));
    a[3] >>= n;
}

void fp_sqrt(fp64 *r, const fp64 *a)
{
    struct un x;
    int c = unpack(a, &x), k;
    unsigned n[4], res[4], bit[4], t[4];
    if (c == NAN_ || (x.s && c != ZERO)) { *r = QNAN; return; }
    if (c == ZERO || c == INF) { *r = *a; return; }
    if (x.e & 1) { shl(&x, 1); x.e--; }
    /* n = m << 61 */
    n[0] = 0; n[1] = x.l << 29; n[2] = (x.l >> 3) | (x.h << 29); n[3] = x.h >> 3;
    for (k = 0; k < 4; k++) res[k] = bit[k] = 0;
    bit[3] = 1u << 28;                  /* 2^124 */
    while (bit[0] | bit[1] | bit[2] | bit[3]) {
        for (k = 0; k < 4; k++) t[k] = res[k];
        w4_add(t, bit);
        w4_shr(res, 1);
        if (w4_ge(n, t)) { w4_sub(n, t); w4_add(res, bit); }
        w4_shr(bit, 2);
    }
    x.h = res[1];
    x.l = res[0] | ((n[0] | n[1] | n[2] | n[3]) != 0);
    x.e /= 2;
    pack(r, &x);
}

void fp_neg(fp64 *r, const fp64 *a) { r->lo = a->lo; r->hi = a->hi ^ 0x80000000; }
void fp_abs(fp64 *r, const fp64 *a) { r->lo = a->lo; r->hi = a->hi & 0x7FFFFFFF; }

void fp_ldexp(fp64 *r, const fp64 *a, int n)
{
    struct un x;
    if (unpack(a, &x) != FINITE) { *r = *a; return; }
    if (n > 5000) n = 5000;
    if (n < -5000) n = -5000;
    x.e += n;
    pack(r, &x);
}

int fp_ilogb(const fp64 *a)
{
    struct un x;
    return unpack(a, &x) == FINITE ? x.e : 0;
}

/* ---------------------------------------------------------------- comparison */
int fp_isnan(const fp64 *a) { return (a->hi & 0x7FF00000) == 0x7FF00000 && ((a->hi & 0xFFFFF) | a->lo); }
int fp_isinf(const fp64 *a) { return (a->hi & 0x7FFFFFFF) == 0x7FF00000 && !a->lo; }
int fp_iszero(const fp64 *a) { return !((a->hi & 0x7FFFFFFF) | a->lo); }
int fp_sign(const fp64 *a) { return fp_isnan(a) || fp_iszero(a) ? 0 : a->hi >> 31 ? -1 : 1; }

int fp_cmp(const fp64 *a, const fp64 *b)
{
    unsigned ah, al, bh, bl;
    if (fp_isnan(a) || fp_isnan(b)) return 2;
    if (fp_iszero(a) && fp_iszero(b)) return 0;
    /* (as unsigned keys that order like the numbers) */
    ah = a->hi >> 31 ? ~a->hi : a->hi | 0x80000000; al = a->hi >> 31 ? ~a->lo : a->lo;
    bh = b->hi >> 31 ? ~b->hi : b->hi | 0x80000000; bl = b->hi >> 31 ? ~b->lo : b->lo;
    if (ah != bh) return ah < bh ? -1 : 1;
    if (al != bl) return al < bl ? -1 : 1;
    return 0;
}

/* ---------------------------------------------------------------- integers */
void fp_fromuint(fp64 *r, unsigned u)
{
    struct un x;
    x.s = 0; x.h = 0; x.l = u; x.e = 61;
    pack(r, &x);
}

void fp_fromint(fp64 *r, int i)
{
    fp_fromuint(r, i < 0 ? 0u - (unsigned)i : (unsigned)i);
    if (i < 0) r->hi |= 0x80000000;
}

int fp_toint(const fp64 *a, int *ok)
{
    struct un x;
    int c = unpack(a, &x), sh;
    unsigned v;
    if (ok) *ok = 1;
    if (c == ZERO) return 0;
    if (c == NAN_) { if (ok) *ok = 0; return 0; }
    if (c == INF || x.e > 31) goto over;
    if (x.e < 0) return 0;
    sh = 61 - x.e;                      /* 30..61: the integer part is m >> sh */
    v = sh >= 32 ? x.h >> (sh - 32) : (x.h << (32 - sh)) | (x.l >> sh);
    if (x.s) { if (v > 0x80000000u) goto over; return (int)(0u - v); }
    if (v > 0x7FFFFFFFu) goto over;
    return (int)v;
over:
    if (ok) *ok = 0;
    return x.s ? (int)0x80000000u : 0x7FFFFFFF;
}

void fp_trunc(fp64 *r, const fp64 *a)
{
    int ex = (a->hi >> 20) & 0x7FF, fb;
    *r = *a;
    if (ex >= 1075) return;             /* already whole (or inf / NaN) */
    if (ex < 1023) { r->hi &= 0x80000000; r->lo = 0; return; }
    fb = 1075 - ex;                     /* fraction bits to clear: 1..52 */
    if (fb >= 32) { r->lo = 0; r->hi &= ~((1u << (fb - 32)) - 1); }
    else r->lo &= ~((1u << fb) - 1);
}

int fp_isint(const fp64 *a)
{
    fp64 t;
    if ((a->hi & 0x7FF00000) == 0x7FF00000) return 0;
    fp_trunc(&t, a);
    return fp_cmp(&t, a) == 0;
}

void fp_floor(fp64 *r, const fp64 *a)
{
    fp64 t;
    fp_trunc(&t, a);
    if ((a->hi >> 31) && fp_cmp(&t, a) != 0) fp_sub(&t, &t, &ONE);
    *r = t;
}

void fp_round(fp64 *r, const fp64 *a)
{
    fp64 t, d;
    fp_trunc(&t, a);
    fp_sub(&d, a, &t);                  /* (exact) */
    fp_abs(&d, &d);
    if (fp_cmp(&d, &HALF) >= 0) {
        if (a->hi >> 31) fp_sub(&t, &t, &ONE);
        else fp_add(&t, &t, &ONE);
    }
    *r = t;
}

/* ---------------------------------------------------------------- decimal */
static struct un p10[9];                /* 10^1, 10^2, 10^4, ... 10^256, to 62 bits */
static int p10_ready;

static void pow10_init(void)
{
    int k;
    p10[0].s = 0; p10[0].h = 0; p10[0].l = 10; p10[0].e = 61;
    normalize(&p10[0]);
    for (k = 1; k < 9; k++) u_mul(&p10[k], &p10[k - 1], &p10[k - 1]);
    p10_ready = 1;
}

/* x *= 10^n (n may be negative; |n| < 512) */
static void scale10(struct un *x, int n)
{
    struct un p;
    int k, neg = n < 0, first = 1;
    if (!n) return;
    if (!p10_ready) pow10_init();
    if (neg) n = -n;
    for (k = 0; k < 9; k++)
        if (n & (1 << k)) {
            if (first) { p = p10[k]; first = 0; }
            else u_mul(&p, &p, &p10[k]);
        }
    if (neg) u_div(x, x, &p);
    else u_mul(x, x, &p);
}

static void mul10_add(unsigned *h, unsigned *l, unsigned d)   /* (h:l) = (h:l) * 10 + d */
{
    unsigned l8 = *l << 3, h8 = (*h << 3) | (*l >> 29), l2 = *l << 1, h2 = (*h << 1) | (*l >> 31), nl;
    nl = l8 + l2;
    *h = h8 + h2 + (nl < l8);
    *l = nl + d;
    if (*l < nl) (*h)++;
}

const char *fp_parse(fp64 *r, const char *s)
{
    struct un x;
    unsigned h = 0, l = 0;
    int neg = 0, nd = 0, dexp = 0, any = 0, sticky = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    while (*s >= '0' && *s <= '9') {
        any = 1;
        if (nd < 19) { if (h || l || *s != '0') { mul10_add(&h, &l, *s - '0'); nd++; } }
        else { dexp++; if (*s != '0') sticky = 1; }
        s++;
    }
    if (*s == '.') {
        const char *t = s + 1;
        if ((*t >= '0' && *t <= '9') || any) {
            s = t;
            while (*s >= '0' && *s <= '9') {
                any = 1;
                if (nd < 19) { if (h || l || *s != '0') { mul10_add(&h, &l, *s - '0'); nd++; } dexp--; }
                else if (*s != '0') sticky = 1;
                s++;
            }
        }
    }
    if (!any) return 0;
    if (*s == 'e' || *s == 'E') {
        const char *t = s + 1;
        int eneg = 0, ev = 0;
        if (*t == '+' || *t == '-') eneg = *t++ == '-';
        if (*t >= '0' && *t <= '9') {
            while (*t >= '0' && *t <= '9') { if (ev < 100000) ev = ev * 10 + (*t - '0'); t++; }
            dexp += eneg ? -ev : ev;
            s = t;
        }
    }
    if (!(h | l)) { r->hi = neg ? 0x80000000 : 0; r->lo = 0; return s; }
    if (dexp + nd > 330) { r->hi = 0x7FF00000 | (neg ? 0x80000000 : 0); r->lo = 0; return s; }
    if (dexp + nd < -350) { r->hi = neg ? 0x80000000 : 0; r->lo = 0; return s; }
    x.s = neg; x.h = h; x.l = l; x.e = 61;
    normalize(&x);
    if (sticky) x.l |= 1;
    if (dexp > 400) dexp = 400;         /* (the checks above keep it in range) */
    if (dexp < -400) dexp = -400;
    if (dexp > 255 || dexp < -255) {    /* (two steps: the table stops at 10^256) */
        int half = dexp / 2;
        scale10(&x, half);
        scale10(&x, dexp - half);
    } else
        scale10(&x, dexp);
    pack(r, &x);
    return s;
}

/* 10^k as a 64-bit integer, k = 0..19 */
static void ipow10(int k, unsigned *h, unsigned *l)
{
    *h = 0; *l = 1;
    while (k-- > 0) mul10_add(h, l, 0);
}

static int lt64(unsigned ah, unsigned al, unsigned bh, unsigned bl) { return ah < bh || (ah == bh && al < bl); }

int fp_digits(const fp64 *a, int ndig, char *digits, int *exp10)
{
    struct un x, w;
    int k, tries, i;
    unsigned h = 0, l = 0, lo_h, lo_l, hi_h, hi_l;
    if (unpack(a, &x) != FINITE) return -1;
    if (ndig < 1) ndig = 1;
    if (ndig > 17) ndig = 17;
    x.s = 0;
    k = (x.e * 78913) >> 18;            /* about floor(log10 |a|) (log10 2 = 78913 / 2^18) */
    ipow10(ndig - 1, &lo_h, &lo_l);
    ipow10(ndig, &hi_h, &hi_l);
    for (tries = 0; tries < 4; tries++) {
        int sh;
        unsigned rb;
        w = x;
        scale10(&w, ndig - 1 - k);
        /* to an integer, rounding half to even */
        sh = 61 - w.e;
        if (sh <= 0) { h = 0xFFFFFFFF; l = 0xFFFFFFFF; }      /* (far too big: k was too small) */
        else if (sh >= 64) { h = 0; l = 0; }
        else {
            struct un f = w;
            unsigned half_h, half_l, mask_h, mask_l, rem_h, rem_l;
            h = w.h; l = w.l;
            if (sh >= 32) { l = h >> (sh - 32); h = 0; if (sh == 32) l = w.h; }
            else { l = (l >> sh) | (h << (32 - sh)); h >>= sh; }
            /* the bits shifted out: compare with a half */
            if (sh >= 32) { mask_h = sh == 32 ? 0 : (1u << (sh - 32)) - 1; mask_l = 0xFFFFFFFF; }
            else { mask_h = 0; mask_l = (1u << sh) - 1; }
            rem_h = f.h & mask_h; rem_l = f.l & mask_l;
            if (sh - 1 >= 32) { half_h = 1u << (sh - 1 - 32); half_l = 0; } else { half_h = 0; half_l = 1u << (sh - 1); }
            rb = rem_h > half_h || (rem_h == half_h && rem_l > half_l) ? 2 : rem_h == half_h && rem_l == half_l ? 1 : 0;
            if (rb == 2 || (rb == 1 && (l & 1))) { l++; if (!l) h++; }
        }
        if (lt64(h, l, lo_h, lo_l)) { k--; continue; }
        if (!lt64(h, l, hi_h, hi_l)) { k++; continue; }
        break;
    }
    for (i = ndig - 1; i >= 0; i--) {   /* the integer's digits, by division by 10 in 16-bit pieces */
        unsigned p[4], rem = 0;
        int j;
        p[3] = h >> 16; p[2] = h & 0xFFFF; p[1] = l >> 16; p[0] = l & 0xFFFF;
        for (j = 3; j >= 0; j--) { unsigned cur = (rem << 16) | p[j]; p[j] = cur / 10; rem = cur % 10; }
        h = p[3] << 16 | p[2];
        l = p[1] << 16 | p[0];
        digits[i] = '0' + rem;
    }
    digits[ndig] = 0;
    *exp10 = k;
    return 0;
}

char *fp_format(char *buf, const fp64 *a, int ndig)
{
    char d[20], *o = buf;
    int x, n, i;
    if (fp_isnan(a)) { strcpy(buf, "nan"); return buf; }
    if (a->hi >> 31) *o++ = '-';
    if (fp_isinf(a)) { strcpy(o, "inf"); return buf; }
    if (fp_iszero(a)) { strcpy(o, "0"); return buf; }
    if (ndig < 1) ndig = 1;
    if (ndig > 17) ndig = 17;
    fp_digits(a, ndig, d, &x);
    for (n = ndig; n > 1 && d[n - 1] == '0'; n--) ;
    if (x < -4 || x >= ndig) {          /* d.ddde+XX */
        *o++ = d[0];
        if (n > 1) { *o++ = '.'; for (i = 1; i < n; i++) *o++ = d[i]; }
        *o++ = 'e';
        *o++ = x < 0 ? '-' : '+';
        if (x < 0) x = -x;
        if (x >= 100) { *o++ = '0' + x / 100; x %= 100; *o++ = '0' + x / 10; }
        else *o++ = '0' + x / 10;
        *o++ = '0' + x % 10;
    } else if (x >= 0) {
        for (i = 0; i <= x; i++) *o++ = i < n ? d[i] : '0';
        if (n > x + 1) { *o++ = '.'; for (i = x + 1; i < n; i++) *o++ = d[i]; }
    } else {
        *o++ = '0';
        *o++ = '.';
        for (i = -1; i > x; i--) *o++ = '0';
        for (i = 0; i < n; i++) *o++ = d[i];
    }
    *o = 0;
    return buf;
}

/* ---------------------------------------------------------------- maths */
static fp64 recip_tab[320];
static unsigned char recip_ok[320];

static const fp64 *recip(int n)        /* 1/n, made once */
{
    if (!recip_ok[n]) {
        fp64 t;
        fp_fromint(&t, n);
        fp_div(&recip_tab[n], &ONE, &t);
        recip_ok[n] = 1;
    }
    return &recip_tab[n];
}

/* exp, log and pow work on unpacked numbers, carrying 62 bits instead of 53
   (so pow's y log x keeps its precision), and round once at the end */
static void u_add(struct un *r, const struct un *a, const struct un *b)    /* (zero: h = l = 0) */
{
    struct un x = *a, y = *b, t;
    if (!(y.h | y.l)) { *r = x; return; }
    if (!(x.h | x.l)) { *r = y; return; }
    if (x.e < y.e || (x.e == y.e && (x.h < y.h || (x.h == y.h && x.l < y.l)))) { t = x; x = y; y = t; }
    shr(&y, x.e - y.e);
    if (x.s == y.s) {
        unsigned l = x.l + y.l;
        x.h += y.h + (l < x.l);
        x.l = l;
    } else {
        unsigned borrow = x.l < y.l;
        x.l -= y.l;
        x.h -= y.h + borrow;
        if (!(x.h | x.l)) { x.s = 0; x.e = 0; *r = x; return; }
    }
    normalize(&x);
    *r = x;
}

static void u_fromint(struct un *u, int i)
{
    u->s = i < 0;
    u->h = 0;
    u->l = i < 0 ? 0u - (unsigned)i : (unsigned)i;
    u->e = 61;
    normalize(u);
}

static struct un u_one, u_ln2, u_ln2hi, u_ln2lo, u_sqrt2, u_rec[40];
static int u_ready;

static void u_init(void)
{
    struct un a, b;
    int n;
    u_fromint(&u_one, 1);
    unpack(&LN2_HI, &a);
    unpack(&LN2_LO, &b);
    u_add(&u_ln2, &a, &b);              /* ln 2 to 62 bits */
    u_ln2hi = a;                        /* and in two parts, for exp's k ln 2 (the first */
    u_ln2lo = b;                        /* of 32 bits: k times it is exact) */
    unpack(&SQRT2, &u_sqrt2);
    for (n = 1; n < 40; n++) { u_fromint(&a, n); u_div(&u_rec[n], &u_one, &a); }
    u_ready = 1;
}

/* e^x for a finite x with |x| < 746 (the caller checks the range) */
static void u_exp(struct un *r, const struct un *x)
{
    struct un kk, t, p;
    fp64 xd, kd;
    int k, n;
    if (!u_ready) u_init();
    t = *x;
    pack(&xd, &t);                      /* (x as a double, to choose k) */
    fp_mul(&kd, &xd, &INV_LN2);
    fp_round(&kd, &kd);
    k = fp_toint(&kd, 0);
    t = *x;
    if (k) {                            /* t = x - k ln 2, |t| <= 0.35 */
        u_fromint(&kk, k);
        u_mul(&p, &kk, &u_ln2hi);
        p.s ^= 1;
        u_add(&t, &t, &p);
        u_mul(&p, &kk, &u_ln2lo);
        p.s ^= 1;
        u_add(&t, &t, &p);
    }
    p = u_one;
    for (n = 18; n >= 1; n--) {         /* 1 + t(1 + t/2(1 + t/3(...))) */
        if (!(t.h | t.l)) break;
        u_mul(&p, &p, &t);
        u_mul(&p, &p, &u_rec[n]);
        u_add(&p, &u_one, &p);
    }
    p.e += k;
    *r = p;
}

/* log x for a finite x > 0 */
static void u_log(struct un *r, const struct un *x)
{
    struct un f = *x, num, den, s, z, q, t, ef, one_neg;
    int e = x->e, d;
    if (!u_ready) u_init();
    f.e = 0;                            /* f in [1, 2) */
    if (f.h > u_sqrt2.h || (f.h == u_sqrt2.h && f.l > u_sqrt2.l)) { f.e = -1; e++; }
    one_neg = u_one;
    one_neg.s = 1;
    u_add(&num, &f, &one_neg);
    u_add(&den, &f, &u_one);
    if (!(num.h | num.l)) s = num;
    else u_div(&s, &num, &den);         /* s = (f - 1)/(f + 1), |s| <= 0.172 */
    u_mul(&z, &s, &s);
    q = u_rec[29];
    for (d = 27; d >= 3; d -= 2) {      /* 1/3 + z/5 + z^2/7 + ... */
        if (!(z.h | z.l)) { q = u_rec[3]; break; }
        u_mul(&q, &q, &z);
        u_add(&q, &q, &u_rec[d]);
    }
    if (s.h | s.l) {
        u_mul(&q, &q, &z);
        u_add(&q, &q, &u_one);
        u_mul(&t, &s, &q);
        t.e++;                          /* log f = 2 s (1 + z/3 + ...) */
    } else
        t = s;
    if (e) {
        u_fromint(&ef, e);
        u_mul(&ef, &ef, &u_ln2);
        u_add(&t, &t, &ef);
    }
    *r = t;
}

void fp_exp(fp64 *r, const fp64 *a)
{
    struct un x, y;
    if (fp_isnan(a)) { *r = QNAN; return; }
    if (fp_cmp(a, &EXP_MAX) > 0) { r->hi = 0x7FF00000; r->lo = 0; return; }
    if (fp_cmp(a, &EXP_MIN) < 0) { r->hi = r->lo = 0; return; }
    if (unpack(a, &x) == ZERO) { *r = ONE; return; }
    u_exp(&y, &x);
    pack(r, &y);
}

void fp_log(fp64 *r, const fp64 *a)
{
    struct un x, y;
    if (fp_isnan(a) || (a->hi >> 31 && !fp_iszero(a))) { *r = QNAN; return; }
    if (fp_iszero(a)) { r->hi = 0xFFF00000; r->lo = 0; return; }
    if (fp_isinf(a)) { *r = *a; return; }
    unpack(a, &x);
    u_log(&y, &x);
    if (!(y.h | y.l)) { r->hi = r->lo = 0; return; }
    pack(r, &y);
}

/* ---------------------------------------------------------------- 96 bits, for pow
   pow's y log x needs more than 62 bits: a relative error e in log x becomes
   an absolute error |y log x| e in the exponent, and so a relative error that
   size in the result (|y log x| up to 745). So log x, the product and the
   reduction by k ln 2 are done in three words (D42), and only the last step,
   e^r for |r| <= 0.35, in 62 bits. A w3 is s, e and a 96-bit m (m[2] the top
   word), its value m / 2^93 * 2^e: the leading 1 at bit 93, a sticky bit 0 */
struct w3 { int s, e; unsigned m[3]; };

static int w3_top(const struct w3 *a)
{
    int i, b;
    for (i = 2; i >= 0; i--)
        if (a->m[i]) {
            for (b = 31; !(a->m[i] >> b & 1); b--) ;
            return 32 * i + b;
        }
    return -1;
}

static void w3_shr(struct w3 *a, int n)  /* right n, the lost bits kept as a sticky bit */
{
    unsigned st = 0;
    while (n >= 32) { st |= a->m[0]; a->m[0] = a->m[1]; a->m[1] = a->m[2]; a->m[2] = 0; n -= 32; if (!(a->m[0] | a->m[1] | a->m[2]) && !st) return; }
    if (n > 0) {
        st |= a->m[0] << (32 - n);
        a->m[0] = a->m[0] >> n | a->m[1] << (32 - n);
        a->m[1] = a->m[1] >> n | a->m[2] << (32 - n);
        a->m[2] >>= n;
    }
    if (st) a->m[0] |= 1;
}

static void w3_shl(struct w3 *a, int n)
{
    while (n >= 32) { a->m[2] = a->m[1]; a->m[1] = a->m[0]; a->m[0] = 0; n -= 32; }
    if (n > 0) {
        a->m[2] = a->m[2] << n | a->m[1] >> (32 - n);
        a->m[1] = a->m[1] << n | a->m[0] >> (32 - n);
        a->m[0] <<= n;
    }
}

static void w3_norm(struct w3 *a)
{
    int t = w3_top(a);
    if (t < 0) { a->e = 0; return; }
    if (t > 93) { w3_shr(a, t - 93); a->e += t - 93; }
    else if (t < 93) { w3_shl(a, 93 - t); a->e -= 93 - t; }
}

static int w3_zero(const struct w3 *a) { return !(a->m[0] | a->m[1] | a->m[2]); }

static void w3_from_un(struct w3 *r, const struct un *u)   /* (exact) */
{
    r->s = u->s; r->e = u->e;
    r->m[2] = u->h; r->m[1] = u->l; r->m[0] = 0;
    w3_norm(r);
}

static void w3_to_un(struct un *u, const struct w3 *a)     /* to 62 bits, the rest sticky */
{
    u->s = a->s; u->e = a->e;
    u->h = a->m[2]; u->l = a->m[1] | (a->m[0] != 0);
    normalize(u);
}

static void w3_fromint(struct w3 *r, int i)
{
    struct un u;
    u_fromint(&u, i);
    w3_from_un(r, &u);
}

static void w3_add(struct w3 *r, const struct w3 *a0, const struct w3 *b0)
{
    struct w3 a = *a0, b = *b0, t;
    unsigned c;
    if (w3_zero(&b)) { *r = a; return; }
    if (w3_zero(&a)) { *r = b; return; }
    if (a.e < b.e || (a.e == b.e && (a.m[2] < b.m[2] || (a.m[2] == b.m[2] && (a.m[1] < b.m[1] || (a.m[1] == b.m[1] && a.m[0] < b.m[0]))))))
        { t = a; a = b; b = t; }
    if (a.e - b.e > 100) { *r = a; return; }
    w3_shr(&b, a.e - b.e);
    if (a.s == b.s) {
        unsigned x0 = a.m[0] + b.m[0], x1, x2;
        c = x0 < a.m[0];
        x1 = a.m[1] + b.m[1] + c;
        c = c ? x1 <= a.m[1] : x1 < a.m[1];
        x2 = a.m[2] + b.m[2] + c;
        a.m[0] = x0; a.m[1] = x1; a.m[2] = x2;
    } else {
        unsigned x0 = a.m[0] - b.m[0], x1, x2;
        c = a.m[0] < b.m[0];
        x1 = a.m[1] - b.m[1] - c;
        c = c ? a.m[1] <= b.m[1] : a.m[1] < b.m[1];
        x2 = a.m[2] - b.m[2] - c;
        a.m[0] = x0; a.m[1] = x1; a.m[2] = x2;
        if (w3_zero(&a)) { a.s = 0; *r = a; return; }
    }
    w3_norm(&a);
    *r = a;
}

static void w3_sub(struct w3 *r, const struct w3 *a, const struct w3 *b)
{
    struct w3 t = *b;
    t.s ^= 1;
    w3_add(r, a, &t);
}

/* a b: the 192-bit product of the mantissas, in 16-bit pieces, the top
   kept (its lower bits sticky) */
static void w3_mul(struct w3 *r, const struct w3 *a, const struct w3 *b)
{
    unsigned x[6], y[6], p[12], t, carry;
    int i, j, k;
    if (w3_zero(a) || w3_zero(b)) { r->s = 0; r->e = 0; r->m[0] = r->m[1] = r->m[2] = 0; return; }
    for (i = 0; i < 3; i++) {
        x[2 * i] = a->m[i] & 0xFFFF; x[2 * i + 1] = a->m[i] >> 16;
        y[2 * i] = b->m[i] & 0xFFFF; y[2 * i + 1] = b->m[i] >> 16;
    }
    for (i = 0; i < 12; i++) p[i] = 0;
    for (i = 0; i < 6; i++) {
        carry = 0;
        for (j = 0; j < 6; j++) {
            t = x[i] * y[j] + p[i + j] + carry;
            p[i + j] = t & 0xFFFF;
            carry = t >> 16;
        }
        p[i + 6] = carry;
    }
    /* the product is m_a m_b / 2^186; its value bits from 93 up are the result's */
    {
        unsigned w[6], st = 0;
        struct w3 q;
        for (k = 0; k < 6; k++) w[k] = p[2 * k] | p[2 * k + 1] << 16;   /* 192 bits, w[0] lowest */
        /* bits 93..191 -> shift right by 93 = 2 words + 29 bits */
        for (k = 0; k < 2; k++) st |= w[k];
        st |= w[2] & ((1u << 29) - 1);
        q.m[0] = w[2] >> 29 | w[3] << 3;
        q.m[1] = w[3] >> 29 | w[4] << 3;
        q.m[2] = w[4] >> 29 | w[5] << 3;
        /* (the top 3 bits of w[5] would overflow: the product of two numbers below 2^94 is below 2^188, so w[5] < 2^28) */
        if (st) q.m[0] |= 1;
        q.e = a->e + b->e;
        q.s = a->s ^ b->s;
        w3_norm(&q);
        *r = q;
    }
}

/* a / b, both normalized and nonzero: 94 quotient bits by subtraction */
static void w3_div(struct w3 *r, const struct w3 *a, const struct w3 *b)
{
    unsigned rm[3], q[3] = { 0, 0, 0 };
    int i;
    rm[0] = a->m[0]; rm[1] = a->m[1]; rm[2] = a->m[2];
    for (i = 93; i >= 0; i--) {
        int ge = rm[2] > b->m[2] || (rm[2] == b->m[2] && (rm[1] > b->m[1] || (rm[1] == b->m[1] && rm[0] >= b->m[0])));
        if (ge) {
            unsigned c = rm[0] < b->m[0], t1;
            rm[0] -= b->m[0];
            t1 = rm[1] - b->m[1] - c;
            c = c ? rm[1] <= b->m[1] : rm[1] < b->m[1];
            rm[1] = t1;
            rm[2] = rm[2] - b->m[2] - c;
            q[i / 32] |= 1u << (i % 32);
        }
        rm[2] = rm[2] << 1 | rm[1] >> 31;
        rm[1] = rm[1] << 1 | rm[0] >> 31;
        rm[0] <<= 1;
    }
    r->m[0] = q[0] | ((rm[0] | rm[1] | rm[2]) != 0);
    r->m[1] = q[1];
    r->m[2] = q[2];
    r->e = a->e - b->e;
    r->s = a->s ^ b->s;
    w3_norm(r);
}

static struct w3 w3_one, w3_ln2, w3_rec[20], w3_logt[17];   /* 1, ln 2, 1/n (odd n), log(1 + j/16) */
static const fp64 LOGT[17][3] = {       /* log(1 + j/16), each in three parts (about 159 bits) */
    { { 0x00000000, 0x00000000 }, { 0x00000000, 0x00000000 }, { 0x00000000, 0x00000000 } },   /* log(1 + 0/16) */
    { { 0xC01162A6, 0x3FAF0A30 }, { 0x5C5BBACD, 0x3C485F32 }, { 0x97165991, 0xB8E0ECE5 } },   /* log(1 + 1/16) */
    { { 0x6E2AF2E6, 0x3FBE2707 }, { 0x001E0162, 0xBC361578 }, { 0x4EBC4018, 0x38B55DB9 } },   /* log(1 + 2/16) */
    { { 0x70A793D4, 0x3FC5FF30 }, { 0xFAFC6F6E, 0xBC5BC60E }, { 0x54719540, 0xB8E14065 } },   /* log(1 + 3/16) */
    { { 0xC79A9A22, 0x3FCC8FF7 }, { 0xF8434012, 0xBC64F689 }, { 0x3B2F53A1, 0x390A24AE } },   /* log(1 + 4/16) */
    { { 0xABABA60E, 0x3FD1675C }, { 0xAB883717, 0x3C2CE63E }, { 0xE82521E1, 0x3881F833 } },   /* log(1 + 5/16) */
    { { 0xC21C5EC2, 0x3FD4618B }, { 0xCDECCF1D, 0x3C7F42DE }, { 0x6996DA00, 0xB9077D44 } },   /* log(1 + 6/16) */
    { { 0xF6BBD007, 0x3FD739D7 }, { 0xEB014B04, 0xBC78C76C }, { 0x10F7918B, 0xB900D2A9 } },   /* log(1 + 7/16) */
    { { 0xECBF984C, 0x3FD9F323 }, { 0x13217F5C, 0xBC4A92E5 }, { 0xA41FF669, 0x38E0C0CF } },   /* log(1 + 8/16) */
    { { 0xC79A9A22, 0x3FDC8FF7 }, { 0xF8434012, 0xBC74F689 }, { 0x3B2F53A1, 0x391A24AE } },   /* log(1 + 9/16) */
    { { 0x5FAF06ED, 0x3FDF128F }, { 0x13BB38C3, 0xBC7328DF }, { 0x92445D0A, 0x391D73D5 } },   /* log(1 + 10/16) */
    { { 0xE4252A83, 0x3FE0BE72 }, { 0x11330801, 0xBC8259DA }, { 0xD9BEEFCD, 0x391A6D90 } },   /* log(1 + 11/16) */
    { { 0x5E7040D0, 0x3FE1E85F }, { 0xD2F9F1E3, 0x3C7EF62C }, { 0x293D205E, 0x3917CB9F } },   /* log(1 + 12/16) */
    { { 0x334F10BE, 0x3FE307D7 }, { 0xA1F566DA, 0x3C6FB590 }, { 0xA47F6664, 0xB9008F3F } },   /* log(1 + 13/16) */
    { { 0xE84672AE, 0x3FE41D8F }, { 0x30BD1806, 0x3C89192F }, { 0xEDE45763, 0xB910D58E } },   /* log(1 + 14/16) */
    { { 0x265BC5AB, 0x3FE52A2D }, { 0x50EA4D0A, 0xBC618837 }, { 0xF6DF095B, 0xB8F58412 } },   /* log(1 + 15/16) */
    { { 0xFEFA39EF, 0x3FE62E42 }, { 0x3B39803F, 0x3C7ABC9E }, { 0x079A1934, 0x3907B57A } },   /* log(1 + 16/16) */
};
static int w3_ready;

static void w3_init(void)
{
    static const fp64 L[3] = { { 0xFEFA39EF, 0x3FE62E42 }, { 0x3B39803F, 0x3C7ABC9E }, { 0x079A1934, 0x3907B57A } };
    struct un u;
    struct w3 t;
    int k;
    if (!u_ready) u_init();
    w3_fromint(&w3_one, 1);
    w3_ln2.s = 0; w3_ln2.e = 0; w3_ln2.m[0] = w3_ln2.m[1] = w3_ln2.m[2] = 0;
    for (k = 2; k >= 0; k--) {          /* ln 2 from its three parts, the smallest first */
        unpack(&L[k], &u);
        w3_from_un(&t, &u);
        w3_add(&w3_ln2, &w3_ln2, &t);
    }
    for (k = 1; k < 17; k++) {
        int i;
        w3_logt[k].s = 0; w3_logt[k].e = 0; w3_logt[k].m[0] = w3_logt[k].m[1] = w3_logt[k].m[2] = 0;
        for (i = 2; i >= 0; i--) {
            unpack(&LOGT[k][i], &u);
            w3_from_un(&t, &u);
            w3_add(&w3_logt[k], &w3_logt[k], &t);
        }
    }
    for (k = 1; k < 20; k += 2) { w3_fromint(&t, k); w3_div(&w3_rec[k], &w3_one, &t); }
    w3_ready = 1;
}

/* log x for a finite x > 0, in 96 bits. x = 2^e f, f in [1, 2), and c =
   1 + j/16 the nearest sixteenth: log x = e ln 2 + log c + 2 atanh(s),
   s = (f - c)/(f + c), |s| <= 1/64, so eight terms of the series do */
static void w3_log(struct w3 *r, const struct un *x)
{
    struct w3 f, c, num, den, s, z, q, ek;
    int e = x->e, k, j;
    if (!w3_ready) w3_init();
    w3_from_un(&f, x);
    f.e = 0;
    j = (f.m[2] >> 25 & 15) + (f.m[2] >> 24 & 1);     /* the four bits after the point, rounded */
    w3_fromint(&c, 16 + j);
    c.e -= 4;
    w3_sub(&num, &f, &c);               /* (exact) */
    w3_add(&den, &f, &c);
    if (w3_zero(&num)) q = num;
    else {
        w3_div(&s, &num, &den);
        w3_mul(&z, &s, &s);
        q = w3_rec[17];
        for (k = 7; k >= 0; k--) {      /* 1 + z/3 + z^2/5 + ... */
            w3_mul(&q, &q, &z);
            w3_add(&q, &w3_rec[2 * k + 1], &q);
        }
        w3_mul(&q, &q, &s);
        q.e++;
    }
    if (j) w3_add(&q, &q, &w3_logt[j]);
    if (e) {
        w3_fromint(&ek, e);
        w3_mul(&ek, &ek, &w3_ln2);
        w3_add(&q, &q, &ek);
    }
    *r = q;
}

void fp_pow(fp64 *r, const fp64 *a, const fp64 *b)
{
    fp64 base, acc, td;
    struct un x, y, t;
    int ok, n, neg;
    if (fp_iszero(b)) { *r = ONE; return; }
    if (fp_isnan(a) || fp_isnan(b)) { *r = QNAN; return; }
    n = fp_toint(b, &ok);
    if (ok && fp_isint(b) && (n < 0 ? -n : n) <= 64) {     /* a small whole power: by squaring (exact when it can be) */
        unsigned u;
        neg = n < 0;
        u = neg ? 0u - (unsigned)n : (unsigned)n;
        acc = ONE;
        base = *a;
        while (u) {
            if (u & 1) fp_mul(&acc, &acc, &base);
            u >>= 1;
            if (u) fp_mul(&base, &base, &base);
        }
        if (neg) fp_div(&acc, &ONE, &acc);
        *r = acc;
        return;
    }
    if (fp_iszero(a)) {
        if (b->hi >> 31) { r->hi = 0x7FF00000; r->lo = 0; } else { r->hi = r->lo = 0; }
        return;
    }
    neg = 0;
    if (a->hi >> 31) {                  /* a negative base: only for whole powers */
        if (!fp_isint(b)) { *r = QNAN; return; }
        fp_ldexp(&td, b, -1);
        neg = !fp_isint(&td);           /* (an odd power keeps the sign) */
    }
    if (fp_isinf(a) || fp_isinf(b)) {
        fp64 m;
        fp_abs(&m, a);
        if (fp_isinf(b)) {
            int big = fp_cmp(&m, &ONE);
            if (big == 0) { *r = ONE; return; }
            r->lo = 0;
            r->hi = (big > 0) == !(b->hi >> 31) ? 0x7FF00000 : 0;
            return;
        }
        r->lo = 0;
        r->hi = (b->hi >> 31 ? 0 : 0x7FF00000) | (neg ? 0x80000000 : 0);
        return;
    }
    unpack(a, &x);
    x.s = 0;
    unpack(b, &y);
    {                                   /* y log |a| in 96 bits (D42) */
        struct w3 L, Y, T, K;
        fp64 kd;
        int k;
        w3_log(&L, &x);
        if (w3_zero(&L)) { *r = ONE; return; }
        w3_from_un(&Y, &y);
        w3_mul(&T, &Y, &L);
        w3_to_un(&t, &T);
        y = t;
        pack(&td, &y);                  /* (pack changes what it is given: a copy) */
        if (fp_cmp(&td, &EXP_MAX) > 0) { r->hi = 0x7FF00000 | (neg ? 0x80000000 : 0); r->lo = 0; return; }
        if (fp_cmp(&td, &EXP_MIN) < 0) { r->hi = neg ? 0x80000000 : 0; r->lo = 0; return; }
        fp_mul(&kd, &td, &INV_LN2);     /* k = round(t / ln 2); r = t - k ln 2, in 96 bits */
        fp_round(&kd, &kd);
        k = fp_toint(&kd, 0);
        if (k) {
            w3_fromint(&K, k);
            w3_mul(&K, &K, &w3_ln2);
            w3_sub(&T, &T, &K);
        }
        w3_to_un(&t, &T);
        if (!(t.h | t.l)) x = u_one;
        else u_exp(&x, &t);             /* (|t| <= 0.35: its own k is 0) */
        x.e += k;
    }
    x.s = neg;
    pack(r, &x);
}

/* ---------------------------------------------------------------- the rest of
   the maths library's core, as exp and log: on unpacked numbers, carrying 62
   bits, rounded once at the end. With 9 bits to spare, the result is the
   correctly rounded one unless the exact value lies within about 2^-60 of
   it of a rounding boundary (D37: about 99% of results, as the Mac's libm) */
static void u_sub(struct un *r, const struct un *a, const struct un *b)
{
    struct un t = *b;
    t.s ^= 1;
    u_add(r, a, &t);
}

static int u_iszero(const struct un *u) { return !(u->h | u->l); }

static int u_gt(const struct un *a, const struct un *b)     /* |a| > |b| (normalized, nonzero) */
{
    if (a->e != b->e) return a->e > b->e;
    return a->h > b->h || (a->h == b->h && a->l > b->l);
}

/* the square root of a finite a > 0: the double's, then one Newton step */
static void u_sqrt(struct un *r, const struct un *a)
{
    struct un t = *a, s, q;
    fp64 d;
    pack(&d, &t);
    fp_sqrt(&d, &d);
    unpack(&d, &s);
    u_div(&q, a, &s);
    u_add(&s, &s, &q);
    s.e--;
    *r = s;
}

static struct un u_p1, u_p2, u_p3, u_p3t, u_pio2, u_pi, u_rs[14], u_rc[14], u_inv_ln10, u_inv_ln2, u_atan8[9];
static const fp64 ATAN8[9][2] = {       /* atan(j/8) in two parts (about 106 bits) */
    { { 0x00000000, 0x00000000 }, { 0x00000000, 0x00000000 } },
    { { 0x9AAC2F6E, 0x3FBFD5BA }, { 0x86760C17, 0xBC4CD376 } },
    { { 0xF92C80DD, 0x3FCF5B75 }, { 0x3CF7AFBD, 0x3C68AB6E } },
    { { 0x41E4DEF1, 0x3FD6F619 }, { 0xE6F6E918, 0xBC7C63AA } },
    { { 0x0561BB4F, 0x3FDDAC67 }, { 0x222F65E2, 0x3C7A2B7F } },
    { { 0xABDEFEB4, 0x3FE1E00B }, { 0x287A668F, 0xBC5928DF } },
    { { 0xA3269EE1, 0x3FE4978F }, { 0x87F2A458, 0x3C72419A } },
    { { 0xC5784634, 0x3FE700A7 }, { 0x25AADEF6, 0xBC78C34D } },
    { { 0x54442D18, 0x3FE921FB }, { 0x33145C07, 0x3C81A626 } },
};
static int v_ready;

static void v_init(void)
{
    static const fp64 P3T = { 0x252049C1, 0x397B839A };    /* pi/2's fourth part */
    struct un a;
    int k;
    if (!u_ready) u_init();
    unpack(&PIO2_1, &u_p1);
    unpack(&PIO2_2, &u_p2);
    unpack(&PIO2_3, &u_p3);
    unpack(&P3T, &u_p3t);
    u_add(&u_pio2, &u_p3, &u_p3t);      /* pi/2 to 62 bits */
    u_add(&u_pio2, &u_p2, &u_pio2);
    u_add(&u_pio2, &u_p1, &u_pio2);
    u_pi = u_pio2;
    u_pi.e++;
    for (k = 1; k < 14; k++) {          /* 1/((2k)(2k+1)), 1/((2k-1)(2k)) */
        u_fromint(&a, 2 * k * (2 * k + 1));
        u_div(&u_rs[k], &u_one, &a);
        u_fromint(&a, (2 * k - 1) * 2 * k);
        u_div(&u_rc[k], &u_one, &a);
    }
    u_fromint(&a, 10);
    u_log(&a, &a);
    u_div(&u_inv_ln10, &u_one, &a);
    u_div(&u_inv_ln2, &u_one, &u_ln2);
    for (k = 1; k < 9; k++) {
        struct un lo;
        unpack(&ATAN8[k][0], &u_atan8[k]);
        unpack(&ATAN8[k][1], &lo);
        u_add(&u_atan8[k], &u_atan8[k], &lo);
    }
    v_ready = 1;
}

/* a = k pi/2 + y for a finite a with |a| < 2^19 or so (fpmath.c reduces
   bigger ones first): y, and k mod 4. pi/2 is in four parts, the first
   three of 33 bits, so that k times each is exact */
static int u_reduce(struct un *y, const fp64 *a)
{
    fp64 t, kd;
    struct un kk, p;
    int k;
    if (!v_ready) v_init();
    unpack(a, y);
    fp_mul(&t, a, &TWO_OVER_PI);
    fp_round(&kd, &t);
    k = fp_toint(&kd, 0);
    if (!k) return 0;
    u_fromint(&kk, k);
    u_mul(&p, &kk, &u_p1); u_sub(y, y, &p);
    u_mul(&p, &kk, &u_p2); u_sub(y, y, &p);
    u_mul(&p, &kk, &u_p3); u_sub(y, y, &p);
    u_mul(&p, &kk, &u_p3t); u_sub(y, y, &p);
    return k & 3;
}

/* sin y and cos y, |y| <= pi/4 (or a little more) */
static void u_sin_small(struct un *r, const struct un *y)
{
    struct un z, c = u_one, t;
    int k;
    if (u_iszero(y)) { *r = *y; return; }
    u_mul(&z, y, y);
    for (k = 13; k >= 1; k--) {         /* 1 - z/6 (1 - z/20 (1 - ...)) */
        u_mul(&t, &z, &c);
        u_mul(&t, &t, &u_rs[k]);
        u_sub(&c, &u_one, &t);
    }
    u_mul(r, y, &c);
}

static void u_cos_small(struct un *r, const struct un *y)
{
    struct un z, c = u_one, t;
    int k;
    if (u_iszero(y)) { *r = u_one; return; }
    u_mul(&z, y, y);
    for (k = 13; k >= 1; k--) {         /* 1 - z/2 (1 - z/12 (1 - ...)) */
        u_mul(&t, &z, &c);
        u_mul(&t, &t, &u_rc[k]);
        u_sub(&c, &u_one, &t);
    }
    *r = c;
}

void fp_sin(fp64 *r, const fp64 *a)
{
    struct un y, s;
    int q;
    if (fp_isnan(a) || fp_isinf(a)) { *r = QNAN; return; }
    if (fp_iszero(a)) { *r = *a; return; }
    q = u_reduce(&y, a);
    if (q & 1) u_cos_small(&s, &y); else u_sin_small(&s, &y);
    if (q & 2) s.s ^= 1;
    pack(r, &s);
}

void fp_cos(fp64 *r, const fp64 *a)
{
    struct un y, c;
    int q;
    if (fp_isnan(a) || fp_isinf(a)) { *r = QNAN; return; }
    q = u_reduce(&y, a);
    if (q & 1) u_sin_small(&c, &y); else u_cos_small(&c, &y);
    if (q == 1 || q == 2) c.s ^= 1;
    pack(r, &c);
}

void fp_tan(fp64 *r, const fp64 *a)
{
    struct un y, s, c, t;
    int q;
    if (fp_isnan(a) || fp_isinf(a)) { *r = QNAN; return; }
    if (fp_iszero(a)) { *r = *a; return; }
    q = u_reduce(&y, a);
    u_sin_small(&s, &y);
    u_cos_small(&c, &y);
    if (q & 1) { u_div(&t, &c, &s); t.s ^= 1; }      /* tan(y + pi/2) = -cot y */
    else u_div(&t, &s, &c);
    pack(r, &t);
}

/* atan t, t > 0: 1/t past 1 (then pi/2 minus); then, with c = j/8 the
   nearest eighth, atan t = atan c + atan((t - c)/(1 + t c)), the second by
   its series (|argument| < 1/16) */
static void u_atan(struct un *r, const struct un *t0)
{
    struct un t = *t0, u, c, d, z, q;
    fp64 td, eight = { 0, 0x40200000 };
    int k, j, inv = 0;
    if (!v_ready) v_init();
    if (u_gt(&t, &u_one)) { u_div(&t, &u_one, &t); inv = 1; }
    u = t;
    pack(&td, &u);                      /* (8t as a double, to choose j) */
    fp_mul(&td, &td, &eight);
    fp_round(&td, &td);
    j = fp_toint(&td, 0);
    u = t;
    if (j) {
        u_fromint(&c, j);
        c.e -= 3;
        u_sub(&u, &t, &c);              /* (exact) */
        if (!u_iszero(&u)) {
            u_mul(&d, &t, &c);
            u_add(&d, &u_one, &d);
            u_div(&u, &u, &d);
        }
    }
    if (!u_iszero(&u)) {
        u_mul(&z, &u, &u);
        q = u_rec[19];
        for (k = 8; k >= 0; k--) {      /* 1 - z/3 + z^2/5 - ... */
            u_mul(&q, &q, &z);
            u_sub(&q, &u_rec[2 * k + 1], &q);
        }
        u_mul(&u, &u, &q);
    }
    if (j) u_add(&u, &u_atan8[j], &u);
    if (inv) u_sub(&u, &u_pio2, &u);
    *r = u;
}

void fp_atan(fp64 *r, const fp64 *a)
{
    struct un t;
    int neg = a->hi >> 31;
    if (fp_isnan(a)) { *r = QNAN; return; }
    if (fp_iszero(a)) { *r = *a; return; }
    if (!v_ready) v_init();
    if (fp_isinf(a)) t = u_pio2;
    else { unpack(a, &t); t.s = 0; u_atan(&t, &t); }
    t.s = neg;
    pack(r, &t);
}

/* the angle of (x, y) for x > 0, y > 0: atan(y/x), or pi/2 - atan(x/y) */
static void u_angle(struct un *r, const struct un *y, const struct un *x)
{
    struct un t;
    if (u_gt(y, x)) { u_div(&t, x, y); u_atan(&t, &t); u_sub(r, &u_pio2, &t); }
    else { u_div(&t, y, x); u_atan(r, &t); }
}

/* atan2 for finite, nonzero x and y (fpmath.c handles the rest) */
void fp_atan2(fp64 *r, const fp64 *y, const fp64 *x)
{
    struct un uy, ux, t;
    if (!v_ready) v_init();
    unpack(y, &uy);
    unpack(x, &ux);
    uy.s = ux.s = 0;
    u_angle(&t, &uy, &ux);
    if (x->hi >> 31) u_sub(&t, &u_pi, &t);
    t.s = y->hi >> 31;
    pack(r, &t);
}

/* sqrt((1 - a)(1 + a)) for |a| <= 1, its factors exact in 62 bits */
static void u_cosof(struct un *r, const struct un *a)
{
    struct un m, p, ua = *a;
    ua.s = 0;
    u_sub(&m, &u_one, &ua);
    u_add(&p, &u_one, &ua);
    if (u_iszero(&m)) { *r = m; return; }
    u_mul(&m, &m, &p);
    u_sqrt(r, &m);
}

/* asin and acos of a finite a with |a| <= 1 (fpmath.c checks) */
void fp_asin(fp64 *r, const fp64 *a)
{
    struct un ua, c, t;
    if (fp_iszero(a)) { *r = *a; return; }
    if (!v_ready) v_init();
    unpack(a, &ua);
    u_cosof(&c, &ua);
    ua.s = 0;
    if (u_iszero(&c)) t = u_pio2;
    else u_angle(&t, &ua, &c);
    t.s = a->hi >> 31;
    pack(r, &t);
}

void fp_acos(fp64 *r, const fp64 *a)
{
    struct un ua, c, t;
    if (!v_ready) v_init();
    if (fp_iszero(a)) { t = u_pio2; pack(r, &t); return; }
    unpack(a, &ua);
    u_cosof(&c, &ua);
    if (u_iszero(&c)) { if (a->hi >> 31) { t = u_pi; pack(r, &t); } else r->hi = r->lo = 0; return; }
    ua.s = 0;
    u_angle(&t, &c, &ua);
    if (a->hi >> 31) u_sub(&t, &u_pi, &t);
    pack(r, &t);
}

/* e^x - 1 for a finite x, |x| < 750: the series below 1/2 (no cancellation) */
static void u_expm1(struct un *r, const struct un *x)
{
    struct un p, half = u_one;
    int n;
    if (!u_ready) u_init();
    half.e = -1;
    if (u_iszero(x)) { *r = *x; return; }
    if (u_gt(&half, x)) {
        p = u_one;
        for (n = 26; n >= 2; n--) {     /* x (1 + x/2 (1 + x/3 (...))) */
            u_mul(&p, &p, x);
            u_mul(&p, &p, &u_rec[n]);
            u_add(&p, &u_one, &p);
        }
        u_mul(r, &p, x);
    } else {
        u_exp(&p, x);
        u_sub(r, &p, &u_one);
    }
}

void fp_expm1(fp64 *r, const fp64 *a)     /* (fpmath.c: the ranges, -64 < a < 709) */
{
    struct un x, t;
    unpack(a, &x);
    u_expm1(&t, &x);
    pack(r, &t);
}

/* log(1 + x) for x > -1: 2 atanh(x/(2 + x)) by its series when that is small */
static void u_log1p(struct un *r, const struct un *x0)
{
    struct un x = *x0, s, d, z, q, two = u_one;
    int k;
    if (u_iszero(&x)) { *r = x; return; }
    if (!u_ready) u_init();
    two.e = 1;
    u_add(&d, &two, &x);
    u_div(&s, &x, &d);
    if (s.e < -2) {                     /* |s| < 1/4: 2s (1 + z/3 + z^2/5 + ...) */
        u_mul(&z, &s, &s);
        q = u_rec[33];
        for (k = 15; k >= 0; k--) {
            u_mul(&q, &q, &z);
            u_add(&q, &u_rec[2 * k + 1], &q);
        }
        u_mul(&q, &q, &s);
        q.e++;
    } else {
        u_add(&d, &u_one, &x);
        u_log(&q, &d);
    }
    *r = q;
}

void fp_log1p(fp64 *r, const fp64 *a)
{
    struct un x, t;
    if (fp_iszero(a)) { *r = *a; return; }
    unpack(a, &x);
    u_log1p(&t, &x);
    pack(r, &t);
}

/* asinh |a| = log(|a| + sqrt(a^2 + 1)), as log1p(|a| + a^2/(1 + sqrt(1 + a^2)))
   below 1/2; a finite */
void fp_asinh(fp64 *r, const fp64 *a)
{
    struct un x, w, t, half = u_one;
    if (fp_iszero(a)) { *r = *a; return; }
    if (!u_ready) u_init();
    half.e = -1;
    unpack(a, &x);
    x.s = 0;
    u_mul(&w, &x, &x);
    u_add(&w, &w, &u_one);
    u_sqrt(&w, &w);
    if (u_gt(&half, &x)) {
        u_mul(&t, &x, &x);
        u_add(&w, &w, &u_one);
        u_div(&t, &t, &w);
        u_add(&t, &x, &t);
        u_log1p(&t, &t);
    } else {
        u_add(&t, &x, &w);
        u_log(&t, &t);
    }
    t.s = a->hi >> 31;
    pack(r, &t);
}

/* acosh a = log1p(d + sqrt(d (a + 1))), d = a - 1 (exact); a >= 1 finite */
void fp_acosh(fp64 *r, const fp64 *a)
{
    struct un x, d, t;
    if (!u_ready) u_init();
    unpack(a, &x);
    u_sub(&d, &x, &u_one);
    if (u_iszero(&d)) { r->hi = r->lo = 0; return; }
    u_add(&t, &x, &u_one);
    u_mul(&t, &d, &t);
    u_sqrt(&t, &t);
    u_add(&t, &d, &t);
    u_log1p(&t, &t);
    pack(r, &t);
}

/* atanh a = log1p(2|a| / (1 - |a|)) / 2 for |a| < 1 */
void fp_atanh(fp64 *r, const fp64 *a)
{
    struct un x, d, t;
    if (fp_iszero(a)) { *r = *a; return; }
    if (!u_ready) u_init();
    unpack(a, &x);
    x.s = 0;
    u_sub(&d, &u_one, &x);
    u_div(&t, &x, &d);
    t.e++;
    u_log1p(&t, &t);
    t.e--;
    t.s = a->hi >> 31;
    pack(r, &t);
}

/* log2 and log10 of a finite a > 0 (fpmath.c: the rest, and exact powers of ten) */
void fp_log2(fp64 *r, const fp64 *a)
{
    struct un x, t;
    if (!v_ready) v_init();
    unpack(a, &x);
    u_log(&t, &x);
    if (!u_iszero(&t)) u_mul(&t, &t, &u_inv_ln2);
    pack(r, &t);
}

void fp_log10(fp64 *r, const fp64 *a)
{
    struct un x, t;
    if (!v_ready) v_init();
    unpack(a, &x);
    u_log(&t, &x);
    if (!u_iszero(&t)) u_mul(&t, &t, &u_inv_ln10);
    pack(r, &t);
}

/* sinh, cosh, tanh for a finite a with |a| < 22 (fpmath.c: the rest) */
void fp_sinh(fp64 *r, const fp64 *a)
{
    struct un x, e, d;
    if (fp_iszero(a)) { *r = *a; return; }
    unpack(a, &x);
    x.s = 0;
    u_expm1(&e, &x);                    /* (e + e/(e + 1)) / 2, e = e^|a| - 1 */
    u_add(&d, &e, &u_one);
    u_div(&d, &e, &d);
    u_add(&e, &e, &d);
    e.e--;
    e.s = a->hi >> 31;
    pack(r, &e);
}

void fp_cosh(fp64 *r, const fp64 *a)
{
    struct un x, e, d;
    if (!u_ready) u_init();
    if (fp_iszero(a)) { *r = ONE; return; }
    unpack(a, &x);
    x.s = 0;
    u_exp(&e, &x);                      /* (e + 1/e) / 2 */
    u_div(&d, &u_one, &e);
    u_add(&e, &e, &d);
    e.e--;
    pack(r, &e);
}

void fp_tanh(fp64 *r, const fp64 *a)
{
    struct un x, e, d, two = u_one;
    if (fp_iszero(a)) { *r = *a; return; }
    if (!u_ready) u_init();
    two.e = 1;
    unpack(a, &x);
    x.s = 0;
    x.e++;
    u_expm1(&e, &x);                    /* e / (e + 2), e = e^(2|a|) - 1 */
    u_add(&d, &e, &two);
    u_div(&e, &e, &d);
    e.s = a->hi >> 31;
    pack(r, &e);
}

/* the cube root of a finite, nonzero a, given y within an ulp or two of it:
   one Newton step in 62 bits, y + (a/y^2 - y)/3 */
void fp_cbrt_step(fp64 *r, const fp64 *a, const fp64 *y)
{
    struct un x, t, q, d;
    if (!u_ready) u_init();
    unpack(a, &x);
    unpack(y, &t);
    u_mul(&q, &t, &t);
    u_div(&q, &x, &q);
    u_sub(&d, &q, &t);
    if (!u_iszero(&d)) {
        u_mul(&d, &d, &u_rec[3]);
        u_add(&t, &t, &d);
    }
    pack(r, &t);
}
