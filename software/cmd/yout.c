/* yout.c -- yacc's output: the tables, compressed, and the parser that
 * reads them (y.tab.c), and the header (y.tab.h); yacc.h has the overview.
 *
 * The tables (the parser's comments say how it uses them):
 *   yytranslate  yylex's token number -> the symbol number (2: unknown)
 *   yyr1, yyr2   each rule's left side (a nonterminal, from 0) and length
 *   yydefact     each state's default reduction (0: an error)
 *   yydefgoto    each nonterminal's commonest goto
 *   yypact       each state's base in yytable/yycheck for its actions on
 *                terminals (YYNINF: none -- the default, without a token)
 *   yypgoto      each nonterminal's base there for its other gotos (by state)
 *   yytable      >0 shift to that state, <0 reduce by that rule, 0 an error;
 *                for a goto, the state
 *   yycheck      the terminal (or state) each entry of yytable belongs to
 * Each state's actions and each nonterminal's gotos are a sparse vector;
 * the vectors are packed into the one yytable at bases chosen first-fit
 * (the busiest vectors first), no two vectors at the same base, so an
 * entry whose yycheck matches is the vector's own. Equal vectors share. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "yacc.h"

#define NINF (-32767)

static FILE *cf;                        /* y.tab.c */
static int cline = 1;                   /* its next line */
static char *cname;

static void cput(char *s)
{
    char *q;
    for (q = s; *q; q++)
        if (*q == '\n') cline++;
    fwrite(s, 1, q - s, cf);
}

static void cprintf(char *fmt, ...)
{
    static char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    cput(buf);
}

/* back to y.tab.c's own line numbers after copied code */
static void cline_reset(void)
{
    if (!lflag) cprintf("#line %d \"%s\"\n", cline + 1, cname);
}

/* ---- packing ---- */

static int *vbase;                      /* per vector: its base, or NINF */
static int *table, *check, tcap, high;
static int *nf;                         /* union-find: the first free slot >= i */
static char *baseused;                  /* bases in use, offset by boff */
static int boff;
static int *defgoto;

static void growtable(int n)
{
    int c = tcap, i;
    if (n < tcap) return;
    while (tcap <= n) tcap = tcap ? tcap * 2 : 1024;
    table = xrealloc(table, tcap * sizeof *table);
    check = xrealloc(check, tcap * sizeof *check);
    nf = xrealloc(nf, (tcap + 1) * sizeof *nf);
    baseused = xrealloc(baseused, boff + tcap);
    if (!c) memset(baseused, 0, boff);
    for (i = c; i < tcap; i++) {
        table[i] = 0;
        check[i] = -1;
        nf[i] = i;
        baseused[boff + i] = 0;
    }
    nf[tcap] = tcap;
}

static int findfree(int i)
{
    int r, t;
    if (i >= tcap) growtable(i);
    for (r = i; nf[r] != r; r = nf[r]) ;
    while (nf[i] != r) { t = nf[i]; nf[i] = r; i = t; }
    if (r >= tcap) growtable(r);
    return r;
}

/* vector v's entries: idx[] and val[], n of them */
static int vlen;
static int *vidx, *vval;

static void vector(int v)
{
    int i, A;
    struct state *st;
    vlen = 0;
    if (v < nstates) {
        st = states[v];
        for (i = 0; i < st->nact; i++) {
            vidx[vlen] = st->asym[i];
            vval[vlen++] = st->aval[i];
        }
        return;
    }
    A = v - nstates;
    for (i = gotomap[A]; i < gotomap[A + 1]; i++)
        if (tost[i] != defgoto[A]) {
            vidx[vlen] = fromst[i];
            vval[vlen++] = tost[i];
        }
}

static int *order, *tally, *width;

