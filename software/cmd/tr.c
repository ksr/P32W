/* tr -- translate, delete or squeeze characters:
 *   tr [-cCs] string1 string2     translate
 *   tr -s [-cC] string1           squeeze repeats of string1's characters
 *   tr -d [-cC] string1           delete string1's characters
 *   tr -ds [-cC] string1 string2  delete string1's, then squeeze string2's
 * The strings: characters, ranges a-z, escapes (\n \t \r \a \b \f \v \\
 * \NNN octal), classes [:alpha:] ..., [=c=], and [c*n] repeats ([c*] in
 * string2 fills it out to string1's length). A short string2 is padded
 * with its last character. Characters are bytes (0-255). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int cflag, dflag, sflag;

struct set {
    unsigned char *c;
    int n, cap;
    int star;                   /* where [c*] goes (string2), or -1 */
    int starc;
};

static void usage(void)
{
    fprintf(stderr, "usage: tr [-cCds] string1 [string2]\n");
    exit(1);
}

static void fail(const char *msg, const char *arg)
{
    fprintf(stderr, "tr: ");
    fprintf(stderr, msg, arg);
    fputc('\n', stderr);
    exit(1);
}

static void add(struct set *s, int c)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? 2 * s->cap : 64;
        if (!(s->c = realloc(s->c, s->cap))) fail("out of memory", "");
    }
    s->c[s->n++] = c;
}

/* one character at *pp, which may be an escape; *pp moves past it */
static int onechar(const char **pp)
{
    const char *p = *pp;
    int c, k;
    if (*p != '\\' || !p[1]) { *pp = p + 1; return (unsigned char)*p; }
    p++;
    if (*p >= '0' && *p <= '7') {
        c = 0;
        for (k = 0; k < 3 && *p >= '0' && *p <= '7'; k++) c = c * 8 + *p++ - '0';
        *pp = p;
        return c & 0377;
    }
    *pp = p + 1;
    switch (*p) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'a': return 7;
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    }
    return (unsigned char)*p;
}

static int inclass(const char *name, int c)
{
    if (!strcmp(name, "alpha")) return isalpha(c);
    if (!strcmp(name, "digit")) return isdigit(c);
    if (!strcmp(name, "alnum")) return isalnum(c);
    if (!strcmp(name, "space")) return isspace(c);
    if (!strcmp(name, "upper")) return isupper(c);
    if (!strcmp(name, "lower")) return islower(c);
    if (!strcmp(name, "punct")) return ispunct(c);
    if (!strcmp(name, "print")) return isprint(c);
    if (!strcmp(name, "graph")) return isgraph(c);
    if (!strcmp(name, "cntrl")) return iscntrl(c);
    if (!strcmp(name, "blank")) return c == ' ' || c == '\t';
    if (!strcmp(name, "xdigit")) return isxdigit(c);
    return -1;
}

/* [c*n] or [c*] at p (p[0] == '['): 1 and the parts, or 0 if it is not one */
static int repeat(const char *p, int *cp, int *np, const char **endp)
{
    const char *q = p + 1;
    int n = 0, base = 10;
    if (!*q) return 0;
    *cp = onechar(&q);
    if (*q != '*') return 0;
    q++;
    if (*q == '0') base = 8;
    while (*q >= '0' && *q <= '9') {
        if (base == 8 && *q > '7') return 0;
        n = n * base + *q++ - '0';
        if (n > 100000) fail("repeat count too large in %s", p);
    }
    if (*q != ']') return 0;
    *np = n;
    *endp = q + 1;
    return 1;
}

