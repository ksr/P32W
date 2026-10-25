/* fpmath.c -- the maths library on fp64 (fplib.h): C89's math.h and the
 * common C99 additions, for libc's double wrappers.
 *
 * The exact ones (floor, ceil, trunc, round, rint, fmod, remainder, modf,
 * frexp, ldexp, sqrt, copysign, fmin, fmax, fdim, nextafter, ...) give the
 * IEEE result bit for bit. The others are within a few ulp, built on
 * softfp's exp, log, pow, sin, cos, tan and atan, with C99's (Annex F)
 * special cases: signed zeros, infinities, NaN in -> NaN out, NaN for a
 * domain error.
 *
 * sin, cos and tan of a big argument (|x| >= 2^19) reduce it here, by
 * Payne and Hanek's method: x * 2/pi is worked out exactly enough from a
 * 1232-bit table of 2/pi, keeping only the bits that matter (the quadrant
 * and 176 bits of the fraction), so sin(1e300) is as good as sin(1).
 * softfp's own reduction is good below that. */
#include <softfp.h>
#include <fplib.h>

static const fp64 ZERO = { 0, 0 };
static const fp64 ONE = { 0, 0x3FF00000 };
static const fp64 TWO = { 0, 0x40000000 };
static const fp64 HALF = { 0, 0x3FE00000 };
static const fp64 THIRD = { 0x55555555, 0x3FD55555 };
static const fp64 QNAN = { 0, 0x7FF80000 };
static const fp64 INF = { 0, 0x7FF00000 };
static const fp64 PI = { 0x54442D18, 0x400921FB };
static const fp64 PI_LO = { 0x33145C07, 0x3CA1A626 };     /* pi - PI */
static const fp64 PI_2 = { 0x54442D18, 0x3FF921FB };
static const fp64 PI_2_LO = { 0x33145C07, 0x3C91A626 };
static const fp64 EXP_MAX = { 0xFEFA39EF, 0x40862E42 };   /* 709.782712893384: exp overflows above */
static const fp64 TWENTY2 = { 0, 0x40360000 };
static const fp64 TEN[23] = {          /* 10^0 .. 10^22 (exact) */
    { 0, 0x3FF00000 }, { 0, 0x40240000 }, { 0, 0x40590000 }, { 0, 0x408F4000 },
    { 0, 0x40C38800 }, { 0, 0x40F86A00 }, { 0, 0x412E8480 }, { 0, 0x416312D0 },
    { 0, 0x4197D784 }, { 0, 0x41CDCD65 }, { 0x20000000, 0x4202A05F }, { 0xE8000000, 0x42374876 },
    { 0xA2000000, 0x426D1A94 }, { 0xE5400000, 0x42A2309C }, { 0x1E900000, 0x42D6BCC4 },
    { 0x26340000, 0x430C6BF5 }, { 0x37E08000, 0x4341C379 }, { 0x85D8A000, 0x43763457 },
    { 0x674EC800, 0x43ABC16D }, { 0x60913D00, 0x43E158E4 }, { 0x78B58C40, 0x4415AF1D },
    { 0xD6E2EF50, 0x444B1AE4 }, { 0x064DD592, 0x4480F0CF }
};

/* ---------------------------------------------------------------- bits */
static int expo(const fp64 *a) { return (a->hi >> 20) & 0x7FF; }         /* the biased exponent field */
static int isnan_(const fp64 *a) { return expo(a) == 0x7FF && ((a->hi & 0xFFFFF) | a->lo); }
static int isinf_(const fp64 *a) { return (a->hi & 0x7FFFFFFF) == 0x7FF00000 && !a->lo; }
static int iszero(const fp64 *a) { return !((a->hi & 0x7FFFFFFF) | a->lo); }
static int neg(const fp64 *a) { return a->hi >> 31; }
static void quiet(fp64 *r, const fp64 *a) { *r = *a; r->hi |= 0x80000; }  /* a NaN, quieted */

int __fp_isnan(const fp64 *a) { return isnan_(a); }
int __fp_isinf(const fp64 *a) { return isinf_(a); }
int __fp_isfinite(const fp64 *a) { return expo(a) != 0x7FF; }
int __fp_signbit(const fp64 *a) { return neg(a); }

int __fp_fpclassify(const fp64 *a)
{
    int e = expo(a);
    if (e == 0x7FF) return isnan_(a) ? 0 : 1;   /* FP_NAN, FP_INFINITE */
    if (!e) return iszero(a) ? 2 : 3;           /* FP_ZERO, FP_SUBNORMAL */
    return 4;                                   /* FP_NORMAL */
}

void __fp_fabs(fp64 *r, const fp64 *a) { r->lo = a->lo; r->hi = a->hi & 0x7FFFFFFF; }

void __fp_copysign(fp64 *r, const fp64 *a, const fp64 *b)
{
    r->lo = a->lo;
    r->hi = (a->hi & 0x7FFFFFFF) | (b->hi & 0x80000000);
}

