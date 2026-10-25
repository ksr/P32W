/* lexre.c -- lex's patterns: each rule's pattern is parsed into a syntax
 * tree (re_rule), and the trees become one NFA (re_nfa) by Thompson's
 * construction: a node either moves on a character of its set (N_SET),
 * or on nothing (N_EPS, up to two ways), or marks the end of a rule's
 * match (N_ACC) or of the r of r/s when both r and s vary in length
 * (N_HEAD). Character sets are kept once each (a hash table), so that the
 * DFA's character classes come from the distinct sets.
 *
 * The syntax (POSIX lex's extended regular expressions):
 *   c          a character other than \ " . [ ] ( ) { } * + ? | / ^ $ < >
 *   \c         \n \t \r \f \v \a \b, \ooo octal, \xhh hex, else c itself
 *   "..."      the characters as they are (with \ escapes)
 *   [...]      a class: ranges a-z, [:alpha:] and the other POSIX names,
 *              \ escapes; [^...] the complement (newline included)
 *   .          any character but newline
 *   {name}     the definition of name, as if in ( )
 *   r* r+ r?   zero or more, one or more, zero or one
 *   r{n} r{n,} r{n,m}   repetition (counts up to 255)
 *   rs  r|s  (r)
 *   ^r         r at the start of a line (only at the start of a pattern)
 *   r$         r at the end of a line (only at the end): as r/\n
 *   r/s        r when followed by s (once, at the outer level) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lex.h"

Cset *csets;
int ncset;
static int csetcap;
static int cshead[512], *csnext;

int nnfa;
int *nfa_op, *nfa_arg, *nfa_out1, *nfa_out2;
static int nfacap;
int *rulestart;
int nvar;

static char *pat;               /* the pattern being parsed, for messages */
static char *pp;                /* the parser's place in it (or a definition) */
static int depth;               /* ( ) nesting */
static int indef;               /* inside {name}s: / and $ are ordinary */

/* the parsed definitions, kept to be used again */
static char **dtext;
static Re **dre;
static int ndre;

static void bad(char *msg)
{
    char buf[200];
    sprintf(buf, "%.100s in the pattern %%s", msg);
    lexerr(buf, pat);
}

/* the number of a character set (made once) */
static int setid(Cset *s)
{
    unsigned h = 0;
    int i;
    for (i = 0; i < 8; i++) h = h * 31 + s->w[i];
    h %= 512;
    if (ncset == 0)
        for (i = 0; i < 512; i++) cshead[i] = -1;
    for (i = cshead[h]; i >= 0; i = csnext[i])
        if (!memcmp(&csets[i], s, sizeof(Cset))) return i;
    if (ncset == csetcap) {
        csetcap = csetcap ? 2 * csetcap : 64;
        csets = xrealloc(csets, csetcap * sizeof(Cset));
        csnext = xrealloc(csnext, csetcap * sizeof(int));
    }
    csets[ncset] = *s;
    csnext[ncset] = cshead[h];
    cshead[h] = ncset;
    return ncset++;
}

static Re *mk(int op, Re *a, Re *b)
{
    Re *e = xalloc(sizeof(Re));
    e->op = op;
    e->a = a;
    e->b = b;
    switch (op) {
    case R_CAT:
        e->min = a->min + b->min;
        e->max = (a->max < 0 || b->max < 0) ? -1 : a->max + b->max;
        break;
    case R_ALT:
        e->min = a->min < b->min ? a->min : b->min;
        e->max = (a->max < 0 || b->max < 0) ? -1 : a->max > b->max ? a->max : b->max;
        break;
    case R_STAR:
        e->max = -1;
        break;
    case R_PLUS:
        e->min = a->min;
        e->max = -1;
        break;
    case R_OPT:
        e->max = a->max;
        break;
    }
    return e;
}

static Re *mkset(Cset *s)
{
    Re *e;
    int c;
    if (caseless)
        for (c = 'A'; c <= 'Z'; c++)
            if (CS_HAS(s, c) || CS_HAS(s, c + 32)) {
                CS_ADD(s, c);
                CS_ADD(s, c + 32);
            }
    e = mk(R_SET, 0, 0);
    e->set = setid(s);
    e->min = e->max = 1;
    return e;
}

static Re *mkchar(int c)
{
    Cset s;
    memset(&s, 0, sizeof s);
    CS_ADD(&s, c);
    return mkset(&s);
}

