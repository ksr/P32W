/* structs by value: as arguments (alone, mixed with scalars, more than four
   words, in variadic calls), as results (assigned, used at once, passed on,
   member of a result, through a function pointer, recursion), and copies
   (the callee's changes do not reach the caller) */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

struct pt { int x, y; };
struct big { int v[6]; char name[10]; };
struct odd { char a, b, c; };
struct sh { short s[3]; };

static struct pt mk(int x, int y) { struct pt p; p.x = x; p.y = y; return p; }
static struct pt add(struct pt a, struct pt b) { return mk(a.x + b.x, a.y + b.y); }
static int dot(struct pt a, int k, struct pt b) { a.x *= k; return a.x * b.x + a.y * b.y; }
static struct big fill(int base, const char *name)
{
    struct big b;
    int k;
    for (k = 0; k < 6; k++) b.v[k] = base + k;
    strcpy(b.name, name);
    return b;
}
static int sum(struct big b) { int s = 0, k; for (k = 0; k < 6; k++) s += b.v[k]; b.v[0] = -1; return s; }
static struct odd flip(struct odd o) { struct odd r = { o.c, o.b, o.a }; return r; }
static struct sh twice(struct sh s) { int k; for (k = 0; k < 3; k++) s.s[k] *= 2; return s; }
static struct pt fib(int n) { struct pt p; if (n == 0) return mk(0, 1); p = fib(n - 1); return mk(p.y, p.x + p.y); }
static int many(int a, int b, struct pt c, int d, int e, struct odd f, int g)
{
    return a + b + c.x + c.y + d + e + f.a + f.b + f.c + g;
}
int aligned(void);
static int vsum(int n, ...)
{
    va_list ap;
    int s = 0;
    va_start(ap, n);
    while (n--) { struct pt p = va_arg(ap, struct pt); s += p.x * 10 + p.y; }
    va_end(ap);
    return s;
}

int main(void)
{
    struct pt a = mk(1, 2), b = { 10, 20 }, c;
    struct pt (*f)(struct pt, struct pt) = add;
    struct big g = fill(100, "first");
    struct odd o = { 'a', 'b', 'c' }, r;
    struct sh s = { { 1, -2, 3 } }, t;
    c = add(a, b);
    printf("c %d %d\n", c.x, c.y);
    printf("nested %d\n", add(add(a, b), mk(100, 200)).y);
    printf("dot %d (a.x still %d)\n", dot(a, 3, b), a.x);
    printf("via pointer %d\n", f(b, b).x);
    printf("sum %d (g.v[0] still %d) %s\n", sum(g), g.v[0], fill(7, "second").name);
    r = flip(o);
    printf("flip %c%c%c\n", r.a, r.b, r.c);
    t = twice(s);
    printf("twice %d %d %d (s %d)\n", t.s[0], t.s[1], t.s[2], s.s[0]);
    printf("fib %d %d\n", fib(20).x, fib(25).x);
    printf("many %d\n", many(1, 2, a, 3, 4, o, 5));
    printf("vsum %d\n", vsum(3, a, b, mk(5, 6)));
    printf("compound %d\n", add((struct pt){ 7, 8 }, a).x);
    aligned();
    return c.x + c.y;
}

/* (copies of a 2-aligned struct at an odd halfword: not by words) */
struct two { short a, b; };
struct outer { short pad; struct two t; struct two u; };
static struct two swap2(struct two x) { struct two r = { x.b, x.a }; return r; }
int aligned(void)
{
    struct outer o = { 1, { 2, 3 }, { 4, 5 } };
    o.u = o.t;
    o.t = swap2(o.u);
    printf("aligned %d %d %d %d\n", o.u.a, o.u.b, o.t.a, o.t.b);
    return 0;
}
