/* ed -- the line editor (M4; os-design.md 7), a Unix V7 subset.
 *
 *   ed [-] [file]        (- : no byte counts)
 * Addresses: n  .  $  +n  -n  /re/  ?re?  a,b  ,  ;
 * Commands:  a i c (text until a line with only ".")  d  p  n  =  w [file]
 *            q  Q  e file  f [file]  r file  m addr  t addr  j
 *            s/re/replacement/[g][p]  (& is the match)  g/re/cmd  v/re/cmd
 * Patterns:  c  .  ^  $  [...]  [^...]  *  (as grep)
 * Errors print "?"; q, e or end of input on a changed buffer warns once
 * ("?"), as V7's does. Lines may be of any length. The exit status is 1 if
 * there were errors and the input is not a terminal (a script).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char **buf;
static int nlines, cap, cur, dirty, warned, quiet, errors;
static char fname[256], lastre[256];
static char *cmdline, *text;            /* growing line buffers */
static int cmdcap, textcap;

/* a line of any length from f into *lp (grown as needed), without its
 * newline; its length, or -1 at end of file. *nl: was there a newline */
static int getln(FILE *f, char **lp, int *capp, int *nl)
{
    int c, n = 0;
    *nl = 0;
    for (;;) {
        if (n + 2 > *capp) { *capp = *capp ? 2 * *capp : 128; *lp = realloc(*lp, *capp); }
        c = getc(f);
        if (c == EOF) break;
        if (c == '\n') { *nl = 1; break; }
        (*lp)[n++] = c;
    }
    (*lp)[n] = 0;
    return c == EOF && n == 0 ? -1 : n;
}
static void setname(const char *n)
{
    strncpy(fname, n, sizeof fname - 1);
    fname[sizeof fname - 1] = 0;
}

/* ---------------------------------------------------------------- patterns */
static int matchhere(const char *re, const char *s, const char **end);
static int one(const char **pre, int c)
{
    const char *re = *pre;
    if (*re == '.') { *pre = re + 1; return c != 0; }
    if (*re == '[') {
        int neg = 0, hit = 0;
        re++;
        if (*re == '^') { neg = 1; re++; }
        while (*re && *re != ']') {
            int lo = *re, hi = lo;
            if (re[1] == '-' && re[2] && re[2] != ']') { hi = re[2]; re += 2; }
            if (c >= lo && c <= hi) hit = 1;
            re++;
        }
        *pre = *re ? re + 1 : re;
        return c && hit != neg;
    }
    if (*re == '\\' && re[1]) re++;
    *pre = re + 1;
    return c && *re == c;
}
static const char *after(const char *re) { const char *r = re; one(&r, 'x'); return r; }
static int matchstar(const char *atom, const char *re, const char *s, const char **end)
{
    const char *t = s;
    while (*t) { const char *a = atom; if (!one(&a, *t)) break; t++; }
    for (; t >= s; t--)                 /* longest first */
        if (matchhere(re, t, end)) return 1;
    return 0;
}
static int matchhere(const char *re, const char *s, const char **end)
{
    const char *next;
    if (!*re) { *end = s; return 1; }
    next = after(re);
    if (*next == '*') return matchstar(re, next + 1, s, end);
    if (*re == '$' && !re[1]) { *end = s; return !*s; }
    { const char *r = re; if (*s && one(&r, *s)) return matchhere(r, s + 1, end); }
    return 0;
}
/* the first match of re in s: its start and end, or 0 */
static int match(const char *re, const char *s, const char **mb, const char **me)
{
    if (*re == '^') { *mb = s; return matchhere(re + 1, s, me); }
    do if (matchhere(re, s, me)) { *mb = s; return 1; } while (*s++);
    return 0;
}

