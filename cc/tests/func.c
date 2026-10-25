/* functions: recursion, pointers, many arguments, variadic, statics */
#include <stdio.h>
#include <stdarg.h>
static int counter(void) { static int c = 10; return c++; }
int add6(int a, int b, int c, int d, int e, int f) { return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f; }
int mix(char a, short b, int c, unsigned char d, char e, short f, int g)
{ return a * 1000000 + b * 10000 + c * 1000 + d * 100 + e * 10 + f + g; }
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
int ack(int m, int n) { return m == 0 ? n + 1 : n == 0 ? ack(m - 1, 1) : ack(m - 1, ack(m, n - 1)); }
int sumv(int n, ...)
{
    va_list ap; int s = 0;
    va_start(ap, n);
    while (n--) s += va_arg(ap, int);
    va_end(ap);
    return s;
}
int twice(int (*f)(int), int x) { return f(f(x)); }
int inc(int x) { return x + 1; }
int sq(int x) { return x * x; }
int (*ops[])(int) = { inc, sq };
void hanoi(int n, int a, int b, int c, int *moves) { if (n) { hanoi(n - 1, a, c, b, moves); ++*moves; hanoi(n - 1, c, b, a, moves); } }
int main(void)
{
    int m = 0, i;
    printf("%d %d %d\n", counter(), counter(), counter());
    printf("%d\n", add6(1, 2, 3, 4, 5, 6));
    printf("%d\n", mix(1, 2, 3, 4, 5, 6, 7));
    printf("%d %d\n", fact(10), ack(2, 3));
    printf("%d %d\n", sumv(3, 10, 20, 30), sumv(6, 1, 2, 3, 4, 5, 6));
    printf("%d %d\n", twice(inc, 5), twice(sq, 3));
    for (i = 0; i < 2; i++) printf("%d ", ops[i](7));
    printf("\n");
    hanoi(10, 1, 2, 3, &m);
    printf("hanoi %d\n", m);
    printf("%d\n", (*ops[1])(9) + add6(sumv(2, 1, 1), 0, 0, 0, 0, fact(3)));
    return 0;
}
