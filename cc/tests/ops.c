/* ops.c -- every binary operator against its neighbours in precedence, and
 * every punctuator, written with and without spaces: the parser's one loop
 * for binary expressions and the lexer's table by first character (D94). */
#include <stdio.h>
#include <stdarg.h>

#define CAT(a, b) a ## b
#define SHOW(e) printf("%s = %d\n", #e, (int)(e))

struct s { int m; };

static int sum(int n, ...)
{
    va_list ap;
    int t = 0;
    va_start(ap, n);
    while (n--) t += va_arg(ap, int);
    va_end(ap);
    return t;
}

int main(void)
{
    int a = 6, b = 3, c = 2, d = 1, x = 12, CAT(y, z) = 5;
    int v[3] = { 1, 2, 3 }, *p = v;
    struct s st = { 4 }, *q = &st;
    SHOW(a * b + c);
    SHOW(a + b * c);
    SHOW(a - b - c);
    SHOW(a / b / c);
    SHOW(a % b + c);
    SHOW(a * b % c);
    SHOW(a + b << c);
    SHOW(a << b + c);
    SHOW(a >> d + d);
    SHOW(a << b < c);
    SHOW(a < b << c);
    SHOW(a > b == c > d);
    SHOW(a == b > c);
    SHOW(a <= b != c >= d);
    SHOW(a & b == c);
    SHOW(a == b & c);
    SHOW(a ^ b & c);
    SHOW(a & b ^ c);
    SHOW(a | b ^ c);
    SHOW(a ^ b | c);
    SHOW(a && b | c);
    SHOW(a | b && c);
    SHOW(a || b && 0);
    SHOW(0 && b || c);
    SHOW(a ? b : c ? d : 0);
    SHOW(a < b ? c + d : c - d);
    SHOW(a - b * c / d % a + b << c >> d < a == b & c ^ d | a && b || c);
    SHOW(a<<b+c>>d);
    SHOW(a+b*c-d);
    SHOW(a<b==c>d);
    SHOW(a&&b||c&&d);
    SHOW(a&b|c^d);
    SHOW(-a + b);
    SHOW(!a == b);
    SHOW(~a & b);
    SHOW(a * -b);
    SHOW(a - -b);
    SHOW(a + +b);
    SHOW(*p + 1);
    SHOW(p[1] * p[2]);
    SHOW(*++p);
    SHOW(*p--);
    SHOW(q->m + st.m);
    SHOW((a, b));
    SHOW(yz);
    SHOW(sum(3, a, b, c));
    x += 3;  SHOW(x);
    x -= 1;  SHOW(x);
    x *= 2;  SHOW(x);
    x /= 3;  SHOW(x);
    x %= 7;  SHOW(x);
    x <<= 3; SHOW(x);
    x >>= 1; SHOW(x);
    x &= 13; SHOW(x);
    x ^= 6;  SHOW(x);
    x |= 16; SHOW(x);
    x<<=2;x>>=1;x+=1;x-=2;x*=3;x/=2;x%=9;x&=7;x^=5;x|=8;
    SHOW(x);
    x++; ++x; x--; --x;
    SHOW(x);
    return a<=b||a>=b?0:1;
}
