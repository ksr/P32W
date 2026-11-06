/* lex.c -- tokens (cc/clex.py) and the compiler's small helpers. */
#include <stdarg.h>
#include <ctype.h>
#include "cc.h"

int warnings = 1;

void *xmalloc(unsigned n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "cc: out of memory\n"); exit(2); }
    return p;
}

void *xrealloc(void *p, unsigned n)
{
    p = realloc(p, n ? n : 1);
    if (!p) { fprintf(stderr, "cc: out of memory\n"); exit(2); }
    return p;
}

char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

#define IHASH 4096
struct istr { char *s; struct istr *next; };
static struct istr *itab[IHASH];

char *intern(const char *s)
{
    unsigned h = 5381;
    const char *p = s;
    struct istr *e;
    while (*p) h = h * 33 + (unsigned char)*p++;
    h %= IHASH;
    for (e = itab[h]; e; e = e->next)
        if (!strcmp(e->s, s)) return e->s;
    e = xmalloc(sizeof *e);
    e->s = xstrdup(s);
    e->next = itab[h];
    itab[h] = e;
    return e->s;
}

char *fmt(const char *f, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsprintf(buf, f, ap);
    va_end(ap);
    return xstrdup(buf);
}

void vpush(struct vec *v, void *p)
{
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->a = xrealloc(v->a, sizeof(void *) * v->cap);
    }
    v->a[v->n++] = p;
}

