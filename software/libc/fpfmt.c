/* fpfmt.c -- printf's floating-point conversions (fplib.h): %e %E %f %F
 * %g %G %a %A with the flags - + space # 0, a width and a precision, on
 * one fp64, character for character as the Mac's printf writes them.
 *
 * The decimal ones take their digits from __fp_dtoa (exact, rounded half
 * to even). %a follows the Mac's (FreeBSD's __hdtoa) rules, quirks
 * included: the leading digit is always 1 (subnormals are normalized), and
 * a dropped part is rounded up when its first hex digit is above 8, or is
 * 8 and the digit after it is odd -- so 0x1.8p+0 prints as 0x1p+0 at %.0a
 * but 0x1.81p+0 as 0x2p+0 -- and a carry out of the leading digit stays
 * there (0x2.000p+0). The sign of a NaN is not printed, and the 0 flag
 * pads inf and nan with spaces.
 *
 * One more quirk of the Mac's dtoa (gdtoa) shows in %g: for a whole number
 * below 10^15 that has more digits than the precision, its quick
 * floating-point attempt gives up near a tie, and the "small integer" path
 * that takes over does not remove trailing zeros when it rounds down --
 * %.4g of 61005000000 is 6.100e+10, not 6.1e+10. mac_zeros() replays the
 * quick attempt with softfp (whose operations round like the hardware's)
 * to know when. */
#include <softfp.h>
#include <fplib.h>

typedef struct {
    void (*put)(void *, int);
    void *arg;
    int n;
} Sink;

static void out(Sink *k, int c) { k->put(k->arg, c); k->n++; }
static void outs(Sink *k, const char *s) { while (*s) out(k, *s++); }
static void pad(Sink *k, int c, int n) { while (n-- > 0) out(k, c); }

static char dg[FP_DTOA_BUF];

static int dig_at(int i, int n) { return i >= 0 && i < n ? dg[i] : '0'; }

static const fp64 TEN[15] = {          /* 10^0 .. 10^14 */
    { 0, 0x3FF00000 }, { 0, 0x40240000 }, { 0, 0x40590000 }, { 0, 0x408F4000 },
    { 0, 0x40C38800 }, { 0, 0x40F86A00 }, { 0, 0x412E8480 }, { 0, 0x416312D0 },
    { 0, 0x4197D784 }, { 0, 0x41CDCD65 }, { 0x20000000, 0x4202A05F }, { 0xE8000000, 0x42374876 },
    { 0xA2000000, 0x426D1A94 }, { 0xE5400000, 0x42A2309C }, { 0x1E900000, 0x42D6BCC4 }
};
static const fp64 HALF = { 0, 0x3FE00000 }, SEVEN = { 0, 0x401C0000 }, TWO = { 0, 0x40000000 };

/* %g's p digits of x (n of them, without trailing zeros): would the Mac keep
   the trailing zeros? (see above) */
static int mac_zeros(const fp64 *x, int p, int n)
{
    fp64 a, u, eps, t, L, top;
    int k, i, ilim = p, ng, dp, sg;
    char full[24];
    a = *x;
    a.hi &= 0x7FFFFFFF;
    fp_mul(&top, &TEN[14], &TEN[1]);    /* 10^15 */
    if (fp_cmp(&a, &TEN[0]) < 0 || fp_cmp(&a, &top) >= 0 || !fp_isint(&a)) return 0;
    for (k = 14; fp_cmp(&a, &TEN[k]) < 0; k--) ;
    if (p > k) return 0;                /* (no digits dropped) */
    /* the quick attempt: u = a / 10^k, eps = (2u + 7) 2^-52 10^(p-1) */
    fp_div(&u, &a, &TEN[k]);
    fp_mul(&eps, &u, &TWO);
    fp_add(&eps, &eps, &SEVEN);
    fp_ldexp(&eps, &eps, -52);
    fp_mul(&eps, &eps, &TEN[p - 1]);
    for (i = 1;; i++) {
        fp_trunc(&L, &u);
        fp_sub(&u, &u, &L);
        if (fp_iszero(&u)) ilim = i;
        if (i == ilim) {
            fp_add(&t, &HALF, &eps);
            if (fp_cmp(&u, &t) > 0) return 0;
            fp_sub(&t, &HALF, &eps);
            if (fp_cmp(&u, &t) < 0) return 0;
            break;
        }
        fp_mul(&u, &u, &TEN[1]);
    }
    /* it gave up: the small-integer path keeps the zeros if it rounds down */
    ng = __fp_dtoa(&a, 'e', 17, full, &dp, &sg);
    for (i = 0; i < p; i++) if ((i < ng ? full[i] : '0') != dig_at(i, n)) return 0;
    return 1;
}


