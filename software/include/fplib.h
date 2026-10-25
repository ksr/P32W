/* fplib.h -- P32W C library: the floating-point part of the library, on
 * softfp's fp64 (softfp.h). Exact decimal <-> binary64 conversion (strtod,
 * printf %e %f %g %a, scanf's float input) and the maths library. Every
 * function takes pointers, the result first, because p32cc cannot pass or
 * return structures yet; libc's double functions are thin wrappers.
 *
 * Conversions are exact: __fp_strtod rounds the decimal (or hexadecimal)
 * number correctly (to nearest, ties to even) however many digits it has,
 * and __fp_dtoa's digits are those of the exact value of the double,
 * rounded half to even -- the same bits and characters as the Mac's libc.
 *
 * Not reentrant: the conversions work in static big-number areas. */
#ifndef _FPLIB_H
#define _FPLIB_H
#include <softfp.h>

/* ---------------------------------------------------------------- conversions (fpconv.c) */
const char *__fp_strtod(fp64 *r, const char *s);
        /* C's strtod: white space, sign, then decimal digits with an optional point and
           exponent, 0x hexadecimal with an optional p exponent, inf, infinity, nan or
           nan(...) (case ignored). Returns the end of the number, or s if there is none
           (*r = 0). Overflow gives +-inf and errno = ERANGE; a decimal number that
           comes out zero or subnormal sets ERANGE too, a hexadecimal one only when it
           is also inexact (both as on the Mac). */

#define FP_DTOA_BUF 800                 /* digits[] for __fp_dtoa: enough for any request */
#define FP_DTOA_INF (-1)
#define FP_DTOA_NAN (-2)
int __fp_dtoa(const fp64 *x, int mode, int ndig, char *digits, int *decpt, int *neg);
        /* x's decimal digits, from its exact value rounded half to even:
             mode 'e': ndig (>= 1) significant digits;
             mode 'f': up to the ndig'th (>= 0) digit after the decimal point.
           digits gets them as characters, NUL-ended, with no trailing zeros (every
           digit after them is 0); the value is 0.DIGITS * 10^*decpt. Returns how many
           there are (0 for a zero, or when 'f' rounds x away to nothing), or
           FP_DTOA_INF / FP_DTOA_NAN. *neg = the sign bit (for every x). */

int __fp_scan(int (*get)(void *), void (*unget)(int, void *), void *src, char *buf, int size, int width);
        /* scanf's float input: reads the longest prefix of the input that is a number
           for __fp_strtod (at most width characters if width > 0, and size - 1),
           copies it NUL-ended into buf and returns its length (0: no number). Every
           character read past it is given back through unget, last first (it may
           take several). */

/* printf: one double for %e %E %f %F %g %G %a %A (conv), with flags (FPF_*),
   a width (0: none) and a precision (< 0: the default). */
#define FPF_LEFT  1                     /* - */
#define FPF_PLUS  2                     /* + */
#define FPF_SPACE 4                     /* space */
#define FPF_ALT   8                     /* # */
#define FPF_ZERO  16                    /* 0 */
int __fp_print(void (*put)(void *, int), void *arg, int conv, int flags, int width, int prec, const fp64 *x);
        /* each character to put(arg, c); returns how many */
int __fp_format(char *buf, int size, int conv, int flags, int width, int prec, const fp64 *x);
        /* into buf like snprintf: at most size - 1 characters and a NUL; returns the
           full length */

int __fp_pack64(fp64 *r, unsigned h, unsigned l, int sticky, int e2, int neg);
        /* (inside the library) r = +-(h:l + a sticky bit) * 2^(e2 - 63), h's top bit
           set, correctly rounded, overflow to inf; returns 1 if inexact */

/* ---------------------------------------------------------------- maths (fpmath.c) */
/* exact: */
void __fp_fabs(fp64 *r, const fp64 *a);
void __fp_floor(fp64 *r, const fp64 *a);
void __fp_ceil(fp64 *r, const fp64 *a);
void __fp_trunc(fp64 *r, const fp64 *a);
void __fp_round(fp64 *r, const fp64 *a);                /* halves away from zero */
void __fp_rint(fp64 *r, const fp64 *a);                 /* halves to even */
void __fp_fmod(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_remainder(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_modf(fp64 *r, const fp64 *a, fp64 *ip);       /* r = fraction, *ip = whole part */
void __fp_frexp(fp64 *r, const fp64 *a, int *e);
void __fp_ldexp(fp64 *r, const fp64 *a, int n);
int __fp_ilogb(const fp64 *a);
void __fp_logb(fp64 *r, const fp64 *a);
void __fp_sqrt(fp64 *r, const fp64 *a);
void __fp_copysign(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_fmin(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_fmax(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_fdim(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_nextafter(fp64 *r, const fp64 *a, const fp64 *b);
/* within a few ulp: */
void __fp_exp(fp64 *r, const fp64 *a);
void __fp_exp2(fp64 *r, const fp64 *a);
void __fp_expm1(fp64 *r, const fp64 *a);
void __fp_log(fp64 *r, const fp64 *a);
void __fp_log2(fp64 *r, const fp64 *a);
void __fp_log10(fp64 *r, const fp64 *a);
void __fp_log1p(fp64 *r, const fp64 *a);
void __fp_pow(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_sin(fp64 *r, const fp64 *a);
void __fp_cos(fp64 *r, const fp64 *a);
void __fp_tan(fp64 *r, const fp64 *a);
void __fp_asin(fp64 *r, const fp64 *a);
void __fp_acos(fp64 *r, const fp64 *a);
void __fp_atan(fp64 *r, const fp64 *a);
void __fp_atan2(fp64 *r, const fp64 *y, const fp64 *x);
void __fp_sinh(fp64 *r, const fp64 *a);
void __fp_cosh(fp64 *r, const fp64 *a);
void __fp_tanh(fp64 *r, const fp64 *a);
void __fp_asinh(fp64 *r, const fp64 *a);
void __fp_acosh(fp64 *r, const fp64 *a);
void __fp_atanh(fp64 *r, const fp64 *a);
void __fp_hypot(fp64 *r, const fp64 *a, const fp64 *b);
void __fp_cbrt(fp64 *r, const fp64 *a);
/* classification: __fp_fpclassify returns math.h's FP_NAN 0, FP_INFINITE 1,
   FP_ZERO 2, FP_SUBNORMAL 3, FP_NORMAL 4 */
int __fp_fpclassify(const fp64 *a);
int __fp_isnan(const fp64 *a);
int __fp_isinf(const fp64 *a);
int __fp_isfinite(const fp64 *a);
int __fp_signbit(const fp64 *a);

#endif
