#include <stdio.h>
#include <string.h>
struct pt { int x, y; };
struct box { struct pt a, b; char name[8]; int tags[3]; };
union u { int i; char c[4]; struct pt p; };
static int sq[6] = { [1] = 1, [3] = 9, 16, [0] = -1 };     /* 16 goes to [4] */
static int open_[] = { [4] = 4, [1] = 1 };                 /* length 5 */
static struct box b1 = { .b = { 3, 4 }, .name = "box", .tags = { [2] = 7 } };
static struct box b2 = { 1, 2, 3, 4, "elided", 5, 6, 7 }; /* brace elision */
static struct box b3 = { .a.y = 9, .b.x = 8, .tags[1] = 6 };
static int mat[2][3] = { 1, 2, 3, 4, 5 };
static int mat2[][2] = { [1] = { 1, 2 }, { 3 } };
static union u u1 = { .p = { 5, 6 } };
static union u u2 = { 0x41424344 };
static struct pt *gp = &(struct pt){ 10, 20 };
static int *gi = (int[]){ 7, 8, 9 };
static const char *names[] = { [2] = "two", [0] = "zero" };
static void show(const char *what, int *v, int n) { int k; printf("%s:", what); for (k = 0; k < n; k++) printf(" %d", v[k]); printf("\n"); }
static int sum(struct pt *p) { return p->x + p->y; }
static int f(int k) { return k * 10; }
int main(void)
{
    int k;
    struct pt loc = { .y = 2 };
    struct box lb = { .name = "local", .a = { .x = f(1) }, .tags = { [1] = f(2), f(3) } };
    int arr[] = { [3] = 1 };
    struct pt *ps[3];
    show("sq", sq, 6); show("open", open_, sizeof open_ / sizeof open_[0]);
    printf("b1: %d %d %d %d %s %d %d %d\n", b1.a.x, b1.a.y, b1.b.x, b1.b.y, b1.name, b1.tags[0], b1.tags[1], b1.tags[2]);
    printf("b2: %d %d %d %d %s %d %d %d\n", b2.a.x, b2.a.y, b2.b.x, b2.b.y, b2.name, b2.tags[0], b2.tags[1], b2.tags[2]);
    printf("b3: %d %d %d %d [%s] %d %d %d\n", b3.a.x, b3.a.y, b3.b.x, b3.b.y, b3.name, b3.tags[0], b3.tags[1], b3.tags[2]);
    show("mat", &mat[0][0], 6); show("mat2", &mat2[0][0], 6); printf("mat2 rows %d\n", (int)(sizeof mat2 / sizeof mat2[0]));
    printf("u1 %d %d  u2 %c%c\n", u1.p.x, u1.p.y, u2.c[0], u2.c[3]);
    printf("gp %d %d gi %d %d %d\n", gp->x, gp->y, gi[0], gi[1], gi[2]);
    printf("names %s %s %s\n", names[0], names[1] ? names[1] : "(null)", names[2]);
    printf("loc %d %d\n", loc.x, loc.y);
    printf("lb %d %d %s %d %d %d\n", lb.a.x, lb.a.y, lb.name, lb.tags[0], lb.tags[1], lb.tags[2]);
    show("arr", arr, sizeof arr / sizeof arr[0]);
    printf("sum %d\n", sum(&(struct pt){ .y = 5, .x = 6 }) + sum(&(struct pt){ 1, 1 }));
    for (k = 0; k < 3; k++) {                       /* a new object each time round */
        ps[k] = &(struct pt){ k, k * k };
        printf("lit %d %d\n", ps[k]->x, ps[k]->y);
    }
    loc = (struct pt){ .x = 41 };
    printf("assigned %d %d; int literal %d; func %s\n", loc.x, loc.y, (int){ 42 } + 0, __func__);
    printf("%d\n", ((int []){ 3, 4, 5 })[2] + (struct pt){ 1, 2 }.y);
    return 0;
}
