/* stdlib.c -- P32W C library (M2, D22) */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

char **environ;
static void (*atexit_fns[8])(void);
static int natexit;

int atexit(void (*fn)(void))
{
    if (natexit == 8) return -1;
    atexit_fns[natexit++] = fn;
    return 0;
}

void exit(int status)
{
    while (natexit) atexit_fns[--natexit]();
    fflush(NULL);                       /* every output stream, fopen'd ones too */
    _exit(status);
}

/* abort: SIGABRT (a handler may run; if it returns, the default action
   ends the process all the same) */
void abort(void)
{
    fflush(stdout);
    raise(SIGABRT);
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
    _exit(134);                         /* (not reached) */
}

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* the digits of a number in base (0: by its prefix, 0x hex, 0 octal);
   *over is set if the value passed max (which is then returned) */
static unsigned long digits(const char *s, char **end, int base, unsigned long max, int *neg, int *over)
{
    const char *start = s;
    unsigned long v = 0;
    int d, any = 0;
    *neg = *over = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { *neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && isxdigit(s[2])) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    if (base < 2 || base > 36) { errno = EINVAL; if (end) *end = (char *)start; return 0; }
    for (;; s++) {
        if (isdigit(*s)) d = *s - '0';
        else if (isalpha(*s)) d = tolower(*s) - 'a' + 10;
        else break;
        if (d >= base) break;
        any = 1;
        if (v > (max - d) / base) *over = 1;  /* (the digits are still read) */
        else v = v * base + d;
    }
    if (end) *end = (char *)(any ? s : start);  /* (no number: nothing read) */
    if (*over) { errno = ERANGE; return max; }
    return v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long v = digits(s, end, base, 0xFFFFFFFFUL, &neg, &over);
    return neg && !over ? -v : v;
}

