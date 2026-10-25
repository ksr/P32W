/* cmp -- compare two files: cmp [-l | -s] file1 file2
 * Prints the first difference (or every one, -l, or none, -s).
 * Status: 0 the same, 1 different, 2 trouble. - is the standard input. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
static int usage(void)
{
    fprintf(stderr, "usage: cmp [-l | -s] file1 file2\n");
    return 2;
}
static FILE *open1(const char *name)
{
    FILE *f = strcmp(name, "-") ? fopen(name, "r") : stdin;
    if (!f) fprintf(stderr, "cmp: %s: %s\n", name, strerror(errno));
    return f;
}
int main(int argc, char **argv)
{
    FILE *a, *b;
    long n = 1, line = 1;
    int c, d, i = 1, lflag = 0, sflag = 0, differ = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++)
            if (*p == 'l') lflag = 1; else if (*p == 's') sflag = 1;
            else { fprintf(stderr, "cmp: unknown option -%c\n", *p); return usage(); }
    }
    if (argc - i != 2 || (lflag && sflag)) return usage();
    if (!(a = open1(argv[i])) || !(b = open1(argv[i + 1]))) return 2;
    for (;; n++) {
        c = getc(a); d = getc(b);
        if (c != d) {
            if (c == EOF || d == EOF) {
                if (!sflag) printf("cmp: EOF on %s\n", c == EOF ? argv[i] : argv[i + 1]);
                return 1;
            }
            if (sflag) return 1;
            if (!lflag) { printf("%s %s differ: char %ld, line %ld\n", argv[i], argv[i + 1], n, line); return 1; }
            printf("%6ld %3o %3o\n", n, c, d);
            differ = 1;
        }
        if (c == EOF) return differ;
        if (c == '\n') line++;
    }
}
