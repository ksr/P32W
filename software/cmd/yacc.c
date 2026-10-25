/* yacc -- yet another compiler-compiler: an LALR(1) parser generator:
 *     yacc [-dltv] [-b prefix] [-p symprefix] grammar
 * POSIX yacc. Reads a grammar (declarations, %%, rules with C actions, and
 * optionally %% and C code) and writes y.tab.c, a table-driven parser
 * yyparse() that calls yylex() for tokens; -d also writes y.tab.h (the
 * token numbers, YYSTYPE and yylval), -v y.output (the states and their
 * actions, conflicts), -t compiles in the debugging trace (yydebug = 1),
 * -l leaves out the #line lines, -b changes y to prefix in the file names
 * and -p yy to symprefix in the parser's external names. Conflicts are
 * resolved as POSIX says (precedence and associativity, else shift over
 * reduce and the earlier rule) and counted on the standard error. This file
 * reads the grammar; ylr.c builds the automaton, yout.c writes the tables
 * and the parser (yacc.h has the overview). Status: 0, or 1 for an error in
 * the grammar, a usage error or a file that cannot be read or written. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "yacc.h"

char *infile;
int lflag, tflag, vflag, dflag;
char *fileprefix = "y", *symprefix = "yy";
struct sym **symtab;
int nsyms, ntokens, nnterms;
struct rule *rules;
int nrules;
int *ritem, nitems;
char **tags;
int ntags;
char *prologue, *unionbody, *epilogue;
int haveunion, unionline, epiline;
int start;

/* a growing string */
typedef struct { char *s; int len, cap; } Buf;

static void bputc(Buf *b, int c)
{
    if (b->len + 2 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->s = xrealloc(b->s, b->cap);
    }
    b->s[b->len++] = c;
    b->s[b->len] = 0;
}

static void bputs(Buf *b, char *s)
{
    while (*s) bputc(b, *s++);
}

static void bputn(Buf *b, char *s, int n)
{
    while (n-- > 0) bputc(b, *s++);
}

static Buf pro;                         /* the prologue being built */
static char *src;                       /* the whole grammar */
static char *p;                         /* where the reader is */
static int lineno = 1;
static int nerrors;

void *xmalloc(int n)
{
    void *q = malloc(n > 0 ? n : 1);
    if (!q) fatal("out of memory", 0);
    memset(q, 0, n > 0 ? n : 1);
    return q;
}

void *xrealloc(void *q, int n)
{
    q = realloc(q, n > 0 ? n : 1);
    if (!q) fatal("out of memory", 0);
    return q;
}

char *xstrdup(char *s)
{
    char *q = xmalloc(strlen(s) + 1);
    strcpy(q, s);
    return q;
}

/* an error in the grammar at the current line: stop */
void fatal(char *fmt, char *a)
{
    fflush(stdout);
    if (infile && lineno > 0) fprintf(stderr, "yacc: %s:%d: ", infile, lineno);
    else fprintf(stderr, "yacc: ");
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
    exit(1);
}

/* an error that does not stop the reading (yacc stops at the end of it) */
static void error(char *fmt, char *a)
{
    fprintf(stderr, "yacc: %s:%d: ", infile, lineno);
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
    nerrors++;
}

void warn(char *fmt, char *a)
{
    fprintf(stderr, "yacc: warning: ");
    fprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
}

/* ---- the symbol table ---- */

#define NHASH 211
static struct sym *hashtab[NHASH];
static struct sym **allsyms;            /* in the order first seen */
static int nall, allcap;
static struct sym *errsym, *endsym, *undefsym, *acceptsym;
static struct sym *startsym;            /* %start's, or the first rule's left side */

