/* the preprocessor */
#include <stdio.h>
#define SQ(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define N 10
#define EMPTY
#define SELF SELF
#define VERSION 3
#if VERSION >= 3 && defined(N) && !defined(NOPE)
#define OK "yes"
#elif VERSION == 2
#define OK "two"
#else
#define OK "no"
#endif
#ifdef NOPE
#error should not happen
#endif
#if (N * 2 + 1) % 7 == 0
int seven = 1;
#else
int seven = 0;
#endif
int CAT(var, 1) = 41;
#define CALL(f, ...) f(__VA_ARGS__)
int main(void)
{
    int SELF = 2;
    printf("%d %d %d\n", SQ(N + 1), MAX(3, N), SQ(SQ(2)));
    printf("%s %s %s\n", STR(a + b), XSTR(N), OK);
    printf("%d %d %d\n", var1 + 1, seven, SELF);
    printf("%d\n", CALL(MAX, 4, 9) EMPTY);
    printf("%s:%d\n", __FILE__ + 0 ? "file" : "?", __LINE__ > 0);
#undef N
#ifndef N
    printf("N undefined\n");
#endif
    return 0;
}
