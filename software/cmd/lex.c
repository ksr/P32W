/* lex -- a generator of lexical analysers:
 *     lex [-t] [-n] [-v] [file ...]
 * Reads a specification (the files in turn, or the standard input) and
 * writes a scanner in C to lex.yy.c (with -t to the standard output). -v
 * reports the sizes of the automata and tables on the standard error; -n
 * (the default) does not.
 *
 * The specification is POSIX lex's (and flex's, in the parts most used):
 *     definitions
 *     %%
 *     rules
 *     %%
 *     user code
 * Definitions: %{ code %} and indented lines (copied to the scanner), name
 * definitions (NAME regex, used as {NAME}), %s / %start and %x start
 * conditions (inclusive, exclusive), %array / %pointer (yytext), %option
 * (noyywrap, main, yylineno, nodefault, case-insensitive, array, pointer;
 * others are ignored with a warning) and the table sizes %e %p %n %k %a %o
 * (ignored). Rules: a pattern at the start of a line, optionally after
 * <SC>, <SC1,SC2> or <*> (and <SC>{ ... } scopes), then the action: a
 * statement on the rest of the line, a { block } over as many lines as it
 * needs, or | (the next rule's action). %{ code %} and indented lines
 * before the first rule open yylex.
 *
 * This file reads the specification. Patterns become syntax trees and the
 * NFA in lexre.c; the DFA is made and minimised in lexdfa.c; lexout.c
 * compresses the tables and writes the scanner. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lex.h"

typedef struct { char *s; int len, cap; } Str;

Rule *rules;
int nrules;
static int rulecap;
char **scname;                  /* the start conditions; 0 is INITIAL */
char *scexcl;                   /* exclusive (%x) */
int nsc;
static int sccap;
int caseless, reject, arraymode, noyywrap, genmain, nodefault, vflag;
int lineno;
char *filename = "stdin";
static int tflag;

struct def { char *name, *text; };
static struct def *defs;
static int ndefs, defcap;

static char *src;               /* the whole specification */
static int srclen, srcpos, srccap;
static Str defcode, rulecode, usercode;

void lexerr(char *fmt, char *a)
{
    fflush(stdout);
    if (lineno) fprintf(stderr, "lex: %s:%d: ", filename, lineno);
    else fprintf(stderr, "lex: ");
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
    exit(1);
}

void lexwarn(char *fmt, char *a)
{
    if (lineno) fprintf(stderr, "lex: %s:%d: warning: ", filename, lineno);
    else fprintf(stderr, "lex: warning: ");
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
}

void *xalloc(int n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p) lexerr("out of memory", 0);
    memset(p, 0, n > 0 ? n : 1);
    return p;
}

void *xrealloc(void *p, int n)
{
    p = realloc(p, n > 0 ? n : 1);
    if (!p) lexerr("out of memory", 0);
    return p;
}

char *xstrdup(char *s)
{
    char *t = xalloc(strlen(s) + 1);
    strcpy(t, s);
    return t;
}

static void addc(Str *s, int c)
{
    if (s->len + 2 > s->cap) {
        s->cap = s->cap ? 2 * s->cap : 256;
        s->s = xrealloc(s->s, s->cap);
    }
    s->s[s->len++] = c;
    s->s[s->len] = 0;
}

static void adds(Str *s, char *t)
{
    while (*t) addc(s, *t++);
}

static void addline(Str *s, char *t)
{
    adds(s, t);
    addc(s, '\n');
}

/* the next line of the specification, without its newline; 0 at the end */
static char *nextline(void)
{
    char *l;
    if (srcpos >= srclen) return 0;
    l = src + srcpos;
    while (srcpos < srclen && src[srcpos] != '\n') srcpos++;
    src[srcpos++] = 0;
    lineno++;
    return l;
}

static int blank(char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    return *p == 0;
}

/* %{ ... %}: the lines up to %} */
static void copyblock(Str *s)
{
    char *l;
    int start = lineno;
    while ((l = nextline()) != 0) {
        if (l[0] == '%' && l[1] == '}') return;
        addline(s, l);
    }
    lineno = start;
    lexerr("%%{ without %%}", 0);
}

/* a comment at the start of a line in the definitions: copied */
static void copycomment(char *l)
{
    for (;;) {
        addline(&defcode, l);
        if (strstr(l, "*/")) return;
        if ((l = nextline()) == 0) lexerr("unterminated comment", 0);
    }
}

static int addsc(char *name, int excl)
{
    int i;
    for (i = 0; i < nsc; i++)
        if (!strcmp(scname[i], name)) {
            if (i) lexwarn("start condition %s declared twice", name);
            return i;
        }
    if (nsc == sccap) {
        sccap = sccap ? 2 * sccap : 16;
        scname = xrealloc(scname, sccap * sizeof(char *));
        scexcl = xrealloc(scexcl, sccap);
    }
    scname[nsc] = xstrdup(name);
    scexcl[nsc] = excl;
    return nsc++;
}

