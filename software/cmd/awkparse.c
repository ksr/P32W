/* awkparse.c -- awk's parser (awk.h): recursive descent over the tokens,
 * building the tree that awkrun.c walks. The operators, loosest first:
 *
 *     = += -= *= /= %= ^=      right to left
 *     ?:                       right to left
 *     ||   &&   in   ~ !~
 *     < <= != == > >=          (in a print list a bare > redirects)
 *     cmd | getline
 *     concatenation            (no operator: two operands side by side)
 *     + -   * / %
 *     ! and unary - +
 *     ^ **                     right to left; -2^2 is -4, 2^-1 is 0.5
 *     ++ -- (before or after)  $ (a field)        ( )
 *
 * Newlines end statements, except where the lexer drops them; a ; does too.
 * print (a, b) > "f" is told from print (a)(b) by trying the list first
 * and backing up if it is not followed by the end of the statement. */
#include "awk.h"

static int tp;                  /* the current token */
#define T (toks[tp].t)
static int inprint;             /* in a print list: > is a redirection */
static int inloop, infunc, inbeginend;
static Func *curfn;

static Node *expr(void);
static Node *stmt(void);
static Node *primary(void);
static Node *concat(void);

static void err(void)
{
    synerr(tp, "syntax error");
}

static void expect(int t)
{
    if (T != t) err();
    tp++;
}

static void optnl(void)
{
    while (T == TK_NL) tp++;
}

static Node *node(int op, Node *a, Node *b)
{
    Node *n = emalloc(sizeof(Node));
    n->op = op;
    n->line = toks[tp > 0 ? tp - 1 : 0].line;
    n->n = 0;
    n->pure = 0;
    n->a = a;
    n->b = b;
    n->c = n->d = n->next = 0;
    n->p = 0;
    return n;
}

static int islvalue(Node *n)
{
    return n && (n->op == N_VAR || n->op == N_LOCAL || n->op == N_FIELD || n->op == N_INDEX);
}

static Func *getfunc(const char *name)
{
    int i;
    for (i = 0; i < nfuncs; i++)
        if (!strcmp(funcs[i]->name, name)) return funcs[i];
    funcs = erealloc(funcs, (nfuncs + 1) * sizeof(Func *));
    funcs[nfuncs] = emalloc(sizeof(Func));
    funcs[nfuncs]->name = sdup(name);
    funcs[nfuncs]->nparam = 0;
    funcs[nfuncs]->param = 0;
    funcs[nfuncs]->body = 0;
    funcs[nfuncs]->line = toks[tp].line;
    return funcs[nfuncs++];
}

static int isfunc(const char *name)
{
    int i;
    for (i = 0; i < nfuncs; i++)
        if (!strcmp(funcs[i]->name, name)) return 1;
    return 0;
}

/* a name: a parameter of the function being parsed, or a global */
static Node *varnode(const char *name)
{
    Node *n;
    int i;
    if (curfn)
        for (i = 0; i < curfn->nparam; i++)
            if (!strcmp(curfn->param[i], name)) {
                n = node(N_LOCAL, 0, 0);
                n->n = i;
                return n;
            }
    if (isfunc(name)) synerr(tp - 1, "can't use function as a variable");
    n = node(N_VAR, 0, 0);
    n->n = globalvar(name);
    return n;
}

static Node *constnum(const char *text)
{
    Node *n = node(N_NUM, 0, 0);
    Cell *c = emalloc(sizeof(Cell));
    cinit(c);
    c->t = T_NUM;
    c->nv = 1;
    num_parse(text, &c->n, 0);
    n->p = c;
    return n;
}

static Node *conststr(char *s)
{
    Node *n = node(N_STR, 0, 0);
    Cell *c = emalloc(sizeof(Cell));
    cinit(c);
    c->t = T_STR;
    c->sv = 3;                  /* (kept for good: never freed) */
    c->s = s;
    n->p = c;
    return n;
}

/* a list of expressions separated by commas (none before a ")") */
static Node *exprlist(int *count)
{
    Node *head = 0, **tail = &head;
    int n = 0;
    if (T == ')') { if (count) *count = 0; return 0; }
    for (;;) {
        *tail = expr();
        tail = &(*tail)->next;
        n++;
        if (T != ',') break;
        tp++;
        optnl();
    }
    if (count) *count = n;
    return head;
}

