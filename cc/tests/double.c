/* double (float is the same type): arithmetic, comparisons (NaN rules),
   conversions both ways (int, unsigned, long long), constant folding,
   static initialisers, ++/--/op=, conditions, arrays and struct members,
   arguments and results; printed as hex bits so it checks the arithmetic
   without printf's %f */
#include <stdio.h>
#include <string.h>

static double ga = 1.5, gb = -0.1, gc = 1e300 * 10, gd = 1.0 / 3, ge = 2.5e-320, gf = -0.0;
static double tab[] = { 0.5, 1e10, -3, 7 / 2, 7.0 / 2, 1e-5 };
static long long gl = 1e15;
static int gi = 3.99;
struct pt { char tag; double x, y; };

static void bits(const char *what, double d)
{
    unsigned w[2];
    memcpy(w, &d, 8);
    printf("%s %08x%08x\n", what, w[1], w[0]);
}

static double sq(double x) { return x * x; }
static double poly(double x, int n) { double r = 0; while (n--) r = r * x + 1.5; return r; }
static struct pt mid(struct pt a, struct pt b) { struct pt r; r.tag = 'm'; r.x = (a.x + b.x) / 2; r.y = (a.y + b.y) / 2; return r; }
static double newton(double v) { double x = v; int k; for (k = 0; k < 30; k++) x = (x + v / x) / 2; return x; }

int main(void)
{
    double a = 0.1, b = 0.2, c, nan, inf, z = 0;
    float f = 2.25f;
    int i, k;
    unsigned u;
    long long ll;
    struct pt p = { 'p', 1.25, -2 }, q;
    bits("ga", ga); bits("gb", gb); bits("gc", gc); bits("gd", gd); bits("ge", ge); bits("gf", gf);
    for (k = 0; k < 6; k++) bits("tab", tab[k]);
    printf("gl %lld gi %d\n", gl, gi);
    bits("a+b", a + b); bits("a-b", a - b); bits("a*b", a * b); bits("a/b", a / b);
    bits("0.1+0.2 folded", 0.1 + 0.2); bits("-a", -a); bits("f", f);
    printf("a+b==0.3 %d, a+b>0.3 %d\n", a + b == 0.3, a + b > 0.3);
    inf = 1 / z; nan = z / z;
    bits("1/0", inf); bits("-1/0", -1 / z);
    printf("nan: < %d > %d <= %d >= %d == %d != %d, self %d\n", nan < 1, nan > 1, nan <= 1, nan >= 1, nan == nan, nan != nan, nan == nan);
    printf("compare %d %d %d %d %d %d\n", a < b, a > b, a <= a, b >= a, a == b, a != b);
    printf("truth %d %d %d %d\n", !a, !z, a && b ? 1 : 0, nan ? 1 : 0);
    i = 7.99; u = 3e9; ll = -1.5e17;
    printf("to int %d %d %u %lld\n", i, (int)-7.99, u, ll);
    printf("to int2 %d %u %d\n", (int)(a * 100), (unsigned)(2.5 * 1000000000), (short)300.7);
    bits("from int", (double)-123456789); bits("from uint", (double)4000000000u);
    bits("from ll", (double)1234567890123456789LL); bits("from ull", (double)18446744073709551615ULL);
    bits("from ll odd", (double)9007199254740993LL);
    bits("int+double", 3 + a); bits("ll*double", ll * 0.5);
    c = 1; for (k = 0; k < 60; k++) c *= 1.5; bits("1.5^60", c);
    c = 10; c += 1.5; c -= 0.25; c *= 3; c /= 7; bits("c", c);
    c = 1; bits("c++", c++); bits("c", c); bits("--c", --c);
    i = 10; i += 2.7; printf("i += 2.7: %d\n", i);
    i = 10; i *= 0.35; printf("i *= 0.35: %d\n", i);
    bits("sq", sq(1.1)); bits("poly", poly(0.5, 20)); bits("newton 2", newton(2.0));
    q = mid(p, (struct pt){ 'x', 3, 4 }); printf("mid %c ", q.tag); bits("x", q.x); bits("y", q.y);
    bits("cond", a > b ? a : b * 2); bits("mix cond", a > b ? 1 : 0.5);
    bits("tiny", ge * 1e-10); bits("huge", 1e308 * 10); bits("sub", 2.2250738585072014e-308 / 3);
    bits("literals", 1e-5 + .5 + 5. + 0x10 + 1.5e+2 + 2E3);
    printf("sizes %d %d\n", (int)sizeof(double), (int)sizeof tab);
    return (int)(ga * 10);
}