static int explen(int x, int min)       /* the characters of an exponent's digits */
{
    int n = 1;
    if (x < 0) x = -x;
    while (x >= 10) { n++; x /= 10; }
    return n < min ? min : n;
}

static void outexp(Sink *k, int x, int min)
{
    char b[12];
    int n = 0;
    out(k, x < 0 ? '-' : '+');
    if (x < 0) x = -x;
    do { b[n++] = '0' + x % 10; x /= 10; } while (x);
    while (n < min) b[n++] = '0';
    while (n) out(k, b[--n]);
}

int __fp_print(void (*put)(void *, int), void *arg, int conv, int flags, int width, int prec, const fp64 *x)
{
    Sink k;
    int upper = conv == 'E' || conv == 'F' || conv == 'G' || conv == 'A';
    int c = upper ? conv + 32 : conv;   /* e f g a */
    int alt = flags & FPF_ALT, neg, n, decpt, len, i, style, pd, X, nan = 0;
    char sign = 0, hx[16];
    const char *prefix = "";
    int hexp = 0, hn = 0;               /* %a: digits hx[0 .. hn-1] (hx[0] the leading one), exponent */
    k.put = put; k.arg = arg; k.n = 0;
    neg = x->hi >> 31;
    if ((x->hi & 0x7FF00000) == 0x7FF00000) {           /* inf, nan */
        nan = (x->hi & 0xFFFFF) || x->lo;
        if (!nan) sign = neg ? '-' : flags & FPF_PLUS ? '+' : flags & FPF_SPACE ? ' ' : 0;
        len = 3 + (sign != 0);
        if (!(flags & FPF_LEFT)) pad(&k, ' ', width - len);
        if (sign) out(&k, sign);
        outs(&k, nan ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"));
        if (flags & FPF_LEFT) pad(&k, ' ', width - len);
        return k.n;
    }
    sign = neg ? '-' : flags & FPF_PLUS ? '+' : flags & FPF_SPACE ? ' ' : 0;
    n = 0;
    decpt = 1;
    style = c;                          /* 'e', 'f' or 'a', after %g's choice */
    if (c == 'a') {
        unsigned mh = x->hi & 0xFFFFF, ml = x->lo;
        int ex = (x->hi >> 20) & 0x7FF;
        prefix = upper ? "0X" : "0x";
        if (!ex && !(mh | ml)) { hx[0] = 0; hn = 1; hexp = 0; for (i = 1; i < 14; i++) hx[i] = 0; }
        else {
            if (!ex) {                  /* subnormal: normalized */
                ex = 1;
                while (!(mh & 0x100000)) { mh = (mh << 1) | (ml >> 31); ml <<= 1; ex--; }
                mh &= 0xFFFFF;
            }
            hexp = ex - 1023;
            hx[0] = 1;
            for (i = 0; i < 5; i++) hx[1 + i] = (mh >> (16 - 4 * i)) & 15;
            for (i = 0; i < 8; i++) hx[6 + i] = (ml >> (28 - 4 * i)) & 15;
            hn = 14;
        }
        if (prec < 0) {                 /* as many as it takes */
            for (hn = 14; hn > 1 && !hx[hn - 1]; hn--) ;
            prec = hn - 1;
        } else if (prec + 1 < 14) {     /* round at hx[prec + 1] the Mac's way */
            int nd = prec + 1;
            int next = nd + 1 < 14 ? hx[nd + 1] : 0;
            if (hx[nd] > 8 || (hx[nd] == 8 && (next & 1))) {
                for (i = nd - 1; i > 0 && hx[i] == 15; i--) hx[i] = 0;
                hx[i]++;                /* (hx[0] may become 2) */
            }
            hn = nd;
        }
        len = 1 + (prec > 0 || alt ? 1 + prec : 0) + 2 + explen(hexp, 1);
    } else {
        int P = prec < 0 ? 6 : prec;
        if (c == 'g') {
            if (!P) P = 1;
            n = __fp_dtoa(x, 'e', P, dg, &decpt, &neg);
            if (!alt && mac_zeros(x, P, n)) { for (i = n; i < P; i++) dg[i] = '0'; n = P; }
            X = n ? decpt - 1 : 0;
            if (X < P && X >= -4) { style = 'f'; P = P - 1 - X; } else { style = 'e'; P = P - 1; }
            if (!alt) {                 /* no trailing zeros */
                int last = style == 'e' ? n - 1 : n - decpt;  /* digits after the point that matter */
                if (last < 0) last = 0;
                if (P > last) P = last;
            }
        } else if (c == 'e')
            n = __fp_dtoa(x, 'e', P + 1, dg, &decpt, &neg);
        else
            n = __fp_dtoa(x, 'f', P, dg, &decpt, &neg);
        if (!n) decpt = style == 'e' ? 1 : 0;           /* (zero) */
        prec = P;
        if (style == 'e') len = 1 + (prec > 0 || alt ? 1 + prec : 0) + 2 + explen(decpt - 1, 2);
        else len = (decpt > 0 ? decpt : 1) + (prec > 0 || alt ? 1 + prec : 0);
    }
    len += (sign != 0) + (c == 'a' ? 2 : 0);
    pd = width - len;
    if (!(flags & FPF_LEFT) && !(flags & FPF_ZERO)) pad(&k, ' ', pd);
    if (sign) out(&k, sign);
    outs(&k, prefix);
    if (!(flags & FPF_LEFT) && (flags & FPF_ZERO)) pad(&k, '0', pd);
    if (style == 'a') {
        const char *xd = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        out(&k, xd[(int)hx[0]]);
        if (prec > 0 || alt) out(&k, '.');
        for (i = 1; i <= prec; i++) out(&k, i < hn ? xd[(int)hx[i]] : '0');
        out(&k, upper ? 'P' : 'p');
        outexp(&k, hexp, 1);
    } else if (style == 'e') {
        out(&k, dig_at(0, n));
        if (prec > 0 || alt) out(&k, '.');
        for (i = 1; i <= prec; i++) out(&k, dig_at(i, n));
        out(&k, upper ? 'E' : 'e');
        outexp(&k, n ? decpt - 1 : 0, 2);
    } else {
        if (decpt <= 0) out(&k, '0');
        else for (i = 0; i < decpt; i++) out(&k, dig_at(i, n));
        if (prec > 0 || alt) out(&k, '.');
        for (i = 0; i < prec; i++) out(&k, dig_at(decpt + i, n));
    }
    if (flags & FPF_LEFT) pad(&k, ' ', pd);
    return k.n;
}

/* into a string, like snprintf */
typedef struct { char *s; int size, n; } Buf;

static void bput(void *a, int c)
{
    Buf *b = (Buf *)a;
    if (b->n + 1 < b->size) b->s[b->n] = c;
    b->n++;
}

int __fp_format(char *buf, int size, int conv, int flags, int width, int prec, const fp64 *x)
{
    Buf b;
    int n;
    b.s = buf; b.size = size; b.n = 0;
    n = __fp_print(bput, &b, conv, flags, width, prec, x);
    if (size > 0) buf[n < size ? n : size - 1] = 0;
    return n;
}
