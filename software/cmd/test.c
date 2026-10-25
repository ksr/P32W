/* test expr, [ expr ] -- conditions for the shell: exit 0 if true, 1 if
 * false, 2 for a syntax error (with a message).
 *   -e -f -d -c -b -p -s -r -w -x -u -g -k -h -L FILE    -t FD
 *   -z -n STRING    STRING    S1 = S2   S1 != S2   S1 < S2   S1 > S2
 *   N1 -eq -ne -lt -le -gt -ge N2    F1 -nt -ot -ef F2
 *   ! EXPR    EXPR -a EXPR    EXPR -o EXPR    ( EXPR )
 * As POSIX says, the number of arguments decides first (0 to 4 are read by
 * fixed rules, so test "$x" = y compares strings even when $x is -f);
 * longer expressions are parsed with the precedence ! over -a over -o. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char *prog;
static char **av;
static int ac, pos;

static void syntax(const char *what, const char *msg)
{
    if (what) fprintf(stderr, "%s: %s: %s\n", prog, what, msg);
    else fprintf(stderr, "%s: %s\n", prog, msg);
    exit(2);
}

static int unop(const char *s)          /* a unary operator: its letter, else 0 */
{
    if (s[0] == '-' && s[1] && !s[2] && strchr("efdcbpsrwxugkhLtzn", s[1])) return s[1];
    return 0;
}

static const char *const binops[] = { "=", "!=", "<", ">", "-eq", "-ne", "-lt", "-le", "-gt", "-ge",
                                      "-nt", "-ot", "-ef", 0 };

static int binop(const char *s)         /* a binary operator: 1 + its index, else 0 */
{
    int k;
    for (k = 0; binops[k]; k++) if (!strcmp(s, binops[k])) return k + 1;
    return 0;
}

static long integer(const char *s)      /* [blanks][+-]digits[blanks], in 32 bits */
{
    const char *p = s;
    unsigned long v = 0, lim;
    int neg = 0, any = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    lim = neg ? 2147483648UL : 2147483647UL;
    while (*p >= '0' && *p <= '9') {
        unsigned d = *p++ - '0';
        if (v > (lim - d) / 10) syntax(s, "integer out of range");
        v = v * 10 + d;
        any = 1;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (!any || *p) syntax(s, "integer expression expected");
    return neg ? -(long)v : (long)v;
}

static int unary(int op, const char *b)
{
    struct stat st;
    int ok;
    switch (op) {
    case 'z': return !*b;
    case 'n': return *b != 0;
    case 'r': return access(b, R_OK) == 0;
    case 'w': return access(b, W_OK) == 0;
    case 'x': return access(b, X_OK) == 0;
    case 't': return isatty((int)integer(b));
    case 'h': case 'L': return 0;      /* (no symbolic links) */
    }
    ok = stat(b, &st) == 0;
    switch (op) {
    case 'e': return ok;
    case 'f': return ok && S_ISREG(st.st_mode);
    case 'd': return ok && S_ISDIR(st.st_mode);
    case 'c': return ok && S_ISCHR(st.st_mode);
    case 'b': return ok && S_ISBLK(st.st_mode);
    case 'p': return ok && (st.st_mode & S_IFMT) == S_IFIFO;
    case 's': return ok && st.st_size > 0;
    case 'u': return ok && (st.st_mode & S_ISUID);
    case 'g': return ok && (st.st_mode & S_ISGID);
    case 'k': return ok && (st.st_mode & S_ISVTX);
    }
    return 0;
}

static int binary(const char *a, int op, const char *b)
{
    struct stat sa, sb;
    long x, y;
    int c;
    switch (op) {
    case 1: return !strcmp(a, b);
    case 2: return strcmp(a, b) != 0;
    case 3: return strcmp(a, b) < 0;
    case 4: return strcmp(a, b) > 0;
    case 11: case 12: case 13:
        if (stat(a, &sa) < 0) return op == 12 && stat(b, &sb) == 0;
        if (stat(b, &sb) < 0) return op == 11;
        if (op == 11) return sa.st_mtime > sb.st_mtime;
        if (op == 12) return sa.st_mtime < sb.st_mtime;
        return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    }
    x = integer(a);
    y = integer(b);
    c = (x > y) - (x < y);
    switch (op) {
    case 5: return c == 0;
    case 6: return c != 0;
    case 7: return c < 0;
    case 8: return c <= 0;
    case 9: return c > 0;
    }
    return c >= 0;
}

/* the general parser, for 5 or more arguments (and inside parentheses) */
static int e_or(void);

static int e_prim(void)
{
    char *a;
    int op;
    if (pos >= ac) syntax(0, "argument expected");
    a = av[pos];
    if (!strcmp(a, "!") && pos + 1 < ac) { pos++; return !e_prim(); }
    if (pos + 2 < ac && (op = binop(av[pos + 1])) != 0) {
        pos += 3;
        return binary(a, op, av[pos - 1]);
    }
    if (!strcmp(a, "(") && pos + 1 < ac) {
        int r;
        pos++;
        r = e_or();
        if (pos >= ac || strcmp(av[pos], ")")) syntax(0, "missing )");
        pos++;
        return r;
    }
    if ((op = unop(a)) != 0 && pos + 1 < ac) {
        pos += 2;
        return unary(op, av[pos - 1]);
    }
    pos++;
    return *a != 0;
}

static int e_and(void)
{
    int r = e_prim();
    while (pos < ac && !strcmp(av[pos], "-a")) { int s; pos++; s = e_prim(); r = r && s; }
    return r;
}

static int e_or(void)
{
    int r = e_and();
    while (pos < ac && !strcmp(av[pos], "-o")) { int s; pos++; s = e_and(); r = r || s; }
    return r;
}

/* POSIX: n arguments from a (n <= 4) */
static int fixed(char **a, int n)
{
    int op;
    switch (n) {
    case 0:
        return 0;
    case 1:
        return a[0][0] != 0;
    case 2:
        if (!strcmp(a[0], "!")) return !fixed(a + 1, 1);
        if ((op = unop(a[0])) != 0) return unary(op, a[1]);
        syntax(a[0], "unary operator expected");
    case 3:
        if ((op = binop(a[1])) != 0) return binary(a[0], op, a[2]);
        if (!strcmp(a[1], "-a")) return a[0][0] && a[2][0];
        if (!strcmp(a[1], "-o")) return a[0][0] || a[2][0];
        if (!strcmp(a[0], "!")) return !fixed(a + 1, 2);
        if (!strcmp(a[0], "(") && !strcmp(a[2], ")")) return fixed(a + 1, 1);
        syntax(a[1], "binary operator expected");
    case 4:
        if (!strcmp(a[0], "!")) return !fixed(a + 1, 3);
        if (!strcmp(a[0], "(") && !strcmp(a[3], ")")) return fixed(a + 1, 2);
    }
    {   /* the rest: parse */
        int r;
        av = a;
        ac = n;
        pos = 0;
        r = e_or();
        if (pos < ac) syntax(av[pos], "unexpected argument");
        return r;
    }
}

int main(int argc, char **argv)
{
    char *base = strrchr(argv[0], '/');
    prog = base ? base + 1 : argv[0];
    argv++;
    argc--;
    if (!strcmp(prog, "[")) {
        if (!argc || strcmp(argv[argc - 1], "]")) syntax(0, "missing ]");
        argc--;
    }
    return !fixed(argv, argc);
}