static struct sym *lookup(char *name)
{
    unsigned h = 0;
    char *s;
    struct sym *sp;
    for (s = name; *s; s++) h = h * 31 + (unsigned char)*s;
    h %= NHASH;
    for (sp = hashtab[h]; sp; sp = sp->next)
        if (!strcmp(sp->name, name)) return sp;
    sp = xmalloc(sizeof *sp);
    sp->name = xstrdup(name);
    sp->value = -1;
    sp->line = lineno;
    sp->next = hashtab[h];
    hashtab[h] = sp;
    if (nall == allcap) {
        allcap = allcap ? allcap * 2 : 64;
        allsyms = xrealloc(allsyms, allcap * sizeof *allsyms);
    }
    allsyms[nall++] = sp;
    return sp;
}

static int tagindex(char *t)
{
    int i;
    for (i = 0; i < ntags; i++)
        if (!strcmp(tags[i], t)) return i + 1;
    tags = xrealloc(tags, (ntags + 1) * sizeof *tags);
    tags[ntags++] = xstrdup(t);
    return ntags;
}

/* ---- the grammar's tokens ---- */

enum {
    T_EOF = 256, T_IDENT, T_CIDENT, T_NUMBER, T_CHAR, T_TAG, T_MARK, T_LCURL,
    T_TOKEN, T_LEFT, T_RIGHT, T_NONASSOC, T_TYPE, T_START, T_UNION, T_PREC
};

static char tok[256];                   /* an identifier, tag or character's name */
static int tokval;                      /* a number or character's value */

/* skip blanks, newlines and comments */
static void skipws(void)
{
    for (;;) {
        if (*p == '\n') { lineno++; p++; }
        else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v') p++;
        else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') lineno++;
                p++;
            }
            if (!*p) fatal("unterminated comment", 0);
            p += 2;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
        } else return;
    }
}

/* the name of the character token c: 'c', or an escape */
static void charname(int c, char *out)
{
    switch (c) {
    case '\n': strcpy(out, "'\\n'"); return;
    case '\t': strcpy(out, "'\\t'"); return;
    case '\r': strcpy(out, "'\\r'"); return;
    case '\b': strcpy(out, "'\\b'"); return;
    case '\f': strcpy(out, "'\\f'"); return;
    case '\v': strcpy(out, "'\\v'"); return;
    case '\\': strcpy(out, "'\\\\'"); return;
    case '\'': strcpy(out, "'\\''"); return;
    }
    if (c >= 32 && c < 127) sprintf(out, "'%c'", c);
    else sprintf(out, "'\\%03o'", c);
}

/* a character constant after its opening quote */
static int charlit(void)
{
    int c = (unsigned char)*p++, n;
    if (c == '\n' || c == 0 || c == '\'') fatal("bad character constant", 0);
    if (c == '\\') {
        c = (unsigned char)*p++;
        switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'v': c = '\v'; break;
        case 'a': c = 7; break;
        case '\\': case '\'': case '"': case '?': break;
        case 'x':
            for (c = 0, n = 0; isxdigit((unsigned char)*p) && n < 2; n++, p++)
                c = c * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (*p | 32) - 'a' + 10);
            if (!n) fatal("bad character constant", 0);
            break;
        default:
            if (c < '0' || c > '7') fatal("bad character constant", 0);
            c -= '0';
            for (n = 1; n < 3 && *p >= '0' && *p <= '7'; n++) c = c * 8 + *p++ - '0';
            if (c > 255) fatal("bad character constant", 0);
        }
    }
    if (*p++ != '\'') fatal("bad character constant (one character between quotes)", 0);
    if (c == 0) fatal("'\\0' cannot be a token", 0);
    return c;
}

static int isidch(int c)
{
    return isalnum(c) || c == '_' || c == '.';
}

