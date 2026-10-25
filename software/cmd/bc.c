/* bc -- an arbitrary-precision calculator language (POSIX):
 *   bc [-l] [file ...]
 * Reads the files, then the standard input; each statement is run as soon
 * as it has been read. -l loads the math library (s c a l e j) and sets
 * scale to 20. This file reads the program (tokens, then a tree);
 * bcexec.c runs it, bcnum.c does the arithmetic (bc.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "bc.h"

/* the math library, in bc; each function works at scale + 10 and cuts */
static char libmath[] =
"define e(x) {\n"
"  auto s, m, k, r, t, i\n"
"  s = scale; m = 0\n"
"  if (x < 0) { m = 1; x = -x }\n"
"  scale = s + 10 + x / 2\n"
"  for (k = 0; x > .5; k++) x = x / 2\n"
"  r = 1; t = 1\n"
"  for (i = 1; 1; i++) { t = t * x / i; if (t == 0) break; r = r + t }\n"
"  for (; k > 0; k--) r = r * r\n"
"  if (m) r = 1 / r\n"
"  scale = s\n"
"  return (r / 1)\n"
"}\n"
"define l(x) {\n"
"  auto s, k, u, v, r, t, i, d\n"
"  if (x <= 0) { print \"l: argument must be positive\\n\"; return (0) }\n"
"  s = scale; scale = s + 10\n"
"  k = 2\n"
"  while (x >= 2) { x = sqrt(x); k = k * 2 }\n"
"  while (x <= .5) { x = sqrt(x); k = k * 2 }\n"
"  u = (x - 1) / (x + 1); v = u * u; r = u; t = u\n"
"  for (i = 3; 1; i = i + 2) { t = t * v; d = t / i; if (d == 0) break; r = r + d }\n"
"  r = k * r\n"
"  scale = s\n"
"  return (r / 1)\n"
"}\n"
"define a(x) {\n"
"  auto s, m, k, v, r, t, i, d\n"
"  s = scale; scale = s + 10\n"
"  m = 1\n"
"  if (x < 0) { m = -1; x = -x }\n"
"  for (k = 1; x > .2; k = k * 2) x = x / (1 + sqrt(1 + x * x))\n"
"  v = x * x; r = x; t = x\n"
"  for (i = 3; 1; i = i + 2) { t = -t * v; d = t / i; if (d == 0) break; r = r + d }\n"
"  r = m * k * r\n"
"  scale = s\n"
"  return (r / 1)\n"
"}\n"
"define s(x) {\n"
"  auto s, m, p, n, v, r, t, i\n"
"  s = scale; scale = s + 10\n"
"  m = 1\n"
"  if (x < 0) { m = -1; x = -x }\n"
"  p = 8 * a(1)\n"
"  scale = 0; n = x / p; scale = s + 10\n"
"  x = x - n * p\n"
"  if (x > p / 2) x = x - p\n"
"  v = x * x; r = x; t = x\n"
"  for (i = 1; 1; i = i + 2) { t = -t * v / ((i + 1) * (i + 2)); if (t == 0) break; r = r + t }\n"
"  r = m * r\n"
"  scale = s\n"
"  return (r / 1)\n"
"}\n"
"define c(x) {\n"
"  auto s, p, n, v, r, t, i\n"
"  s = scale; scale = s + 10\n"
"  if (x < 0) x = -x\n"
"  p = 8 * a(1)\n"
"  scale = 0; n = x / p; scale = s + 10\n"
"  x = x - n * p\n"
"  if (x > p / 2) x = x - p\n"
"  v = x * x; r = 1; t = 1\n"
"  for (i = 0; 1; i = i + 2) { t = -t * v / ((i + 1) * (i + 2)); if (t == 0) break; r = r + t }\n"
"  scale = s\n"
"  return (r / 1)\n"
"}\n"
"define j(n, x) {\n"
"  auto s, m, v, r, t, k, i\n"
"  s = scale\n"
"  scale = 0; n = n / 1; m = 1\n"
"  if (n < 0) { n = -n; if (n % 2 == 1) m = -1 }\n"
"  t = x; if (t < 0) t = -t\n"
"  scale = s + 10 + t / 2\n"
"  v = x * x / 4; t = 1\n"
"  for (i = 1; i <= n; i++) t = t * x / (2 * i)\n"
"  r = t\n"
"  for (k = 1; 1; k++) { t = -t * v / (k * (n + k)); if (t == 0) break; r = r + t }\n"
"  scale = s\n"
"  return (m * r / 1)\n"
"}\n";

