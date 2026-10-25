/* bench.c -- hand-written assembly against p32cc's C (the backlog's
 * measurement): the same routines both ways, each run on its own in the
 * microcode-level emulator, which counts P32W microcycles.
 *   -DBENCH=n   which routine (0: none -- the baseline: setup only)
 *   -DASM       the assembly versions (bench_asm.s) instead of the C ones
 * run.sh builds every combination and writes the table. */
#include <stdio.h>
#include <string.h>

#define N 4096
static unsigned char buf[N], dst[N];
static unsigned words[1024], wdst[1024];
static int arr[200];
static unsigned short hsrc[256], hdst[256];
static unsigned table[256];

#ifdef ASM
int b_strlen(const char *s);
void b_wcopy(unsigned *d, const unsigned *s, int n);
unsigned b_sum(const unsigned char *p, int n);
unsigned b_crc(const unsigned char *p, int n, const unsigned *t);
void b_isort(int *a, int n);
int b_count(const unsigned char *p, int n, int c);
void b_hcopy(unsigned short *d, const unsigned short *s, int n);
#else
static int b_strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}
static void b_wcopy(unsigned *d, const unsigned *s, int n)
{
    while (n--) *d++ = *s++;
}
static unsigned b_sum(const unsigned char *p, int n)
{
    unsigned s = 0;
    while (n--) s += *p++;
    return s;
}
static unsigned b_crc(const unsigned char *p, int n, const unsigned *t)
{
    unsigned c = 0xFFFFFFFF;
    while (n--) c = t[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}
static void b_isort(int *a, int n)
{
    int i, j, v;
    for (i = 1; i < n; i++) {
        v = a[i];
        for (j = i - 1; j >= 0 && a[j] > v; j--) a[j + 1] = a[j];
        a[j + 1] = v;
    }
}
static int b_count(const unsigned char *p, int n, int c)
{
    int k = 0;
    while (n--) if (*p++ == c) k++;
    return k;
}
static void b_hcopy(unsigned short *d, const unsigned short *s, int n)
{
    while (n--) *d++ = *s++;
}
#endif

int main(void)
{
    unsigned r = 0, k, i;
    for (i = 0; i < N; i++) buf[i] = (i * 7 + 3) % 251 + 1;
    buf[999] = 0;
    for (i = 0; i < 1024; i++) words[i] = i * 2654435761u;
    for (i = 0; i < 200; i++) arr[i] = (i * 7919) % 1000;
    for (i = 0; i < 256; i++) hsrc[i] = i * 3;
    for (k = 0; k < 256; k++) {
        unsigned c = k, b;
        for (b = 0; b < 8; b++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
        table[k] = c;
    }
#if BENCH == 1
    for (k = 0; k < 20; k++) r += b_strlen((char *)buf);           /* strlen: 999 bytes x 20 */
#elif BENCH == 2
    for (k = 0; k < 20; k++) b_wcopy(wdst, words, 1024);           /* copy 4 KiB in words x 20 */
#elif BENCH == 3
    for (k = 0; k < 10; k++) r += b_sum(buf, N);                   /* byte sum, 4 KiB x 10 */
#elif BENCH == 4
    for (k = 0; k < 4; k++) r += b_crc(buf, N, table);             /* CRC-32, 4 KiB x 4 */
#elif BENCH == 5
    b_isort(arr, 200);                                             /* insertion sort, 200 ints */
#elif BENCH == 6
    for (k = 0; k < 10; k++) r += b_count(buf, N, 'a');            /* count a byte (grep's loop), 4 KiB x 10 */
#elif BENCH == 7
    for (k = 0; k < 40; k++) b_hcopy(hdst, hsrc, 256);             /* a CF sector in halfwords x 40 */
#endif
    (void)dst;
    return (int)(r & 0x7F) + (arr[0] > arr[199]);
}