static Re *mkrep(Re *a, int n, int m)
{
    Re *e = mk(R_REP, a, 0);
    e->n = n;
    e->m = m;
    e->min = a->min * n;
    e->max = (m < 0 || a->max < 0) ? -1 : a->max * m;
    return e;
}

static int hexval(int c)
{
    return isdigit(c) ? c - '0' : (c | 32) - 'a' + 10;
}

/* the character a \ escape stands for; *p is after the \ */
static int escape(char **p)
{
    char *s = *p;
    int c = *s++ & 255, n, i;
    switch (c) {
    case 'n': c = '\n'; break;
    case 't': c = '\t'; break;
    case 'r': c = '\r'; break;
    case 'f': c = '\f'; break;
    case 'v': c = '\v'; break;
    case 'a': c = 7; break;
    case 'b': c = 8; break;
    case 'x':
        if (isxdigit(*s)) {
            n = 0;
            for (i = 0; i < 2 && isxdigit(*s); i++) n = 16 * n + hexval(*s++);
            c = n;
        }
        break;
    case 0:
        bad("\\ at the end");
        break;
    default:
        if (c >= '0' && c <= '7') {
            n = c - '0';
            for (i = 1; i < 3 && *s >= '0' && *s <= '7'; i++) n = 8 * n + *s++ - '0';
            c = n & 255;
        }
    }
    *p = s;
    return c;
}

/* [:name:] */
static void namedclass(Cset *s, char *name, int len)
{
    static char *names[] = { "alpha", "digit", "alnum", "upper", "lower", "space",
                             "blank", "punct", "print", "graph", "cntrl", "xdigit", 0 };
    int k, c, in;
    for (k = 0; names[k]; k++)
        if ((int) strlen(names[k]) == len && !strncmp(names[k], name, len)) break;
    if (!names[k]) bad("unknown [:class:]");
    for (c = 0; c < 128; c++) {
        int up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', dig = c >= '0' && c <= '9';
        switch (k) {
        case 0: in = up || lo; break;
        case 1: in = dig; break;
        case 2: in = up || lo || dig; break;
        case 3: in = up; break;
        case 4: in = lo; break;
        case 5: in = c == ' ' || (c >= 9 && c <= 13); break;
        case 6: in = c == ' ' || c == '\t'; break;
        case 7: in = c > 32 && c < 127 && !up && !lo && !dig; break;
        case 8: in = c >= 32 && c < 127; break;
        case 9: in = c > 32 && c < 127; break;
        case 10: in = c < 32 || c == 127; break;
        default: in = dig || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); break;
        }
        if (in) CS_ADD(s, c);
    }
}

static int classchar(void)
{
    if (*pp == '\\') {
        pp++;
        return escape(&pp);
    }
    return *pp++ & 255;
}

/* [...]: pp is after the [ */
static Re *p_class(void)
{
    Cset s;
    int neg = 0, first = 1, c, c2, i;
    char *e;
    memset(&s, 0, sizeof s);
    if (*pp == '^') {
        neg = 1;
        pp++;
    }
    for (;;) {
        if (!*pp) bad("[ without ]");
        if (*pp == ']' && !first) {
            pp++;
            break;
        }
        first = 0;
        if (pp[0] == '[' && pp[1] == ':') {
            if ((e = strstr(pp + 2, ":]")) == 0) bad("[: without :]");
            namedclass(&s, pp + 2, e - (pp + 2));
            pp = e + 2;
            continue;
        }
        c = classchar();
        if (pp[0] == '-' && pp[1] && pp[1] != ']') {
            pp++;
            c2 = classchar();
            if (c2 < c) bad("a range backwards");
            for (; c <= c2; c++) CS_ADD(&s, c);
        } else
            CS_ADD(&s, c);
    }
    if (caseless)
        for (c = 'A'; c <= 'Z'; c++)
            if (CS_HAS(&s, c) || CS_HAS(&s, c + 32)) {
                CS_ADD(&s, c);
                CS_ADD(&s, c + 32);
            }
    if (neg)
        for (i = 0; i < 8; i++) s.w[i] = ~s.w[i];
    return mkset(&s);
}

static Re *p_alt(void);

