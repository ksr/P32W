/* head -- the first lines: head [-n count | -count] [file ...]
 * With several files each is preceded by a header ==> name <==. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int usage(void)
{
    fprintf(stderr, "usage: head [-n count | -count] [file ...]\n");
    return 1;
}
/* a count: digits only */
static int count(const char *s, long *n)
{
    long v = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 100000000) return 0;
        v = v * 10 + *s - '0';
    }
    *n = v;
    return 1;
}
int main(int argc, char **argv)
{
    long n = 10, k;
    int c, i = 1, st = 0, many, first = 1;
    FILE *f;
    if (i < argc && !strcmp(argv[i], "-n")) {
        if (i + 1 >= argc || !count(argv[i + 1], &n)) return usage();
        i += 2;
    } else if (i < argc && !strncmp(argv[i], "-n", 2)) {
        if (!count(argv[i] + 2, &n)) return usage();
        i++;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "--")) {
        if (!count(argv[i] + 1, &n)) return usage();
        i++;
    }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    many = argc - i > 1;
    if (i == argc) argv[--i] = "-";     /* no file: the standard input */
    for (; i < argc; i++) {
        f = strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin;
        if (!f) { fprintf(stderr, "head: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        if (many) { printf("%s==> %s <==\n", first ? "" : "\n", f == stdin ? "standard input" : argv[i]); first = 0; }
        for (k = n; k > 0 && (c = getc(f)) != EOF; ) { putchar(c); if (c == '\n') k--; }
        if (f != stdin) fclose(f);
    }
    return st;
}