/* ---------------------------------------------------------------- whole numbers */
void __fp_trunc(fp64 *r, const fp64 *a) { if (isnan_(a)) quiet(r, a); else fp_trunc(r, a); }
void __fp_floor(fp64 *r, const fp64 *a) { if (isnan_(a)) quiet(r, a); else fp_floor(r, a); }

void __fp_ceil(fp64 *r, const fp64 *a)
{
    fp64 t;
    if (isnan_(a)) { quiet(r, a); return; }
    fp_neg(&t, a);
    fp_floor(&t, &t);
    fp_neg(r, &t);
}

void __fp_round(fp64 *r, const fp64 *a)
{
    if (expo(a) >= 1075) { if (isnan_(a)) quiet(r, a); else *r = *a; return; }    /* whole already */
    fp_round(r, a);
    r->hi |= a->hi & 0x80000000;        /* (round(-0.4) is -0) */
}

void __fp_rint(fp64 *r, const fp64 *a)
{
    static const fp64 TWO52 = { 0, 0x43300000 };
    fp64 t;
    if (expo(a) >= 1075) { if (isnan_(a)) quiet(r, a); else *r = *a; return; }
    __fp_fabs(&t, a);
    fp_add(&t, &t, &TWO52);             /* (the addition rounds half to even) */
    fp_sub(&t, &t, &TWO52);
    t.hi |= a->hi & 0x80000000;
    *r = t;
}

void __fp_modf(fp64 *r, const fp64 *a, fp64 *ip)
{
    fp64 w;
    if (isnan_(a)) { quiet(r, a); *ip = *r; return; }
    fp_trunc(&w, a);
    *ip = w;
    if (isinf_(a)) { r->lo = 0; r->hi = a->hi & 0x80000000; return; }
    fp_sub(r, a, &w);                   /* (exact) */
    r->hi = (r->hi & 0x7FFFFFFF) | (a->hi & 0x80000000);
}

/* ---------------------------------------------------------------- remainders */
/* (h:l) for fmod: the significand as a 53-bit integer, with its exponent */
static void sig(const fp64 *a, unsigned *h, unsigned *l, int *e)
{
    *e = expo(a);
    *h = a->hi & 0xFFFFF;
    *l = a->lo;
    if (*e) *h |= 0x100000;
    else {                              /* subnormal: normalized, the exponent below 1 */
        *e = 1;
        while (!(*h & 0x100000)) { *h = (*h << 1) | (*l >> 31); *l <<= 1; (*e)--; }
    }
}

void __fp_fmod(fp64 *r, const fp64 *a, const fp64 *b)
{
    unsigned xh, xl, yh, yl, th, tl;
    int ex, ey, c;
    unsigned s = a->hi & 0x80000000;
    fp64 aa, bb;
    if (isnan_(a)) { quiet(r, a); return; }
    if (isnan_(b)) { quiet(r, b); return; }
    if (iszero(b) || isinf_(a)) { *r = QNAN; return; }
    __fp_fabs(&aa, a);
    __fp_fabs(&bb, b);
    c = fp_cmp(&aa, &bb);
    if (c < 0) { *r = *a; return; }     /* (also a = 0 and b = inf) */
    if (c == 0) { r->hi = s; r->lo = 0; return; }
    sig(a, &xh, &xl, &ex);
    sig(b, &yh, &yl, &ey);
    /* long division, one bit at a time, keeping only the remainder: exact */
    for (;; ex--) {
        if (xh > yh || (xh == yh && xl >= yl)) {
            th = xh - yh - (xl < yl);
            tl = xl - yl;
            if (!(th | tl)) { r->hi = s; r->lo = 0; return; }
            xh = th; xl = tl;
        }
        if (ex == ey) break;
        xh = (xh << 1) | (xl >> 31);
        xl <<= 1;
    }
    while (!(xh & 0x100000)) { xh = (xh << 1) | (xl >> 31); xl <<= 1; ex--; }
    if (ex > 0) { r->hi = ((unsigned)ex << 20) | (xh & 0xFFFFF); r->lo = xl; }
    else {                              /* subnormal (exact: the bits are there) */
        int sh = 1 - ex;
        if (sh >= 32) { xl = xh >> (sh - 32); xh = 0; }
        else { xl = (xl >> sh) | (xh << (32 - sh)); xh >>= sh; }
        r->hi = xh; r->lo = xl;
    }
    r->hi |= s;
}

void __fp_remainder(fp64 *r, const fp64 *a, const fp64 *b)    /* (fdlibm's way) */
{
    fp64 x, p, t, ph;
    unsigned s = a->hi & 0x80000000;
    if (isnan_(a)) { quiet(r, a); return; }
    if (isnan_(b)) { quiet(r, b); return; }
    if (iszero(b) || isinf_(a)) { *r = QNAN; return; }
    __fp_fabs(&p, b);
    x = *a;
    if (expo(&p) < 0x7FE) {             /* x = fmod(x, 2p) < 2p */
        fp_add(&t, &p, &p);
        __fp_fmod(&x, &x, &t);
    }
    __fp_fabs(&x, &x);
    if (fp_cmp(&x, &p) == 0) { r->hi = s; r->lo = 0; return; }
    if (expo(&p) < 2) {
        fp_add(&t, &x, &x);
        if (fp_cmp(&t, &p) > 0) {
            fp_sub(&x, &x, &p);
            fp_add(&t, &x, &x);
            if (fp_cmp(&t, &p) >= 0) fp_sub(&x, &x, &p);
        }
    } else {
        fp_mul(&ph, &p, &HALF);
        if (fp_cmp(&x, &ph) > 0) {
            fp_sub(&x, &x, &p);
            if (fp_cmp(&x, &ph) >= 0) fp_sub(&x, &x, &p);
        }
    }
    x.hi ^= s;
    *r = x;
}

