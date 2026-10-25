/* expr args -- evaluate an expression and print it; status 0 if the result
 * is neither empty nor 0 (as a number: 00 and -0 are 0 too), 1 if it is,
 * 2 for an error. From the lowest precedence to the highest:
 *   a | b    a & b    a = != < <= > >= b    a + - b    a * / % b    a : regex    ( ... )
 * Numbers are 32-bit integers (overflow is an error); = and the comparisons
 * compare strings unless both are numbers. a : regex matches a basic regular
 * expression, anchored at the start of a: . * [set] [^set] \{m,n\} $ (at the
 * end) \( \) and \1-\9; the result is the text of the first \( \), else the
 * number of characters matched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char **av;
static int ac, i;
static char *e_or(void);

static void die(const char *msg) { fprintf(stderr, "expr: %s\n", msg); exit(2); }
static void bad(void) { die("syntax error"); }

static int isnum(const char *s)
{
    if (*s == '-') s++;
    if (!*s) return 0;
    while (*s) if (*s < '0' || *s++ > '9') return 0;
    return 1;
}

static long val(const char *s)          /* a number's value; overflow is an error */
{
    unsigned long v = 0, lim;
    int neg = *s == '-';
    if (!isnum(s)) die("not a number");
    if (neg) s++;
    lim = neg ? 2147483648UL : 2147483647UL;
    for (; *s; s++) {
        unsigned d = *s - '0';
        if (v > (lim - d) / 10) die("integer overflow");
        v = v * 10 + d;
    }
    return neg ? -(long)v : (long)v;
}

static char *num(long v) { char *b = malloc(16); sprintf(b, "%ld", v); return b; }

static int truth(const char *s) { return *s && !(isnum(s) && val(s) == 0); }

/* ---------------------------------------------------------------- regular expressions */
enum { R_END, R_EOL, R_CHAR, R_ANY, R_SET, R_OPEN, R_CLOSE, R_REF };
struct node { int type, c, min, max; unsigned char set[32]; };
static struct node re[256];
static int nre, ngroups;
static const char *gs[10], *ge[10];

static void rebad(void) { die("bad regular expression"); }

static int renum(const char **pp)
{
    const char *p = *pp;
    int v = 0;
    if (*p < '0' || *p > '9') rebad();
    while (*p >= '0' && *p <= '9') { v = v * 10 + *p++ - '0'; if (v > 255) rebad(); }
    *pp = p;
    return v;
}

static void recompile(const char *p)
{
    int open[10], depth = 0, last = -1;
    nre = ngroups = 0;
    if (*p == '^') p++;                 /* (it is anchored anyway) */
    while (*p) {
        struct node *n;
        if (nre >= 250) rebad();
        n = &re[nre];
        memset(n, 0, sizeof *n);
        n->min = n->max = 1;
        if (*p == '*' && last >= 0) {   /* (a * first is an ordinary character) */
            re[last].min = 0;
            re[last].max = -1;
            last = -1;
            p++;
            continue;
        }
        if (p[0] == '\\' && p[1] == '{' && last >= 0) {
            p += 2;
            re[last].min = renum(&p);
            re[last].max = re[last].min;
            if (*p == ',') {
                p++;
                re[last].max = *p >= '0' && *p <= '9' ? renum(&p) : -1;
            }
            if (p[0] != '\\' || p[1] != '}') rebad();
            if (re[last].max >= 0 && re[last].max < re[last].min) rebad();
            p += 2;
            last = -1;
            continue;
        }
        last = nre;
        if (*p == '.') { n->type = R_ANY; p++; }
        else if (*p == '$' && !p[1]) { n->type = R_EOL; p++; last = -1; }
        else if (*p == '[') {
            int neg = 0, first = 1, k;
            p++;
            if (*p == '^') { neg = 1; p++; }
            while (*p && (first || *p != ']')) {
                int a = (unsigned char)*p++, b = a;
                if (*p == '-' && p[1] && p[1] != ']') { b = (unsigned char)p[1]; p += 2; }
                for (k = a; k <= b; k++) n->set[k >> 3] |= 1 << (k & 7);
                first = 0;
            }
            if (*p != ']') rebad();
            p++;
            if (neg) for (k = 0; k < 32; k++) n->set[k] = ~n->set[k];
            n->type = R_SET;
        } else if (*p == '\\') {
            p++;
            if (!*p) rebad();
            if (*p == '(') {
                if (ngroups >= 9) rebad();
                n->type = R_OPEN;
                n->c = ++ngroups;
                open[depth++] = ngroups;
                last = -1;
            } else if (*p == ')') {
                if (!depth) rebad();
                n->type = R_CLOSE;
                n->c = open[--depth];
                last = -1;
            } else if (*p >= '1' && *p <= '9') {
                n->type = R_REF;
                n->c = *p - '0';
                if (n->c > ngroups) rebad();
                last = -1;
            } else { n->type = R_CHAR; n->c = (unsigned char)*p; }
            p++;
        } else { n->type = R_CHAR; n->c = (unsigned char)*p++; }
        nre++;
    }
    if (depth) rebad();
    re[nre].type = R_END;
}