/* ---------------------------------------------------------------- the buffer */
static void insert(int at, const char *text)       /* after line `at` (0: at the top) */
{
    if (nlines == cap) { cap = cap ? 2 * cap : 64; buf = realloc(buf, cap * sizeof *buf); }
    memmove(buf + at + 1, buf + at, (nlines - at) * sizeof *buf);
    buf[at] = strdup(text);
    nlines++;
    dirty = 1;
}
static void delete_(int a, int b)
{
    int k;
    for (k = a; k <= b; k++) free(buf[k - 1]);
    memmove(buf + a - 1, buf + b, (nlines - b) * sizeof *buf);
    nlines -= b - a + 1;
    dirty = 1;
}
static int readfile(const char *name, int at)
{
    FILE *f = fopen(name, "r");
    long bytes = 0;
    int n = 0, len, nl;
    if (!f) return -1;
    while ((len = getln(f, &text, &textcap, &nl)) >= 0) {
        bytes += len + nl;
        insert(at + n++, text);
    }
    fclose(f);
    if (!quiet) printf("%ld\n", bytes);
    return n;
}
static int writefile(const char *name, int a, int b, const char *mode)
{
    FILE *f = fopen(name, mode);
    long bytes = 0;
    int k;
    if (!f) return -1;
    for (k = a; k <= b && nlines; k++) { fputs(buf[k - 1], f); putc('\n', f); bytes += strlen(buf[k - 1]) + 1; }
    fclose(f);
    if (!quiet) printf("%ld\n", bytes);
    return 0;
}
static int input(int at)                /* text lines after `at`, until "." */
{
    int n = 0, nl;
    while (getln(stdin, &text, &textcap, &nl) >= 0) {
        if (!strcmp(text, ".")) break;
        insert(at + n++, text);
    }
    return n;
}

/* ---------------------------------------------------------------- addresses */
static const char *s;                   /* the command being parsed */

static int pattern(char delim, char *out)
{
    int k = 0;
    while (*s && *s != delim && k < 253) {
        if (*s == '\\' && s[1] == delim) s++;
        else if (*s == '\\' && s[1]) out[k++] = *s++;
        out[k++] = *s++;
    }
    out[k] = 0;
    if (*s == delim) s++;
    if (!k) { if (!lastre[0]) return -1; strcpy(out, lastre); }
    else strcpy(lastre, out);
    return 0;
}

static int address(int *ok)
{
    int a = cur, have = 0;
    *ok = 1;
    for (;;) {
        if (*s >= '0' && *s <= '9') { a = strtol(s, (char **)&s, 10); have = 1; }
        else if (*s == '.') { a = cur; s++; have = 1; }
        else if (*s == '$') { a = nlines; s++; have = 1; }
        else if (*s == '/' || *s == '?') {
            char re[256], d = *s++;
            int k, step = d == '/' ? 1 : -1;
            const char *mb, *me;
            if (pattern(d, re) < 0) { *ok = 0; return 0; }
            for (k = 1; k <= nlines; k++) {
                int l = ((a - 1 + step * k) % nlines + nlines) % nlines + 1;
                if (match(re, buf[l - 1], &mb, &me)) { a = l; break; }
            }
            if (k > nlines) { *ok = 0; return 0; }
            have = 1;
        } else if (*s == '+' || *s == '-') {
            int sign = *s++ == '+' ? 1 : -1, n = 1;
            if (*s >= '0' && *s <= '9') n = strtol(s, (char **)&s, 10);
            a += sign * n;
            have = 1;
        } else
            break;
    }
    if (!have) *ok = -1;
    return a;
}

