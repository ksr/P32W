/* structs, unions, linked lists */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
struct node { int val; struct node *next; };
struct rec { char c; int i; short s; char name[5]; };
union u { int i; unsigned char b[4]; short h[2]; };
typedef struct { int x, y; } Point;
typedef struct line { Point a, b; } Line;
struct node *push(struct node *h, int v) { struct node *n = malloc(sizeof *n); n->val = v; n->next = h; return n; }
int len(Line *l) { int dx = l->b.x - l->a.x, dy = l->b.y - l->a.y; return dx * dx + dy * dy; }
int main(void)
{
    struct node *h = 0, *n;
    struct rec r, rs[3];
    union u x;
    Point p = { 1, 2 }, q;
    Line l;
    int i, total = 0;
    for (i = 1; i <= 10; i++) h = push(h, i);
    for (n = h; n; n = n->next) total += n->val;
    printf("list %d first %d\n", total, h->val);
    printf("sizes %d %d %d %d\n", (int)sizeof(struct rec), (int)offsetof(struct rec, i),
           (int)offsetof(struct rec, s), (int)offsetof(struct rec, name));
    r.c = 'x'; r.i = 1234567; r.s = -5;
    r.name[0] = 'o'; r.name[1] = 'k'; r.name[2] = 0;
    rs[1] = r;
    printf("%c %d %d %s\n", rs[1].c, rs[1].i, rs[1].s, rs[1].name);
    x.i = 0x11223344;
    printf("%x %x %x\n", x.b[0], x.b[3], x.h[1]);
    q = p; q.x += 10;
    l.a = p; l.b = q; l.b.y = 7;
    printf("%d %d %d %d len %d\n", p.x, q.x, l.b.x, l.b.y, len(&l));
    {
        struct { int a[3]; struct { char k; int v; } in; } s;
        s.a[2] = 9; s.in.k = 'k'; s.in.v = 77;
        printf("%d %c %d %d\n", s.a[2], s.in.k, s.in.v, (int)sizeof s);
    }
    return 0;
}