static int findsc(char *name)
{
    int i;
    for (i = 0; i < nsc; i++)
        if (!strcmp(scname[i], name)) return i;
    return -1;
}

static int isname(int c)
{
    return isalnum(c) || c == '_';
}

/* %s and %x: a list of names */
static void sclist(char *p, int excl)
{
    char name[128];
    int n;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) return;
        n = 0;
        if (!isalpha(*p) && *p != '_') lexerr("bad start condition name: %s", p);
        while (isname(*p) && n < 127) name[n++] = *p++;
        name[n] = 0;
        if (*p && *p != ' ' && *p != '\t' && *p != ',') lexerr("bad start condition name: %s", name);
        addsc(name, excl);
    }
}

static char *harmless[] = {
    "input", "noinput", "unput", "nounput", "yymore", "noyymore", "reject",
    "noreject", "interactive", "nointeractive", "always-interactive",
    "never-interactive", "batch", "8bit", "7bit", "warn", "nowarn",
    "stdinit", "nostdinit", "yylineno", "noyylineno", "yywrap", "nomain",
    "noyy_top_state", "noyy_push_state", "noyy_pop_state", "noyyget_text",
    "noyyget_leng", "noyyget_lineno", "noyyset_lineno", "noyyget_in",
    "noyyset_in", "noyyget_out", "noyyset_out", "noyyget_lval",
    "noyyset_lval", "noyyget_lloc", "noyyset_lloc", "noyyget_extra",
    "noyyset_extra", "noyyget_debug", "noyyset_debug", "nodebug",
    "verbose", "noverbose", 0
};

/* %option words */
static void options(char *p)
{
    char w[128];
    int n, i;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) return;
        n = 0;
        while (*p && *p != ' ' && *p != '\t') {
            if (*p == '"') {    /* name="value" */
                if (n < 127) w[n++] = *p;
                p++;
                while (*p && *p != '"') { if (n < 127) w[n++] = *p; p++; }
                if (*p) p++;
                continue;
            }
            if (n < 127) w[n++] = *p;
            p++;
        }
        w[n] = 0;
        if (!strcmp(w, "noyywrap")) noyywrap = 1;
        else if (!strcmp(w, "yywrap")) noyywrap = 0;
        else if (!strcmp(w, "main")) genmain = noyywrap = 1;
        else if (!strcmp(w, "array")) arraymode = 1;
        else if (!strcmp(w, "pointer")) arraymode = 0;
        else if (!strcmp(w, "nodefault")) nodefault = 1;
        else if (!strcmp(w, "default")) nodefault = 0;
        else if (!strcmp(w, "case-insensitive") || !strcmp(w, "caseless")) caseless = 1;
        else if (!strcmp(w, "case-sensitive")) caseless = 0;
        else {
            for (i = 0; harmless[i]; i++)
                if (!strcmp(w, harmless[i])) break;
            if (!harmless[i]) lexwarn("%%option %s ignored", w);
        }
    }
}

/* a line starting with % in the definitions */
static void directive(char *l)
{
    char w[32];
    int n = 0;
    char *p = l + 1;
    while (isalpha(*p) && n < 31) w[n++] = *p++;
    w[n] = 0;
    if (!strcmp(w, "s") || !strcmp(w, "S") || !strcmp(w, "start") || !strcmp(w, "Start"))
        sclist(p, 0);
    else if (!strcmp(w, "x") || !strcmp(w, "X"))
        sclist(p, 1);
    else if (!strcmp(w, "array"))
        arraymode = 1;
    else if (!strcmp(w, "pointer"))
        arraymode = 0;
    else if (!strcmp(w, "option"))
        options(p);
    else if (n == 1 && strchr("epnkaoc", w[0]))
        ;                       /* table sizes (and %c, C): nothing to do */
    else
        lexerr("unknown directive: %s", l);
}

/* NAME definition */
static void namedef(char *l)
{
    char *p = l, *e;
    if (!isalpha(*p) && *p != '_') lexerr("bad line in the definitions: %s", l);
    while (isname(*p) || *p == '-') p++;
    if (*p && *p != ' ' && *p != '\t') lexerr("bad name in a definition: %s", l);
    if (*p) *p++ = 0;
    while (*p == ' ' || *p == '\t') p++;
    e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    *e = 0;
    if (!*p) lexerr("the definition of %s is empty", l);
    if (finddef(l)) lexwarn("%s defined again", l);
    if (ndefs == defcap) {
        defcap = defcap ? 2 * defcap : 32;
        defs = xrealloc(defs, defcap * sizeof(struct def));
    }
    defs[ndefs].name = xstrdup(l);
    defs[ndefs].text = xstrdup(p);
    ndefs++;
}

