/* math.c -- P32W C library: math.h's functions, on fplib (fpmath.c, D30).
 * Each is the double interface to an __fp_ function on softfp's fp64 (the
 * same 8 bytes as a double). float and double are one type on P32W, so
 * math.h makes the f and l names these. */
#include <math.h>
#include <string.h>
#include <softfp.h>
#include <fplib.h>

/* (math.h's NAN is __nan, defined in fpmath.c) */

/* x -> fp64 -> x */
static fp64 in(double x) { fp64 a; memcpy(&a, &x, 8); return a; }
static double out(fp64 r) { double x; memcpy(&x, &r, 8); return x; }

double fabs(double x) { fp64 a = in(x), r; __fp_fabs(&r, &a); return out(r); }
double floor(double x) { fp64 a = in(x), r; __fp_floor(&r, &a); return out(r); }
double ceil(double x) { fp64 a = in(x), r; __fp_ceil(&r, &a); return out(r); }
double trunc(double x) { fp64 a = in(x), r; __fp_trunc(&r, &a); return out(r); }
double round(double x) { fp64 a = in(x), r; __fp_round(&r, &a); return out(r); }
double rint(double x) { fp64 a = in(x), r; __fp_rint(&r, &a); return out(r); }
double logb(double x) { fp64 a = in(x), r; __fp_logb(&r, &a); return out(r); }
double sqrt(double x) { fp64 a = in(x), r; __fp_sqrt(&r, &a); return out(r); }
double exp(double x) { fp64 a = in(x), r; __fp_exp(&r, &a); return out(r); }
double exp2(double x) { fp64 a = in(x), r; __fp_exp2(&r, &a); return out(r); }
double expm1(double x) { fp64 a = in(x), r; __fp_expm1(&r, &a); return out(r); }
double log(double x) { fp64 a = in(x), r; __fp_log(&r, &a); return out(r); }
double log2(double x) { fp64 a = in(x), r; __fp_log2(&r, &a); return out(r); }
double log10(double x) { fp64 a = in(x), r; __fp_log10(&r, &a); return out(r); }
double log1p(double x) { fp64 a = in(x), r; __fp_log1p(&r, &a); return out(r); }
double sin(double x) { fp64 a = in(x), r; __fp_sin(&r, &a); return out(r); }
double cos(double x) { fp64 a = in(x), r; __fp_cos(&r, &a); return out(r); }
double tan(double x) { fp64 a = in(x), r; __fp_tan(&r, &a); return out(r); }
double asin(double x) { fp64 a = in(x), r; __fp_asin(&r, &a); return out(r); }
double acos(double x) { fp64 a = in(x), r; __fp_acos(&r, &a); return out(r); }
double atan(double x) { fp64 a = in(x), r; __fp_atan(&r, &a); return out(r); }
double sinh(double x) { fp64 a = in(x), r; __fp_sinh(&r, &a); return out(r); }
double cosh(double x) { fp64 a = in(x), r; __fp_cosh(&r, &a); return out(r); }
double tanh(double x) { fp64 a = in(x), r; __fp_tanh(&r, &a); return out(r); }
double cbrt(double x) { fp64 a = in(x), r; __fp_cbrt(&r, &a); return out(r); }
double asinh(double x) { fp64 a = in(x), r; __fp_asinh(&r, &a); return out(r); }
double acosh(double x) { fp64 a = in(x), r; __fp_acosh(&r, &a); return out(r); }
double atanh(double x) { fp64 a = in(x), r; __fp_atanh(&r, &a); return out(r); }
double fmod(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_fmod(&r, &a, &b); return out(r); }
double remainder(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_remainder(&r, &a, &b); return out(r); }
double copysign(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_copysign(&r, &a, &b); return out(r); }
double fmin(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_fmin(&r, &a, &b); return out(r); }
double fmax(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_fmax(&r, &a, &b); return out(r); }
double fdim(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_fdim(&r, &a, &b); return out(r); }
double nextafter(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_nextafter(&r, &a, &b); return out(r); }
double pow(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_pow(&r, &a, &b); return out(r); }
double atan2(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_atan2(&r, &a, &b); return out(r); }
double hypot(double x, double y) { fp64 a = in(x), b = in(y), r; __fp_hypot(&r, &a, &b); return out(r); }

double frexp(double x, int *e) { fp64 a = in(x), r; __fp_frexp(&r, &a, e); return out(r); }
double ldexp(double x, int n) { fp64 a = in(x), r; __fp_ldexp(&r, &a, n); return out(r); }
double scalbn(double x, int n) { return ldexp(x, n); }
int ilogb(double x) { fp64 a = in(x); return __fp_ilogb(&a); }
double modf(double x, double *ip) { fp64 a = in(x), r, w; __fp_modf(&r, &a, &w); *ip = out(w); return out(r); }
double nearbyint(double x) { return rint(x); }
long lround(double x) { return (long)round(x); }
long lrint(double x) { return (long)rint(x); }

int __fpclassify(double x) { fp64 a = in(x); return __fp_fpclassify(&a); }
int __isnan(double x) { fp64 a = in(x); return __fp_isnan(&a); }
int __isinf(double x) { fp64 a = in(x); return __fp_isinf(&a); }
int __isfinite(double x) { fp64 a = in(x); return __fp_isfinite(&a); }
int __signbit(double x) { fp64 a = in(x); return __fp_signbit(&a); }

/* a quiet NaN; C99 lets the tag choose its payload, here it is ignored */
double nan(const char *tag) { (void)tag; return NAN; }