static int one(struct node *n, int c)   /* does one character match the node? */
{
    if (!c) return 0;
    if (n->type == R_ANY) return 1;
    if (n->type == R_CHAR) return c == n->c;
    return (n->set[c >> 3] >> (c & 7)) & 1;
}

static const char *rematch(int k, const char *s)    /* the end of a match of re[k..] at s, or 0 */
{
    struct node *n = &re[k];
    const char *r;
    int cnt;
    switch (n->type) {
    case R_END: return s;
    case R_EOL: return *s ? 0 : s;
    case R_OPEN: {
        const char *o = gs[n->c];
        gs[n->c] = s;
        if ((r = rematch(k + 1, s)) != 0) return r;
        gs[n->c] = o;
        return 0;
    }
    case R_CLOSE: {
        const char *o = ge[n->c];
        ge[n->c] = s;
        if ((r = rematch(k + 1, s)) != 0) return r;
        ge[n->c] = o;
        return 0;
    }
    case R_REF: {
        int len;
        if (!gs[n->c] || !ge[n->c] || ge[n->c] < gs[n->c]) return 0;
        len = ge[n->c] - gs[n->c];
        if (strncmp(s, gs[n->c], len)) return 0;
        return rematch(k + 1, s + len);
    }
    }
    for (cnt = 0; (n->max < 0 || cnt < n->max) && one(n, (unsigned char)s[cnt]); cnt++) ;
    for (; cnt >= n->min; cnt--)        /* (greedy: the longest first) */
        if ((r = rematch(k + 1, s + cnt)) != 0) return r;
    return 0;
}

static char *colon(const char *s, const char *pat)
{
    const char *end;
    recompile(pat);
    memset(gs, 0, sizeof gs);
    memset(ge, 0, sizeof ge);
    end = rematch(0, s);
    if (ngroups) {
        char *g;
        int len;
        if (!end) return "";
        len = ge[1] - gs[1];
        g = malloc(len + 1);
        memcpy(g, gs[1], len);
        g[len] = 0;
        return g;
    }
    return num(end ? (long)(end - s) : 0);
}

/* ---------------------------------------------------------------- the grammar */
static char *prim(void)
{
    char *a;
    if (i >= ac) bad();
    a = av[i++];
    if (!strcmp(a, "(") && i < ac) { char *r = e_or(); if (i >= ac || strcmp(av[i++], ")")) bad(); return r; }
    return a;
}

static char *match(void)
{
    char *a = prim();
    while (i < ac && !strcmp(av[i], ":")) { i++; a = colon(a, prim()); }
    return a;
}

static char *mul(void)
{
    char *a = match();
    while (i < ac && (!strcmp(av[i], "*") || !strcmp(av[i], "/") || !strcmp(av[i], "%"))) {
        char op = av[i++][0], *b = match();
        long x = val(a), y = val(b), r;
        if (op != '*' && y == 0) die("division by zero");
        if (op == '/' && x == -2147483647L - 1 && y == -1) die("integer overflow");
        if (op == '*') {
            r = (long)((unsigned long)x * (unsigned long)y);
            if (x && ((x == -1 && y == -2147483647L - 1) || r / x != y)) die("integer overflow");
        } else r = op == '/' ? x / y : y == -1 ? 0 : x % y;
        a = num(r);
    }
    return a;
}

static char *add(void)
{
    char *a = mul();
    while (i < ac && (!strcmp(av[i], "+") || !strcmp(av[i], "-"))) {
        char op = av[i++][0], *b = mul();
        long x = val(a), y = val(b), r;
        r = (long)(op == '+' ? (unsigned long)x + (unsigned long)y : (unsigned long)x - (unsigned long)y);
        if (op == '+' ? ((x >= 0) == (y >= 0) && (r >= 0) != (x >= 0))
                      : ((x >= 0) != (y >= 0) && (r >= 0) != (x >= 0))) die("integer overflow");
        a = num(r);
    }
    return a;
}

static char *cmp(void)
{
    char *a = add();
    static const char *ops[] = { "=", "!=", "<", "<=", ">", ">=", 0 };
    while (i < ac) {
        int k, c;
        char *b;
        for (k = 0; ops[k] && strcmp(av[i], ops[k]); k++) ;
        if (!ops[k]) break;
        i++;
        b = add();
        if (isnum(a) && isnum(b)) { long x = val(a), y = val(b); c = (x > y) - (x < y); }
        else c = strcmp(a, b);
        a = num(k == 0 ? c == 0 : k == 1 ? c != 0 : k == 2 ? c < 0 : k == 3 ? c <= 0 : k == 4 ? c > 0 : c >= 0);
    }
    return a;
}

static char *e_and(void)
{
    char *a = cmp();
    while (i < ac && !strcmp(av[i], "&")) { char *b; i++; b = cmp(); if (!truth(a) || !truth(b)) a = "0"; }
    return a;
}

static char *e_or(void)
{
    char *a = e_and();
    while (i < ac && !strcmp(av[i], "|")) {
        char *b;
        i++;
        b = e_and();
        if (!truth(a)) a = *b ? b : "0";
    }
    return a;
}

int main(int argc, char **argv)
{
    char *r;
    av = argv + 1;
    ac = argc - 1;
    if (!ac) bad();
    r = e_or();
    if (i < ac) bad();
    puts(r);
    return truth(r) ? 0 : 1;
}