/* first-fit: the lowest base where every entry is free */
static int place(void)
{
    int j, k, loc, ok;
    loc = findfree(vidx[0] - boff > 0 ? vidx[0] - boff : 0);
    for (;;) {
        j = loc - vidx[0];
        if (j + vidx[vlen - 1] >= tcap) growtable(j + vidx[vlen - 1]);
        if (!baseused[boff + j]) {
            for (ok = 1, k = 1; k < vlen; k++)
                if (check[j + vidx[k]] != -1) { ok = 0; break; }
            if (ok) break;
        }
        loc = findfree(loc + 1);
    }
    for (k = 0; k < vlen; k++) {
        loc = j + vidx[k];
        table[loc] = vval[k];
        check[loc] = vidx[k];
        nf[loc] = loc + 1;
        if (loc > high) high = loc;
    }
    baseused[boff + j] = 1;
    return j;
}

/* is vector v the same as w (already placed)? */
static int same(int v, int w)
{
    int i, *sidx = vidx, *sval = vval, n = vlen, r = 1, *ti, *tv;
    ti = xmalloc(n * sizeof *ti);
    tv = xmalloc(n * sizeof *tv);
    vidx = ti;
    vval = tv;
    vector(w);
    if (vlen != n) r = 0;
    for (i = 0; r && i < n; i++)
        if (ti[i] != sidx[i] || tv[i] != sval[i]) r = 0;
    vidx = sidx;
    vval = sval;
    vlen = n;
    free(ti);
    free(tv);
    (void)v;
    return r;
}

static void packtables(void)
{
    int nv = nstates + nnterms, i, j, k, v, A, max, *cnt, w, t;
    defgoto = xmalloc(nnterms * sizeof *defgoto);
    cnt = xmalloc(nstates * sizeof *cnt);
    for (A = 0; A < nnterms; A++) {
        for (i = gotomap[A]; i < gotomap[A + 1]; i++) cnt[tost[i]]++;
        defgoto[A] = 0;
        for (i = gotomap[A], max = 0; i < gotomap[A + 1]; i++)
            if (cnt[tost[i]] > max || (cnt[tost[i]] == max && tost[i] < defgoto[A])) {
                max = cnt[tost[i]];
                defgoto[A] = tost[i];
            }
        for (i = gotomap[A]; i < gotomap[A + 1]; i++) cnt[tost[i]] = 0;
    }
    free(cnt);
    vidx = xmalloc((nstates + ntokens + 1) * sizeof *vidx);
    vval = xmalloc((nstates + ntokens + 1) * sizeof *vval);
    vbase = xmalloc(nv * sizeof *vbase);
    order = xmalloc(nv * sizeof *order);
    tally = xmalloc(nv * sizeof *tally);
    width = xmalloc(nv * sizeof *width);
    /* the vectors with entries, the busiest and widest first */
    for (v = 0, k = 0; v < nv; v++) {
        vector(v);
        vbase[v] = NINF;
        if (!vlen) continue;
        t = tally[v] = vlen;
        w = width[v] = vidx[vlen - 1] - vidx[0] + 1;
        for (j = k; j > 0 && (tally[order[j - 1]] < t || (tally[order[j - 1]] == t && width[order[j - 1]] < w)); j--)
            order[j] = order[j - 1];
        order[j] = v;
        k++;
    }
    boff = nstates > ntokens ? nstates : ntokens;
    high = -1;
    growtable(1024);
    for (i = 0; i < k; i++) {
        v = order[i];
        vector(v);
        for (j = i - 1; j >= 0 && tally[order[j]] == tally[v]; j--)
            if (width[order[j]] == width[v] && same(v, order[j])) break;
        if (j >= 0 && tally[order[j]] == tally[v]) vbase[v] = vbase[order[j]];
        else vbase[v] = place();
    }
    if (high < 0) {                     /* (no entries at all) */
        high = 0;
    }
}

/* ---- writing ---- */