void error(struct tok *t, const char *f, ...)
{
    va_list ap;
    if (t) fprintf(stderr, "%s:%d: error: ", t->file, t->line);
    else fprintf(stderr, "error: ");
    va_start(ap, f);
    vfprintf(stderr, f, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void warning(struct tok *t, const char *f, ...)
{
    va_list ap;
    if (!warnings) return;
    fprintf(stderr, "%s:%d: warning: ", t->file, t->line);
    va_start(ap, f);
    vfprintf(stderr, f, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Tokens come from slabs of their own, 146 to a 4 KB slab with no malloc
   header each. Each slab counts its live tokens and keeps its own free
   list; a slab with nothing live goes back to the heap, where the parser's
   node blocks (the same size) can use it. The parser frees a function's
   tokens statement by statement, so a long function's slabs come back as
   it goes (D33) */
#define SLABTOKS (4088 / sizeof(struct tok))
struct slab { struct tok *mem, *free; int live, prev, next; };
static struct slab *slabs;
static int nslab, slabcap, partial = -1, spare = -1;    /* partial: slabs with free slots; spare: unused entries */

static void unlink_partial(int k)
{
    struct slab *s = &slabs[k];
    if (s->prev >= 0) slabs[s->prev].next = s->next; else partial = s->next;
    if (s->next >= 0) slabs[s->next].prev = s->prev;
}

static void link_partial(int k)
{
    slabs[k].prev = -1;
    slabs[k].next = partial;
    if (partial >= 0) slabs[partial].prev = k;
    partial = k;
}

struct tok *tok_new(void)
{
    struct slab *s;
    struct tok *t;
    int k;
    if (partial < 0) {                  /* a new slab */
        unsigned i;
        if (spare >= 0) { k = spare; spare = slabs[k].next; }
        else {
            if (nslab == slabcap) {
                slabcap = slabcap ? 2 * slabcap : 64;
                slabs = xrealloc(slabs, slabcap * sizeof *slabs);
            }
            k = nslab++;
        }
        s = &slabs[k];
        s->mem = xmalloc(SLABTOKS * sizeof(struct tok));
        s->free = 0;
        s->live = 0;
        for (i = 0; i < SLABTOKS; i++) {
            *(struct tok **)&s->mem[i] = s->free;
            s->free = &s->mem[i];
        }
        link_partial(k);
    }
    k = partial;
    s = &slabs[k];
    t = s->free;
    s->free = *(struct tok **)t;
    s->live++;
    if (!s->free) unlink_partial(k);    /* (full) */
    memset(t, 0, sizeof *t);
    t->slab = k;
    return t;
}

void tok_free(struct tok *t)
{
    int k = t->slab;
    struct slab *s = &slabs[k];
    if (!s->free) link_partial(k);      /* (it was full) */
    *(struct tok **)t = s->free;
    s->free = t;
    if (--s->live == 0) {               /* all free: back to the heap */
        unlink_partial(k);
        free(s->mem);
        s->mem = 0;
        s->next = spare;
        spare = k;
    }
}

struct tok *tokcopy(struct tok *t)
{
    struct tok *c = tok_new();
    int k = c->slab;
    *c = *t;
    c->slab = k;                        /* (its own slab, not t's) */
    return c;
}

/* ---------------------------------------------------------------- the lexer */
static const char *const punct[] = {
    "...", "<<=", ">>=", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##",
    "[", "]", "(", ")", "{", "}", ".", "&", "*", "+", "-", "~", "!", "/", "%", "<", ">", "^", "|",
    "?", ":", ";", "=", ",", "#", 0 };

/* A punctuator is looked for among those that start with its first character
   (in the table's order: longest first), and needs no copying or hashing, for
   it is interned here */
#define NPUNCT 48
static short punct_first[256], punct_next[NPUNCT];
static char *punct_text[NPUNCT];

static void punct_init(void)
{
    int i;
    memset(punct_first, -1, sizeof punct_first);
    for (i = NPUNCT - 1; i >= 0; i--) {
        int c = (unsigned char)punct[i][0];
        punct_next[i] = punct_first[c];
        punct_first[c] = i;
        punct_text[i] = intern(punct[i]);
    }
}

static int esc(int c)
{
    switch (c) {
    case 'n': return 10; case 't': return 9; case 'r': return 13; case '0': return 0;
    case 'a': return 7; case 'b': return 8; case 'f': return 12; case 'v': return 11;
    case '\\': return 92; case '\'': return 39; case '"': return 34; case '?': return 63;
    case 'e': return 27;
    }
    return -1;
}

/* the bytes of a literal's body (between the quotes) into out; their count */
static int unescape(const char *b, int n, unsigned char *out, struct tok *t)
{
    int i = 0, k = 0;
    while (i < n) {
        int c = (unsigned char)b[i];
        if (c != '\\') { out[k++] = c; i++; continue; }
        i++;
        if (i >= n) error(t, "bad escape");
        c = b[i];
        if (c == 'x') {
            int v = 0, d = 0;
            i++;
            while (i < n && isxdigit((unsigned char)b[i])) {
                v = v * 16 + (isdigit((unsigned char)b[i]) ? b[i] - '0' : (tolower((unsigned char)b[i]) - 'a' + 10));
                i++;
                d++;
            }
            if (!d) error(t, "\\x needs hex digits");
            out[k++] = v & 0xFF;
        } else if (c >= '0' && c <= '7') {
            int v = 0, d = 0;
            while (d < 3 && i < n && b[i] >= '0' && b[i] <= '7') { v = v * 8 + (b[i] - '0'); i++; d++; }
            out[k++] = v & 0xFF;
        } else if (esc(c) >= 0) {
            out[k++] = esc(c);
            i++;
        } else
            error(t, "unknown escape \\%c", c);
    }
    return k;
}

static struct tok *newpunct(int k, const char *file, int line, int bol, int space)
{
    struct tok *t = tok_new();
    t->kind = T_PUNCT;
    t->text = punct_text[k];
    t->file = (char *)file;
    t->line = line;
    t->bol = bol;
    t->space = space;
    return t;
}

static struct tok *newtok(int kind, const char *text, int len, const char *file, int line, int bol, int space)
{
    struct tok *t = tok_new();
    char buf[512], *s = len < (int)sizeof buf ? buf : xmalloc(len + 1);
    memcpy(s, text, len);
    s[len] = 0;
    t->kind = kind;
    t->text = intern(s);
    if (s != buf) free(s);
    t->file = (char *)file;
    t->line = line;
    t->bol = bol;
    t->space = space;
    return t;
}

struct tok **tokenize(const char *src0, const char *fname, int *ntoks)
{
    struct vec v;
    char *src = xmalloc(strlen(src0) + 1), *d;
    const char *s;
    int line = 1, bol = 1, space = 0;
    char *file = intern(fname);
    if (!punct_text[0]) punct_init();
    /* backslash-newline out first */
    for (s = src0, d = src; *s; s++) {
        if (s[0] == '\\' && s[1] == '\n') { s++; continue; }
        *d++ = *s;
    }
    *d = 0;
    memset(&v, 0, sizeof v);
    s = src;
    while (*s) {
        const char *st = s;
        int c = (unsigned char)*s;
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r') {
            while (*s == ' ' || *s == '\t' || *s == '\f' || *s == '\v' || *s == '\r') s++;
            space = 1;
            continue;
        }
        if (c == '\n') { line++; bol = 1; space = 0; s++; continue; }
        if (c == '/' && s[1] == '/') {
            while (*s && *s != '\n') s++;
            space = 1;
            continue;
        }
        if (c == '/' && s[1] == '*') {
            const char *e = strstr(s + 2, "*/");
            if (!e) { struct tok tt; memset(&tt, 0, sizeof tt); tt.file = file; tt.line = line; error(&tt, "unterminated comment"); }
            for (; s < e; s++) if (*s == '\n') { line++; bol = 1; }
            s = e + 2;
            space = 1;
            continue;
        }
        if (isdigit(c) || (c == '.' && isdigit((unsigned char)s[1]))) {    /* a pp-number */
            if (c == '.') s++;
            s++;
            for (;;) {
                if ((*s == 'e' || *s == 'E' || *s == 'p' || *s == 'P') && (s[1] == '+' || s[1] == '-')) s += 2;
                else if (isalnum((unsigned char)*s) || *s == '_' || *s == '.') s++;
                else break;
            }
            VPUSH(&v, newtok(T_NUM, st, s - st, file, line, bol, space));
            bol = space = 0;
            continue;
        }
        if (isalpha(c) || c == '_') {
            while (isalnum((unsigned char)*s) || *s == '_') s++;
            VPUSH(&v, newtok(T_ID, st, s - st, file, line, bol, space));
            bol = space = 0;
            continue;
        }
        if (c == '\'' || c == '"') {               /* (an unterminated one falls through to punct) */
            const char *p = s + 1;
            int ok = 0, n = 0;
            while (*p && *p != '\n') {
                if (*p == '\\' && p[1] && p[1] != '\n') { p += 2; n++; continue; }
                if (*p == '\\') break;
                if (*p == c) { ok = c == '"' || n > 0; break; }
                p++;
                n++;
            }
            if (ok) {
                struct tok *t;
                unsigned char tmp[4096], *buf = (p - s) < (int)sizeof tmp ? tmp : xmalloc(p - s);
                int k;
                s = p + 1;
                t = newtok(c == '"' ? T_STR : T_CHAR, st, s - st, file, line, bol, space);
                k = unescape(st + 1, s - st - 2, buf, t);
                if (c == '"') {
                    t->sval = xmalloc(k + 1);
                    memcpy(t->sval, buf, k);
                    t->slen = k;
                } else {
                    if (k != 1) error(t, "character constant must be one character");
                    t->cval = buf[0] > 127 ? buf[0] - 256 : buf[0];
                }
                if (buf != tmp) free(buf);
                vpush(&v, t);
                bol = space = 0;
                continue;
            }
        }
        {
            int i;
            for (i = punct_first[(unsigned char)s[0]]; i >= 0; i = punct_next[i])
                if (!punct[i][1] || !strncmp(s, punct[i], strlen(punct[i]))) break;
            if (i < 0) {
                struct tok tt;
                memset(&tt, 0, sizeof tt);
                tt.file = file;
                tt.line = line;
                error(&tt, "stray character '%c'", c);
            }
            VPUSH(&v, newpunct(i, file, line, bol, space));
            s += punct[i][1] ? strlen(punct[i]) : 1;
            bol = space = 0;
        }
    }
    vpush(&v, newtok(T_EOF, "", 0, file, line, 1, 0));
    free(src);
    *ntoks = v.n;
    return (struct tok **)v.a;
}

/* an integer constant, in 64 bits (*over: it does not fit them): 0, or -1
   if it is not one; *lng counts the l's (2: ll) */
int parse_int64(struct tok *t, unsigned long long *v, int *uns, int *lng, int *nondec, int *over)
{
    char s[256];
    int i, n = strlen(t->text), base = 10, end;
    unsigned long long val = 0;
    unsigned lo = 0;
    *over = 0;
    if (n >= (int)sizeof s) return -1;
    for (i = 0; i <= n; i++) s[i] = tolower((unsigned char)t->text[i]);
    i = 0;
    if (s[0] == '0' && s[1] == 'x') {
        base = 16;
        i = 2;
        if (!isxdigit((unsigned char)s[2])) return -1;
    } else if (s[0] == '0')
        base = 8;
    for (; s[i] && (base == 16 ? isxdigit((unsigned char)s[i]) : base == 8 ? (s[i] >= '0' && s[i] <= '7') : isdigit((unsigned char)s[i])); i++) {
        int d = isdigit((unsigned char)s[i]) ? s[i] - '0' : s[i] - 'a' + 10;
        if (lo < 0x0FFFFFFFu) {         /* (it cannot overflow yet, and 64 bits are software) */
            lo = lo * base + d;
            val = lo;
            continue;
        }
        if (val > (0xFFFFFFFFFFFFFFFFull - d) / base) *over = 1;
        val = val * base + d;
    }
    end = i;
    if (base == 10 && (s[0] < '1' || s[0] > '9')) return -1;
    *uns = *lng = 0;
    if (!strcmp(s + end, "u")) *uns = 1;
    else if (!strcmp(s + end, "l")) *lng = 1;
    else if (!strcmp(s + end, "ll")) *lng = 2;
    else if (!strcmp(s + end, "ul") || !strcmp(s + end, "lu")) *uns = *lng = 1;
    else if (!strcmp(s + end, "ull") || !strcmp(s + end, "llu")) { *uns = 1; *lng = 2; }
    else if (s[end]) return -1;
    *v = val;
    *nondec = base != 10;
    return 0;
}

/* the same in 32 bits (#if) */
int parse_int(struct tok *t, unsigned *v, int *uns, int *lng, int *nondec)
{
    unsigned long long w;
    int over, r = parse_int64(t, &w, uns, lng, nondec, &over);
    *v = (unsigned)w;
    return r;
}