/* ---------------------------------------------------------------- commands */
static char *out;                       /* the substituted line, grown as needed */
static int outn, outcap;
static void oput(int c)
{
    if (outn + 2 > outcap) { outcap = outcap ? 2 * outcap : 256; out = realloc(out, outcap); }
    out[outn++] = c;
}
static int substitute(int a, int b)
{
    char re[256], rep[256], d = *s++;
    int k, g = 0, p = 0, any = 0;
    if (!d || d == ' ' || pattern(d, re) < 0) return -1;
    /* the replacement: & is the match (code 1), \c a literal c (\& \\ \/) */
    for (k = 0; *s && *s != d && k < 255; s++) {
        if (*s == '\\' && s[1]) rep[k++] = *++s;
        else rep[k++] = *s == '&' ? 1 : *s;
    }
    rep[k] = 0;
    if (*s == d) s++;
    while (*s == 'g' || *s == 'p') { if (*s == 'g') g = 1; else p = 1; s++; }
    for (k = a; k <= b; k++) {
        const char *in = buf[k - 1], *mb, *me;
        int done = 0;
        outn = 0;
        while (*in && match(re, in, &mb, &me) && !(done && !g)) {
            const char *r;
            while (in < mb) oput(*in++);
            for (r = rep; *r; r++)
                if (*r == 1) { const char *m; for (m = mb; m < me; m++) oput(*m); }
                else oput(*r);
            in = me;
            done = 1;
            if (me == mb) {                     /* an empty match: step over a character */
                if (!*in) break;
                oput(*in++);
            }
            if (re[0] == '^') break;
        }
        if (!done) continue;
        while (*in) oput(*in++);
        oput(0);
        free(buf[k - 1]);
        buf[k - 1] = strdup(out);
        cur = k;
        any = dirty = 1;
    }
    if (!any) return -1;
    if (p) puts(buf[cur - 1]);
    return 0;
}

static int command(int global);

