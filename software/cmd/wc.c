/* wc -- count lines, words and characters: wc [-lwc] [file ...] */
#include <stdio.h>
#include <string.h>
#include <errno.h>
static long tl, tw, tc;
static int show = 7;                    /* 4 lines, 2 words, 1 characters */
static void out(long l, long w, long c, const char *name)
{
    const char *sp = "";
    if (show & 4) { printf("%7ld", l); sp = " "; }
    if (show & 2) { printf("%s%7ld", sp, w); sp = " "; }
    if (show & 1) printf("%s%7ld", sp, c);
    printf("%s%s\n", name ? " " : "", name ? name : "");
}
static void count(FILE *f, const char *name)
{
    long l = 0, w = 0, c = 0;
    int ch, inword = 0;
    while ((ch = getc(f)) != EOF) {
        c++;
        if (ch == '\n') l++;
        if (ch == ' ' || ch == '\t' || ch == '\n') inword = 0;
        else if (!inword) { inword = 1; w++; }
    }
    out(l, w, c, name);
    tl += l; tw += w; tc += c;
}
int main(int argc, char **argv)
{
    int i = 1, st = 0, first, opts = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'l') opts |= 4; else if (*f == 'w') opts |= 2; else if (*f == 'c') opts |= 1;
            else {
                fprintf(stderr, "wc: unknown option -%c\nusage: wc [-lwc] [file ...]\n", *f);
                return 1;
            }
    }
    if (opts) show = opts;
    if (i == argc) { count(stdin, 0); return 0; }
    for (first = i; i < argc; i++) {
        FILE *f = strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin;
        if (!f) { fprintf(stderr, "wc: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        count(f, argv[i]);
        if (f != stdin) fclose(f);
    }
    if (argc - first > 1) out(tl, tw, tc, "total");
    return st;
}
