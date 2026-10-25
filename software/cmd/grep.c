/* grep -- print lines that match: grep [-vnci] [--] pattern [file ...]
 * Patterns: c . ^ $ [...] [^...] * (Kernighan & Pike's matcher, plus classes).
 * Lines may be of any length. Status: 0 a line was selected, 1 none was,
 * 2 a usage error or a file that could not be opened (even if others matched). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
static int vflag, nflag, cflag, iflag;
static int matchhere(const char *re, const char *s);
static int one(const char **pre, int c)
{
    const char *re = *pre;
    int ok;
    if (iflag) c = tolower(c);
    if (*re == '.') { *pre = re + 1; return c != 0; }
    if (*re == '[') {
        int neg = 0, hit = 0, first = 1;
        re++;
        if (*re == '^') { neg = 1; re++; }
        while (*re && (first || *re != ']')) {         /* a leading ] is a member */
            int lo = iflag ? tolower(*re) : *re, hi = lo;
            if (re[1] == '-' && re[2] && re[2] != ']') { hi = iflag ? tolower(re[2]) : re[2]; re += 2; }
            if (c >= lo && c <= hi) hit = 1;
            re++;
            first = 0;
        }
        *pre = *re ? re + 1 : re;
        return c && hit != neg;
    }
    if (*re == '\\' && re[1]) re++;
    ok = c && (iflag ? tolower(*re) : *re) == c;
    *pre = re + 1;
    return ok;
}
static const char *after(const char *re)
{
    const char *r = re;
    one(&r, 'x');
    return r;
}
static int matchstar(const char *atom, const char *re, const char *s)
{
    do {
        if (matchhere(re, s)) return 1;
        { const char *a = atom; if (!one(&a, *s)) return 0; }
    } while (*s++);
    return 0;
}
static int matchhere(const char *re, const char *s)
{
    const char *next;
    if (!*re) return 1;
    next = after(re);
    if (*next == '*') return matchstar(re, next + 1, s);
    if (*re == '$' && !re[1]) return !*s;
    { const char *r = re; if (*s && one(&r, *s)) return matchhere(r, s + 1); }
    return 0;
}
static int match(const char *re, const char *s)
{
    if (*re == '^') return matchhere(re + 1, s);
    do if (matchhere(re, s)) return 1; while (*s++);
    return 0;
}
/* the next line of f without its newline, in a buffer that grows; 0 at the end */
static char *getline_(FILE *f)
{
    static char *b;
    static int cap;
    int c, n = 0;
    if (!b) { cap = 256; b = malloc(cap); }
    while ((c = getc(f)) != EOF && c != '\n') {
        if (n + 1 >= cap) {
            cap *= 2;
            if (!(b = realloc(b, cap))) { fprintf(stderr, "grep: out of memory\n"); exit(2); }
        }
        b[n++] = c;
    }
    if (c == EOF && n == 0) return 0;
    b[n] = 0;
    return b;
}
static int grep(const char *re, FILE *f, const char *name, int many)
{
    char *line;
    long n = 0, hits = 0;
    while ((line = getline_(f)) != 0) {
        n++;
        if (match(re, line) != vflag) {
            hits++;
            if (cflag) continue;
            if (many) printf("%s:", name);
            if (nflag) printf("%ld:", n);
            puts(line);
        }
    }
    if (cflag) { if (many) printf("%s:", name); printf("%ld\n", hits); }
    return hits ? 0 : 1;
}
static int usage(void)
{
    fprintf(stderr, "usage: grep [-vnci] pattern [file ...]\n");
    return 2;
}
int main(int argc, char **argv)
{
    int i = 1, st = 1, pi, many;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            if (*f == 'v') vflag = 1; else if (*f == 'n') nflag = 1;
            else if (*f == 'c') cflag = 1; else if (*f == 'i') iflag = 1;
            else { fprintf(stderr, "grep: unknown option -%c\n", *f); return usage(); }
    }
    if (i >= argc) return usage();
    pi = i++;
    if (iflag) { char *p; for (p = argv[pi]; *p; p++) *p = tolower(*p); }
    if (i == argc) return grep(argv[pi], stdin, "", 0);
    many = argc - i > 1;
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { fprintf(stderr, "grep: %s: %s\n", argv[i], strerror(errno)); st = 2; continue; }
        if (grep(argv[pi], f, argv[i], many) == 0 && st != 2) st = 0;
        fclose(f);
    }
    return st;
}
