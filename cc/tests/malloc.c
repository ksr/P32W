/* malloc, free, realloc and calloc under load: thousands of blocks of
   random sizes (small, medium, large), each filled with a pattern made
   from its number and checked before it is freed or resized; blocks freed
   in random order, so neighbours merge both ways; realloc growing and
   shrinking; and (on P32W) the break moved behind malloc's back */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __P32__
#include <unistd.h>
#endif

#define N 300
static unsigned char *blk[N];
static unsigned len[N], tag[N];
static unsigned seed = 12345, errors, checks;

static unsigned rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static unsigned size_for(void)
{
    unsigned r = rnd() % 100;
    if (r < 60) return 1 + rnd() % 64;              /* small */
    if (r < 90) return 65 + rnd() % 1000;           /* medium */
    if (r < 99) return 1100 + rnd() % 3000;         /* large */
    return 8000 + rnd() % 8000;                     /* very large */
}

static void fill(int k, unsigned from)
{
    unsigned i;
    for (i = from; i < len[k]; i++) blk[k][i] = (unsigned char)(tag[k] * 7 + i * 13);
}

static void check(int k, unsigned n)
{
    unsigned i;
    checks++;
    for (i = 0; i < n; i++)
        if (blk[k][i] != (unsigned char)(tag[k] * 7 + i * 13)) { errors++; return; }
}

int main(void)
{
    int round, k, live = 0, maxlive = 0;
    unsigned long total = 0;
    for (round = 0; round < 8000; round++) {
        k = rnd() % N;
        if (round == 4000) {
#ifdef __P32__
            sbrk(100);                                /* someone else's memory between */
#endif
        }
        if (!blk[k]) {
            len[k] = size_for();
            tag[k] = round;
            blk[k] = (rnd() & 7) ? malloc(len[k]) : calloc(len[k], 1);
            if (!blk[k]) { printf("out of memory at round %d\n", round); return 1; }
            if (((unsigned long)blk[k] & 7) != 0) errors++;
            fill(k, 0);
            live++;
            total += len[k];
        } else if (rnd() % 4 == 0) {                  /* resize */
            unsigned n = (rnd() & 1) ? len[k] / 2 + 1 : len[k] * 2 + rnd() % 100;
            unsigned char *p;
            check(k, len[k]);
            p = realloc(blk[k], n);
            if (!p) { printf("realloc failed at round %d\n", round); return 1; }
            if (((unsigned long)p & 7) != 0) errors++;
            blk[k] = p;
            check(k, n < len[k] ? n : len[k]);          /* the contents kept */
            if (n > len[k]) { unsigned old = len[k]; len[k] = n; fill(k, old); }
            else len[k] = n;
        } else {
            check(k, len[k]);
            free(blk[k]);
            blk[k] = 0;
            live--;
        }
        if (live > maxlive) maxlive = live;
    }
    for (k = 0; k < N; k++)
        if (blk[k]) { check(k, len[k]); free(blk[k]); }
    {                                                 /* calloc's zeros after reuse */
        unsigned char *z = calloc(5000, 1);
        unsigned i, nz = 0;
        for (i = 0; i < 5000; i++) nz += z[i] != 0;
        printf("calloc nonzero %u\n", nz);
        free(z);
    }
    printf("max live %d, %lu bytes allocated, %u checks, %u errors\n", maxlive, total, checks, errors);
    printf("realloc(0, 8) %s, free(0) ok\n", realloc(0, 8) ? "gives a block" : "fails");
    free(0);
    return errors != 0;
}
