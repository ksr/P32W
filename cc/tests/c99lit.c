/* C99 constants and designators MicroPython uses (D35): hexadecimal
   floating constants, NAN and INFINITY as constant initialisers, and
   designators naming members of anonymous structs and unions */
#include <stdio.h>
#include <string.h>
#include <math.h>

static void bits(const char *what, double d)
{
    unsigned w[2];
    memcpy(w, &d, 8);
    printf("%s %08x%08x\n", what, w[1], w[0]);
}

static const double hex[] = { 0x1p120f, 0x1p-1022, 0x1p-126, 0x1.8p3, 0X.8P1, 0xA.Bp-2, 0x1p+0L,
                              0x1.fffffffffffffp1023, 0x1p-1074, 0x0.0000000000001p-1022, 0xffp0 };
static const double special[] = { NAN, INFINITY, -INFINITY };

struct node {
    int kind;
    union { struct { int lo, hi; }; double d; };
    struct { const char *name; struct node *next; };
};
static struct node tail = { .kind = 2, .d = 2.5, .name = "tail" };
static struct node head = { .kind = 1, .lo = 10, .hi = 20, .next = &tail, .name = "head" };

int main(void)
{
    unsigned i;
    int n = 0x1f;                       /* still an integer: no p exponent */
    for (i = 0; i < sizeof hex / sizeof hex[0]; i++) bits("hex", hex[i]);
    for (i = 0; i < 3; i++) bits("special", special[i]);
    printf("isnan %d isinf %d %d n %d\n", isnan(special[0]) != 0, isinf(special[1]) != 0, special[2] < 0, n);
    printf("%s %d %d %d -> %s %d %g\n", head.name, head.kind, head.lo, head.hi,
           head.next->name, head.next->kind, head.next->d);
    {
        struct node local = { .hi = 7, .name = "local", .kind = 3 };
        printf("%s %d %d %d %d\n", local.name, local.kind, local.lo, local.hi, local.next == 0);
    }
    return 0;
}