/* {name}: its definition, parsed once */
static Re *p_name(void)
{
    char name[128], *text, *save;
    int n = 0, i, savedepth;
    Re *e;
    while (*pp && *pp != '}' && n < 127) name[n++] = *pp++;
    name[n] = 0;
    if (*pp != '}') bad("{ without }");
    pp++;
    if ((text = finddef(name)) == 0) {
        sprintf(name + n, " (undefined)");
        bad(name);
    }
    for (i = 0; i < ndre; i++)
        if (dtext[i] == text) return dre[i];
    if (indef > 50) bad("definitions nested too deeply (one uses itself?)");
    save = pp;
    savedepth = depth;
    pp = text;
    depth = 0;
    indef++;
    e = p_alt();
    if (*pp) bad("unmatched ) in a definition");
    indef--;
    pp = save;
    depth = savedepth;
    dtext = xrealloc(dtext, (ndre + 1) * sizeof(char *));
    dre = xrealloc(dre, (ndre + 1) * sizeof(Re *));
    dtext[ndre] = text;
    dre[ndre++] = e;
    return e;
}

static Re *p_atom(void)
{
    Cset s;
    Re *e = 0, *b;
    int c = *pp & 255;
    switch (c) {
    case '(':
        pp++;
        depth++;
        e = p_alt();
        if (*pp != ')') bad("( without )");
        pp++;
        depth--;
        return e;
    case '[':
        pp++;
        return p_class();
    case '"':
        pp++;
        while (*pp != '"') {
            if (!*pp) bad("\" without \"");
            b = mkchar(classchar());
            e = e ? mk(R_CAT, e, b) : b;
        }
        pp++;
        return e ? e : mk(R_EMPTY, 0, 0);
    case '.':
        pp++;
        memset(&s, 0, sizeof s);
        for (c = 0; c < NCHAR; c++)
            if (c != '\n') CS_ADD(&s, c);
        return mkset(&s);
    case '\\':
        pp++;
        return mkchar(escape(&pp));
    case '{':
        pp++;
        if (!isalpha(*pp) && *pp != '_') bad("{ that is neither {name} nor a repetition");
        return p_name();
    case '*': case '+': case '?':
        bad("* + or ? with nothing before it");
    }
    pp++;
    return mkchar(c);
}

static int number(void)
{
    int n = 0;
    while (isdigit(*pp)) {
        n = 10 * n + *pp++ - '0';
        if (n > 255) bad("a repetition count over 255");
    }
    return n;
}

static Re *p_post(void)
{
    Re *e = p_atom();
    int n, m;
    for (;;) {
        if (*pp == '*') e = mk(R_STAR, e, 0);
        else if (*pp == '+') e = mk(R_PLUS, e, 0);
        else if (*pp == '?') e = mk(R_OPT, e, 0);
        else if (*pp == '{' && isdigit(pp[1])) {
            pp++;
            n = m = number();
            if (*pp == ',') {
                pp++;
                m = isdigit(*pp) ? number() : -1;
            }
            if (*pp != '}') bad("a bad {n,m}");
            if (m >= 0 && m < n) bad("{n,m} with m less than n");
            e = mkrep(e, n, m);
        } else
            return e;
        pp++;
    }
}

/* the end of a concatenation: | ) or, at the outer level, / and a final $ */
static int catend(void)
{
    if (!*pp || *pp == '|' || *pp == ')') return 1;
    if (depth == 0 && !indef && (*pp == '/' || (*pp == '$' && !pp[1]))) return 1;
    return 0;
}

static Re *p_cat(void)
{
    Re *e = 0, *b;
    while (!catend()) {
        b = p_post();
        e = e ? mk(R_CAT, e, b) : b;
    }
    return e ? e : mk(R_EMPTY, 0, 0);
}

static Re *p_alt(void)
{
    Re *e = p_cat();
    while (*pp == '|') {
        pp++;
        e = mk(R_ALT, e, p_cat());
    }
    return e;
}

/* parse a rule's pattern: ^, r, /s, $ */
void re_rule(Rule *r, char *p)
{
    Re *nl;
    pat = pp = p;
    depth = indef = 0;
    if (*pp == '^') {
        r->bol = 1;
        pp++;
    }
    r->re = p_alt();
    if (*pp == '/') {
        pp++;
        r->trail = p_alt();
    }
    if (*pp == '$' && !pp[1]) {
        pp++;
        nl = mkchar('\n');
        r->trail = r->trail ? mk(R_CAT, r->trail, nl) : nl;
    }
    if (*pp == ')') bad(") without (");
    if (*pp) bad("a second / or $");
    if (r->re->max == 0) lexwarn("the pattern %s matches only the empty string", p);
    if (r->trail) {
        if (r->re->min == 0)
            lexwarn("r of r/s in %s can match nothing; then the rule is not used", p);
        if (r->trail->min == r->trail->max) {
            r->tkind = TK_BACK;
            r->tlen = r->trail->min;
        } else if (r->re->min == r->re->max) {
            r->tkind = TK_HEAD;
            r->tlen = r->re->min;
        } else {
            if (nvar == MAXVAR) bad("too many rules with r and s of r/s both of varying length");
            r->tkind = TK_VAR;
            r->tlen = nvar++;
            r->tmin = r->trail->min;
        }
    }
}

