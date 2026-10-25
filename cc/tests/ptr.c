/* pointers, arrays, strings */
#include <stdio.h>
#include <string.h>
int grid[4][5];
char *words[] = { "alpha", "beta", "gamma", 0 };
int sum(int *p, int n) { int s = 0; while (n--) s += *p++; return s; }
void rev(char *s) { char *e = s + strlen(s) - 1; while (s < e) { char t = *s; *s++ = *e; *e-- = t; } }
int main(void)
{
    int a[10], i, j, *p, *q, **pp;
    char buf[32], *w, **ww;
    for (i = 0; i < 10; i++) a[i] = i * i;
    p = a; q = &a[7];
    printf("%d %d %d %d\n", *p, *(p + 3), (int)(q - p), *q--);
    printf("%d %d\n", p[9], 9[p]);
    printf("%d\n", sum(a + 2, 5));
    pp = &p; **pp = 100; printf("%d\n", a[0]);
    for (i = 0; i < 4; i++) for (j = 0; j < 5; j++) grid[i][j] = i * 10 + j;
    printf("%d %d %d\n", grid[2][3], *(*(grid + 3) + 4), (int)(sizeof grid / sizeof grid[0]));
    for (ww = words; *ww; ww++) printf("%s:%d ", *ww, (int)strlen(*ww));
    printf("\n");
    strcpy(buf, "pointers");
    rev(buf);
    printf("%s %c %c\n", buf, *buf, buf[strlen(buf) - 1]);
    w = buf;
    while (*w) w++;
    printf("%d\n", (int)(w - buf));
    p = &a[5];
    printf("%d %d %d\n", p[-1], p > a, p == a + 5);
    printf("%d\n", "hello"[1]);
    {
        int (*row)[5] = grid;
        row++;
        printf("%d\n", (*row)[2]);
    }
    return 0;
}
