/* math.h -- P32W C library: the maths functions (D30: p32cc has
 * double). P32W has no FPU: every function is software (fplib.h's __fp_*
 * on softfp, D27). float and double are the same type here -- IEEE binary64
 * -- so the f forms are the double functions under another name.
 *
 * The exact functions (floor ceil trunc round rint nearbyint fmod
 * remainder modf frexp ldexp scalbn ilogb logb sqrt copysign fabs fmin
 * fmax fdim nextafter) give the IEEE result bit for bit; the others are
 * within a few ulp. C99 Annex F's special values (signed zeros, infinities,
 * NaN) are followed. errno is not set and there are no floating-point
 * exception flags (like the Mac, which reports through flags only). */
#ifndef _MATH_H
#define _MATH_H

typedef double float_t;
typedef double double_t;

/* An overflowing constant becomes +inf (C99 6.4.4.2 with Annex F): p32cc
   reads constants with a correctly rounding strtod. */
#define HUGE_VAL   1e999
#define HUGE_VALF  1e999
#define HUGE_VALL  1e999
#define INFINITY   __builtin_inf()      /* (constants for both compilers, D35) */
extern const double __nan;              /* (the quiet NaN 0x7FF8000000000000, in libm) */
#define NAN        __builtin_nan("")

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#define FP_ILOGB0    (-2147483647 - 1)  /* (as on the Mac) */
#define FP_ILOGBNAN  (-2147483647 - 1)
#define MATH_ERRNO     1
#define MATH_ERREXCEPT 2
#define math_errhandling 0              /* (neither: no errno, no flags) */

#define M_E        2.71828182845904523536028747135266250
#define M_LOG2E    1.44269504088896340735992468100189214
#define M_LOG10E   0.434294481903251827651128918916605082
#define M_LN2      0.693147180559945309417232121458176568
#define M_LN10     2.30258509299404568401799145468436421
#define M_PI       3.14159265358979323846264338327950288
#define M_PI_2     1.57079632679489661923132169163975144
#define M_PI_4     0.785398163397448309615660845819875721
#define M_1_PI     0.318309886183790671537767526745028724
#define M_2_PI     0.636619772367581343075535053490057448
#define M_2_SQRTPI 1.12837916709551257389615890312154517
#define M_SQRT2    1.41421356237309504880168872420969808
#define M_SQRT1_2  0.707106781186547524400844362104849039

/* classification (any real argument: float and double are one type) */
int __fpclassify(double x);
int __isnan(double x);
int __isinf(double x);
int __isfinite(double x);
int __signbit(double x);
#define fpclassify(x) __fpclassify(x)
#define isnan(x)      __isnan(x)
#define isinf(x)      __isinf(x)
#define isfinite(x)   __isfinite(x)
#define isnormal(x)   (__fpclassify(x) == FP_NORMAL)
#define signbit(x)    __signbit(x)
/* comparisons that are false for NaN (P32W raises no "invalid" flag anyway) */
#define isgreater(x, y)      ((x) > (y))
#define isgreaterequal(x, y) ((x) >= (y))
#define isless(x, y)         ((x) < (y))
#define islessequal(x, y)    ((x) <= (y))
#define islessgreater(x, y)  ((x) < (y) || (x) > (y))
#define isunordered(x, y)    (__isnan(x) || __isnan(y))

/* C89 */
double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double cos(double x);
double sin(double x);
double tan(double x);
double cosh(double x);
double sinh(double x);
double tanh(double x);
double exp(double x);
double frexp(double x, int *e);
double ldexp(double x, int n);
double log(double x);
double log10(double x);
double modf(double x, double *ip);
double pow(double x, double y);
double sqrt(double x);
double ceil(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);

/* C99 */
double exp2(double x);
double expm1(double x);
double log2(double x);
double log1p(double x);
double logb(double x);
int ilogb(double x);
double scalbn(double x, int n);
double cbrt(double x);
double hypot(double x, double y);
double trunc(double x);
double round(double x);
long lround(double x);
double rint(double x);
double nearbyint(double x);
long lrint(double x);
double remainder(double x, double y);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double erf(double x);                   /* (erf, erfc, tgamma, lgamma: musl's, libc/erf.c, gamma.c) */
double erfc(double x);
double tgamma(double x);
double lgamma(double x);
double copysign(double x, double y);
double nan(const char *tag);       /* a quiet NaN (the tag is ignored) */

/* P32W: C's float is the 8-byte double, so 4-byte IEEE floats (binary32) in
   files and binary data are read and written as their bits (libc/f32.c) */
unsigned __f32_bits(double x);          /* x rounded to nearest, as binary32 bits */
double __f32_value(unsigned f);         /* binary32 bits f as a double (exact) */
double nextafter(double x, double y);
double fdim(double x, double y);
double fmax(double x, double y);
double fmin(double x, double y);

/* float and long double: the same functions */
#define acosf acos
#define asinf asin
#define atanf atan
#define atan2f atan2
#define cosf cos
#define sinf sin
#define tanf tan
#define coshf cosh
#define sinhf sinh
#define tanhf tanh
#define expf exp
#define exp2f exp2
#define expm1f expm1
#define frexpf frexp
#define ldexpf ldexp
#define logf log
#define log10f log10
#define log2f log2
#define log1pf log1p
#define logbf logb
#define ilogbf ilogb
#define scalbnf scalbn
#define modff modf
#define powf pow
#define sqrtf sqrt
#define cbrtf cbrt
#define asinhf asinh
#define acoshf acosh
#define atanhf atanh
#define erff erf
#define erfcf erfc
#define tgammaf tgamma
#define lgammaf lgamma
#define hypotf hypot
#define ceilf ceil
#define fabsf fabs
#define nanf nan
#define floorf floor
#define fmodf fmod
#define truncf trunc
#define roundf round
#define lroundf lround
#define rintf rint
#define nearbyintf nearbyint
#define lrintf lrint
#define remainderf remainder
#define copysignf copysign
#define nextafterf nextafter
#define fdimf fdim
#define fmaxf fmax
#define fminf fmin
#define acosl acos
#define asinl asin
#define atanl atan
#define atan2l atan2
#define cosl cos
#define sinl sin
#define tanl tan
#define coshl cosh
#define sinhl sinh
#define tanhl tanh
#define expl exp
#define exp2l exp2
#define expm1l expm1
#define frexpl frexp
#define ldexpl ldexp
#define logl log
#define log10l log10
#define log2l log2
#define log1pl log1p
#define logbl logb
#define ilogbl ilogb
#define scalbnl scalbn
#define modfl modf
#define powl pow
#define sqrtl sqrt
#define cbrtl cbrt
#define asinhl asinh
#define acoshl acosh
#define atanhl atanh
#define erfl erf
#define erfcl erfc
#define tgammal tgamma
#define lgammal lgamma
#define hypotl hypot
#define ceill ceil
#define fabsl fabs
#define nanl nan
#define floorl floor
#define fmodl fmod
#define truncl trunc
#define roundl round
#define lroundl lround
#define rintl rint
#define nearbyintl nearbyint
#define lrintl lrint
#define remainderl remainder
#define copysignl copysign
#define nextafterl nextafter
#define fdiml fdim
#define fmaxl fmax
#define fminl fmin

#endif