/* the next token of the grammar */
static int gettok(void)
{
    int n;
    char *q;
    static struct { char *name; int t; } kw[] = {
        { "token", T_TOKEN }, { "term", T_TOKEN }, { "left", T_LEFT },
        { "right", T_RIGHT }, { "nonassoc", T_NONASSOC }, { "binary", T_NONASSOC },
        { "type", T_TYPE }, { "start", T_START }, { "union", T_UNION },
        { "prec", T_PREC }, { 0, 0 }
    };
    skipws();
    if (!*p) return T_EOF;
    if (isalpha((unsigned char)*p) || *p == '_') {
        for (n = 0; isidch((unsigned char)*p); p++)
            if (n < (int)sizeof tok - 1) tok[n++] = *p;
        tok[n] = 0;
        /* an identifier followed by a colon starts a rule */
        q = p;
        n = lineno;
        skipws();
        if (*p == ':') { p++; return T_CIDENT; }
        p = q;
        lineno = n;
        return T_IDENT;
    }
    if (isdigit((unsigned char)*p)) {
        for (tokval = 0; isdigit((unsigned char)*p); p++) tokval = tokval * 10 + *p - '0';
        return T_NUMBER;
    }
    if (*p == '\'') {
        p++;
        tokval = charlit();
        charname(tokval, tok);
        return T_CHAR;
    }
    if (*p == '"') fatal("string literals are not tokens in yacc (use 'c' or a name)", 0);
    if (*p == '<') {
        for (p++, n = 0; *p && *p != '>' && *p != '\n'; p++)
            if (n < (int)sizeof tok - 1) tok[n++] = *p;
        if (*p != '>') fatal("unterminated <type>", 0);
        p++;
        while (n > 0 && tok[n - 1] == ' ') n--;
        tok[n] = 0;
        for (q = tok; *q == ' '; q++) ;
        if (!*q) fatal("empty <type>", 0);
        memmove(tok, q, strlen(q) + 1);
        return T_TAG;
    }
    if (*p == '%' || *p == '\\') {
        if (p[1] == '%' || p[1] == '\\') { p += 2; return T_MARK; }
        if (p[1] == '{') { p += 2; return T_LCURL; }
        if (p[1] == '<') { p += 2; return T_LEFT; }
        if (p[1] == '>') { p += 2; return T_RIGHT; }
        if (p[1] == '2') { p += 2; return T_NONASSOC; }
        if (p[1] == '0') { p += 2; return T_TOKEN; }
        if (p[1] == '=') { p += 2; return T_PREC; }
        for (p++, n = 0; isalpha((unsigned char)*p); p++)
            if (n < (int)sizeof tok - 1) tok[n++] = *p;
        tok[n] = 0;
        for (n = 0; kw[n].name; n++)
            if (!strcmp(kw[n].name, tok)) return kw[n].t;
        fatal("unknown declaration %%%s", tok);
    }
    return *p++;
}

/* copy C code from p up to the brace that closes the one before p (depth 1),
 * or (depth 0) up to %}; strings, characters and comments are skipped */
static void copycode(Buf *b, int depth)
{
    int c, q, line0 = lineno;
    for (;;) {
        c = *p;
        if (!c) {
            lineno = line0;
            fatal(depth ? "unterminated action or %%union" : "unterminated %%{", 0);
        }
        if (!depth && c == '%' && p[1] == '}') { p += 2; return; }
        if (c == '\n') lineno++;
        if (c == '"' || c == '\'') {
            q = c;
            bputc(b, *p++);
            while (*p && *p != q) {
                if (*p == '\n') {
                    if (q == '\'') break;        /* an apostrophe, not a constant */
                    lineno++;
                }
                if (*p == '\\' && p[1]) {
                    if (p[1] == '\n') lineno++;
                    bputc(b, *p++);
                }
                bputc(b, *p++);
            }
            if (*p == q) bputc(b, *p++);
            continue;
        }
        if (c == '/' && p[1] == '*') {
            bputn(b, p, 2);
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') lineno++;
                bputc(b, *p++);
            }
            if (*p) { bputn(b, p, 2); p += 2; }
            continue;
        }
        if (c == '/' && p[1] == '/') {
            while (*p && *p != '\n') bputc(b, *p++);
            continue;
        }
        if (depth && c == '{') depth++;
        if (depth && c == '}' && --depth == 0) { bputc(b, *p++); return; }
        bputc(b, *p++);
    }
}

static void linedir(Buf *b, int line)
{
    char s[40];
    if (lflag) return;
    sprintf(s, "#line %d \"", line);
    bputs(b, s);
    bputs(b, infile);
    bputs(b, "\"\n");
}

