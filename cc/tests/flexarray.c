/* a flexible array member: in a malloc'd object, and statically initialised
   with elements (GCC's extension, which MicroPython's qstr pools use: the
   object runs on past sizeof), next to bit-fields and other objects that
   must not overlap it (D35) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct pool { const struct pool *prev; unsigned total : 31; unsigned sorted : 1; unsigned len; const char *qstrs[]; } pool_t;
static const pool_t a = { NULL, 0, 0, 3, { "one", "two", "three" } };
static const int between = 12345;
static const pool_t b = { &a, 3, 1, 2, { "four", "five" } };
struct nums { int n; short v[]; };
static const struct nums sq = { 5, { 0, 1, 4, 9, 16 } };
static const struct nums none = { 0 };
static const char tail[] = "after";

static void show(const pool_t *p)
{
    unsigned k;
    printf("total %u sorted %u len %u:", p->total, p->sorted, p->len);
    for (k = 0; k < p->len; k++) printf(" %s", p->qstrs[k]);
    printf("%s\n", p->prev ? " (has prev)" : "");
}

int main(void)
{
    int k;
    struct nums *m = malloc(sizeof(struct nums) + 4 * sizeof(short));
    show(&a);
    show(&b);
    show(b.prev);
    printf("between %d, tail %s, none %d\n", between, tail, none.n);
    for (k = 0; k < sq.n; k++) printf("%d ", sq.v[k]);
    m->n = 4;
    for (k = 0; k < m->n; k++) m->v[k] = (short)(k * 100);
    for (k = 0; k < m->n; k++) printf("%d ", m->v[k]);
    printf("\n");
    free(m);
    return 0;
}