/* ---------------------------------------------------------------- exponents */
void __fp_ldexp(fp64 *r, const fp64 *a, int n) { if (isnan_(a)) quiet(r, a); else fp_ldexp(r, a, n); }

void __fp_frexp(fp64 *r, const fp64 *a, int *e)
{
    unsigned h, l;
    int ex;
    if (expo(a) == 0x7FF || iszero(a)) { *e = 0; if (isnan_(a)) quiet(r, a); else *r = *a; return; }
    sig(a, &h, &l, &ex);
    *e = ex - 1022;                     /* a = 0.1xxx (binary) * 2^*e */
    r->hi = (a->hi & 0x80000000) | (1022u << 20) | (h & 0xFFFFF);
    r->lo = l;
}

int __fp_ilogb(const fp64 *a)
{
    unsigned h, l;
    int ex;
    if (iszero(a) || isnan_(a)) return (int)0x80000000;     /* FP_ILOGB0, FP_ILOGBNAN (the Mac's) */
    if (isinf_(a)) return 0x7FFFFFFF;
    sig(a, &h, &l, &ex);
    return ex - 1023;
}

void __fp_logb(fp64 *r, const fp64 *a)
{
    if (isnan_(a)) { quiet(r, a); return; }
    if (isinf_(a)) { *r = INF; return; }
    if (iszero(a)) { *r = INF; r->hi |= 0x80000000; return; }
    fp_fromint(r, __fp_ilogb(a));
}

void __fp_sqrt(fp64 *r, const fp64 *a) { if (isnan_(a)) quiet(r, a); else fp_sqrt(r, a); }

/* ---------------------------------------------------------------- comparisons */
static int snan(const fp64 *a) { return isnan_(a) && !(a->hi & 0x80000); }

/* (IEEE minNum and maxNum: a quiet NaN is ignored, a signalling one is not;
   p32cc has no ?: on structures, hence *(c ? &a : &b)) */
void __fp_fmin(fp64 *r, const fp64 *a, const fp64 *b)
{
    if (snan(a) || snan(b)) { *r = QNAN; return; }
    if (isnan_(a)) { *r = *(isnan_(b) ? &QNAN : b); return; }
    if (isnan_(b)) { *r = *a; return; }
    if (iszero(a) && iszero(b)) { *r = *(neg(a) ? a : b); return; }  /* -0 < +0 */
    *r = *(fp_cmp(a, b) <= 0 ? a : b);
}

void __fp_fmax(fp64 *r, const fp64 *a, const fp64 *b)
{
    if (snan(a) || snan(b)) { *r = QNAN; return; }
    if (isnan_(a)) { *r = *(isnan_(b) ? &QNAN : b); return; }
    if (isnan_(b)) { *r = *a; return; }
    if (iszero(a) && iszero(b)) { *r = *(neg(a) ? b : a); return; }
    *r = *(fp_cmp(a, b) >= 0 ? a : b);
}

void __fp_fdim(fp64 *r, const fp64 *a, const fp64 *b)
{
    if (isnan_(a)) { quiet(r, a); return; }
    if (isnan_(b)) { quiet(r, b); return; }
    if (fp_cmp(a, b) > 0) fp_sub(r, a, b);
    else *r = ZERO;
}

void __fp_nextafter(fp64 *r, const fp64 *a, const fp64 *b)
{
    int c;
    if (isnan_(a)) { quiet(r, a); return; }
    if (isnan_(b)) { quiet(r, b); return; }
    c = fp_cmp(a, b);
    if (c == 0) { *r = *b; return; }
    if (iszero(a)) {                    /* the smallest subnormal, towards b */
        r->lo = 1;
        r->hi = b->hi & 0x80000000;
        return;
    }
    *r = *a;
    if ((c < 0) == !neg(a)) { r->lo++; if (!r->lo) r->hi++; }      /* away from zero */
    else { if (!r->lo) r->hi--; r->lo--; }                          /* towards zero */
}

/* ---------------------------------------------------------------- exp and log */
void __fp_exp(fp64 *r, const fp64 *a) { if (isnan_(a)) quiet(r, a); else fp_exp(r, a); }