static void outarray(char *name, int *v, int n)
{
    int i, j, k, x, neg, min = 0, max = 0;
    char line[100], *q, d[12];
    for (i = 0; i < n; i++) {
        if (v[i] < min) min = v[i];
        if (v[i] > max) max = v[i];
    }
    cprintf("static const %s %s[] = {\n", min >= -32767 && max <= 32767 ? "short" : "int", name);
    for (i = 0; i < n; ) {
        q = line;
        for (k = 0; k < 10 && i < n; k++, i++) {
            x = v[i];                           /* (by hand: printf is slow on P32W) */
            neg = x < 0;
            if (neg) x = -x;
            d[11] = 0;
            for (j = 11; ; ) {
                d[--j] = '0' + x % 10;
                x /= 10;
                if (!x) break;
            }
            if (neg) d[--j] = '-';
            while (j > 5) d[--j] = ' ';
            strcpy(q, d + j);
            q += strlen(q);
            if (i < n - 1) *q++ = ',';
        }
        *q++ = '\n';
        *q = 0;
        cput(line);
    }
    cput("};\n");
}

/* a C identifier? (a token's #define) */
static int isname(char *s)
{
    if (!(*s == '_' || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return 0;
    for (; *s; s++)
        if (!(*s == '_' || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')))
            return 0;
    return 1;
}

static void defines(FILE *f, int toc)
{
    int i;
    for (i = 3; i < ntokens; i++)
        if (isname(symtab[i]->name)) {
            if (toc) cprintf("#define %s %d\n", symtab[i]->name, symtab[i]->value);
            else fprintf(f, "#define %s %d\n", symtab[i]->name, symtab[i]->value);
        }
}

static char *names[] = { "parse", "lex", "error", "lval", "char", "debug", "nerrs", 0 };

/* the parser (after the tables and before the actions; then after them) */
static char *skel1[] = {
"",
"/* the parser: a stack of states (yyss) and of their values (yyvs) */",
"#ifndef YYMAXDEPTH",
"#define YYMAXDEPTH 500",
"#endif",
"#define YYEMPTY (-2)",
"#define YYEOF 0",
"#define yyclearin (yychar = YYEMPTY)",
"#define yyerrok (yyerrflag = 0)",
"#define YYACCEPT goto yyacceptlab",
"#define YYABORT goto yyabortlab",
"#define YYERROR goto yyerrorlab",
"#define YYRECOVERING() (yyerrflag != 0)",
"",
"int yychar;                     /* the lookahead token (YYEMPTY: none) */",
"YYSTYPE yylval;                 /* its value, set by yylex */",
"int yynerrs;                    /* syntax errors reported */",
"#if YYDEBUG",
"#include <stdio.h>",
"int yydebug;                    /* nonzero: trace the parse on stderr */",
"#endif",
"",
"int yyparse(void)",
"{",
"    short yyss[YYMAXDEPTH], *yyssp;",
"    YYSTYPE yyvs[YYMAXDEPTH];",
"    YYSTYPE *yyvsp;                     /* (YYSTYPE may be a #define) */",
"    YYSTYPE yyval;",
"    int yystate, yyn, yylen, yytoken, yyerrflag;",
"",
"    yyerrflag = 0;",
"    yynerrs = 0;",
"    yychar = YYEMPTY;",
"    yytoken = 0;",
"    yylen = 0;",
"    yyssp = yyss;",
"    yyvsp = yyvs;",
"    yystate = 0;",
"    *yyssp = 0;",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Starting parse\\n\");",
"#endif",
"yynewstate:",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Entering state %d\\n\", yystate);",
"#endif",
"    yyn = yypact[yystate];",
"    if (yyn == YYNINF) goto yydefault;          /* no lookahead needed */",
"    if (yychar == YYEMPTY) {",
"#if YYDEBUG",
"        if (yydebug) fprintf(stderr, \"Reading a token: \");",
"#endif",
"        yychar = yylex();",
"    }",
"    if (yychar <= YYEOF) {",
"        yychar = YYEOF;",
"        yytoken = 0;",
"    } else yytoken = yychar <= YYMAXUTOK ? yytranslate[yychar] : 2;",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Next token is %s (%d)\\n\", yytname[yytoken], yychar);",
"#endif",
"    yyn += yytoken;",
"    if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yytoken) goto yydefault;",
"    yyn = yytable[yyn];",
"    if (yyn < 0) {",
"        yyn = -yyn;",
"        goto yyreduce;",
"    }",
"    if (yyn == 0) goto yyerrlab;",
"    if (yyn == YYFINAL) YYACCEPT;",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Shifting %s, to state %d\\n\", yytname[yytoken], yyn);",
"#endif",
"    if (yyerrflag) yyerrflag--;",
"    if (yychar != YYEOF) yychar = YYEMPTY;",
"    yystate = yyn;",
"yypush:",
"    if (yyssp >= yyss + YYMAXDEPTH - 1) goto yyoverflow;",
"    *++yyssp = yystate;",
"    *++yyvsp = yylval;",
"    goto yynewstate;",
"",
"yydefault:",
"    yyn = yydefact[yystate];",
"    if (yyn == 0) goto yyerrlab;",
"yyreduce:",
"    yylen = yyr2[yyn];",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Reducing by rule %d (line %d)\\n\", yyn, yyrline[yyn]);",
"#endif",
"    if (yylen) yyval = yyvsp[1 - yylen];        /* $$ = $1 */",
"    else yyval = yyvsp[0];",
"    switch (yyn) {",
0 };

static char *skel2[] = {
"    }",
"    yyssp -= yylen;",
"    yyvsp -= yylen;",
"    yyn = yyr1[yyn];",
"    yystate = yypgoto[yyn] + *yyssp;",
"    if (yystate >= 0 && yystate <= YYLAST && yycheck[yystate] == *yyssp)",
"        yystate = yytable[yystate];",
"    else yystate = yydefgoto[yyn];",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Going to state %d\\n\", yystate);",
"#endif",
"    if (yyssp >= yyss + YYMAXDEPTH - 1) goto yyoverflow;",
"    *++yyssp = yystate;",
"    *++yyvsp = yyval;",
"    goto yynewstate;",
"",
"yyerrlab:                       /* a syntax error */",
"    if (!yyerrflag) {",
"        ++yynerrs;",
"        yyerror(\"syntax error\");",
"    } else if (yyerrflag == 3) {    /* no recovery yet: lose the token */",
"        if (yychar == YYEOF) YYABORT;",
"#if YYDEBUG",
"        if (yydebug) fprintf(stderr, \"Error: discarding %s\\n\", yytname[yytoken]);",
"#endif",
"        yychar = YYEMPTY;",
"    }",
"    goto yyerrlab1;",
"yyerrorlab:                     /* YYERROR: no message */",
"    yyssp -= yylen;",
"    yyvsp -= yylen;",
"    yystate = *yyssp;",
"yyerrlab1:                      /* pop to a state that shifts error */",
"    yyerrflag = 3;",
"    for (;;) {",
"        yyn = yypact[yystate];",
"        if (yyn != YYNINF) {",
"            yyn += 1;",
"            if (yyn >= 0 && yyn <= YYLAST && yycheck[yyn] == 1) {",
"                yyn = yytable[yyn];",
"                if (yyn > 0) break;",
"            }",
"        }",
"        if (yyssp == yyss) YYABORT;",
"#if YYDEBUG",
"        if (yydebug) fprintf(stderr, \"Error: popping state %d\\n\", yystate);",
"#endif",
"        yyssp--;",
"        yyvsp--;",
"        yystate = *yyssp;",
"    }",
"    if (0) goto yyerrorlab;",
"#if YYDEBUG",
"    if (yydebug) fprintf(stderr, \"Shifting error, to state %d\\n\", yyn);",
"#endif",
"    yystate = yyn;",
"    goto yypush;",
"",
"yyoverflow:",
"    yyerror(\"yacc stack overflow\");",
"    return 2;",
"yyabortlab:",
"    return 1;",
"yyacceptlab:",
"    return 0;",
"}",
0 };

static void cstring(char *s)
{
    char b[8];
    cput("\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { b[0] = '\\'; b[1] = *s; b[2] = 0; }
        else { b[0] = *s; b[1] = 0; }
        cput(b);
    }
    cput("\"");
}

void output(FILE *c, FILE *h)
{
    int i, n, maxv, *v;
    char **sk;
    cf = c;
    cname = xmalloc(strlen(fileprefix) + 8);
    sprintf(cname, "%s.tab.c", fileprefix);
    packtables();
    cprintf("/* %s: the parser made by yacc from %s */\n", cname, infile);
    if (strcmp(symprefix, "yy"))
        for (i = 0; names[i]; i++) cprintf("#define yy%s %s%s\n", names[i], symprefix, names[i]);
    defines(0, 1);
    cput(prologue);
    if (*prologue) cline_reset();
    cput("#if !defined(YYSTYPE) && !defined(YYSTYPE_IS_DECLARED)\n");
    cput("typedef int YYSTYPE;\n#define YYSTYPE_IS_DECLARED 1\n#endif\n");
    cprintf("#ifndef YYDEBUG\n#define YYDEBUG %d\n#endif\n", tflag);
    /* the tables */
    for (i = 0, maxv = 0; i < ntokens; i++)
        if (i != 2 && symtab[i]->value > maxv) maxv = symtab[i]->value;
    cprintf("\n#define YYFINAL %d\n#define YYLAST %d\n#define YYNINF (%d)\n#define YYMAXUTOK %d\n",
            finalstate, high, NINF, maxv);
    n = maxv + 1;
    if (n < nstates + 1) n = nstates + 1;
    if (n < nrules) n = nrules;
    if (n < nnterms) n = nnterms;
    if (n < high + 1) n = high + 1;
    v = xmalloc(n * sizeof *v);
    for (i = 0; i <= maxv; i++) v[i] = 2;
    for (i = 0; i < ntokens; i++)
        if (i != 2) v[symtab[i]->value] = i;
    outarray("yytranslate", v, maxv + 1);
    for (i = 0; i < nrules; i++) v[i] = rules[i].lhs - ntokens;
    outarray("yyr1", v, nrules);
    for (i = 0; i < nrules; i++) v[i] = rules[i].len;
    outarray("yyr2", v, nrules);
    for (i = 0; i < nstates; i++) v[i] = states[i]->defrule;
    outarray("yydefact", v, nstates);
    outarray("yydefgoto", defgoto, nnterms);
    outarray("yypact", vbase, nstates);
    outarray("yypgoto", vbase + nstates, nnterms);
    outarray("yytable", table, high + 1);
    outarray("yycheck", check, high + 1);
    cput("#if YYDEBUG\nstatic const char *yytname[] = {");
    for (i = 0; i < nsyms; i++) {
        cput(i ? (i % 6 ? ", " : ",\n") : "\n");
        cstring(symtab[i]->name);
    }
    cput("\n};\n");
    for (i = 0; i < nrules; i++) v[i] = rules[i].line;
    outarray("yyrline", v, nrules);
    cput("#endif\n");
    free(v);
    /* the parser and the actions */
    for (sk = skel1; *sk; sk++) { cput(*sk); cput("\n"); }
    for (i = 1; i < nrules; i++)
        if (rules[i].act) {
            cprintf("    case %d:\n", i);
            cput(rules[i].act);
            cput("\n");
            cline_reset();
            cput("        break;\n");
        }
    for (sk = skel2; *sk; sk++) { cput(*sk); cput("\n"); }
    if (epilogue && *epilogue) {
        if (!lflag) cprintf("#line %d \"%s\"\n", epiline, infile);
        cput(epilogue);
    }
    /* y.tab.h */
    if (h) {
        fprintf(h, "/* %s.tab.h: the tokens of the parser made by yacc from %s */\n", fileprefix, infile);
        defines(h, 0);
        fprintf(h, "#if !defined(YYSTYPE) && !defined(YYSTYPE_IS_DECLARED)\n");
        if (haveunion) fprintf(h, "typedef union YYSTYPE %s YYSTYPE;\n", unionbody);
        else fprintf(h, "typedef int YYSTYPE;\n");
        fprintf(h, "#define YYSTYPE_IS_DECLARED 1\n#endif\nextern YYSTYPE %slval;\n", symprefix);
    }
}