/* ---- declarations ---- */

static int preclevel;

/* %token %left %right %nonassoc %type: [<tag>] names [numbers] */
static void decl(int kind)
{
    int t, tag = 0, assoc = 0;
    char *savep;
    int saveline;
    struct sym *sp = 0;
    if (kind == T_LEFT) assoc = A_LEFT;
    if (kind == T_RIGHT) assoc = A_RIGHT;
    if (kind == T_NONASSOC) assoc = A_NONASSOC;
    if (assoc) preclevel++;
    for (;;) {
        savep = p;
        saveline = lineno;
        t = gettok();
        if (t == T_TAG) {
            if (tag) fatal("two <types>", 0);
            tag = tagindex(tok);
            continue;
        }
        if (t == ',') continue;
        if (t == T_NUMBER && sp && kind != T_TYPE) {
            if (sp == errsym) continue;
            if (*sp->name == '\'') fatal("a character token has its own number: %s", sp->name);
            sp->value = tokval;
            continue;
        }
        if (t != T_IDENT && t != T_CHAR) break;
        sp = lookup(tok);
        if (t == T_CHAR) { sp->class = S_TOKEN; sp->value = tokval; }
        if (kind == T_TYPE) {
            if (!tag) fatal("%%type needs a <type>", 0);
        } else {
            if (sp->class == S_NTERM) fatal("%s is a nonterminal", sp->name);
            sp->class = S_TOKEN;
        }
        if (tag) {
            if (sp->tag && sp->tag != tag) error("%s has two types", sp->name);
            sp->tag = tag;
        }
        if (assoc) {
            if (sp->prec) error("%s has two precedences", sp->name);
            sp->prec = preclevel;
            sp->assoc = assoc;
        }
    }
    p = savep;
    lineno = saveline;
}

static void declarations(void)
{
    int t, line;
    Buf u = { 0, 0, 0 };
    for (;;) {
        t = gettok();
        switch (t) {
        case T_MARK:
            return;
        case T_LCURL:
            linedir(&pro, lineno);
            copycode(&pro, 0);
            bputc(&pro, '\n');
            break;
        case T_TOKEN: case T_LEFT: case T_RIGHT: case T_NONASSOC: case T_TYPE:
            decl(t);
            break;
        case T_START:
            if (gettok() != T_IDENT) fatal("%%start needs a nonterminal", 0);
            if (startsym) error("two %%start declarations", 0);
            startsym = lookup(tok);
            break;
        case T_UNION:
            if (haveunion) fatal("two %%union declarations", 0);
            skipws();
            if (*p != '{') fatal("%%union needs { ... }", 0);
            haveunion = 1;
            unionline = line = lineno;
            u.len = 0;
            bputc(&u, *p++);
            copycode(&u, 1);
            unionbody = u.s;
            bputs(&pro, "#if !defined(YYSTYPE) && !defined(YYSTYPE_IS_DECLARED)\n");
            linedir(&pro, line);
            bputs(&pro, "typedef union YYSTYPE ");
            bputs(&pro, unionbody);
            bputs(&pro, " YYSTYPE;\n#define YYSTYPE_IS_DECLARED 1\n#endif\n");
            break;
        case T_EOF:
            fatal("no %%%% (the rules) in the grammar", 0);
        default:
            if (t < 256) { tok[0] = t; tok[1] = 0; }
            fatal("syntax error in the declarations at %s", tok);
        }
    }
}

/* ---- rules ---- */

static struct sym **body;               /* every rule's right side, in rule order */
static int nbody, bodycap;
static struct sym **rlhs;               /* each rule's left side */
static struct sym **rprec;              /* each rule's %prec token, or 0 */
static int rulecap;
static int typed;                       /* types are in use: $n needs one */

/* the rule being read */
static struct sym **cur;
static int ncur, curcap;
static struct sym *curlhs, *curprec;
static Buf act;                         /* a pending action, untranslated */
static int haveact, actline, nmid;