void __fp_exp2(fp64 *r, const fp64 *a)
{
    int ok, n;
    if (isnan_(a)) { quiet(r, a); return; }
    if (isinf_(a)) { *r = *(neg(a) ? &ZERO : &INF); return; }
    n = fp_toint(a, &ok);
    if (fp_isint(a)) {                  /* 2^n exactly */
        if (!ok || n > 1024) { *r = *(neg(a) ? &ZERO : &INF); return; }
        fp_ldexp(r, &ONE, n);
        return;
    }
    if (!ok) { *r = *(neg(a) ? &ZERO : &INF); return; }
    {                                   /* 2^n 2^f, f = a - n in [-1/2, 1/2] */
        fp64 nf, f;
        fp_round(&nf, a);
        fp_sub(&f, a, &nf);
        n = fp_toint(&nf, 0);
        if (n > 1100) { *r = INF; return; }
        if (n < -1100) { *r = ZERO; return; }
        fp_pow(&f, &TWO, &f);
        fp_ldexp(r, &f, n);
    }
}

void __fp_expm1(fp64 *r, const fp64 *a)
{
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 54) { *r = *a; return; }      /* |a| < 2^-54 (and zeros): a */
    if (isinf_(a)) { *r = *(neg(a) ? &ONE : &INF); if (neg(a)) r->hi |= 0x80000000; return; }
    if (neg(a) && expo(a) >= 1023 + 6) { *r = ONE; r->hi |= 0x80000000; return; }   /* a <= -64: -1 */
    if (fp_cmp(a, &EXP_MAX) > 0) { *r = INF; return; }
    fp_expm1(r, a);
}

void __fp_log(fp64 *r, const fp64 *a) { if (isnan_(a)) quiet(r, a); else fp_log(r, a); }

/* log x's special values: 0 when x is not finite and positive (r then holds
   the answer) */
static int logok(fp64 *r, const fp64 *a)
{
    if (isnan_(a)) { quiet(r, a); return 0; }
    if (iszero(a)) { *r = INF; r->hi |= 0x80000000; return 0; }
    if (neg(a)) { *r = QNAN; return 0; }
    if (isinf_(a)) { *r = INF; return 0; }
    return 1;
}

void __fp_log2(fp64 *r, const fp64 *a)
{
    if (logok(r, a)) fp_log2(r, a);     /* (powers of two come out exact) */
}

void __fp_log10(fp64 *r, const fp64 *a)
{
    int i;
    if (!logok(r, a)) return;
    for (i = 0; i < 23; i++)            /* 10^i: exactly i */
        if (a->hi == TEN[i].hi && a->lo == TEN[i].lo) { fp_fromint(r, i); return; }
    fp_log10(r, a);
}

void __fp_log1p(fp64 *r, const fp64 *a)
{
    fp64 u;
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 54) { *r = *a; return; }      /* tiny (and zeros) */
    fp_add(&u, &ONE, a);
    if (iszero(&u)) { *r = INF; r->hi |= 0x80000000; return; }     /* a = -1 */
    if (neg(&u)) { *r = QNAN; return; }
    if (isinf_(a)) { *r = *a; return; }
    fp_log1p(r, a);
}

/* double-double arithmetic (a value as hi + lo), for whole powers: softfp
   rounds exactly like IEEE hardware, so Dekker's exact products work */
static void split(fp64 *h, fp64 *l, const fp64 *a)       /* a = h + l, 26 bits each */
{
    static const fp64 C = { 0x02000000, 0x41A00000 };   /* 2^27 + 1 */
    fp64 c, t;
    fp_mul(&c, a, &C);
    fp_sub(&t, &c, a);
    fp_sub(h, &c, &t);
    fp_sub(l, a, h);
}

static void two_prod(fp64 *p, fp64 *e, const fp64 *a, const fp64 *b)   /* a b = p + e exactly */
{
    fp64 ah, al, bh, bl, t, u;
    fp_mul(p, a, b);
    split(&ah, &al, a);
    split(&bh, &bl, b);
    fp_mul(&t, &ah, &bh); fp_sub(&t, &t, p);
    fp_mul(&u, &ah, &bl); fp_add(&t, &t, &u);
    fp_mul(&u, &al, &bh); fp_add(&t, &t, &u);
    fp_mul(&u, &al, &bl); fp_add(e, &t, &u);
}

static void dd_mul(fp64 *h, fp64 *l, const fp64 *xh, const fp64 *xl, const fp64 *yh, const fp64 *yl)
{
    fp64 p, e, t;
    two_prod(&p, &e, xh, yh);
    fp_mul(&t, xh, yl); fp_add(&e, &e, &t);
    fp_mul(&t, xl, yh); fp_add(&e, &e, &t);
    fp_add(h, &p, &e);                  /* (fast two-sum) */
    fp_sub(&t, h, &p);
    fp_sub(l, &e, &t);
}

/* (h + l) = (h' + l') 2^k with h' in [1/2, 1): exact (powers of two) */
static void dd_norm(fp64 *h, fp64 *l, int *e)
{
    fp64 t;
    int k;
    __fp_frexp(&t, h, &k);
    fp_ldexp(h, h, -k);
    fp_ldexp(l, l, -k);
    *e += k;
}