long strtol(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long v = digits(s, end, base, 0x80000000UL, &neg, &over);
    if (over) return neg ? (long)0x80000000UL : 0x7FFFFFFFL;   /* (errno is ERANGE) */
    if (!neg && v > 0x7FFFFFFFUL) { errno = ERANGE; return 0x7FFFFFFFL; }
    return neg ? (long)(0UL - v) : (long)v;    /* (-2147483648 too) */
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* the same in 64 bits (as digits above) */
static unsigned long long digits64(const char *s, char **end, int base, unsigned long long max, int *neg, int *over)
{
    const char *start = s;
    unsigned long long v = 0;
    int d, any = 0;
    *neg = *over = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { *neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && isxdigit(s[2])) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    if (base < 2 || base > 36) { errno = EINVAL; if (end) *end = (char *)start; return 0; }
    for (;; s++) {
        if (isdigit(*s)) d = *s - '0';
        else if (isalpha(*s)) d = tolower(*s) - 'a' + 10;
        else break;
        if (d >= base) break;
        any = 1;
        if (v > (max - d) / base) *over = 1;
        else v = v * base + d;
    }
    if (end) *end = (char *)(any ? s : start);
    if (*over) { errno = ERANGE; return max; }
    return v;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long long v = digits64(s, end, base, 0xFFFFFFFFFFFFFFFFull, &neg, &over);
    return neg && !over ? -v : v;
}

long long strtoll(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long long v = digits64(s, end, base, 0x8000000000000000ull, &neg, &over);
    if (over) return neg ? (long long)0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFll;
    if (!neg && v > 0x7FFFFFFFFFFFFFFFull) { errno = ERANGE; return 0x7FFFFFFFFFFFFFFFll; }
    return neg ? (long long)(0 - v) : (long long)v;
}

long long atoll(const char *s) { return strtoll(s, NULL, 10); }
long long llabs(long long v) { return v < 0 ? -v : v; }

static unsigned long rnd = 1;
int rand(void) { rnd = rnd * 1103515245 + 12345; return (int)(rnd >> 16) & RAND_MAX; }
void srand(unsigned seed) { rnd = seed; }

static void swap(char *a, char *b, size_t n)
{
    if (!(((unsigned)a | (unsigned)b) & 3))     /* (most keys are words: a word at a time) */
        for (; n >= 4; n -= 4, a += 4, b += 4) { int t = *(int *)a; *(int *)a = *(int *)b; *(int *)b = t; }
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

/* quicksort: the median of three as the pivot, and a partition whose two
   scans both stop at keys equal to it (so equal keys split evenly, and
   sorted or reversed input splits in the middle); the smaller part is
   recursed into and the larger looped on (the stack is at most log2 n deep) */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    char *b = base;
    while (n > 1) {
        size_t i, j;
        char *x = b, *y = b + (n / 2) * size, *z = b + (n - 1) * size;
        if (cmp(y, x) < 0) swap(x, y, size);    /* the three in order: x <= y <= z */
        if (cmp(z, y) < 0) { swap(y, z, size); if (cmp(y, x) < 0) swap(x, y, size); }
        swap(b, y, size);               /* the median, the pivot, at b[0] */
        i = 0;
        j = n;
        for (;;) {
            do i++; while (i < n && cmp(b + i * size, b) < 0);
            do j--; while (cmp(b + j * size, b) > 0);      /* (stops at b[0] at worst) */
            if (i >= j) break;
            swap(b + i * size, b + j * size, size);
        }
        swap(b, b + j * size, size);    /* [0,j) <= pivot, b[j] the pivot, (j,n) >= */
        if (j < n - j - 1) { qsort(b, j, size, cmp); b += (j + 1) * size; n -= j + 1; }
        else { qsort(b + (j + 1) * size, n - j - 1, size, cmp); n = j; }
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *))
{
    const char *b = base;
    while (n) {
        const char *m = b + (n / 2) * size;
        int c = cmp(key, m);
        if (c == 0) return (void *)m;
        if (c > 0) { b = m + size; n = n - n / 2 - 1; }
        else n = n / 2;
    }
    return NULL;
}

char *getenv(const char *name)
{
    char **e;
    size_t k = strlen(name);
    if (!environ) return NULL;
    for (e = environ; *e; e++)
        if (!strncmp(*e, name, k) && (*e)[k] == '=')
            return *e + k + 1;
    return NULL;
}

void __assert(const char *expr, const char *file, int line)
{
    fprintf(stderr, "assertion failed: %s, %s line %d\n", expr, file, line);
    abort();
}

/* the environment: setenv copies; the array grows on the heap */
static int env_owned;
int setenv(const char *name, const char *value, int overwrite)
{
    int n = 0, k = strlen(name), i;
    char *e, **ne;
    for (i = 0; environ && environ[i]; i++)
        if (!strncmp(environ[i], name, k) && environ[i][k] == '=') {
            if (!overwrite) return 0;
            break;
        }
    if (!(e = malloc(k + strlen(value) + 2))) return -1;
    sprintf(e, "%s=%s", name, value);
    if (environ && environ[i]) { environ[i] = e; return 0; }
    while (environ && environ[n]) n++;
    if (!(ne = malloc((n + 2) * sizeof *ne))) return -1;
    for (i = 0; i < n; i++) ne[i] = environ[i];
    ne[n] = e;
    ne[n + 1] = 0;
    if (env_owned) free(environ);
    environ = ne;
    env_owned = 1;
    return 0;
}
int unsetenv(const char *name)
{
    int k = strlen(name), i, j;
    for (i = j = 0; environ && environ[i]; i++)
        if (!(strncmp(environ[i], name, k) == 0 && environ[i][k] == '=')) environ[j++] = environ[i];
    if (environ) environ[j] = 0;
    return 0;
}

/* template ends in XXXXXX: they become letters and digits that make a new
   file, opened for reading and writing (mode 0600); its descriptor, or -1 */
int mkstemp(char *template)
{
    static const char abc[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static unsigned seq;
    int n = strlen(template), tries, k, fd;
    unsigned v;
    if (n < 6 || strcmp(template + n - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    for (tries = 0; tries < 1000; tries++) {
        v = (unsigned)getpid() * 7919u + seq++ * 104729u + (unsigned)time(0);
        for (k = 1; k <= 6; k++, v /= 36) template[n - k] = abc[v % 36];
        if ((fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600)) >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    return -1;
}
