/* control flow */
#include <stdio.h>
int side;
int f(int v) { side++; return v; }
const char *name(int n)
{
    switch (n) {
    case 0: return "zero";
    case 1: case 2: return "small";
    case 100: return "hundred";
    case -1: return "minus one";
    default: return "other";
    }
}
int main(void)
{
    int i, j, n = 0, k;
    for (i = 0; i < 10; i++) {
        if (i == 2) continue;
        if (i == 8) break;
        for (j = 0; j < 3; j++) n += i * j;
    }
    printf("for %d\n", n);
    i = 0; while (i < 5) i += 2; printf("while %d\n", i);
    i = 10; do i--; while (i > 3); printf("do %d\n", i);
    for (i = -1; i <= 101; i += 1) if (i == -1 || i == 0 || i == 2 || i == 50 || i == 100) printf("%s ", name(i));
    printf("\n");
    k = 0;
    switch (3) { case 3: k += 3; case 4: k += 4; break; case 5: k += 5; }
    printf("fallthrough %d\n", k);
    side = 0;
    if (f(0) && f(1)) printf("no\n");
    if (f(1) || f(1)) printf("short %d\n", side);
    i = (f(1), f(2), 3);
    printf("comma %d %d\n", i, side);
    i = 0;
again:
    i++;
    if (i < 5) goto again;
    printf("goto %d\n", i);
    printf("cond %d %d\n", i > 3 ? 10 : 20, i < 3 ? 10 : i < 6 ? 30 : 40);
    for (i = 0, j = 10; i < j; i++, j--) ;
    printf("two vars %d %d\n", i, j);
    n = 0;
    for (;;) { if (++n == 7) break; }
    printf("forever %d\n", n);
    return n;
}
