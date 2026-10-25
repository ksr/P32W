/* softfp.h -- P32W C library: software floating point (D27). P32W has no FPU
 * and p32cc no float type, so numbers are IEEE 754 binary64 values (C's
 * double: 53-bit significand, about 15-16 decimal digits, exponents to
 * 10^308) held in an fp64 and worked on through these functions. +, -, *,
 * / and sqrt are correctly rounded (round to nearest, ties to even) with
 * subnormals, infinities and NaNs, so results agree bit for bit with any
 * IEEE machine; the conversions and the maths functions are within an ulp
 * or two.
 *
 * p32cc cannot pass or return structures, so every function takes
 * pointers, the result first:  fp_add(&r, &a, &b) is r = a + b.  The
 * result may be one of the operands. */
#ifndef _SOFTFP_H
#define _SOFTFP_H

typedef struct { unsigned lo, hi; } fp64;       /* the binary64 bits: hi holds sign, exponent, top of the fraction */

/* arithmetic */
void fp_add(fp64 *r, const fp64 *a, const fp64 *b);
void fp_sub(fp64 *r, const fp64 *a, const fp64 *b);
void fp_mul(fp64 *r, const fp64 *a, const fp64 *b);
void fp_div(fp64 *r, const fp64 *a, const fp64 *b);
void fp_sqrt(fp64 *r, const fp64 *a);
void fp_neg(fp64 *r, const fp64 *a);
void fp_abs(fp64 *r, const fp64 *a);
void fp_ldexp(fp64 *r, const fp64 *a, int n);   /* a * 2^n */
int fp_ilogb(const fp64 *a);                    /* the binary exponent of a nonzero finite a */

/* comparison: -1, 0, 1, or 2 if either is a NaN */
int fp_cmp(const fp64 *a, const fp64 *b);
int fp_isnan(const fp64 *a);
int fp_isinf(const fp64 *a);
int fp_iszero(const fp64 *a);
int fp_sign(const fp64 *a);                     /* -1, 0, 1 (0 for zeros and NaNs) */

/* integers */
void fp_fromint(fp64 *r, int i);
void fp_fromuint(fp64 *r, unsigned u);
int fp_toint(const fp64 *a, int *ok);           /* truncated; *ok = 0 (and saturated) if out of range; ok may be 0 */
void fp_trunc(fp64 *r, const fp64 *a);          /* toward zero, kept as a number */
void fp_floor(fp64 *r, const fp64 *a);
void fp_round(fp64 *r, const fp64 *a);          /* to nearest, halves away from zero */
int fp_isint(const fp64 *a);                    /* a whole number (finite)? */

/* decimal */
const char *fp_parse(fp64 *r, const char *s);   /* [sign] digits [. digits] [e|E [sign] digits]: the end, or 0 if no number */
int fp_digits(const fp64 *a, int ndig, char *digits, int *exp10);
        /* a finite nonzero |a| rounded to ndig (1-17) significant digits: digits gets
           them (NUL-ended), *exp10 the power of ten of the first digit; returns 0, or -1
           for zero, infinity or NaN */
char *fp_format(char *buf, const fp64 *a, int ndig);
        /* like printf's %.<ndig>g: "1.5", "-0.001", "1e+20", "inf", "nan"; buf needs ndig + 10 bytes */

/* maths (out of the domain: NaN). Each is worked out in 62 bits and rounded
   once, so its result is the correctly rounded one about 99% of the time,
   and otherwise next to it (D37) */
void fp_exp(fp64 *r, const fp64 *a);
void fp_log(fp64 *r, const fp64 *a);
void fp_pow(fp64 *r, const fp64 *a, const fp64 *b);
void fp_sin(fp64 *r, const fp64 *a);
void fp_cos(fp64 *r, const fp64 *a);
void fp_tan(fp64 *r, const fp64 *a);
void fp_atan(fp64 *r, const fp64 *a);
/* (for the C library's fpmath.c, which handles the special values and ranges
   noted, then calls these) */
void fp_atan2(fp64 *r, const fp64 *y, const fp64 *x);  /* x, y finite and nonzero */
void fp_asin(fp64 *r, const fp64 *a);                   /* |a| <= 1 */
void fp_acos(fp64 *r, const fp64 *a);                   /* |a| <= 1 */
void fp_expm1(fp64 *r, const fp64 *a);                  /* e^a - 1, -64 < a < 709 */
void fp_log1p(fp64 *r, const fp64 *a);                  /* log(1 + a), a > -1 finite */
void fp_log2(fp64 *r, const fp64 *a);                   /* a > 0 finite */
void fp_log10(fp64 *r, const fp64 *a);                  /* a > 0 finite */
void fp_sinh(fp64 *r, const fp64 *a);                   /* |a| < 22 */
void fp_cosh(fp64 *r, const fp64 *a);                   /* |a| < 22 */
void fp_tanh(fp64 *r, const fp64 *a);                   /* |a| < 22 */
void fp_cbrt_step(fp64 *r, const fp64 *a, const fp64 *y);  /* y near a's cube root: one Newton step */
void fp_asinh(fp64 *r, const fp64 *a);                  /* a finite */
void fp_acosh(fp64 *r, const fp64 *a);                  /* a >= 1 finite */
void fp_atanh(fp64 *r, const fp64 *a);                  /* |a| < 1 */

#endif
