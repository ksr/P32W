/* the floating-point library through the compilers' double: printf's %e %f
   %g %a with flags, widths and precisions; strtod and atof (correctly
   rounded, end pointers, specials); sscanf %f %lf %e %g; the exact maths
   functions bit for bit, and the others printed to 12 digits (within a few
   ulp of the host's) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void bits(const char *what, double d)
{
    unsigned w[2];
    memcpy(w, &d, 8);
    printf("%s %08x%08x\n", what, w[1], w[0]);
}

int main(void)
{
    static const double v[] = { 0, -0.0, 1, -1, 0.1, 1.0 / 3, 2.5, 3.5, 1e100, 1.7976931348623157e308,
                                2.2250738585072014e-308, 5e-324, 123456.789, 6.02214076e23, 1e15, 1e16, 0.5e-5 };
    static const char *const fmts[] = { "%e", "%f", "%g", "%.0e", "%.0f", "%#.0f", "%.3g", "%#g", "%12.4f|", "%-12.2e|",
                                        "%+.10g", "% .1f", "%010.3f", "%.17g", "%.20f", "%a", "%.3A", "%G", "%E" };
    static const char *const strs[] = { "3.14159", "  -2.5e-3xyz", "1e400", "-1e-400", "0x1.8p3", "inf", "-Infinity",
                                        "nan", ".5", "5.", "1e", "e5", "0.000000000000000000000000000000001",
                                        "2.2250738585072011e-308", "123456789012345678901234567890", "+0.0" };
    char *end, buf[64];
    double d, e, f;
    int k, j, n;
    for (k = 0; k < (int)(sizeof v / sizeof v[0]); k++)
        for (j = 0; j < (int)(sizeof fmts / sizeof fmts[0]); j++) {
            printf(fmts[j], v[k]);
            putchar(j % 6 == 5 ? '\n' : ' ');
        }
    printf("\ninf %f %e %g %F nan %f %5.1f|\n", 1 / v[0] * -1, 1 / v[1], 1e999, 1e999, v[0] / v[0], -(v[0] / v[0]));
    printf("star %*.*f|%-*d|\n", 10, 3, 3.14159, 4, 7);
    for (k = 0; k < (int)(sizeof strs / sizeof strs[0]); k++) {
        d = strtod(strs[k], &end);
        bits(strs[k], d);
        printf("  rest [%s]\n", end);
    }
    printf("atof %g %g\n", atof("12.5e1"), atof("-0.001"));
    n = sscanf("1.5 -2e3 100ergs 0x10p0", "%lf %lf %lf%s %la", &d, &e, &f, buf, &d);
    printf("sscanf %d: %g %g %g [%s] %g\n", n, d, e, f, buf, d);
    n = sscanf("  3.25e+2, nan", "%lf, %lf", &d, &e);
    printf("sscanf %d: %g %g\n", n, d, e);
    bits("floor", floor(-2.5)); bits("ceil", ceil(-2.5)); bits("trunc", trunc(-2.7)); bits("round", round(2.5));
    bits("rint", rint(2.5)); bits("fmod", fmod(10.3, 3)); bits("fmod-", fmod(-10.3, 3)); bits("sqrt", sqrt(2));
    bits("fabs", fabs(-3.5)); bits("ldexp", ldexp(1.5, 10)); bits("copysign", copysign(2, -0.0));
    bits("remainder", remainder(10.3, 3)); bits("nextafter", nextafter(1, 2)); bits("hypot", hypot(3, 4));
    d = frexp(48, &n); printf("frexp %g %d\n", d, n);
    d = modf(-3.75, &e); printf("modf %g %g\n", d, e);
    printf("ilogb %d isnan %d isinf %d %d\n", ilogb(1000), isnan(NAN), isinf(INFINITY), isinf(-HUGE_VAL));
    printf("exp %.12g log %.12g log10 %.12g pow %.12g\n", exp(1), log(10), log10(2), pow(2, 0.5));
    printf("sin %.12g cos %.12g tan %.12g atan2 %.12g\n", sin(1), cos(1), tan(1), atan2(1, -1));
    printf("asin %.12g acos %.12g sinh %.12g cbrt %.12g\n", asin(0.5), acos(-0.5), sinh(1), cbrt(27));
    printf("asinh %.12g acosh %.12g atanh %.12g erf %.12g erfc %.12g\n", asinh(-2.5), acosh(3), atanh(0.25), erf(0.5), erfc(1.5));
    printf("tgamma %.12g %.12g lgamma %.12g %.12g\n", tgamma(5.5), tgamma(-2.5), lgamma(100), lgamma(0.5));
    printf("expm1 %.12g log1p %.12g tanh %.12g cosh %.12g\n", expm1(1e-5), log1p(-0.25), tanh(0.75), cosh(2));
    printf("pi %.15f e %.15f\n", M_PI, M_E);
    printf("pow(2,10) %g pow(-2,3) %g pow(4,-0.5) %g\n", pow(2, 10), pow(-2, 3), pow(4, -0.5));
    return 0;
}
