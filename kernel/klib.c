/* klib.c -- the kernel's own string routines (M3, D22) */
#include "defs.h"

/* memset and memcpy: ../software/libc/mem.s */
void *memmove(void *d, const void *s, uint n)
{
    uchar *dp = d;
    const uchar *sp = s;
    if (dp <= sp || dp >= sp + n) return memcpy(d, s, n);
    dp += n; sp += n;
    while (n--) *--dp = *--sp;
    return d;
}
int memcmp(const void *a, const void *b, uint n)
{
    const uchar *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}
uint strlen(const char *s) { const char *p = s; while (*p) p++; return p - s; }
int strncmp(const char *a, const char *b, uint n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uchar)*a - (uchar)*b;
        if (!*a) return 0;
    }
    return 0;
}
char *safestrcpy(char *d, const char *s, int n)
{
    char *r = d;
    if (n <= 0) return r;
    while (--n > 0 && (*d++ = *s++) != 0) ;
    *d = 0;
    return r;
}