static void addbody(struct sym *sp)
{
    if (nbody == bodycap) {
        bodycap = bodycap ? bodycap * 2 : 256;
        body = xrealloc(body, bodycap * sizeof *body);
    }
    body[nbody++] = sp;
}

static int newrule(struct sym *lhs, int n, struct sym *prec, char *code, int line)
{
    if (nrules == rulecap) {
        rulecap = rulecap ? rulecap * 2 : 64;
        rules = xrealloc(rules, rulecap * sizeof *rules);
        rlhs = xrealloc(rlhs, rulecap * sizeof *rlhs);
        rprec = xrealloc(rprec, rulecap * sizeof *rprec);
    }
    memset(&rules[nrules], 0, sizeof rules[nrules]);
    rules[nrules].rhs = nbody - n;
    rules[nrules].len = n;
    rules[nrules].act = code;
    rules[nrules].line = line;
    rlhs[nrules] = lhs;
    rprec[nrules] = prec;
    return nrules++;
}

/* a $n's type: <tag> (explicit), else the symbol's; L symbols precede */
static void valref(Buf *b, int n, int L, char *tag, int ismid)
{
    char s[40];
    struct sym *sp;
    if (n > L) {
        sprintf(s, "%d", n);
        error(ismid ? "$%s is not before this mid-rule action" : "$%s is past the end of the rule", s);
        return;
    }
    sprintf(s, "yyvsp[%d]", n - L);
    bputs(b, s);
    if (tag) {
        bputc(b, '.');
        bputs(b, tag);
        return;
    }
    if (!typed) return;
    sp = n >= 1 && n <= ncur ? cur[n - 1] : 0;
    if (sp && sp->tag) {
        bputc(b, '.');
        bputs(b, tags[sp->tag - 1]);
        return;
    }
    sprintf(s, "%d", n);
    if (n < 1) error("$%s needs a <type> ($<type>n)", s);
    else error("$%s has no type", s);
}

/* translate the pending action: $$ $n $-n $<t>$ $<t>n; L symbols precede it
 * in the rule; ismid: a mid-rule action, $$ its own value */
static char *translate(int L, int ismid)
{
    Buf b = { 0, 0, 0 };
    char *s = act.s, *t, tag[128], num[24];
    int q, n, neg, line = actline, k;
    linedir(&b, actline);
    while (*s) {
        if (*s == '"' || *s == '\'') {
            q = *s;
            bputc(&b, *s++);
            while (*s && *s != q) {
                if (*s == '\n') { line++; if (q == '\'') break; }
                if (*s == '\\' && s[1]) bputc(&b, *s++);
                bputc(&b, *s++);
            }
            if (*s == q) bputc(&b, *s++);
            continue;
        }
        if (s[0] == '/' && s[1] == '*') {
            t = strstr(s + 2, "*/");
            t = t ? t + 2 : s + strlen(s);
            for (; s < t; s++) { if (*s == '\n') line++; bputc(&b, *s); }
            continue;
        }
        if (s[0] == '/' && s[1] == '/') {
            while (*s && *s != '\n') bputc(&b, *s++);
            continue;
        }
        if (*s == '\n') line++;
        if (*s != '$') { bputc(&b, *s++); continue; }
        k = lineno;
        lineno = line;
        s++;
        tag[0] = 0;
        if (*s == '<') {
            for (s++, n = 0; *s && *s != '>' && n < (int)sizeof tag - 1; ) tag[n++] = *s++;
            tag[n] = 0;
            if (*s != '>') { error("unterminated $<type>", 0); break; }
            s++;
            tagindex(tag);
        }
        if (*s == '$') {
            s++;
            bputs(&b, "yyval");
            if (tag[0]) { bputc(&b, '.'); bputs(&b, tag); }
            else if (typed) {
                if (ismid) error("$$ of a mid-rule action needs a <type> ($<type>$)", 0);
                else if (curlhs->tag) { bputc(&b, '.'); bputs(&b, tags[curlhs->tag - 1]); }
                else error("$$ of %s has no type", curlhs->name);
            }
        } else if (isdigit((unsigned char)*s) || (*s == '-' && isdigit((unsigned char)s[1]))) {
            neg = *s == '-';
            if (neg) s++;
            for (n = 0; isdigit((unsigned char)*s); s++) n = n * 10 + *s - '0';
            valref(&b, neg ? -n : n, L, tag[0] ? tag : 0, ismid);
        } else {
            num[0] = *s;
            num[1] = 0;
            error("bad $ in an action: $%s", num);
        }
        lineno = k;
    }
    return b.s;
}

