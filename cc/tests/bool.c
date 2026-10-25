/* _Bool (D35): every value converted to it -- by assignment, initialiser,
   argument, return or cast -- is 0 or 1 (not its low byte): from ints
   whose low byte is 0, long longs, doubles, pointers and constants; bool in
   arrays, structs and arithmetic */
#include <stdio.h>
#include <stdbool.h>

static bool gb = 512;                           /* a constant: 1 */
static bool garr[4] = { 0, 256, -1, 2 };
struct flags { bool a; int n; bool b; };
static struct flags gs = { 1024, 7, 0 };

static bool ret(int x) { return x; }
static bool retp(const char *p) { return p; }
static int take(bool b) { return b; }

int main(void)
{
    int big = 0x100, k;
    long long ll = 0x100000000LL;
    double d = 0.5, z = 0.0;
    char c = 'x', *np = 0;
    unsigned u = 0x80000000u;
    bool b;
    bool a[3];
    struct flags s;
    b = big;          printf("int 0x100 -> %d\n", b);
    b = ll;           printf("long long 2^32 -> %d\n", b);
    b = d;            printf("double 0.5 -> %d\n", b);
    b = z;            printf("double 0.0 -> %d\n", b);
    b = &c;           printf("pointer -> %d\n", b);
    b = np;           printf("null pointer -> %d\n", b);
    b = u;            printf("0x80000000u -> %d\n", b);
    b = -256;         printf("constant -256 -> %d\n", b);
    printf("cast (bool)0x200 = %d, (bool)big = %d, (bool)0 = %d\n", (bool)0x200, (bool)big, (bool)0);
    printf("argument %d, return %d %d, pointer return %d %d\n", take(big), ret(big), ret(0), retp(&c), retp(np));
    printf("globals %d, %d %d %d %d, struct %d %d %d\n", gb, garr[0], garr[1], garr[2], garr[3], gs.a, gs.n, gs.b);
    for (k = 0; k < 3; k++) a[k] = big * k;
    printf("array %d %d %d\n", a[0], a[1], a[2]);
    s.a = 0x300; s.n = 5; s.b = big - 0x100;
    printf("members %d %d %d\n", s.a, s.n, s.b);
    printf("arithmetic %d %d, sizeof %d, true + true = %d\n", b + 2, !b, (int)sizeof(bool), true + true);
    b = big; b = !b;   printf("not -> %d\n", b);
    printf("compare (bool)big == true: %d\n", (bool)big == true);
    return take(big) + ret(0x100) - 2;
}
