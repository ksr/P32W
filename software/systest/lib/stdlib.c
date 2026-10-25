/* stdlib.c -- strtol and strtoul at their limits, qsort on awkward input,
 * getcwd, and abort. Standard C and POSIX only: the expected output is what
 * the build machine's library prints (where long is wider, each machine
 * checks its own limits, and prints the same words). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static void tol(const char *s, int base)
{
    char *end;
    long v;
    errno = 0;
    v = strtol(s, &end, base);
    printf("  strtol(\"%s\", %d) = ", s, base);
    if (v == LONG_MAX) printf("LONG_MAX");
    else if (v == LONG_MIN) printf("LONG_MIN");
    else printf("%ld", v);
    printf(", rest \"%s\"%s\n", end, errno == ERANGE ? ", ERANGE" : "");
}

static int cmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

static int calls;
static int cmpcount(const void *a, const void *b) { calls++; return cmp(a, b); }

static int v[3000];

static void check(const char *what, int n)
{
    int k, ok = 1;
    long sum = 0;
    calls = 0;
    qsort(v, n, sizeof v[0], cmpcount);
    for (k = 0; k < n; k++) { sum += v[k]; if (k && v[k - 1] > v[k]) ok = 0; }
    printf("  %s: %s, sum %ld, %s\n", what, ok ? "sorted" : "NOT SORTED", sum,
           calls <= 20 * n ? "few comparisons" : "TOO MANY COMPARISONS");
}

static void onabrt(int sig) { printf("  handler ran for signal %d\n", sig); fflush(stdout); }

static void aborted(int handler)
{
    int st, pid;
    fflush(stdout);
    if ((pid = fork()) == 0) {
        if (handler) signal(SIGABRT, onabrt);
        abort();
        printf("  abort returned\n");
        exit(0);
    }
    wait(&st);
    printf("  %s\n", WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT ? "killed by SIGABRT" : "NOT killed by SIGABRT");
}

int main(void)
{
    char buf[64], cwd[1024], *p, *end;
    int k;
    unsigned long u;
    long v2;

    printf("strtol\n");
    tol("  -123xyz", 10);
    tol("0x1A", 0);
    tol("0x", 16);
    tol("0xg", 0);
    tol("077", 0);
    tol("z", 36);
    tol("", 10);
    tol("  +", 10);
    tol("99999999999999999999999", 10);
    tol("-99999999999999999999999", 10);
    errno = 0;
    sprintf(buf, "%ld", LONG_MAX);
    v2 = strtol(buf, &end, 10);
    printf("  LONG_MAX: %s\n", v2 == LONG_MAX && !errno && !*end ? "exact" : "WRONG");
    strcat(buf, "0");
    v2 = strtol(buf, 0, 10);
    printf("  10 * LONG_MAX: %s\n", v2 == LONG_MAX && errno == ERANGE ? "LONG_MAX, ERANGE" : "WRONG");
    errno = 0;
    sprintf(buf, "%ld", LONG_MIN);
    v2 = strtol(buf, 0, 10);
    printf("  LONG_MIN: %s\n", v2 == LONG_MIN && !errno ? "exact" : "WRONG");
    strcat(buf, "0");
    v2 = strtol(buf, 0, 10);
    printf("  10 * LONG_MIN: %s\n", v2 == LONG_MIN && errno == ERANGE ? "LONG_MIN, ERANGE" : "WRONG");
    errno = 0;
    sprintf(buf, "%lu", ULONG_MAX);
    u = strtoul(buf, 0, 10);
    printf("  ULONG_MAX: %s\n", u == ULONG_MAX && !errno ? "exact" : "WRONG");
    strcat(buf, "0");
    u = strtoul(buf, 0, 10);
    printf("  10 * ULONG_MAX: %s\n", u == ULONG_MAX && errno == ERANGE ? "ULONG_MAX, ERANGE" : "WRONG");
    errno = 0;
    u = strtoul("-1", 0, 10);
    printf("  strtoul(\"-1\"): %s\n", u == ULONG_MAX && !errno ? "ULONG_MAX" : "WRONG");
    printf("  atoi(\" 42abc\") = %d\n", atoi(" 42abc"));

    printf("qsort\n");
    for (k = 0; k < 3000; k++) v[k] = 7;
    check("3000 equal", 3000);
    for (k = 0; k < 3000; k++) v[k] = k;
    check("3000 sorted", 3000);
    for (k = 0; k < 3000; k++) v[k] = 3000 - k;
    check("3000 reversed", 3000);
    for (k = 0; k < 3000; k++) v[k] = k % 3;
    check("3000 of 0, 1, 2", 3000);
    for (k = 0; k < 3000; k++) v[k] = (k * 7919) % 3001;
    check("3000 mixed", 3000);
    check("1", 1);
    check("0", 0);

    printf("getcwd\n");
    p = getcwd(cwd, sizeof cwd);
    printf("  %s\n", p && cwd[0] == '/' ? "an absolute path" : "FAILED");
    errno = 0;
    p = getcwd(cwd, 1);
    printf("  size 1: %s\n", !p && errno == ERANGE ? "ERANGE" : "WRONG");

    printf("abort\n");
    aborted(0);
    aborted(1);
    return 0;
}