/* a^n for a finite nonzero a and 0 < |n| < 2048: a = f 2^k with f in [1, 2),
   f^|n| in double-double, kept in [1/2, 1) by exact powers of two after each
   product (their exponent counted aside, so nothing overflows on the way),
   the reciprocal if n < 0, 2^(kn + the exponents) at the end. About 106 bits
   all the way, so the result is correctly rounded but in the rarest cases
   (pow(10, 300) was off by ulps when n > 64 went to the general pow) */
static void ipow(fp64 *r, const fp64 *a, int n)
{
    fp64 f, bh, bl, ph, pl, t, q1, q2, e;
    int k, m = n < 0 ? -n : n, sg = neg(a) && (m & 1), first = 1, kn, be = 0, pe = 0;
    __fp_frexp(&f, a, &k);
    f.hi &= 0x7FFFFFFF;
    fp_ldexp(&f, &f, 1);
    k--;
    bh = f; bl = ZERO; ph = ONE; pl = ZERO;
    while (m) {
        if (m & 1) {
            if (first) { ph = bh; pl = bl; pe = be; first = 0; }
            else {
                dd_mul(&ph, &pl, &ph, &pl, &bh, &bl);
                pe += be;
                dd_norm(&ph, &pl, &pe);
            }
        }
        m >>= 1;
        if (m) {
            dd_mul(&bh, &bl, &bh, &bl, &bh, &bl);
            be *= 2;
            dd_norm(&bh, &bl, &be);
        }
    }
    kn = k * n + (n < 0 ? -pe : pe);
    if (n < 0) {                        /* 1 / (ph + pl) */
        fp_div(&q1, &ONE, &ph);
        two_prod(&t, &e, &q1, &ph);
        fp_sub(&t, &ONE, &t);
        fp_sub(&t, &t, &e);
        fp_mul(&e, &q1, &pl);
        fp_sub(&t, &t, &e);
        fp_div(&q2, &t, &ph);
        fp_add(&ph, &q1, &q2);
        fp_sub(&t, &ph, &q1);
        fp_sub(&pl, &q2, &t);
    }
    /* ph is ph + pl rounded (dd_mul and dd_norm keep it so), so for a normal
       result scaling it is exact and correct (or overflows to inf). A
       subnormal one rounds at 2^-1074 instead: ph + pl as a whole number of
       2^-1074 units (y + yl), rounded once, ties to even */
    if (kn >= -1021) fp_ldexp(r, &ph, kn);
    else {
        fp64 y, yl, w, d, half = { 0, 0x3FE00000 };
        fp_ldexp(&y, &ph, kn + 1074);   /* (exact: below 2^53, and normal) */
        fp_ldexp(&yl, &pl, kn + 1074);
        __fp_rint(&w, &y);              /* (whole, ties to even) */
        fp_sub(&d, &y, &w);             /* (exact) */
        fp_add(&d, &d, &yl);            /* how far w is from the true value */
        {
            fp64 mhalf, h;
            fp_neg(&mhalf, &half);
            if (fp_cmp(&d, &half) > 0) fp_add(&w, &w, &ONE);
            else if (fp_cmp(&d, &mhalf) < 0) fp_sub(&w, &w, &ONE);
            else if (fp_cmp(&d, &half) == 0 || fp_cmp(&d, &mhalf) == 0) {     /* a true tie: to even */
                fp_ldexp(&h, &w, -1);
                if (!fp_isint(&h)) {
                    if (neg(&d)) fp_sub(&w, &w, &ONE);
                    else fp_add(&w, &w, &ONE);
                }
            }
        }
        fp_ldexp(r, &w, -1074);         /* (exact) */
    }
    if (sg) fp_neg(r, r);
}

static int oddint(const fp64 *b)        /* an odd whole number? */
{
    fp64 h;
    if (!fp_isint(b)) return 0;
    fp_ldexp(&h, b, -1);
    return !fp_isint(&h);
}

void __fp_pow(fp64 *r, const fp64 *a, const fp64 *b)
{
    fp64 m;
    int odd;
    if (iszero(b)) { *r = ONE; return; }                    /* even pow(NaN, 0) */
    if (a->hi == ONE.hi && !a->lo) { *r = ONE; return; }    /* even pow(1, NaN) */
    if (isnan_(a)) { quiet(r, a); return; }
    if (isnan_(b)) { quiet(r, b); return; }
    __fp_fabs(&m, a);
    if (isinf_(b)) {
        int c = fp_cmp(&m, &ONE);
        if (c == 0) { *r = ONE; return; }                   /* pow(-1, +-inf) */
        *r = *((c > 0) == !neg(b) ? &INF : &ZERO);
        return;
    }
    odd = oddint(b);
    if (fp_cmp(&m, &ONE) == 0 && fp_isint(b)) {     /* (-1)^b (softfp's pow loses the sign of a huge odd power) */
        *r = ONE;
        if (odd) r->hi |= 0x80000000;
        return;
    }
    if (isinf_(a) || iszero(a)) {
        int big = isinf_(a);
        *r = *(big == !neg(b) ? &INF : &ZERO);                   /* inf^+, 0^-: inf; inf^-, 0^+: 0 */
        if (neg(a) && odd) r->hi |= 0x80000000;
        return;
    }
    if (neg(a) && !fp_isint(b)) { *r = QNAN; return; }
    if (fp_isint(b) && expo(b) < 1023 + 11) {   /* |b| < 2048, whole: double-double */
        ipow(r, a, fp_toint(b, 0));             /* (beyond, the result is 0 or inf when |a| != 1) */
        return;
    }
    fp_pow(r, a, b);
}