/* an action followed by more symbols becomes an empty rule of its own,
 * $$1 : {action}, the name standing in its place */
static void midrule(void)
{
    char name[24];
    struct sym *sp;
    sprintf(name, "$$%d", ++nmid);
    sp = lookup(name);
    sp->class = S_NTERM;
    sp->isrule = 1;
    newrule(sp, 0, 0, translate(ncur, 1), actline);
    haveact = 0;
    if (ncur == curcap) {
        curcap = curcap ? curcap * 2 : 32;
        cur = xrealloc(cur, curcap * sizeof *cur);
    }
    cur[ncur++] = sp;
}

static void addsym(struct sym *sp)
{
    if (haveact) midrule();
    if (ncur == curcap) {
        curcap = curcap ? curcap * 2 : 32;
        cur = xrealloc(cur, curcap * sizeof *cur);
    }
    cur[ncur++] = sp;
}

static void endrule(int line)
{
    int i;
    char *code = haveact ? translate(ncur, 0) : 0;
    for (i = 0; i < ncur; i++) addbody(cur[i]);
    newrule(curlhs, ncur, curprec, code, line);
    ncur = 0;
    curprec = 0;
    haveact = 0;
}

static void readrules(void)
{
    int t, line;
    struct sym *sp;
    t = gettok();
    if (t != T_CIDENT) fatal("no rules (each starts name:)", 0);
    while (t == T_CIDENT) {
        curlhs = lookup(tok);
        if (curlhs->class == S_TOKEN) error("token %s on the left of a rule", curlhs->name);
        curlhs->class = S_NTERM;
        curlhs->isrule = 1;
        if (!startsym) startsym = curlhs;
        line = lineno;
        for (;;) {
            t = gettok();
            if (t == T_IDENT || t == T_CHAR) {
                sp = lookup(tok);
                if (t == T_CHAR) { sp->class = S_TOKEN; sp->value = tokval; }
                addsym(sp);
            } else if (t == '{' || t == '=') {
                if (t == '=') {
                    skipws();
                    if (*p != '{') fatal("= needs an action { ... }", 0);
                    p++;
                }
                if (haveact) midrule();
                act.len = 0;
                bputc(&act, '{');
                actline = lineno;
                copycode(&act, 1);
                haveact = 1;
            } else if (t == T_PREC) {
                t = gettok();
                if (t != T_IDENT && t != T_CHAR) fatal("%%prec needs a token", 0);
                sp = lookup(tok);
                if (t == T_CHAR) { sp->class = S_TOKEN; sp->value = tokval; }
                if (sp->class != S_TOKEN) error("%%prec %s: not a token", sp->name);
                curprec = sp;
            } else if (t == '|') {
                endrule(line);
                line = lineno;
            } else if (t == ';') {
                endrule(line);
                do t = gettok(); while (t == ';');
                break;
            } else if (t == T_CIDENT || t == T_MARK || t == T_EOF) {
                endrule(line);
                break;
            } else {
                if (t < 256) { tok[0] = t; tok[1] = 0; }
                fatal("syntax error in a rule at %s", tok);
            }
        }
    }
    if (t == T_MARK) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; lineno++; }
        epiline = lineno;
        epilogue = p;
    } else if (t != T_EOF) {
        if (t < 256) { tok[0] = t; tok[1] = 0; }
        fatal("syntax error in the rules at %s", tok);
    }
}

/* ---- numbering ---- */

