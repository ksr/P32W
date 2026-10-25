/* initialisers */
#include <stdio.h>
struct s { int a; char b[4]; int *p; };
int g = 5, garr[5] = { 1, 2, 3 }, *gp = &garr[2];
char gs[] = "global", gs8[8] = "ab", *gpp = "pointer";
struct s gst = { 7, "xyz", &g };
struct s gsa[2] = { { 1, "a", 0 }, { 2, "b", garr } };
int matrix[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
const char *names[] = { "one", "two", "three" };
static int zero_inited[4];
int main(void)
{
    int l = 3, la[4] = { 9, 8 }, i;
    char ls[] = "local", ls6[6] = "hi";
    struct s lst = { 11, "qq", &l };
    int lm[2][2] = { { 1, 2 }, { 3 } };
    printf("%d %d %d %d %d\n", g, garr[0], garr[3], *gp, zero_inited[3]);
    printf("%s %d %s %d %s\n", gs, (int)sizeof gs, gs8, gs8[5], gpp);
    printf("%d %s %d\n", gst.a, gst.b, *gst.p);
    printf("%d %s %d %d\n", gsa[1].a, gsa[1].b, gsa[1].p[1], gsa[0].p == 0);
    printf("%d %d\n", matrix[1][2], matrix[0][1]);
    for (i = 0; i < 3; i++) printf("%s ", names[i]);
    printf("\n%d %d %d %d\n", l, la[1], la[2], la[3]);
    printf("%s %d %s %d\n", ls, (int)sizeof ls, ls6, ls6[4]);
    printf("%d %s %d\n", lst.a, lst.b, *lst.p);
    printf("%d %d %d\n", lm[0][1], lm[1][0], lm[1][1]);
    return 0;
}
