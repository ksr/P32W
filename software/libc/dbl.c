/* dbl.c -- P32W C library: double arithmetic for the compilers (D30).
 *
 * A double (float is the same type) lives in memory, and the compilers turn
 * each operation on one into a call of one of these, passing and returning
 * doubles by value (the convention for structs). They are softfp (D27) on
 * a struct of the same layout: + - * / are correctly rounded, so a
 * program's arithmetic agrees bit for bit with any IEEE machine's. */
#include <softfp.h>

fp64 __dadd(fp64 a, fp64 b) { fp64 r; fp_add(&r, &a, &b); return r; }
fp64 __dsub(fp64 a, fp64 b) { fp64 r; fp_sub(&r, &a, &b); return r; }
fp64 __dmul(fp64 a, fp64 b) { fp64 r; fp_mul(&r, &a, &b); return r; }
fp64 __ddiv(fp64 a, fp64 b) { fp64 r; fp_div(&r, &a, &b); return r; }
fp64 __dneg(fp64 a) { fp64 r; fp_neg(&r, &a); return r; }

/* -1, 0, 1, or 2 if either is a NaN (the compilers make every comparison
   with a NaN false but !=) */
int __dcmp(fp64 a, fp64 b) { return fp_cmp(&a, &b); }

/* a used as a condition: 1 unless it is a zero (a NaN is true) */
int __dtest(fp64 a) { return !fp_iszero(&a); }

fp64 __dfromint(int i) { fp64 r; fp_fromint(&r, i); return r; }
fp64 __dfromuint(unsigned u) { fp64 r; fp_fromuint(&r, u); return r; }

/* hi * 2^32 and lo are both exact, so their sum is rounded once: correct */
fp64 __dfromll(long long v)
{
    fp64 h, l;
    fp_fromint(&h, (int)(v >> 32));
    fp_ldexp(&h, &h, 32);
    fp_fromuint(&l, (unsigned)v);
    fp_add(&h, &h, &l);
    return h;
}

fp64 __dfromull(unsigned long long v)
{
    fp64 h, l;
    fp_fromuint(&h, (unsigned)(v >> 32));
    fp_ldexp(&h, &h, 32);
    fp_fromuint(&l, (unsigned)v);
    fp_add(&h, &h, &l);
    return h;
}

/* toward zero; out of range the result is undefined in C (here: saturated) */
int __dtoint(fp64 a) { return fp_toint(&a, 0); }

/* the magnitude's integer part, from the bits: *big set if it needs more
   than 64 bits (inf, NaN, |a| >= 2^64) */
static unsigned long long whole(fp64 a, int *big)
{
    int e = (int)((a.hi >> 20) & 0x7FF) - 1075;
    unsigned long long m = ((unsigned long long)(a.hi & 0xFFFFF | 0x100000) << 32) | a.lo;
    *big = 0;
    if (((a.hi >> 20) & 0x7FF) == 0) return 0;          /* zero or subnormal: below 1 */
    if (e >= 12) { *big = 1; return 0; }                /* (2^52 * 2^12: at least 2^64) */
    if (e >= 0) return m << e;
    if (e <= -53) return 0;
    return m >> -e;
}

unsigned __dtouint(fp64 a)
{
    int big;
    unsigned long long w = whole(a, &big);
    if (a.hi >> 31) return (unsigned)-(int)(w > 0x80000000u ? 0x80000000u : (unsigned)w);
    return big || w > 0xFFFFFFFFu ? 0xFFFFFFFFu : (unsigned)w;
}

long long __dtoll(fp64 a)
{
    int big;
    unsigned long long w = whole(a, &big);
    if (a.hi >> 31) return big || w > 0x8000000000000000ull ? (long long)0x8000000000000000ull : -(long long)w;
    return big || w > 0x7FFFFFFFFFFFFFFFull ? 0x7FFFFFFFFFFFFFFFll : (long long)w;
}

unsigned long long __dtoull(fp64 a)
{
    int big;
    unsigned long long w = whole(a, &big);
    if (a.hi >> 31) return 0 - w;
    return big ? 0xFFFFFFFFFFFFFFFFull : w;
}

/* strtod and atof: fplib's correctly rounded conversion (fpconv.c) */
#include <fplib.h>
fp64 strtod(const char *s, char **end)
{
    fp64 r;
    const char *e = __fp_strtod(&r, s);
    if (end) *end = (char *)e;
    return r;
}

fp64 atof(const char *s) { return strtod(s, 0); }