/* ---------------------------------------------------------------- expressions */
static Node *subscripts(void)
{
    Node *l;
    int save = inprint;
    inprint = 0;
    expect('[');
    l = exprlist(0);
    if (!l) err();
    expect(']');
    inprint = save;
    return l;
}

/* a variable, an element or a field, for getline's var (0 if none here) */
static Node *optlvalue(void)
{
    Node *n;
    if (T != TK_NAME && T != '$') return 0;
    n = primary();
    if (T == TK_INCR || T == TK_DECR) { n = node(T == TK_INCR ? N_POSTINC : N_POSTDEC, n, 0); tp++; }
    return n;
}

static void checkargs(int b, int nargs, Node *args)
{
    static const char mins[] = { 0, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 1, 1, 0 };
    static const char maxs[] = { 1, 3, 2, 3, 3, 3, 2, 99, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 1, 2, 1, 1 };
    if (nargs < mins[b] || nargs > maxs[b]) synerr(tp - 1, "wrong number of arguments to a built-in function");
    if (b == B_SPLIT && args->next->op != N_VAR && args->next->op != N_LOCAL)
        synerr(tp - 1, "split's second argument must be an array name");
    if ((b == B_SUB || b == B_GSUB) && nargs == 3 && !islvalue(args->next->next))
        synerr(tp - 1, "sub and gsub need a variable, element or field to change");
}

static Node *primary(void)
{
    Node *n, *l;
    int t = T, save, k = 0;
    char *msg;
    char probe;
    if (&probe < stacklimit) synerr(tp, "program nested too deeply");
    switch (t) {
    case TK_NUMBER:
        tp++;
        return constnum(toks[tp - 1].s);
    case TK_STRING:
        tp++;
        return conststr(toks[tp - 1].s);
    case TK_ERE:
        tp++;
        n = node(N_REGEX, 0, 0);
        n->p = re_comp(toks[tp - 1].s, &msg);
        if (!n->p) synerr(tp - 1, msg);
        return n;
    case '(':
        tp++;
        save = inprint;
        inprint = 0;
        optnl();
        l = exprlist(&k);
        if (!l) err();
        expect(')');
        inprint = save;
        if (k > 1) {                    /* (a, b) in array */
            if (T != TK_IN || toks[tp + 1].t != TK_NAME) err();
            tp += 2;
            n = node(N_IN, l, varnode(toks[tp - 1].s));
            return n;
        }
        return l;
    case '$':
        tp++;
        if (T == '-') { tp++; return node(N_FIELD, node(N_NEG, primary(), 0), 0); }
        if (T == TK_INCR || T == TK_DECR) {
            tp++;
            l = primary();
            if (!islvalue(l)) err();
            return node(N_FIELD, node(t == TK_INCR ? N_PREINC : N_PREDEC, l, 0), 0);
        }
        return node(N_FIELD, primary(), 0);
    case TK_INCR: case TK_DECR:
        tp++;
        l = primary();
        if (!islvalue(l)) err();
        return node(t == TK_INCR ? N_PREINC : N_PREDEC, l, 0);
    case '-':                           /* (after $ or ^) */
        tp++;
        return node(N_NEG, primary(), 0);
    case '+':
        tp++;
        return node(N_PLUS, primary(), 0);
    case '!':
        tp++;
        return node(N_NOT, primary(), 0);
    case TK_NAME:
        tp++;
        n = varnode(toks[tp - 1].s);
        if (T == '[') n = node(N_INDEX, n, subscripts());
        return n;
    case TK_FUNCNAME:
        n = node(N_CALL, 0, 0);
        n->p = getfunc(toks[tp].s);
        tp++;
        expect('(');
        save = inprint;
        inprint = 0;
        optnl();
        n->a = exprlist(&n->n);
        expect(')');
        inprint = save;
        n->line = toks[tp - 1].line;
        return n;
    case TK_BUILTIN:
        n = node(N_BUILTIN, 0, 0);
        n->n = toks[tp].n;
        tp++;
        if (T == '(') {
            tp++;
            save = inprint;
            inprint = 0;
            optnl();
            n->a = exprlist(&k);
            expect(')');
            inprint = save;
        } else if (n->n == B_LENGTH)
            k = 0;
        else
            err();
        checkargs(n->n, k, n->a);
        return n;
    case TK_GETLINE:                    /* getline [var] [< file] */
        tp++;
        n = node(N_GETLINE, optlvalue(), 0);
        n->n = G_SIMPLE;
        if (T == '<') {
            tp++;
            n->n = G_FILE;
            n->b = primary();
        }
        return n;
    }
    err();
    return 0;
}