static int newnode(int op, int arg)
{
    if (nnfa == nfacap) {
        nfacap = nfacap ? 2 * nfacap : 1024;
        nfa_op = xrealloc(nfa_op, nfacap * sizeof(int));
        nfa_arg = xrealloc(nfa_arg, nfacap * sizeof(int));
        nfa_out1 = xrealloc(nfa_out1, nfacap * sizeof(int));
        nfa_out2 = xrealloc(nfa_out2, nfacap * sizeof(int));
    }
    nfa_op[nnfa] = op;
    nfa_arg[nnfa] = arg;
    nfa_out1[nnfa] = nfa_out2[nnfa] = -1;
    return nnfa++;
}

/* the NFA of e: returns its start; *end is its end, a node with no way
 * out yet. (The arrays move as they grow, so a node's links are set only
 * after the calls that make nodes.) */
static int build(Re *e, int *end)
{
    int s, t, x, e1, e2, cur, i;
    switch (e->op) {
    case R_SET:
        s = newnode(N_SET, e->set);
        t = newnode(N_EPS, 0);
        nfa_out1[s] = t;
        *end = t;
        return s;
    case R_CAT:
        s = build(e->a, &e1);
        x = build(e->b, &e2);
        nfa_out1[e1] = x;
        *end = e2;
        return s;
    case R_ALT:
        s = build(e->a, &e1);
        x = build(e->b, &e2);
        t = newnode(N_EPS, 0);
        i = newnode(N_EPS, 0);
        nfa_out1[i] = s;
        nfa_out2[i] = x;
        nfa_out1[e1] = t;
        nfa_out1[e2] = t;
        *end = t;
        return i;
    case R_STAR:
    case R_OPT:
        x = build(e->a, &e1);
        s = newnode(N_EPS, 0);
        t = newnode(N_EPS, 0);
        nfa_out1[s] = x;
        nfa_out2[s] = t;
        nfa_out1[e1] = t;
        if (e->op == R_STAR) {
            nfa_out1[e1] = x;
            nfa_out2[e1] = t;
        }
        *end = t;
        return s;
    case R_PLUS:
        x = build(e->a, &e1);
        t = newnode(N_EPS, 0);
        nfa_out1[e1] = x;
        nfa_out2[e1] = t;
        *end = t;
        return x;
    case R_REP:
        s = cur = newnode(N_EPS, 0);
        for (i = 0; i < e->n; i++) {
            x = build(e->a, &e1);
            nfa_out1[cur] = x;
            cur = e1;
        }
        t = newnode(N_EPS, 0);
        if (e->m < 0) {         /* {n,}: then a* */
            x = build(e->a, &e1);
            nfa_out1[cur] = x;
            nfa_out2[cur] = t;
            nfa_out1[e1] = x;
            nfa_out2[e1] = t;
        } else {                /* {n,m}: then m - n optional copies */
            for (; i < e->m; i++) {
                x = build(e->a, &e1);
                nfa_out1[cur] = x;
                nfa_out2[cur] = t;
                cur = e1;
            }
            nfa_out1[cur] = t;
        }
        *end = t;
        return s;
    }
    s = newnode(N_EPS, 0);      /* R_EMPTY */
    *end = s;
    return s;
}

/* the whole NFA: for each rule, start -> r [head] [s] -> accept */
void re_nfa(void)
{
    int i, s, x, e, h;
    Rule *r;
    rulestart = xalloc((nrules + 1) * sizeof(int));
    for (i = 0; i < nrules; i++) {
        r = &rules[i];
        lineno = r->line;
        s = newnode(N_EPS, 0);
        x = build(r->re, &e);
        nfa_out1[s] = x;
        if (r->tkind == TK_VAR) {
            h = newnode(N_HEAD, r->tlen);
            nfa_out1[e] = h;
            e = h;
        }
        if (r->trail) {
            x = build(r->trail, &h);
            nfa_out1[e] = x;
            e = h;
        }
        x = newnode(N_ACC, i);
        nfa_out1[e] = x;
        rulestart[i] = s;
    }
    lineno = 0;
}