/* give every symbol its number, the tokens their values, build ritem */
static void pack(void)
{
    int i, j, k, n, v;
    struct sym *sp;
    char *used;
    lineno = 0;
    /* every symbol used must be a token or have rules */
    for (i = 0; i < nbody; i++)
        if (body[i] && body[i]->class == S_UNKNOWN) {
            body[i]->class = S_NTERM;
            fprintf(stderr, "yacc: %s:%d: %s is used but is not a token and has no rules\n",
                    infile, body[i]->line, body[i]->name);
            nerrors++;
        }
    if (startsym->class != S_NTERM) {
        if (startsym->class == S_TOKEN) error("the start symbol %s is a token", startsym->name);
        else error("the start symbol %s has no rules", startsym->name);
        startsym->class = S_NTERM;
    }
    if (nerrors) exit(1);
    /* the numbers */
    ntokens = nnterms = 0;
    for (i = 0; i < nall; i++) {
        if (allsyms[i]->class == S_TOKEN) ntokens++;
        else if (allsyms[i]->class == S_NTERM) nnterms++;
    }
    nsyms = ntokens + nnterms;
    symtab = xmalloc(nsyms * sizeof *symtab);
    endsym->num = 0;
    errsym->num = 1;
    undefsym->num = 2;
    acceptsym->num = ntokens;
    n = 3;
    k = ntokens + 1;
    for (i = 0; i < nall; i++) {
        sp = allsyms[i];
        if (sp == endsym || sp == errsym || sp == undefsym || sp == acceptsym) ;
        else if (sp->class == S_TOKEN) sp->num = n++;
        else if (sp->class == S_NTERM) sp->num = k++;
        if (sp->class != S_UNKNOWN) symtab[sp->num] = sp;
    }
    /* the token values: from 257, past any given */
    used = xmalloc(65536);
    for (i = 0; i < ntokens; i++) {
        v = symtab[i]->value;
        if (i == 2 || v < 0) continue;
        if (v > 65535) { lineno = 0; fatal("token %s: number too big", symtab[i]->name); }
        if (used[v]) {
            for (j = 0; j < i; j++)
                if (j != 2 && symtab[j]->value == v) break;
            fprintf(stderr, "yacc: tokens %s and %s have the same number %d\n",
                    symtab[j]->name, symtab[i]->name, v);
            exit(1);
        }
        used[v] = 1;
    }
    for (i = 0, v = 257; i < ntokens; i++)
        if (i != 2 && symtab[i]->value < 0) {
            while (used[v]) v++;
            symtab[i]->value = v;
            used[v] = 1;
        }
    free(used);
    /* rule 0, $accept : start $end; ritem */
    body[0] = startsym;
    body[1] = endsym;
    rules[0].rhs = 0;
    rules[0].len = 2;
    rlhs[0] = acceptsym;
    start = startsym->num;
    nitems = nbody + nrules;
    ritem = xmalloc(nitems * sizeof *ritem);
    for (i = 0, k = 0; i < nrules; i++) {
        rules[i].lhs = rlhs[i]->num;
        n = rules[i].rhs;
        rules[i].rhs = k;
        sp = rprec[i];
        for (j = 0; j < rules[i].len; j++) {
            ritem[k++] = body[n + j]->num;
            if (!rprec[i] && body[n + j]->class == S_TOKEN) sp = body[n + j];
        }
        ritem[k++] = -1 - i;
        if (sp) {
            rules[i].prec = sp->prec;
            rules[i].assoc = sp->assoc;
        }
    }
}

/* every nonterminal should derive some string of tokens; the start symbol must */
static void productive(void)
{
    char *ok = xmalloc(nsyms);
    int i, j, changed;
    for (i = 0; i < ntokens; i++) ok[i] = 1;
    do {
        changed = 0;
        for (i = 0; i < nrules; i++) {
            if (ok[rules[i].lhs]) continue;
            for (j = rules[i].rhs; ritem[j] >= 0 && ok[ritem[j]]; j++) ;
            if (ritem[j] < 0) ok[rules[i].lhs] = changed = 1;
        }
    } while (changed);
    if (!ok[start]) fatal("the start symbol %s derives no sentence", symtab[start]->name);
    for (i = ntokens + 1; i < nsyms; i++)
        if (!ok[i]) warn("%s derives no sentence", symtab[i]->name);
    free(ok);
}