static Node *postfix(void)
{
    Node *n = primary();
    if ((T == TK_INCR || T == TK_DECR) && islvalue(n)) {
        n = node(T == TK_INCR ? N_POSTINC : N_POSTDEC, n, 0);
        tp++;
    }
    return n;
}

static Node *powrhs(void);

static Node *power(void)
{
    Node *n = postfix();
    if (T == TK_POW) {
        tp++;
        n = node(N_POW, n, powrhs());
    }
    return n;
}

static Node *powrhs(void)
{
    if (T == '-') { tp++; return node(N_NEG, powrhs(), 0); }
    if (T == '+') { tp++; return node(N_PLUS, powrhs(), 0); }
    if (T == '!') { tp++; return node(N_NOT, powrhs(), 0); }
    return power();
}

static Node *unary(void)
{
    if (T == '!') { tp++; return node(N_NOT, unary(), 0); }
    if (T == '-') { tp++; return node(N_NEG, unary(), 0); }
    if (T == '+') { tp++; return node(N_PLUS, unary(), 0); }
    return power();
}

static Node *mul(void)
{
    Node *n = unary();
    int t;
    while ((t = T) == '*' || t == '/' || t == '%') {
        tp++;
        n = node(t == '*' ? N_MUL : t == '/' ? N_DIV : N_MOD, n, unary());
    }
    return n;
}

static Node *additive(void)
{
    Node *n = mul();
    int t;
    while ((t = T) == '+' || t == '-') {
        tp++;
        n = node(t == '+' ? N_ADD : N_SUB, n, mul());
    }
    return n;
}

static int startsoperand(int t)
{
    return t == TK_NUMBER || t == TK_STRING || t == TK_ERE || t == TK_NAME || t == TK_FUNCNAME
        || t == TK_BUILTIN || t == '$' || t == '(' || t == TK_INCR || t == TK_DECR || t == '!' || t == '-';
}

static Node *concat(void)
{
    Node *n = additive();
    while (startsoperand(T) && T != '-' && T != '!' && !(T == TK_GETLINE))
        n = node(N_CAT, n, additive());
    return n;
}

static Node *pipeget(void)
{
    Node *n = concat();
    while (T == '|' && toks[tp + 1].t == TK_GETLINE) {
        tp += 2;
        n = node(N_GETLINE, optlvalue(), n);
        n->n = G_CMD;
    }
    return n;
}

static Node *rel(void)
{
    Node *n = pipeget();
    int t, op;
    for (;;) {
        t = T;
        if (t == '<') op = C_LT;
        else if (t == TK_LE) op = C_LE;
        else if (t == '>' && !inprint) op = C_GT;
        else if (t == TK_GE) op = C_GE;
        else if (t == TK_EQ) op = C_EQ;
        else if (t == TK_NE) op = C_NE;
        else return n;
        tp++;
        n = node(N_CMP, n, pipeget());
        n->n = op;
    }
}

static Node *matchx(void)
{
    Node *n = rel();
    int t;
    while ((t = T) == '~' || t == TK_NOMATCH) {
        tp++;
        n = node(N_MATCH, n, rel());
        n->n = t == TK_NOMATCH;
    }
    return n;
}

static Node *inx(void)
{
    Node *n = matchx();
    while (T == TK_IN) {
        tp++;
        if (T != TK_NAME) err();
        tp++;
        n = node(N_IN, n, varnode(toks[tp - 1].s));
    }
    return n;
}

static Node *andx(void)
{
    Node *n = inx();
    while (T == TK_AND) {
        tp++;
        optnl();
        n = node(N_AND, n, inx());
    }
    return n;
}

static Node *orx(void)
{
    Node *n = andx();
    while (T == TK_OR) {
        tp++;
        optnl();
        n = node(N_OR, n, andx());
    }
    return n;
}