/* ---------------------------------------------------------------- sin, cos, tan */
static const unsigned short two_pi[77] = {     /* 2/pi: bits 1..1232 after the point */
    0xA2F9, 0x836E, 0x4E44, 0x1529, 0xFC27, 0x57D1, 0xF534, 0xDDC0, 0xDB62, 0x9599,
    0x3C43, 0x9041, 0xFE51, 0x63AB, 0xDEBB, 0xC561, 0xB724, 0x6E3A, 0x424D, 0xD2E0,
    0x0649, 0x2EEA, 0x09D1, 0x921C, 0xFE1D, 0xEB1C, 0xB129, 0xA73E, 0xE882, 0x35F5,
    0x2EBB, 0x4484, 0xE99C, 0x7026, 0xB45F, 0x7E41, 0x3991, 0xD639, 0x8353, 0x39F4,
    0x9C84, 0x5F8B, 0xBDF9, 0x283B, 0x1FF8, 0x97FF, 0xDE05, 0x980F, 0xEF2F, 0x118B,
    0x5A0A, 0x6D1F, 0x6D36, 0x7ECF, 0x27CB, 0x09B7, 0x4F46, 0x3F66, 0x9E5F, 0xEA2D,
    0x7527, 0xBAC7, 0xEBE5, 0xF17B, 0x3D07, 0x39F7, 0x8A52, 0x92EA, 0x6BFB, 0x5FB1,
    0x1F8D, 0x5D08, 0x5603, 0x3046, 0xFC7B, 0x6BAB, 0xF0CF
};

static unsigned tpbit(int i)            /* bit i of 2/pi (i >= 1 after the point; 0 elsewhere) */
{
    if (i < 1 || i > 77 * 16) return 0;
    i--;
    return (two_pi[i >> 4] >> (15 - (i & 15))) & 1;
}

#define FB 176                          /* fraction bits kept */
#define WL 12                           /* window limbs: FB + 2 bits */

/* |x| = (4N + q + f) pi/2, f in [-1/2, 1/2]: y = f pi/2 (|y| <= pi/4), returns q */
static int reduce_big(fp64 *y, const fp64 *x)
{
    unsigned m[4], w[WL], p[WL + 4], t, c, fh, fl, ph[8], pp[8];
    static const unsigned pio2[4] = { 0x611A, 0x10B4, 0xED51, 0x6487 };    /* pi/2 * 2^62 */
    int e, i, j, k, q, top, sg = 0, sticky = 0, base;
    unsigned h = (x->hi & 0xFFFFF) | 0x100000;
    e = expo(x) - 1075;                 /* |x| = m 2^e, e >= -33 here */
    m[0] = x->lo & 0xFFFF; m[1] = x->lo >> 16; m[2] = h & 0xFFFF; m[3] = h >> 16;
    /* w = bits e-1 .. e+FB of 2/pi as an integer: x 2/pi mod 4 = m w / 2^FB mod 4 */
    for (i = 0; i < WL; i++) {
        t = 0;
        base = e + FB - 16 * i;         /* limb i holds bits base-15 .. base (base the lowest) */
        for (j = 15; j >= 0; j--) t = (t << 1) | tpbit(base - j);
        w[i] = t;
    }
    for (i = 0; i < WL + 4; i++) p[i] = 0;
    for (i = 0; i < 4; i++) {
        c = 0;
        for (j = 0; j < WL; j++) {
            t = m[i] * w[j] + p[i + j] + c;
            p[i + j] = t & 0xFFFF;
            c = t >> 16;
        }
        p[i + WL] = c;
    }
    /* p: bits 0..FB-1 the fraction, FB and FB+1 the quadrant */
    q = (p[FB / 16] & 3);
    if (p[FB / 16 - 1] & 0x8000) {      /* f >= 1/2: f - 1, the next quadrant */
        q = (q + 1) & 3;
        sg = 1;
        c = 1;
        for (i = 0; i < FB / 16; i++) { t = (~p[i] & 0xFFFF) + c; p[i] = t & 0xFFFF; c = t >> 16; }
    }
    /* the fraction's top 64 bits (f >= 2^-80 or so for any double) */
    for (top = FB / 16 - 1; top >= 0 && !p[top]; top--) ;
    if (top < 0) { *y = ZERO; return q; }
    i = 0;
    while (!(p[top] & 0x8000)) {        /* normalize: shift the limbs left until the top bit is set */
        for (j = top; j > 0; j--) p[j] = ((p[j] << 1) | (p[j - 1] >> 15)) & 0xFFFF;
        p[0] = (p[0] << 1) & 0xFFFF;
        i++;
    }
    fh = (p[top] << 16) | (top >= 1 ? p[top - 1] : 0);
    fl = ((top >= 2 ? p[top - 2] : 0) << 16) | (top >= 3 ? p[top - 3] : 0);
    for (j = top - 4; j >= 0; j--) if (p[j]) sticky = 1;
    /* f = (fh:fl) 2^(16(top+1) - i - FB - 64); y = f * (pi/2 * 2^62) * 2^-62 */
    ph[0] = fl & 0xFFFF; ph[1] = fl >> 16; ph[2] = fh & 0xFFFF; ph[3] = fh >> 16;
    for (j = 0; j < 8; j++) pp[j] = 0;
    for (j = 0; j < 4; j++) {
        c = 0;
        for (k = 0; k < 4; k++) {
            t = ph[j] * pio2[k] + pp[j + k] + c;
            pp[j + k] = t & 0xFFFF;
            c = t >> 16;
        }
        pp[j + 4] = c;
    }
    fh = (pp[7] << 16) | pp[6];         /* the product's top 64 bits: y = (fh:fl) 2^(16(top+1) - i - FB - 62) */
    fl = (pp[5] << 16) | pp[4];
    for (j = 0; !(fh & 0x80000000); j++) {          /* (at most 2 steps) */
        fh = (fh << 1) | (fl >> 31);
        fl = (fl << 1) | ((pp[3] >> (15 - j)) & 1);
    }
    if ((pp[3] & (0xFFFF >> j)) || pp[2] || pp[1] || pp[0]) sticky = 1;
    __fp_pack64(y, fh, fl, sticky, 16 * (top + 1) - i - FB + 1 - j, sg);
    return q;
}