/* the whole grammar into memory */
static void readfile(char *name)
{
    FILE *f = fopen(name, "r");
    int n = 0, cap = 4096, k, i, j;
    if (!f) {
        fprintf(stderr, "yacc: cannot open %s\n", name);
        exit(1);
    }
    src = xmalloc(cap + 1);
    while ((k = fread(src + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap) {
            cap *= 2;
            src = xrealloc(src, cap + 1);
        }
    }
    fclose(f);
    for (i = j = 0; i < n; i++)         /* (NULs dropped) */
        if (src[i]) src[j++] = src[i];
    src[j] = 0;
}

static void usage(void)
{
    fprintf(stderr, "usage: yacc [-dltv] [-b file_prefix] [-p sym_prefix] grammar\n");
    exit(1);
}

static FILE *create(char *suffix)
{
    char *name = xmalloc(strlen(fileprefix) + strlen(suffix) + 1);
    FILE *f;
    strcpy(name, fileprefix);
    strcat(name, suffix);
    f = fopen(name, "w");
    if (!f) {
        fprintf(stderr, "yacc: cannot create %s\n", name);
        exit(1);
    }
    return f;
}

static void closeout(FILE *f)
{
    if (fflush(f) || ferror(f)) {
        fprintf(stderr, "yacc: write error\n");
        exit(1);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int i;
    char *a;
    FILE *c, *h = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (a = argv[i] + 1; *a; a++) {
            switch (*a) {
            case 'd': dflag = 1; break;
            case 'l': lflag = 1; break;
            case 't': tflag = 1; break;
            case 'v': vflag = 1; break;
            case 'b': case 'p':
                if (a[1]) {
                    if (*a == 'b') fileprefix = a + 1; else symprefix = a + 1;
                } else if (i + 1 < argc) {
                    if (*a == 'b') fileprefix = argv[++i]; else symprefix = argv[++i];
                } else usage();
                a += strlen(a) - 1;
                break;
            default:
                usage();
            }
        }
    }
    if (i != argc - 1) usage();
    infile = argv[i];
    readfile(infile);
    /* the predefined symbols */
    endsym = lookup("$end");
    endsym->class = S_TOKEN;
    endsym->value = 0;
    errsym = lookup("error");
    errsym->class = S_TOKEN;
    errsym->value = 256;
    undefsym = lookup("$undefined");
    undefsym->class = S_TOKEN;
    acceptsym = lookup("$accept");
    acceptsym->class = S_NTERM;
    /* rule 0 and its two symbols, filled in by pack() */
    newrule(acceptsym, 0, 0, 0, 0);
    addbody(0);
    addbody(0);
    p = src;
    declarations();
    typed = haveunion || ntags > 0;
    readrules();
    if (nerrors) exit(1);
    prologue = pro.s ? pro.s : "";
    pack();
    productive();
    lr0();
    lalr();
    if (vflag) vout = create(".output");
    actions();
    if (vflag) closeout(vout);
    if (nsr || nrr) {
        fprintf(stderr, "yacc: ");
        if (nsr) fprintf(stderr, "%d shift/reduce conflict%s", nsr, nsr == 1 ? "" : "s");
        if (nsr && nrr) fprintf(stderr, ", ");
        if (nrr) fprintf(stderr, "%d reduce/reduce conflict%s", nrr, nrr == 1 ? "" : "s");
        fprintf(stderr, "\n");
    }
    if (nunused) fprintf(stderr, "yacc: %d rule%s never reduced\n", nunused, nunused == 1 ? "" : "s");
    c = create(".tab.c");
    if (dflag) h = create(".tab.h");
    output(c, h);
    closeout(c);
    if (h) closeout(h);
    return 0;
}