static Node *ternary(void)
{
    Node *n = orx(), *a;
    if (T != '?') return n;
    tp++;
    optnl();
    a = ternary();
    optnl();
    expect(':');
    optnl();
    n = node(N_COND, n, a);
    n->c = ternary();
    return n;
}

static int assignop(int t)
{
    switch (t) {
    case '=': return '=';
    case TK_ADDEQ: return N_ADD;
    case TK_SUBEQ: return N_SUB;
    case TK_MULEQ: return N_MUL;
    case TK_DIVEQ: return N_DIV;
    case TK_MODEQ: return N_MOD;
    case TK_POWEQ: return N_POW;
    }
    return 0;
}

static Node *expr(void)
{
    Node *n = ternary();
    int op = assignop(T);
    if (op && islvalue(n)) {
        tp++;
        optnl();
        n = node(N_ASSIGN, n, expr());
        n->n = op;
    }
    return n;
}

/* ---------------------------------------------------------------- statements */
static int endsstmt(int t)
{
    return t == ';' || t == TK_NL || t == '}' || t == TK_EOF;
}

static void endsimple(void)
{
    if (T == ';' || T == TK_NL) tp++;
    else if (T != '}' && T != TK_EOF) err();
}

static Node *printstmt(void)
{
    Node *n = node(toks[tp].t == TK_PRINT ? S_PRINT : S_PRINTF, 0, 0);
    int save, k, ok = 0;
    tp++;
    n->line = toks[tp - 1].line;
    if (T == '(') {                     /* print (list) [redirection] ? */
        save = tp;
        tp++;
        optnl();
        n->a = exprlist(&k);
        if (T == ')') {
            int t = toks[tp + 1].t;
            if (endsstmt(t) || t == '>' || t == TK_APPEND || t == '|') { tp++; ok = 1; }
        }
        if (!ok) tp = save;
    }
    if (!ok && !endsstmt(T) && T != '>' && T != TK_APPEND && T != '|') {
        inprint = 1;
        n->a = exprlist(&k);
        inprint = 0;
    } else if (!ok)
        n->a = 0;
    if (n->op == S_PRINTF && !n->a) err();
    if (T == '>' || T == TK_APPEND || T == '|') {
        n->n = T == '>' ? R_FILE : T == TK_APPEND ? R_APPEND : R_PIPE;
        tp++;
        inprint = 1;
        n->b = concat();
        inprint = 0;
    }
    return n;
}

static Node *simple(void)
{
    Node *n;
    int t = T;
    switch (t) {
    case TK_PRINT: case TK_PRINTF:
        return printstmt();
    case TK_DELETE:
        tp++;
        if (T != TK_NAME) err();
        tp++;
        n = node(S_DELETE, varnode(toks[tp - 1].s), 0);
        if (T == '[') n->b = subscripts();
        return n;
    case TK_EXIT:
        tp++;
        return node(S_EXIT, endsstmt(T) ? 0 : expr(), 0);
    case TK_RETURN:
        if (!infunc) synerr(tp, "return not in function");
        tp++;
        return node(S_RETURN, endsstmt(T) ? 0 : expr(), 0);
    case TK_NEXT: case TK_NEXTFILE:
        if (inbeginend) synerr(tp, t == TK_NEXT ? "next is illegal inside BEGIN or END" : "nextfile is illegal inside BEGIN or END");
        tp++;
        return node(t == TK_NEXT ? S_NEXT : S_NEXTFILE, 0, 0);
    case TK_BREAK: case TK_CONTINUE:
        if (!inloop) synerr(tp, t == TK_BREAK ? "break illegal outside of loops" : "continue illegal outside of loops");
        tp++;
        return node(t == TK_BREAK ? S_BREAK : S_CONTINUE, 0, 0);
    }
    return node(S_EXPR, expr(), 0);
}

static Node *stmtlist(void)
{
    Node *head = 0, **tail = &head;
    for (;;) {
        while (T == TK_NL || T == ';') tp++;
        if (T == '}' || T == TK_EOF) return head;
        *tail = stmt();
        tail = &(*tail)->next;
    }
}

static Node *body(void)
{
    Node *n;
    if (T == ';') { tp++; return node(S_BLOCK, 0, 0); }
    optnl();
    inloop++;
    n = stmt();
    inloop--;
    return n;
}

static Node *cond(void)
{
    Node *n;
    expect('(');
    optnl();
    n = expr();
    optnl();
    expect(')');
    return n;
}