/* sin (which = 0), cos (1) or tan (2) of a */
static void trig(fp64 *r, const fp64 *a, int which)
{
    fp64 y, s, c, ax;
    int q, sg = neg(a);
    if (isnan_(a)) { quiet(r, a); return; }
    if (isinf_(a)) { *r = QNAN; return; }
    if (which != 1 && expo(a) < 1023 - 27) { *r = *a; return; }   /* tiny (and zeros): sin x = tan x = x */
    if (expo(a) < 1023 + 19) {          /* softfp reduces well enough */
        if (which == 0) fp_sin(r, a);
        else if (which == 1) fp_cos(r, a);
        else fp_tan(r, a);
        return;
    }
    __fp_fabs(&ax, a);
    q = reduce_big(&y, &ax);
    if (which == 2) {
        fp_sin(&s, &y);
        fp_cos(&c, &y);
        if (q & 1) { fp_div(r, &c, &s); fp_neg(r, r); }        /* tan(y + pi/2) = -cot y */
        else fp_div(r, &s, &c);
    } else {
        if ((q + which) & 1) fp_cos(r, &y); else fp_sin(r, &y);
        if (which == 0 ? q >= 2 : (q == 1 || q == 2)) fp_neg(r, r);
        if (which == 1) sg = 0;         /* cos is even */
    }
    if (sg) fp_neg(r, r);
}

void __fp_sin(fp64 *r, const fp64 *a) { trig(r, a, 0); }
void __fp_cos(fp64 *r, const fp64 *a) { trig(r, a, 1); }
void __fp_tan(fp64 *r, const fp64 *a) { trig(r, a, 2); }

/* ---------------------------------------------------------------- inverse trig */
void __fp_atan(fp64 *r, const fp64 *a)
{
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 27) { *r = *a; return; }
    fp_atan(r, a);
}

void __fp_atan2(fp64 *r, const fp64 *y, const fp64 *x)
{
    fp64 ay, ax;
    int sy = neg(y), sx = neg(x);
    if (isnan_(y)) { quiet(r, y); return; }
    if (isnan_(x)) { quiet(r, x); return; }
    __fp_fabs(&ay, y);
    __fp_fabs(&ax, x);
    if (iszero(y) || (isinf_(x) && !isinf_(y))) {           /* on the x axis */
        if (sx) { fp_add(r, &PI, &PI_LO); } else *r = ZERO;
        if (sy) fp_neg(r, r);
        return;
    }
    if (iszero(x) || (isinf_(y) && !isinf_(x))) {           /* on the y axis */
        fp_add(r, &PI_2, &PI_2_LO);
        if (sy) fp_neg(r, r);
        return;
    }
    if (isinf_(x)) {                    /* both infinite: pi/4 or 3pi/4 */
        static const fp64 PI_4 = { 0x54442D18, 0x3FE921FB }, PI3_4 = { 0x7F3321D2, 0x4002D97C };
        *r = *(sx ? &PI3_4 : &PI_4);
        if (sy) fp_neg(r, r);
        return;
    }
    fp_atan2(r, y, x);
}

void __fp_asin(fp64 *r, const fp64 *a)
{
    fp64 m;
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 27) { *r = *a; return; }
    __fp_fabs(&m, a);
    if (fp_cmp(&m, &ONE) > 0) { *r = QNAN; return; }
    fp_asin(r, a);
}