char *finddef(char *name)
{
    int i;
    for (i = ndefs - 1; i >= 0; i--)
        if (!strcmp(defs[i].name, name)) return defs[i].text;
    return 0;
}

static void definitions(void)
{
    char *l;
    addsc("INITIAL", 0);
    while ((l = nextline()) != 0) {
        if (l[0] == '%' && l[1] == '%') return;
        if (l[0] == '%' && l[1] == '{') copyblock(&defcode);
        else if (l[0] == ' ' || l[0] == '\t') {
            if (!blank(l)) addline(&defcode, l);
        } else if (blank(l))
            ;
        else if (l[0] == '/' && l[1] == '*')
            copycomment(l);
        else if (l[0] == '%')
            directive(l);
        else
            namedef(l);
    }
    lineno = 0;
    lexerr("no %%%% line: the specification has no rules section", 0);
}

/* the end of the pattern that starts at p: the first blank outside quotes
 * and brackets */
static char *patend(char *p)
{
    while (*p && *p != ' ' && *p != '\t' && *p != '\r') {
        if (*p == '\\') p += p[1] ? 2 : 1;
        else if (*p == '"') {
            p++;
            while (*p && *p != '"') p += (*p == '\\' && p[1]) ? 2 : 1;
            if (*p) p++;
        } else if (*p == '[') {
            p++;
            if (*p == '^') p++;
            if (*p == ']') p++;
            while (*p && *p != ']') {
                if (p[0] == '[' && p[1] == ':') {
                    char *q = strstr(p + 2, ":]");
                    p = q ? q + 2 : p + 1;
                } else
                    p += (*p == '\\' && p[1]) ? 2 : 1;
            }
            if (*p) p++;
        } else
            p++;
    }
    return p;
}

/* an action in { }: up to the matching brace, over as many lines as it
 * takes (braces in strings, character constants and comments are not
 * counted) */
static char *braceaction(char *p)
{
    Str s;
    int depth = 0, state = 0, start = lineno;
    s.s = 0;
    s.len = s.cap = 0;
    for (;;) {
        for (; *p; p++) {
            addc(&s, *p);
            if (state == 0) {
                if (*p == '"') state = 1;
                else if (*p == '\'') state = 2;
                else if (*p == '/' && p[1] == '*') { addc(&s, '*'); p++; state = 3; }
                else if (*p == '/' && p[1] == '/') { adds(&s, p + 1); break; }
                else if (*p == '{') depth++;
                else if (*p == '}' && --depth == 0) {
                    if (!blank(p + 1)) lexwarn("text after the action's } ignored: %s", p + 1);
                    return s.s;
                }
            } else if (state == 3) {
                if (*p == '*' && p[1] == '/') { addc(&s, '/'); p++; state = 0; }
            } else {
                if (*p == '\\' && p[1]) { addc(&s, p[1]); p++; }
                else if (*p == (state == 1 ? '"' : '\'')) state = 0;
            }
        }
        addc(&s, '\n');
        if ((p = nextline()) == 0) {
            lineno = start;
            lexerr("unterminated action", 0);
        }
    }
}

/* does the action use REJECT? */
static int usesreject(char *a)
{
    char *p = a;
    while ((p = strstr(p, "REJECT")) != 0) {
        if ((p == a || !isname(p[-1])) && !isname(p[6])) return 1;
        p += 6;
    }
    return 0;
}

static Rule *newrule(void)
{
    Rule *r;
    if (nrules == rulecap) {
        rulecap = rulecap ? 2 * rulecap : 64;
        rules = xrealloc(rules, rulecap * sizeof(Rule));
    }
    r = &rules[nrules++];
    memset(r, 0, sizeof(Rule));
    r->line = lineno;
    return r;
}

/* <A,B> or <*>: the start conditions in sc (flags); returns after the > */
static char *scprefix(char *p, char *sc)
{
    char name[128];
    int n, i;
    p++;
    for (;;) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == '>') return p + 1;
        if (*p == '*') {
            for (i = 0; i < nsc; i++) sc[i] = 1;
            p++;
            continue;
        }
        n = 0;
        while (isname(*p) && n < 127) name[n++] = *p++;
        name[n] = 0;
        if (n == 0) lexerr("bad start condition list", 0);
        if ((i = findsc(name)) < 0) lexerr("undeclared start condition %s", name);
        sc[i] = 1;
    }
}

#define MAXSCOPE 16