/* ---- the input: the library, the files, the standard input ---- */

static char **files;
static int nfiles, nextf;
static char *src;                       /* reading a string (the library) */
static FILE *in;
static char *inname;
static int lineno, eof;
static int tokline;                     /* the line the token began on */
static jmp_buf top;
static int synerr;                      /* the error was in reading */
static int lflag;

static int getch(void)
{
    int c;
    if (src) return *src ? *src++ : EOF;
    if (!in || eof) return EOF;
    if ((c = getc(in)) == EOF) eof = 1;
    return c;
}

static void ungetch(int c)
{
    if (c == EOF) return;
    if (src) src--;
    else ungetc(c, in);
}

/* the next source; 0 when there are no more */
static int nextsource(void)
{
    if (src) { src = 0; scale = 20; }
    else if (in && in != stdin) fclose(in);
    else if (in == stdin) return 0;
    eof = 0;
    lineno = 1;
    if (nextf < nfiles) {
        inname = files[nextf++];
        if (!(in = fopen(inname, "r"))) {
            fprintf(stderr, "bc: cannot open %s\n", inname);
            exit(1);
        }
    } else {
        in = stdin;
        inname = 0;
    }
    return 1;
}

/* ---- errors ---- */

static void message(char *kind, char *fmt, va_list ap)
{
    fflush(stdout);
    fprintf(stderr, "bc: ");
    if (synerr && inname) fprintf(stderr, "%s:%d: ", inname, tokline);
    else if (synerr && !src) fprintf(stderr, "line %d: ", tokline);
    else if (synerr) fprintf(stderr, "library: line %d: ", tokline);
    fprintf(stderr, "%s", kind);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void bcerror(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    message("", fmt, ap);
    va_end(ap);
    unwind();
    longjmp(top, 1);
}

void bcwarn(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    message("warning: ", fmt, ap);
    va_end(ap);
}

/* ---- tokens ---- */

enum {
    T_EOF = 256, T_NL, T_NUM, T_STR, T_NAME, T_ASSIGN, T_REL, T_INC, T_DEC, T_AND, T_OR,
    T_AUTO, T_BREAK, T_CONTINUE, T_DEFINE, T_ELSE, T_FOR, T_HALT, T_IBASE, T_IF, T_LAST,
    T_LENGTH, T_OBASE, T_PRINT, T_QUIT, T_RETURN, T_SCALE, T_SQRT, T_WHILE
};
static char *keywords[] = {
    "auto", "break", "continue", "define", "else", "for", "halt", "ibase", "if", "last",
    "length", "obase", "print", "quit", "return", "scale", "sqrt", "while", 0
};

int nsyms;
char **symname;
static int tok, tokop;                  /* the token; its operator or symbol */
static char *text;                      /* a number's or string's or name's text */
static int ntext, captext;

static void synerror(char *fmt, ...)
{
    va_list ap;
    synerr = 1;
    va_start(ap, fmt);
    message("", fmt, ap);
    va_end(ap);
    unwind();
    longjmp(top, 1);
}

static void addc(int c)
{
    if (ntext + 1 >= captext) {
        captext = captext ? captext * 2 : 64;
        if (!(text = realloc(text, captext))) { fprintf(stderr, "bc: out of memory\n"); exit(1); }
    }
    text[ntext++] = c;
    text[ntext] = 0;
}

static char *xstrdup(char *s)
{
    char *t = malloc(strlen(s) + 1);
    if (!t) bcerror("out of memory");
    return strcpy(t, s);
}

static int intern(char *name)
{
    int i;
    for (i = 0; i < nsyms; i++)
        if (symname[i][0] == name[0] && !strcmp(symname[i], name)) return i;
    if (!(nsyms & 31)) {
        symname = realloc(symname, (nsyms + 32) * sizeof(char *));
        if (!symname) { fprintf(stderr, "bc: out of memory\n"); exit(1); }
    }
    symname[nsyms] = xstrdup(name);
    growsyms(nsyms + 1);
    return nsyms++;
}

static int isdig(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

static int next(void)
{
    int c, d, i;
again:
    ntext = 0;
    if (text) text[0] = 0;
    c = getch();
    while (c == ' ' || c == '\t' || c == '\r') c = getch();
    tokline = lineno;
    if (c == '\\') {
        if ((c = getch()) == '\n') { lineno++; goto again; }
        synerror("syntax error: \\ not at the end of a line");
    }
    if (c == '#') {
        while ((c = getch()) != '\n' && c != EOF) ;
        ungetch(c);
        goto again;
    }
    if (c == '/') {
        if ((d = getch()) == '*') {
            for (d = 0; (c = getch()) != EOF; d = c) {
                if (c == '\n') lineno++;
                if (d == '*' && c == '/') break;
            }
            if (c == EOF) synerror("unterminated comment");
            goto again;
        }
        if (d == '=') { tokop = '/'; return tok = T_ASSIGN; }
        ungetch(d);
        return tok = '/';
    }
    if (c == '\n') { lineno++; return tok = T_NL; }
    if (c == EOF) return tok = T_EOF;
    if (isdig(c) || c == '.') {
        d = 0;                                  /* a point seen */
        for (;;) {
            if (c == '\\') {
                if ((c = getch()) == '\n') { lineno++; c = getch(); continue; }
                ungetch(c);
                c = '\\';
                break;
            }
            if (c == '.' && !d) d = 1;
            else if (!isdig(c)) break;
            addc(c);
            c = getch();
        }
        ungetch(c);
        if (!strcmp(text, ".")) synerror("syntax error at .");
        return tok = T_NUM;
    }
    if (c >= 'a' && c <= 'z') {
        while ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            addc(c);
            c = getch();
        }
        ungetch(c);
        for (i = 0; keywords[i]; i++)
            if (keywords[i][0] == text[0] && !strcmp(text, keywords[i])) {
                if (T_AUTO + i == T_QUIT) { fflush(stdout); exit(0); }
                return tok = T_AUTO + i;
            }
        tokop = intern(text);
        return tok = T_NAME;
    }
    if (c == '"') {
        addc(0); ntext = 0;
        while ((c = getch()) != '"') {
            if (c == EOF) synerror("unterminated string");
            if (c == '\n') lineno++;
            addc(c);
        }
        return tok = T_STR;
    }
    d = getch();
    switch (c) {
    case '+': case '-':
        if (d == c) return tok = c == '+' ? T_INC : T_DEC;
        /* fall through */
    case '*': case '%': case '^':
        if (d == '=') { tokop = c; return tok = T_ASSIGN; }
        ungetch(d);
        return tok = c;
    case '=':
        if (d == '=') { tokop = 'E'; return tok = T_REL; }
        ungetch(d);
        tokop = '=';
        return tok = T_ASSIGN;
    case '<': case '>':
        if (d == '=') { tokop = c == '<' ? 'L' : 'G'; return tok = T_REL; }
        ungetch(d);
        tokop = c;
        return tok = T_REL;
    case '!':
        if (d == '=') { tokop = 'N'; return tok = T_REL; }
        ungetch(d);
        return tok = '!';
    case '&': case '|':
        if (d == c) return tok = c == '&' ? T_AND : T_OR;
        break;
    case '(': case ')': case '[': case ']': case '{': case '}': case ',': case ';':
        ungetch(d);
        return tok = c;
    }
    ungetch(d);
    if (c >= ' ' && c < 127) synerror("syntax error at %c", c);
    synerror("illegal character \\%03o", c & 255);
    return 0;
}

/* ---- the tree ---- */

static int nest;                        /* inside braces: newlines may be skipped */
static int loops, infunc;

static Node *node(int t, int op, Node *a, Node *b)
{
    Node *n = malloc(sizeof(Node));
    if (!n) bcerror("out of memory");
    memset(n, 0, sizeof *n);
    n->t = t;
    n->op = op;
    n->a = a;
    n->b = b;
    return n;
}

static void expect(int t, char *what)
{
    if (tok != t) {
        if (tok == T_EOF) synerror("syntax error: unexpected end of file");
        if (tok == T_NL) synerror("syntax error: %s expected at the end of the line", what);
        synerror("syntax error: %s expected", what);
    }
    next();
}

static void skipnl(void)
{
    while (tok == T_NL) next();
}

static Node *expr(void);

static int islv(Node *e)
{
    return e->t == E_VAR || e->t == E_ARR || e->t == E_SCALE || e->t == E_IBASE ||
           e->t == E_OBASE || e->t == E_LAST;
}

static Node *call(int sym)
{
    Node *n = node(E_CALL, sym, 0, 0), **tail = &n->a;
    next();
    if (tok != ')')
        for (;;) {
            *tail = expr();
            tail = &(*tail)->next;
            if (tok != ',') break;
            next();
        }
    expect(')', ")");
    return n;
}

static Node *primary(void)
{
    Node *e;
    int t = tok, sym = tokop;
    switch (tok) {
    case T_NUM:
        e = node(E_NUM, 0, 0, 0);
        e->s = xstrdup(text);
        next();
        break;
    case '(':
        next();
        e = expr();
        expect(')', ")");
        if (e->t == E_ASSIGN) e = node(E_PAREN, 0, e, 0);   /* (a = 1) is printed */
        return e;
    case T_INC: case T_DEC:
        next();
        e = primary();
        if (!islv(e)) synerror("syntax error: ++ or -- needs a variable");
        return node(E_PRE, t == T_INC ? '+' : '-', e, 0);
    case T_NAME:
        next();
        if (tok == '(') return call(sym);
        if (tok == '[') {
            next();
            if (tok == ']') { next(); return node(E_ARRARG, sym, 0, 0); }
            e = node(E_ARR, sym, expr(), 0);
            expect(']', "]");
        } else
            e = node(E_VAR, sym, 0, 0);
        break;
    case T_SCALE: case T_SQRT: case T_LENGTH:
        next();
        if (t == T_SCALE && tok != '(') { e = node(E_SCALE, 0, 0, 0); break; }
        expect('(', "(");
        e = node(t == T_SCALE ? E_FSCALE : t == T_SQRT ? E_SQRT : E_LENGTH, 0, expr(), 0);
        expect(')', ")");
        return e;
    case T_IBASE: next(); e = node(E_IBASE, 0, 0, 0); break;
    case T_OBASE: next(); e = node(E_OBASE, 0, 0, 0); break;
    case T_LAST: next(); e = node(E_LAST, 0, 0, 0); break;
    case T_EOF: synerror("syntax error: unexpected end of file");
    case T_NL: synerror("syntax error: unexpected end of the line");
    default:
        if (tok < 256) synerror("syntax error at %c", tok);
        synerror("syntax error: unexpected %s", tok >= T_AUTO ? keywords[tok - T_AUTO] :
                 tok == T_STR ? "string" : "operator");
    }
    if (tok == T_INC || tok == T_DEC) {
        e = node(E_POST, tok == T_INC ? '+' : '-', e, 0);
        next();
    }
    return e;
}

static Node *unary(void)
{
    if (tok == '-') {
        next();
        return node(E_NEG, 0, unary(), 0);
    }
    return primary();
}

static Node *power(void)
{
    Node *e = unary();
    if (tok == '^') {
        next();
        e = node(E_BIN, '^', e, power());
    }
    return e;
}

static Node *term(void)
{
    Node *e = power();
    int op;
    while (tok == '*' || tok == '/' || tok == '%') {
        op = tok;
        next();
        e = node(E_BIN, op, e, power());
    }
    return e;
}

static Node *sum(void)
{
    Node *e = term();
    int op;
    while (tok == '+' || tok == '-') {
        op = tok;
        next();
        e = node(E_BIN, op, e, term());
    }
    return e;
}

static Node *notexpr(void);

/* a = b binds tighter than a < b, as in every bc: a = 1 < 2 is (a = 1) < 2 */
static Node *assign(void)
{
    Node *e = sum();
    int op;
    if (tok == T_ASSIGN) {
        if (!islv(e)) synerror("syntax error: assignment to a non-variable");
        op = tokop;
        next();
        e = node(E_ASSIGN, op, e, tok == '!' ? notexpr() : assign());
    }
    return e;
}

static Node *rel(void)
{
    Node *e = assign();
    int op;
    while (tok == T_REL) {
        op = tokop;
        next();
        e = node(E_REL, op, e, assign());
    }
    return e;
}

static Node *notexpr(void)
{
    if (tok == '!') {
        next();
        return node(E_NOT, 0, notexpr(), 0);
    }
    return rel();
}

static Node *andexpr(void)
{
    Node *e = notexpr();
    while (tok == T_AND) {
        next();
        e = node(E_AND, 0, e, notexpr());
    }
    return e;
}

static Node *expr(void)
{
    Node *e = andexpr();
    while (tok == T_OR) {
        next();
        e = node(E_OR, 0, e, andexpr());
    }
    return e;
}

static Node *statement(void);

static int compound(Node *s)
{
    return s->t == S_IF || s->t == S_WHILE || s->t == S_FOR || s->t == S_BLOCK;
}

/* statements up to }, which is left as the token */
static Node *block(void)
{
    Node *b = node(S_BLOCK, 0, 0, 0), **tail = &b->a, *s;
    nest++;
    for (;;) {
        while (tok == T_NL || tok == ';') next();
        if (tok == '}') break;
        s = statement();
        *tail = s;
        tail = &s->next;
        if (tok == ';' || tok == T_NL || tok == '}') continue;
        if (!compound(s)) synerror("syntax error: ; or newline expected");
    }
    nest--;
    return b;
}

static Node *loopbody(void)
{
    Node *s;
    skipnl();
    loops++;
    s = statement();
    loops--;
    return s;
}

static Node *statement(void)
{
    Node *s, **tail;
    switch (tok) {
    case '{':
        next();
        s = block();
        next();
        return s;
    case T_STR:
        s = node(S_STR, 0, 0, 0);
        s->s = xstrdup(text);
        next();
        return s;
    case T_PRINT:
        s = node(S_PRINT, 0, 0, 0);
        tail = &s->a;
        do {
            next();
            if (tok == T_STR) {
                *tail = node(S_STR, 0, 0, 0);
                (*tail)->s = xstrdup(text);
                next();
            } else
                *tail = expr();
            tail = &(*tail)->next;
        } while (tok == ',');
        return s;
    case T_IF:
        next();
        expect('(', "(");
        s = node(S_IF, 0, expr(), 0);
        expect(')', ")");
        skipnl();
        s->b = statement();
        if (nest && (tok == T_NL || tok == ';')) {     /* (an else may follow) */
            if (tok == ';') next();
            skipnl();
        }
        if (tok == T_ELSE) {
            next();
            skipnl();
            s->c = statement();
        }
        return s;
    case T_WHILE:
        next();
        expect('(', "(");
        s = node(S_WHILE, 0, expr(), 0);
        expect(')', ")");
        s->b = loopbody();
        return s;
    case T_FOR:
        next();
        expect('(', "(");
        s = node(S_FOR, 0, 0, 0);
        if (tok != ';') s->a = expr();
        expect(';', ";");
        if (tok != ';') s->b = expr();
        expect(';', ";");
        if (tok != ')') s->c = expr();
        expect(')', ")");
        s->d = loopbody();
        return s;
    case T_BREAK: case T_CONTINUE:
        if (!loops) synerror("syntax error: %s outside a loop", keywords[tok - T_AUTO]);
        s = node(tok == T_BREAK ? S_BREAK : S_CONT, 0, 0, 0);
        next();
        return s;
    case T_RETURN:
        if (!infunc) synerror("syntax error: return outside a function");
        next();
        s = node(S_RETURN, 0, 0, 0);
        if (tok != T_NL && tok != ';' && tok != '}' && tok != T_EOF) s->a = expr();
        return s;
    case T_HALT:
        next();
        return node(S_HALT, 0, 0, 0);
    case ';': case T_NL: case '}': case T_EOF:
        return node(S_NULL, 0, 0, 0);
    }
    return node(S_EXPR, 0, expr(), 0);
}

/* name or name[] in a definition's list */
static int param(int *syms, char *arr, int n)
{
    int i;
    if (tok != T_NAME) synerror("syntax error: a name expected");
    for (i = 0; i < n; i++)
        if (syms[i] == tokop) synerror("syntax error: %s is listed twice", symname[tokop]);
    syms[n] = tokop;
    arr[n] = 0;
    next();
    if (tok == '[') {
        next();
        expect(']', "]");
        arr[n] = 1;
    }
    return n + 1;
}

static void define(void)
{
    int sym, syms[256], n = 0, np;
    char arr[256];
    Func *f;
    next();
    if (tok != T_NAME) synerror("syntax error: a function name expected");
    sym = tokop;
    next();
    expect('(', "(");
    if (tok != ')')
        for (;;) {
            if (n == 255) synerror("too many parameters");
            n = param(syms, arr, n);
            if (tok != ',') break;
            next();
        }
    expect(')', ")");
    np = n;
    skipnl();
    expect('{', "{");
    skipnl();
    if (tok == T_AUTO)
        for (next();;) {
            if (n == 255) synerror("too many autos");
            n = param(syms, arr, n);
            if (tok != ',') break;
            next();
        }
    infunc = 1;
    f = malloc(sizeof(Func));
    if (!f) bcerror("out of memory");
    f->body = block();
    infunc = 0;
    next();
    f->defined = 1;
    f->np = np;
    f->na = n - np;
    f->sym = malloc((n + 1) * sizeof(int));
    f->arr = malloc(n + 1);
    if (!f->sym || !f->arr) bcerror("out of memory");
    memcpy(f->sym, syms, n * sizeof(int));
    memcpy(f->arr, arr, n);
    if (funcs[sym]) {                   /* a new definition replaces the old */
        freenode(funcs[sym]->body);
        free(funcs[sym]->sym);
        free(funcs[sym]->arr);
        free(funcs[sym]);
    }
    funcs[sym] = f;
}

/* read and run statements until the sources run out */
static void run(void)
{
    static Node *s;
    if (setjmp(top)) {
        if (synerr && tok != T_NL && tok != T_EOF) {    /* the rest of the line */
            int c;
            while ((c = getch()) != '\n' && c != EOF) ;
            if (c == '\n') lineno++;
        }
        synerr = 0;
        nest = loops = infunc = 0;
        s = 0;
    }
    next();
    for (;;) {
        while (tok == T_NL || tok == ';') next();
        if (tok == T_EOF) {
            if (!nextsource()) return;
            next();
            continue;
        }
        if (tok == T_DEFINE) define();
        else {
            s = statement();
            if (tok != T_NL && tok != ';' && tok != T_EOF)
                synerror("syntax error: ; or newline expected");
            exec(s);
            freenode(s);
            s = 0;
        }
        if (tok != T_NL && tok != ';' && tok != T_EOF)
            synerror("syntax error: ; or newline expected");
    }
}

int main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++)
            if (*p == 'l') lflag = 1;
            else {
                fprintf(stderr, "bc: unknown option -%c\nusage: bc [-l] [file ...]\n", *p);
                return 2;
            }
    }
    files = argv + i;
    nfiles = argc - i;
    growsyms(32);
    if (lflag) {
        src = libmath;
        lineno = 1;
    } else
        nextsource();
    run();
    fflush(stdout);
    return 0;
}