static int command(int global)
{
    int a, b, ok1, ok2, defaulted = 0;
    char c;
    s = cmdline;
    if (*s == ',' || *s == '%') { a = 1; b = nlines; s++; }
    else {
        a = address(&ok1);
        if (!ok1) return -1;
        b = a;
        if (*s == ',' || *s == ';') {
            if (*s == ';') {                    /* a;b: dot moves to a first */
                if (a < 1 || a > nlines) return -1;
                cur = a;
            }
            s++;
            b = address(&ok2);
            if (!ok2) return -1;
            if (ok2 < 0) b = nlines;
        } else if (ok1 < 0) { a = b = cur; defaulted = 1; }
    }
    c = *s ? *s++ : 0;
    if (c == 0) {                       /* an address alone: print that line */
        if (defaulted) { if (cur >= nlines) return -1; a = b = cur + 1; }
        c = 'p';
    }
    if (strchr("wgvW", c) && defaulted) { a = 1; b = nlines; }
    if (c != 'a' && c != 'i' && c != 'r' && c != 'e' && c != 'f' && c != 'q' && c != 'Q' && c != '=' && c != 'w' && c != 'W') {
        if (a < 1 || b > nlines || a > b) return -1;
    }
    if ((c == 'w' || c == 'W') && nlines && (a < 1 || b > nlines || a > b)) return -1;
    if ((c == 'r' || c == 'a' || c == '=') && (b < 0 || b > nlines)) return -1;
    switch (c) {
    case 'p': case 'n':
        for (; a <= b; a++) { if (c == 'n') printf("%d\t", a); puts(buf[a - 1]); }
        cur = b;
        return 0;
    case '=':
        printf("%d\n", defaulted ? nlines : b);
        return 0;
    case 'd':
        delete_(a, b);
        cur = a > nlines ? nlines : a;
        return 0;
    case 'a': case 'i': case 'c':
        if (global) return -1;
        if (c == 'c') { delete_(a, b); b = a - 1; }
        else if (c == 'i') b = b > 0 ? b - 1 : 0;
        if (b < 0 || b > nlines) return -1;
        cur = b + input(b);
        return 0;
    case 's':
        return substitute(a, b);
    case 'j':
        if (a == b) b = a + 1;
        if (b > nlines) return -1;
        {
            int len = 1, k;
            char *j;
            for (k = a; k <= b; k++) len += strlen(buf[k - 1]);
            j = malloc(len);
            j[0] = 0;
            for (k = a; k <= b; k++) strcat(j, buf[k - 1]);
            delete_(a + 1, b);
            free(buf[a - 1]);
            buf[a - 1] = j;
            cur = a;
            dirty = 1;
        }
        return 0;
    case 'm': case 't': {
        int ok, to = address(&ok), k, n = b - a + 1;
        char **tmp;
        if (!ok || ok < 0 || to < 0 || to > nlines || (c == 'm' && to >= a - 1 && to <= b)) return ok < 0 ? -1 : -1;
        tmp = malloc(n * sizeof *tmp);
        for (k = 0; k < n; k++) tmp[k] = strdup(buf[a - 1 + k]);
        if (c == 'm') { delete_(a, b); if (to > b) to -= n; }
        for (k = 0; k < n; k++) { insert(to + k, tmp[k]); free(tmp[k]); }
        free(tmp);
        cur = to + n;
        return 0;
    }
    case 'g': case 'v': {
        char re[256], *sub, d = *s++;
        int k, *marks;
        const char *mb, *me;
        if (global || !d || pattern(d, re) < 0) return -1;
        sub = strdup(*s ? s : "p");             /* (no longer than cmdline) */
        marks = calloc(nlines + 1, sizeof *marks);
        for (k = a; k <= b; k++) marks[k] = match(re, buf[k - 1], &mb, &me) == (c == 'g');
        for (k = 1; k <= nlines; k++)
            if (marks[k]) {
                int before = nlines;
                cur = k;
                strcpy(cmdline, sub);
                if (command(1) < 0) { free(marks); free(sub); return -1; }
                if (nlines < before) {          /* lines went: shift the marks */
                    memmove(marks + k, marks + k + (before - nlines), (nlines - k + 1) * sizeof *marks);
                    k--;
                }
            }
        free(marks);
        free(sub);
        return 0;
    }
    case 'w': case 'W':
        while (*s == ' ') s++;
        if (*s && !fname[0]) setname(s);       /* a name is remembered only if there is none */
        if (!*s && !fname[0]) return -1;
        if (writefile(*s ? s : fname, a, b, c == 'W' ? "a" : "w") < 0) return -1;
        if (a <= 1 && b >= nlines) dirty = 0;   /* (all of it written) */
        return 0;
    case 'r':
        while (*s == ' ') s++;
        if (!*s && !fname[0]) return -1;
        {
            int at = defaulted ? nlines : b, n = readfile(*s ? s : fname, at);
            if (n < 0) return -1;
            if (*s && !fname[0]) setname(s);
            if (n) cur = at + n;                /* dot: the last line read */
        }
        return 0;
    case 'e':
        if (dirty && !warned) { warned = 1; return -1; }
        while (*s == ' ') s++;
        if (*s) setname(s);
        if (nlines) delete_(1, nlines);
        if (readfile(fname, 0) < 0) return -1;
        cur = nlines;
        dirty = 0;
        return 0;
    case 'f':
        while (*s == ' ') s++;
        if (*s) setname(s);
        puts(fname);
        return 0;
    case 'q':
        if (dirty && !warned) { warned = 1; return -1; }
        /* fall through */
    case 'Q':
        exit(errors && !isatty(0));
    }
    return -1;
}

int main(int argc, char **argv)
{
    int nl;
    if (argc > 1 && !strcmp(argv[1], "-")) { quiet = 1; argc--; argv++; }
    if (argc > 1) {
        setname(argv[1]);
        if (readfile(fname, 0) < 0) printf("?%s\n", fname);
        dirty = 0;
        cur = nlines;
    }
    for (;;) {
        if (getln(stdin, &cmdline, &cmdcap, &nl) < 0) {
            /* end of input: a changed buffer is warned of once, as by q */
            if (dirty && !warned) { warned = 1; errors = 1; puts("?"); fflush(stdout); continue; }
            break;
        }
        if (cmdline[0] != 'q' && cmdline[0] != 'e') warned = 0;
        if (command(0) < 0) { puts("?"); errors = 1; }
        fflush(stdout);
    }
    return errors && !isatty(0);
}