static Node *stmt(void)
{
    Node *n;
    int save;
    switch (T) {
    case '{':
        tp++;
        n = node(S_BLOCK, stmtlist(), 0);
        expect('}');
        return n;
    case ';':
        tp++;
        return node(S_BLOCK, 0, 0);
    case TK_IF:
        tp++;
        n = node(S_IF, cond(), 0);
        if (T == ';') { tp++; n->b = node(S_BLOCK, 0, 0); }
        else { optnl(); n->b = stmt(); }
        save = tp;
        while (T == TK_NL || T == ';') tp++;
        if (T == TK_ELSE) {
            tp++;
            optnl();
            n->c = stmt();
        } else
            tp = save;
        return n;
    case TK_WHILE:
        tp++;
        n = node(S_WHILE, cond(), 0);
        n->b = body();
        return n;
    case TK_DO:
        tp++;
        optnl();
        inloop++;
        n = node(S_DO, 0, stmt());
        inloop--;
        while (T == TK_NL || T == ';') tp++;
        expect(TK_WHILE);
        n->a = cond();
        endsimple();
        return n;
    case TK_FOR:
        tp++;
        expect('(');
        if (T == TK_NAME && toks[tp + 1].t == TK_IN && toks[tp + 2].t == TK_NAME && toks[tp + 3].t == ')') {
            n = node(S_FORIN, varnode(toks[tp].s), varnode(toks[tp + 2].s));
            tp += 4;
            n->d = body();
            return n;
        }
        n = node(S_FOR, 0, 0);
        if (T != ';') n->a = simple();
        expect(';');
        optnl();
        if (T != ';') n->b = expr();
        expect(';');
        optnl();
        if (T != ')') n->c = simple();
        expect(')');
        n->d = body();
        return n;
    }
    n = simple();
    endsimple();
    return n;
}

/* ---------------------------------------------------------------- the program */
static void addrule(Rule **list, Node *p1, Node *p2, Node *action)
{
    Rule *r = emalloc(sizeof(Rule)), **t = list;
    r->pat1 = p1;
    r->pat2 = p2;
    r->action = action;
    r->inrange = 0;
    r->next = 0;
    while (*t) t = &(*t)->next;
    *t = r;
}

static void function(void)
{
    Func *f;
    int i;
    tp++;
    if (T != TK_NAME && T != TK_FUNCNAME) err();
    f = getfunc(toks[tp].s);
    if (f->body) synerr(tp, "function defined twice");
    for (i = 0; i < ngvar; i++)
        if (!strcmp(gname[i], f->name)) synerr(tp, "function name used as a variable");
    tp++;
    expect('(');
    while (T == TK_NAME) {
        f->param = erealloc(f->param, (f->nparam + 1) * sizeof(char *));
        f->param[f->nparam++] = toks[tp].s;
        tp++;
        if (T != ',') break;
        tp++;
        optnl();
    }
    expect(')');
    optnl();
    if (T != '{') err();
    curfn = f;
    infunc = 1;
    f->body = stmt();
    infunc = 0;
    curfn = 0;
}

void parse(void)
{
    Node *p1, *p2;
    int i, t;
    Func *f;
    tp = 0;
    for (;;) {
        while (T == TK_NL || T == ';') tp++;
        t = T;
        if (t == TK_EOF) break;
        if (t == TK_FUNCTION) {
            function();
        } else if (t == TK_BEGIN || t == TK_END) {
            tp++;
            optnl();
            if (T != '{') err();
            inbeginend = 1;
            addrule(t == TK_BEGIN ? &begins : &ends, 0, 0, stmt());
            inbeginend = 0;
        } else if (t == '{') {
            addrule(&rules, 0, 0, stmt());
        } else {
            p1 = expr();
            p2 = 0;
            if (T == ',') {
                tp++;
                optnl();
                p2 = expr();
            }
            addrule(&rules, p1, p2, T == '{' ? stmt() : 0);
        }
        if (!endsstmt(T) && T != '{' && toks[tp - 1].t != '}') err();
    }
    for (i = 0; i < nfuncs; i++) {
        f = funcs[i];
        if (!f->body) {
            fprintf(stderr, "awk: calling undefined function %s\n source line number %d\n", f->name, f->line);
            exit(2);
        }
    }
}