static void parse(const char *str, struct set *s, int second)
{
    const char *p = str;
    s->c = 0;
    s->n = s->cap = 0;
    s->star = -1;
    while (*p) {
        int c, d, n;
        const char *e;
        if (p[0] == '[' && p[1] == ':' && (e = strstr(p + 2, ":]")) != 0) {
            char name[16];
            if (e - p - 2 < 15) {
                strncpy(name, p + 2, e - p - 2);
                name[e - p - 2] = 0;
                if (inclass(name, 'a') >= 0) {
                    for (c = 0; c < 256; c++) if (inclass(name, c)) add(s, c);
                    p = e + 2;
                    continue;
                }
            }
            fail("invalid character class in %s", str);
        }
        if (p[0] == '[' && p[1] == '=' && p[2]) {
            e = p + 2;
            c = onechar(&e);
            if (e[0] == '=' && e[1] == ']') { add(s, c); p = e + 2; continue; }
        }
        if (p[0] == '[' && repeat(p, &c, &n, &e)) {
            if (n == 0) {
                if (!second) fail("[c*] may only be in string2: %s", str);
                if (s->star >= 0) fail("only one [c*] may be in string2: %s", str);
                s->star = s->n;
                s->starc = c;
            }
            while (n-- > 0) add(s, c);
            p = e;
            continue;
        }
        c = onechar(&p);
        if (p[0] == '-' && p[1]) {
            p++;
            d = onechar(&p);
            if (d < c) fail("range out of order in %s", str);
            for (; c <= d; c++) add(s, c);
            continue;
        }
        add(s, c);
    }
}

int main(int argc, char **argv)
{
    struct set s1, s2;
    static char in1[256], in2[256];
    static unsigned char map[256], comp[256];
    int i = 1, c, k, last = -1, nops;
    unsigned char *set1;
    int n1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++)
            if (*p == 'c' || *p == 'C') cflag = 1;
            else if (*p == 'd') dflag = 1;
            else if (*p == 's') sflag = 1;
            else { fprintf(stderr, "tr: unknown option -%c\n", *p); usage(); }
    }
    nops = argc - i;
    if (nops < 1 || nops > 2) usage();
    if (dflag && !sflag && nops != 1) { fprintf(stderr, "tr: -d takes only string1 (unless with -s)\n"); usage(); }
    if (dflag && sflag && nops != 2) { fprintf(stderr, "tr: -ds needs string2\n"); usage(); }
    if (!dflag && !sflag && nops != 2) usage();
    parse(argv[i], &s1, 0);
    if (nops == 2) parse(argv[i + 1], &s2, 1);
    else { s2.n = 0; s2.star = -1; }
    set1 = s1.c;
    n1 = s1.n;
    if (cflag) {                        /* the complement, in ascending order */
        for (k = 0; k < n1; k++) in1[set1[k]] = 1;
        n1 = 0;
        for (c = 0; c < 256; c++) if (!in1[c]) comp[n1++] = c;
        set1 = comp;
        memset(in1, 0, sizeof in1);
    }
    for (k = 0; k < n1; k++) in1[set1[k]] = 1;
    if (s2.star >= 0) {                 /* [c*]: fill string2 out to string1's length */
        int fill = n1 - s2.n, j;
        if (fill > 0) {
            for (j = 0; j < fill; j++) add(&s2, 0);
            memmove(s2.c + s2.star + fill, s2.c + s2.star, s2.n - fill - s2.star);
            for (j = 0; j < fill; j++) s2.c[s2.star + j] = s2.starc;
        }
    }
    for (c = 0; c < 256; c++) map[c] = c;
    if (!dflag && nops == 2) {          /* translating */
        if (s2.n == 0) fail("string2 must not be empty", "");
        for (k = 0; k < n1; k++) map[set1[k]] = s2.c[k < s2.n ? k : s2.n - 1];
    }
    /* the characters squeezed: string2's when there is one, else string1's */
    if (sflag) {
        if (nops == 2) for (k = 0; k < s2.n; k++) in2[s2.c[k]] = 1;
        else memcpy(in2, in1, sizeof in2);
    }
    while ((c = getchar()) != EOF) {
        if (dflag && in1[c]) continue;
        c = map[c];
        if (sflag && in2[c] && c == last) continue;
        last = c;
        putchar(c);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) { fprintf(stderr, "tr: write error\n"); return 1; }
    return 0;
}
