/* a few classic small programs */
#include <stdio.h>
#include <string.h>
#define NQ 8
int cols[NQ], solutions;
int ok(int r, int c) { int i; for (i = 0; i < r; i++) if (cols[i] == c || cols[i] - c == r - i || c - cols[i] == r - i) return 0; return 1; }
void queens(int r) { int c; if (r == NQ) { solutions++; return; } for (c = 0; c < NQ; c++) if (ok(r, c)) { cols[r] = c; queens(r + 1); } }
char sieve[2000];
struct word { char w[12]; int n; } words[64];
int nwords;
void count(char *w)
{
    int i;
    for (i = 0; i < nwords; i++) if (!strcmp(words[i].w, w)) { words[i].n++; return; }
    strcpy(words[nwords].w, w); words[nwords++].n = 1;
}
int main(void)
{
    int i, j, primes = 0;
    char text[] = "the cat and the hat and the bat sat on the mat", *p, *s;
    queens(0);
    printf("queens %d\n", solutions);
    for (i = 2; i < 2000; i++) if (!sieve[i]) { primes++; for (j = i + i; j < 2000; j += i) sieve[j] = 1; }
    printf("primes %d\n", primes);
    for (p = text; *p; ) {
        while (*p == ' ') p++;
        s = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
        if (*s) count(s);
    }
    for (i = 0; i < nwords; i++) printf("%s=%d ", words[i].w, words[i].n);
    printf("\n");
    {
        unsigned crc = 0xFFFFFFFF; int k;
        for (i = 0; i < 26; i++) { crc ^= 'a' + i; for (k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1; }
        printf("crc %08x\n", ~crc);
    }
    return 0;
}
