/* volatile and setjmp / longjmp: a volatile local or parameter changed
   after setjmp keeps its new value when longjmp comes back (C99 7.13.2.1).
   Both spellings: volatile T x, and T *volatile p (the pointer itself; it
   was missed, and MicroPython's VM lost its exception handlers, D35) */
#include <stdio.h>
#include <setjmp.h>

static jmp_buf env;
static int table[4] = { 10, 20, 30, 40 };

static void jump(int v) { longjmp(env, v); }

static int run(volatile int n)
{
    int *volatile p = table;            /* the pointer is volatile */
    volatile int count = 0;
    int *volatile q = table + 3, plain = 7;   /* q volatile, plain not (and unused after) */
    if (setjmp(env) == 0) {
        p = table + 2;
        count = 5;
        n = n * 2;
        q = table;
        (void)plain;
        jump(1);
    }
    return *p + count + n + *q;
}

int main(void)
{
    int k, total = 0;
    for (k = 1; k <= 3; k++) {
        int r = run(k);
        printf("run(%d) = %d\n", k, r);
        total += r;
    }
    return total & 255;
}
