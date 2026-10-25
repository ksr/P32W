/* struct assignment through an index with side effects (m[k++] = m[i]):
   the statement's "no value wanted" must not reach the index */
#include <stdio.h>
struct m { int a, b, c; };
static int keep(struct m *m, int n)
{
    int i, k = 0;
    for (i = 0; i < n; i++)
        if (m[i].a) m[k++] = m[i];
    return k;
}
int main(void)
{
    struct m v[6];
    int i, n;
    for (i = 0; i < 6; i++) { v[i].a = i % 2; v[i].b = i * 10; v[i].c = -i; }
    n = keep(v, 6);
    printf("%d kept:", n);
    for (i = 0; i < n; i++) printf(" (%d %d %d)", v[i].a, v[i].b, v[i].c);
    printf("\n");
    i = 0;
    v[i++] = v[5];
    printf("i=%d v0=(%d %d %d)\n", i, v[0].a, v[0].b, v[0].c);
    return 0;
}
