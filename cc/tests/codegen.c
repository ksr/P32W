/* code generation corners: register pressure, nested calls, narrow types */
#include <stdio.h>
#include <string.h>
struct ops { int (*f)(int, int); char name[6]; short count; unsigned char flags; };
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int g(int a, int b) { return a * 3 - b; }
int h(int a) { return a + 100; }
int five(int a, int b, int c, int d, int e) { return a - b + c - d + e; }
int deep(int a, int b, int c, int d, int e, int f, int gg, int hh, int i)
{
    return a * (b + (c * (d + (e * (f + (gg * (hh + i * (a + (b * (c + d)))))))))) +
           ((a + b) * (c + d)) * ((e + f) * (gg + hh));
}
struct ops table[2] = { { add, "add", 0, 1 }, { mul, "mul", 0, 2 } };
int main(void)
{
    int i, x = 3, y = 4, z, arr[5] = { 1, 2, 3, 4, 5 }, *p, *q;
    char c = 100, s[16], t[16];
    unsigned char uc = 250;
    short sh = 32000;
    struct ops *o;
    printf("%d\n", deep(1, 2, 3, 4, 5, 6, 7, 8, 9));
    printf("%d\n", g(x + h(y), g(h(x), y * h(2))) + h(g(1, 2)));
    printf("%d\n", five(h(1), g(2, 3), five(1, 2, 3, 4, 5), add(x, y), mul(h(0), 2)));
    for (i = 0; i < 2; i++) {
        o = &table[i];
        z = o->f(o->f(x, y), table[1 - i].f(x + 1, h(i)));
        o->count += z;
        printf("%s %d %d %d\n", o->name, z, o->count, o->flags);
    }
    c += 200; uc += 10; sh += 1000;
    printf("%d %d %d\n", c, uc, sh);
    c = -5; c *= 30; printf("%d\n", c);
    uc = 3; uc -= 5; uc >>= 1; printf("%d\n", uc);
    strcpy(s, "copy me");
    p = arr; q = arr + 4;
    while (p < q) { int tmp = *p; *p++ = *q; *q-- = tmp; }
    for (i = 0; i < 5; i++) printf("%d", arr[i]);
    printf("\n");
    {
        char *d = t, *e = s;
        while ((*d++ = *e++) != 0) ;
        printf("%s %d\n", t, (int)(d - t));
    }
    table[0].name[0]++;
    ++table[1].name[1];
    printf("%s %s\n", table[0].name, table[1].name);
    switch (s[0]) { case 'c': printf("c!\n"); break; case 'x': printf("x\n"); break; }
    switch (-3) { case -3: printf("neg\n"); break; default: printf("bad\n"); }
    {
        unsigned n = 5;
        int k = 0;
        while (n-- > 0) k += n;
        printf("%d %u\n", k, n);
    }
    printf("%d %d\n", (x > y) == (y < x), (x & 1) ? x << 2 : x >> 1);
    return 0;
}