static void rulessection(void)
{
    char *l, *p, *q, *pat, *a, *sc;
    char *scope[MAXSCOPE];
    int nscope = 0, i, c;
    Rule *r;
    while ((l = nextline()) != 0) {
        if (l[0] == '%' && l[1] == '%') {
            if (srcpos < srclen) {
                usercode.s = src + srcpos;
                usercode.len = srclen - srcpos;
            }
            return;
        }
        if (l[0] == '%' && l[1] == '{') {
            copyblock(&rulecode);
            continue;
        }
        p = l;
        while (*p == ' ' || *p == '\t') p++;
        if (blank(p)) continue;
        if (p != l && nscope == 0) {    /* indented: code */
            addline(&rulecode, l);
            continue;
        }
        if (nscope && p[0] == '}' && blank(p + 1)) {
            nscope--;
            continue;
        }
        if (nscope && p[0] == '/' && p[1] == '*') {     /* a comment in a scope */
            while (!strstr(p, "*/"))
                if ((p = nextline()) == 0) lexerr("unterminated comment", 0);
            continue;
        }
        if (!strncmp(p, "<<EOF>>", 7)) lexerr("<<EOF>> rules are not supported (use yywrap)", 0);
        sc = xalloc(nsc);
        if (*p == '<') {
            p = scprefix(p, sc);
            if (*p == '{' && blank(p + 1)) {     /* <SC>{ opens a scope */
                if (nscope == MAXSCOPE) lexerr("start condition scopes nested too deeply", 0);
                if (nscope)
                    for (i = 0; i < nsc; i++) sc[i] |= scope[nscope - 1][i];
                scope[nscope++] = sc;
                continue;
            }
        }
        if (nscope)
            for (i = 0; i < nsc; i++) sc[i] |= scope[nscope - 1][i];
        for (i = 0; i < nsc; i++)
            if (sc[i]) break;
        if (i == nsc)           /* no <...>: every inclusive condition */
            for (i = 0; i < nsc; i++) sc[i] = !scexcl[i];
        q = patend(p);
        if (q == p) lexerr("missing pattern", 0);
        c = *q;
        *q = 0;
        pat = xstrdup(p);
        *q = c;
        r = newrule();
        r->sc = sc;
        re_rule(r, pat);
        a = q;
        while (*a == ' ' || *a == '\t') a++;
        if (blank(a))
            r->action = ";";
        else if (a[0] == '|' && blank(a + 1))
            r->action = 0;
        else if (a[0] == '{')
            r->action = braceaction(a);
        else
            r->action = xstrdup(a);
        if (r->action && usesreject(r->action)) reject = 1;
    }
}

static void readspec(FILE *f, char *name)
{
    int c;
    while ((c = getc(f)) != EOF) {
        if (srclen + 3 > srccap) {
            srccap = srccap ? 2 * srccap : 4096;
            src = xrealloc(src, srccap);
        }
        src[srclen++] = c;
    }
    if (ferror(f)) lexerr("cannot read %s", name);
}

static void usage(void)
{
    fprintf(stderr, "usage: lex [-t] [-n] [-v] [file ...]\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int i, j, nfiles = 0;
    FILE *f, *out;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (j = 1; argv[i][j]; j++)
            switch (argv[i][j]) {
            case 't': tflag = 1; break;
            case 'n': vflag = 0; break;
            case 'v': vflag = 1; break;
            default: usage();
            }
    }
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-")) f = stdin;
        else if ((f = fopen(argv[i], "r")) == 0) lexerr("cannot open %s", argv[i]);
        if (nfiles++ == 0 && f != stdin) filename = argv[i];
        readspec(f, argv[i]);
        if (f != stdin) fclose(f);
    }
    if (nfiles == 0) readspec(stdin, "the standard input");
    if (srclen && src[srclen - 1] != '\n') {    /* a last line without a newline */
        src[srclen++] = '\n';
    }
    if (src) src[srclen] = 0;           /* (readspec left room for both) */
    definitions();
    rulessection();
    lineno = 0;
    re_nfa();
    dfa_build();
    for (i = 0; i < nrules; i++)
        if (!rules[i].used) {
            lineno = rules[i].line;
            lexwarn("this rule can never be matched", 0);
        }
    lineno = 0;
    if (tflag) out = stdout;
    else if ((out = fopen("lex.yy.c", "w")) == 0) lexerr("cannot create lex.yy.c", 0);
    out_scanner(out, defcode.s, rulecode.s, usercode.s);
    fflush(out);
    if (ferror(out)) lexerr("error writing the scanner", 0);
    if (out != stdout) fclose(out);
    if (vflag) {
        fprintf(stderr, "lex: %d rules, %d start conditions, %d NFA states\n", nrules, nsc, nnfa);
        fprintf(stderr, "lex: %d DFA states (%d before minimisation), %d character classes\n", ndfa, ndfa0, nclass);
        fprintf(stderr, "lex: %d transitions stored (of %d)\n", ncomb, ndfa * nclass);
    }
    return 0;
}
