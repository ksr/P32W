/* the software floating point (libc softfp.c, D27): arithmetic, conversions
   and maths, printed to 17 digits -- the same on P32W and on the host */
#include <stdio.h>
#include <softfp.h>
static unsigned seed = 12345;
static unsigned rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static void show(const char *what, const fp64 *x)
{
    char b[40];
    printf("%s %s %08x%08x\n", what, fp_format(b, x, 17), x->hi, x->lo);
}
int main(void)
{
    static const char *nums[] = { "0", "1", "-2.5", "0.1", "3.14159265358979", "1e300", "-7e-310",
                                  "123456789012345678", "6.02214076e23", "2.2250738585072014e-308", "0.3" };
    fp64 v[11], r, t;
    int i, j, ok;
    char b[40];
    for (i = 0; i < 11; i++) fp_parse(&v[i], nums[i]);
    for (i = 0; i < 11; i++)
        for (j = 0; j < 11; j += 3) {
            fp_add(&r, &v[i], &v[j]); show("+", &r);
            fp_mul(&r, &v[i], &v[j]); show("*", &r);
            fp_div(&r, &v[i], &v[j]); show("/", &r);
        }
    for (i = 0; i < 11; i++) {
        fp_sqrt(&r, &v[i]); show("sqrt", &r);
        fp_floor(&r, &v[i]); show("floor", &r);
        printf("toint %d %d  cmp %d  %s\n", fp_toint(&v[i], &ok), ok, fp_cmp(&v[i], &v[3]), fp_format(b, &v[i], 6));
    }
    for (i = 0; i < 40; i++) {          /* random operands */
        fp64 a, c;
        a.hi = rnd() ^ (rnd() << 24); a.lo = rnd() ^ (rnd() << 24);
        c.hi = rnd() ^ (rnd() << 24); c.lo = rnd() ^ (rnd() << 24);
        fp_sub(&r, &a, &c); show("-", &r);
        fp_div(&r, &a, &c); show("/", &r);
    }
    fp_parse(&t, "0.7");
    fp_exp(&r, &t); show("exp", &r);
    fp_log(&r, &t); show("log", &r);
    fp_sin(&r, &t); show("sin", &r);
    fp_cos(&r, &t); show("cos", &r);
    fp_tan(&r, &t); show("tan", &r);
    fp_atan(&r, &t); show("atan", &r);
    fp_pow(&r, &v[4], &t); show("pow", &r);
    fp_fromint(&r, -2147483647 - 1); show("int", &r);
    return 0;
}