void __fp_acos(fp64 *r, const fp64 *a)
{
    fp64 m;
    if (isnan_(a)) { quiet(r, a); return; }
    __fp_fabs(&m, a);
    if (fp_cmp(&m, &ONE) > 0) { *r = QNAN; return; }
    fp_acos(r, a);
}

/* ---------------------------------------------------------------- hyperbolic */
/* e^|a| / 2 for a big |a|, without overflowing too soon */
static void halfexp(fp64 *r, const fp64 *ax)
{
    fp64 w, h;
    if (fp_cmp(ax, &EXP_MAX) <= 0) { fp_exp(&w, ax); fp_mul(r, &w, &HALF); return; }
    fp_mul(&h, ax, &HALF);
    fp_exp(&w, &h);
    fp_mul(&h, &w, &HALF);
    fp_mul(r, &h, &w);
}

void __fp_sinh(fp64 *r, const fp64 *a)
{
    fp64 ax;
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 28 || isinf_(a)) { *r = *a; return; }
    __fp_fabs(&ax, a);
    if (fp_cmp(&ax, &TWENTY2) < 0) { fp_sinh(r, a); return; }
    halfexp(r, &ax);
    if (neg(a)) fp_neg(r, r);
}

void __fp_cosh(fp64 *r, const fp64 *a)
{
    fp64 ax;
    if (isnan_(a)) { quiet(r, a); return; }
    __fp_fabs(&ax, a);
    if (isinf_(a)) { *r = ax; return; }
    if (fp_cmp(&ax, &TWENTY2) < 0) fp_cosh(r, a);
    else halfexp(r, &ax);
}

void __fp_tanh(fp64 *r, const fp64 *a)
{
    fp64 ax;
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 28) { *r = *a; return; }
    __fp_fabs(&ax, a);
    if (fp_cmp(&ax, &TWENTY2) < 0) { fp_tanh(r, a); return; }
    *r = ONE;
    if (neg(a)) fp_neg(r, r);
}

void __fp_asinh(fp64 *r, const fp64 *a)
{
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 28 || isinf_(a)) { *r = *a; return; }     /* tiny (and zeros), infinite */
    fp_asinh(r, a);
}

void __fp_acosh(fp64 *r, const fp64 *a)
{
    if (isnan_(a)) { quiet(r, a); return; }
    if (fp_cmp(a, &ONE) < 0) { *r = QNAN; return; }
    if (isinf_(a)) { *r = INF; return; }
    fp_acosh(r, a);
}

void __fp_atanh(fp64 *r, const fp64 *a)
{
    fp64 m;
    if (isnan_(a)) { quiet(r, a); return; }
    if (expo(a) < 1023 - 28) { *r = *a; return; }
    __fp_fabs(&m, a);
    if (fp_cmp(&m, &ONE) > 0) { *r = QNAN; return; }
    if (fp_cmp(&m, &ONE) == 0) { *r = INF; if (neg(a)) r->hi |= 0x80000000; return; }   /* a pole */
    fp_atanh(r, a);
}

/* ---------------------------------------------------------------- hypot, cbrt */
void __fp_hypot(fp64 *r, const fp64 *a, const fp64 *b)
{
    fp64 x, y, t, s;
    int k = 0;
    if (isinf_(a) || isinf_(b)) { *r = INF; return; }
    if (isnan_(a)) { quiet(r, a); return; }
    if (isnan_(b)) { quiet(r, b); return; }
    __fp_fabs(&x, a);
    __fp_fabs(&y, b);
    if (fp_cmp(&x, &y) < 0) { t = x; x = y; y = t; }
    if (iszero(&y)) { *r = x; return; }
    if (expo(&x) - expo(&y) > 60) { fp_add(r, &x, &y); return; }   /* y is lost */
    if (expo(&x) > 1023 + 500) k = 600;                 /* scaled by a power of two: no overflow, */
    else if (expo(&y) < 1023 - 500) k = -600;           /* nor underflow */
    if (k) { fp_ldexp(&x, &x, -k); fp_ldexp(&y, &y, -k); }
    fp_mul(&t, &x, &x);
    fp_mul(&s, &y, &y);
    fp_add(&t, &t, &s);
    fp_sqrt(&t, &t);
    fp_ldexp(r, &t, k);
}

void __fp_cbrt(fp64 *r, const fp64 *a)
{
    fp64 x, y;
    if (isnan_(a)) { quiet(r, a); return; }
    if (iszero(a) || isinf_(a)) { *r = *a; return; }
    __fp_fabs(&x, a);
    fp_pow(&y, &x, &THIRD);             /* (within an ulp or two of the cube root) */
    fp_cbrt_step(&y, &x, &y);           /* one Newton step, in 62 bits */
    if (neg(a)) fp_neg(&y, &y);
    *r = y;
}

/* math.h's NAN is this object (declared there as a const double: the same 8 bytes) */
const fp64 __nan = { 0, 0x7FF80000 };
