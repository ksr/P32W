/* setjmp.c -- setjmp and longjmp: back out of nested calls, longjmp's value,
 * and a volatile local changed after setjmp keeps its new value (C's rule:
 * cc keeps volatile locals out of registers). Standard C only. */
#include <stdio.h>
#include <setjmp.h>

static jmp_buf env;
static int depth;

static void dive(int n)
{
    depth++;
    if (n == 0) longjmp(env, 7);
    dive(n - 1);
    printf("not reached\n");
}

int main(void)
{
    volatile int count = 0;
    volatile int sum = 0;
    int r, k;
    r = setjmp(env);
    printf("setjmp returned %d\n", r);
    for (k = 0; k < 5; k++) sum += k;       /* (used often: a register, were it not volatile) */
    count++;
    if (count < 4) dive(count * 10);
    printf("count %d, sum %d, depth %d\n", count, sum, depth);
    if (setjmp(env) == 0) longjmp(env, 0);   /* (0 comes back as 1) */
    else printf("longjmp(env, 0) gave a non-zero return\n");
    return 0;
}
