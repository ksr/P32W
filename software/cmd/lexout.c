/* lexout.c -- lex's output: the DFA's tables, compressed, and the scanner
 * that runs them (lex.yy.c).
 *
 * The transition table (states x classes) is stored by row displacement
 * with defaults: a state may name another state (its default) whose row
 * its own mostly repeats, and then keeps only the entries that differ; a
 * state without one keeps the entries that are not "no move". The kept
 * entries of all rows are laid over each other in one array, each row at
 * an offset (its base) where its entries fall on free places; each entry
 * holds the state it belongs to (the check) and the next state. The next
 * state from s on class c is then the entry at base(s) + c if its check
 * is s, else the same from s's default, and none without one. The
 * default chosen for a row is the state it moves to most often (an
 * identifier's state, for the states of keywords) or the state before
 * it, if either saves entries.
 *
 * The native cc spends most of its time on long initialisers (some 20 ms
 * of P32W time a number), so the tables are packed, two or four values to
 * an int, plainly enough:
 *   yy_ec[c / 4]   byte c % 4: the class of character c
 *   yy_bd[s]       base << 16 | default + 1 (0: none)
 *   yy_comb[i]     check + 1 << 16 | next + 1 (0: an empty place)
 *   yy_acc[s]      the rule s accepts + 1 (0: none); with REJECT, the
 *                  rules s accepts are yy_accl[yy_acc[s] .. yy_acc[s + 1])
 *
 * The scanner keeps the input in one buffer that grows as needed; the
 * DFA runs as far as it can, noting its state after each character, and
 * the longest prefix that ends in an accepting state is the match (the
 * earliest rule among those it accepts). REJECT goes on to the next rule
 * of that state and then to shorter prefixes. input(), unput() and
 * yyless() are written out only when the specification uses them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lex.h"

int ncomb;                      /* entries kept */
static int *cbase, *cdef, *cnxt, *cchk, ccap, csize;

/* make room for comb entries up to index n */
static void combroom(int n)
{
    int i, old = ccap;
    if (n < ccap) return;
    while (n >= ccap) ccap = ccap ? 2 * ccap : 1024;
    cnxt = xrealloc(cnxt, ccap * sizeof(int));
    cchk = xrealloc(cchk, ccap * sizeof(int));
    for (i = old; i < ccap; i++) {
        cnxt[i] = 0;
        cchk[i] = -1;
    }
}

/* would p as s's default make a loop? */
static int loops(int p, int s)
{
    for (; p >= 0; p = cdef[p])
        if (p == s) return 1;
    return 0;
}

static void compress(void)
{
    int *count = xalloc((ndfa + 1) * sizeof(int));
    int *ent = xalloc((nclass + 1) * sizeof(int));
    int s, k, p, b, j, n, best, bestcost, cost, cand[2], ci, most, *row, *prow;
    int lowfree = 0;
    cbase = xalloc((ndfa + 1) * sizeof(int));
    cdef = xalloc((ndfa + 1) * sizeof(int));
    for (s = 0; s < ndfa; s++) cdef[s] = -1;
    combroom(nclass);
    for (s = 0; s < ndfa; s++) {
        row = dtrans + s * nclass;
        /* the candidates: the most frequent target, and the state before */
        most = -1;
        for (k = 0; k < nclass; k++)
            if (row[k] >= 0 && row[k] != s && ++count[row[k]] > (most < 0 ? 0 : count[most])) most = row[k];
        for (k = 0; k < nclass; k++)
            if (row[k] >= 0) count[row[k]] = 0;
        cand[0] = most;
        cand[1] = s - 1;
        best = -1;
        bestcost = 0;
        for (k = 0; k < nclass; k++)
            if (row[k] >= 0) bestcost++;
        for (ci = 0; ci < 2; ci++) {
            p = cand[ci];
            if (p < 0 || p == s || loops(p, s)) continue;
            prow = dtrans + p * nclass;
            cost = 0;
            for (k = 0; k < nclass && cost < bestcost; k++)
                if (row[k] != prow[k]) cost++;
            if (cost < bestcost) {
                best = p;
                bestcost = cost;
            }
        }
        cdef[s] = best;
        n = 0;
        prow = best >= 0 ? dtrans + best * nclass : 0;
        for (k = 0; k < nclass; k++)
            if (prow ? row[k] != prow[k] : row[k] >= 0) ent[n++] = k;
        ncomb += n;
        if (n == 0) {
            cbase[s] = 0;
            continue;
        }
        /* the first offset where its entries fit */
        while (lowfree < ccap && cchk[lowfree] >= 0) lowfree++;
        for (b = lowfree - ent[0] > 0 ? lowfree - ent[0] : 0;; b++) {
            combroom(b + nclass);
            for (j = 0; j < n; j++)
                if (cchk[b + ent[j]] >= 0) break;
            if (j == n) break;
        }
        cbase[s] = b;
        for (j = 0; j < n; j++) {
            cchk[b + ent[j]] = s;
            cnxt[b + ent[j]] = row[ent[j]];
        }
        if (b + nclass > csize) csize = b + nclass;
    }
    if (csize == 0) csize = nclass;
    free(count);
    free(ent);
}

static void outs(FILE *f, char *s)
{
    fwrite(s, 1, strlen(s), f);
}

/* the values of a table, twelve to a line ("%d" through printf is slow) */
static void values(FILE *f, int *v, int n)
{
    char line[200], *p;
    int i, x, k;
    char digits[12];
    p = line;
    for (i = 0; i < n; i++) {
        if (i % 12 == 0) {
            strcpy(line, "   ");
            p = line + 3;
        }
        *p++ = ' ';
        x = v[i];
        if (x < 0) {
            *p++ = '-';
            x = -x;
        }
        k = 0;
        do {
            digits[k++] = '0' + x % 10;
            x /= 10;
        } while (x);
        while (k > 0) *p++ = digits[--k];
        if (i < n - 1) *p++ = ',';
        if (i % 12 == 11 || i == n - 1) {
            *p++ = '\n';
            *p = 0;
            outs(f, line);
        }
    }
    if (n == 0) outs(f, "    0\n");
}

/* static short name[n] = { ... }; (int if a value needs it) */
static void table(FILE *f, char *name, int *v, int n, int wide)
{
    int i;
    for (i = 0; i < n; i++)
        if (v[i] < -32768 || v[i] > 32767) wide = 1;
    fprintf(f, "static %s %s[%d] = {\n", wide ? "int" : "short", name, n > 0 ? n : 1);
    values(f, v, n);
    outs(f, "};\n");
}

/* The scanner. A line's first character says when it is written:
 * . always, A with %array, P with %pointer, T with trailing context, V
 * with variable trailing context, R with REJECT (r without), I U L when
 * input(), unput(), yyless() are used, W when the user gives
 * yywrap, w without (noyywrap), D with the default rule, d with
 * nodefault, M with %option main; % lines are where the parts go. */
static char *skel[] = {
".#include <stdio.h>",
".#include <stdlib.h>",
".#include <string.h>",
".",
".int isatty(int fd);              /* (<unistd.h> costs the native cc time) */",
"Aextern char yytext[];",
"Pchar *yytext;",
".int yyleng;",
".int yylineno = 1;",
".FILE *yyin, *yyout;",
"Wint yywrap(void);",
"w#define yywrap() 1",
"Iint yyinput(void);",
"Uvoid yyunput(int c);",
".void yyrestart(FILE *f);",
"Lvoid yy_less(int n);",
".static int yy_start, yy_more_flag;",
".#define BEGIN yy_start =",
".#define YY_START yy_start",
".#define YYSTATE yy_start",
".#define yymore() (yy_more_flag = 1)",
".#define yyless(n) yy_less(n)",
"R#define REJECT goto yy_reject",
".#define yyterminate() return 0",
".#define input() yyinput()",
".#define unput(c) yyunput(c)",
".#define output(c) putc(c, yyout)",
".#define YY_NULL 0",
"%sc",
"%defcode",
".#ifndef YY_DECL",
".#define YY_DECL int yylex(void)",
".#endif",
".#ifndef ECHO",
".#define ECHO fwrite(yytext, 1, yyleng, yyout)",
".#endif",
".#ifndef YY_INPUT",
".#define YY_INPUT(buf, result, max) ((result) = yy_read(buf, max))",
".#endif",
".#ifndef YY_USER_ACTION",
".#define YY_USER_ACTION",
".#endif",
".#ifndef YY_BREAK",
".#define YY_BREAK break;",
".#endif",
".#ifndef YY_FATAL_ERROR",
".#define YY_FATAL_ERROR(msg) yy_fatal(msg)",
".#endif",
"A#ifndef YYLMAX",
"A#define YYLMAX 8192",
"A#endif",
"Achar yytext[YYLMAX];",
"%tables",
".",
".static char *yy_buf;            /* the input read: yy_buf[0 .. yy_n) */",
".static int *yy_sbuf;            /* the states along a match: [i] after i characters */",
".static int yy_size, yy_n;",
".static int yy_tok;              /* where yytext starts in yy_buf */",
".static int yy_cp;               /* the first character not yet matched */",
".static int yy_holdat = -1;      /* yytext's NUL is at yy_buf[yy_holdat], */",
".static char yy_holdc;           /* in place of yy_holdc */",
".static int yy_bol = 1;          /* at the start of a line */",
".static int yy_eof, yy_init;",
".static int yy_tty = -1;         /* yyin is a terminal (-1: not known yet) */",
".",
".static void yy_fatal(char *msg)",
".{",
".    fprintf(stderr, \"%s\\n\", msg);",
".    exit(2);",
".}",
".",
"./* the standard YY_INPUT: from yyin, up to a line at a time from a terminal */",
".static int yy_read(char *buf, int max)",
".{",
".    int n = 0, c;",
".    if (yy_tty < 0) yy_tty = isatty(fileno(yyin));",
".    if (!yy_tty) {",
".        n = fread(buf, 1, max, yyin);",
".        if (n == 0 && ferror(yyin)) YY_FATAL_ERROR(\"lex scanner: input error\");",
".        return n;",
".    }",
".    while (n < max && (c = getc(yyin)) != EOF) {",
".        buf[n++] = c;",
".        if (c == '\\n') break;",
".    }",
".    if (n == 0 && ferror(yyin)) YY_FATAL_ERROR(\"lex scanner: input error\");",
".    return n;",
".}",
".",
".static void yy_unhold(void)",
".{",
".    if (yy_holdat >= 0) {",
".        yy_buf[yy_holdat] = yy_holdc;",
".        yy_holdat = -1;",
".    }",
".}",
".",
"./* yytext: the yyleng characters at yy_tok, with a NUL after them */",
".static void yy_settext(void)",
".{",
".    yy_unhold();",
".    yy_holdat = yy_tok + yyleng;",
".    yy_holdc = yy_buf[yy_holdat];",
".    yy_buf[yy_holdat] = 0;",
"P    yytext = yy_buf + yy_tok;",
"A    if (yyleng >= YYLMAX) YY_FATAL_ERROR(\"lex scanner: token longer than YYLMAX\");",
"A    memcpy(yytext, yy_buf + yy_tok, yyleng + 1);",
".}",
".",
"./* room for k more characters and a NUL: drop what is before yytext, or grow */",
".static void yy_room(int k)",
".{",
".    if (yy_n + k + 1 <= yy_size) return;",
".    if (yy_tok > 0) {",
".        memmove(yy_buf, yy_buf + yy_tok, yy_n - yy_tok);",
".        yy_n -= yy_tok;",
".        yy_cp -= yy_tok;",
".        if (yy_holdat >= 0) yy_holdat -= yy_tok;",
".        yy_tok = 0;",
".    }",
".    while (yy_n + k + 1 > yy_size) {",
".        yy_size = yy_size ? 2 * yy_size : 1024;",
".        yy_buf = (char *) realloc(yy_buf, yy_size);",
".        yy_sbuf = (int *) realloc(yy_sbuf, (yy_size + 1) * sizeof(int));",
".        if (!yy_buf || !yy_sbuf) YY_FATAL_ERROR(\"lex scanner: out of memory\");",
".    }",
".}",
".",
"./* more input: 0 at the end */",
".static int yy_fill(void)",
".{",
".    int r;",
".    if (yy_eof) return 0;",
".    yy_room(256);",
".    YY_INPUT((yy_buf + yy_n), r, (yy_size - yy_n - 1));",
".    if (r <= 0) {",
".        yy_eof = 1;",
".        return 0;",
".    }",
".    yy_n += r;",
".    return 1;",
".}",
".",
".static void yy_setup(void)",
".{",
".    if (yy_init) return;",
".    yy_init = 1;",
".    if (!yyin) yyin = stdin;",
".    if (!yyout) yyout = stdout;",
".    yy_room(1024);",
".}",
".",
".void yyrestart(FILE *f)",
".{",
".    yy_setup();",
".    yy_unhold();",
".    yyin = f;",
".    yy_tty = -1;",
".    yy_n = yy_cp = yy_tok = 0;",
".    yy_eof = 0;",
".    yy_bol = 1;",
".}",
".",
"I/* input(): the next character, taken from the input (EOF at the end) */",
"Iint yyinput(void)",
"I{",
"I    int c;",
"I    yy_setup();",
"I    yy_unhold();",
"I    while (yy_cp >= yy_n && !yy_fill()) {",
"I        if (yywrap()) {",
"I            yy_settext();",
"I            return EOF;",
"I        }",
"I        yy_eof = 0;",
"I        yy_tty = -1;",
"I    }",
"I    c = yy_buf[yy_cp++] & 255;",
"I    if (c == '\\n') yylineno++;",
"I    yy_bol = c == '\\n';",
"I    yy_settext();",
"I    return c;",
"I}",
"I",
"U/* unput(c): c is the next character of the input; yytext is kept */",
"Uvoid yyunput(int c)",
"U{",
"U    int k;",
"U    yy_setup();",
"U    yy_unhold();",
"U    if (yy_tok == 0) {",
"U        k = yy_size / 4 + 1;",
"U        yy_room(k);",
"U        memmove(yy_buf + k, yy_buf, yy_n);",
"U        yy_n += k;",
"U        yy_cp += k;",
"U        yy_tok += k;",
"U    }",
"U    memmove(yy_buf + yy_tok - 1, yy_buf + yy_tok, yy_cp - yy_tok);",
"U    yy_tok--;",
"U    yy_buf[--yy_cp] = c;",
"U    if (c == '\\n') yylineno--;",
"U    yy_settext();",
"U}",
"U",
"L/* yyless(n): keep n characters of yytext, give back the rest */",
"Lvoid yy_less(int n)",
"L{",
"L    int i;",
"L    yy_unhold();",
"L    for (i = yy_tok + n; i < yy_cp; i++)",
"L        if (yy_buf[i] == '\\n') yylineno--;",
"L    yy_cp = yy_tok + n;",
"L    yyleng = n;",
"L    yy_settext();",
"L}",
"L",
".YY_DECL",
".{",
".    int yy_st, yy_c, yy_i, yy_v, yy_l, yy_m, yy_rule, *yy_sp;",
"R    int yy_ai, yy_off;",
".    char *yy_bp, *yy_ep;",
"V    int yy_p;",
"%rulecode",
".    yy_setup();",
".    for (;;) {",
".        yy_unhold();",
".        if (yy_more_flag) yy_more_flag = 0;",
".        else yy_tok = yy_cp;",
"R        yy_off = yy_cp - yy_tok;",
".        yy_st = yy_start_states[2 * yy_start + yy_bol];",
".        yy_sp = yy_sbuf;",
".        *yy_sp = yy_st;",
".        yy_bp = yy_buf + yy_cp;",
".        yy_ep = yy_buf + yy_n;",
".        for (;;) {              /* run the DFA as far as it goes */",
".            if (yy_bp == yy_ep) {",
".                yy_l = yy_bp - yy_buf - yy_cp;",
".                yy_c = yy_fill();       /* (the buffers may move) */",
".                yy_bp = yy_buf + yy_cp + yy_l;",
".                yy_ep = yy_buf + yy_n;",
".                yy_sp = yy_sbuf + yy_l;",
".                if (!yy_c) break;",
".            }",
".            yy_c = *yy_bp & 255;",
".            yy_c = yy_ec[yy_c >> 2] >> ((yy_c & 3) << 3) & 255;",
".            for (;;) {          /* (the tables are explained above) */",
".                yy_i = yy_bd[yy_st];",
".                yy_v = yy_comb[(yy_i >> 16) + yy_c];",
".                if ((yy_v >> 16) == yy_st + 1) {",
".                    yy_st = (yy_v & 65535) - 1;",
".                    break;",
".                }",
".                if ((yy_st = (yy_i & 65535) - 1) < 0) break;",
".            }",
".            if (yy_st < 0) break;",
".            yy_bp++;",
".            *++yy_sp = yy_st;",
".        }",
".        yy_l = yy_bp - yy_buf - yy_cp;",
".        if (yy_l == 0 && yy_cp >= yy_n) {      /* the end of the input */",
".            if (yywrap()) return 0;",
".            yy_eof = 0;",
".            yy_tty = -1;",
".            yy_bol = 1;",
".            continue;",
".        }",
".        yy_m = yy_l;           /* the longest match */",
"r        while (yy_m > 0 && !yy_acc[yy_sbuf[yy_m]]) yy_m--;",
"r        yy_rule = yy_m > 0 ? yy_acc[yy_sbuf[yy_m]] : 0;",
"R        while (yy_m > 0 && yy_acc[yy_sbuf[yy_m]] == yy_acc[yy_sbuf[yy_m] + 1]) yy_m--;",
"R        yy_ai = yy_acc[yy_sbuf[yy_m]];",
"R    yy_found:",
"R        yy_rule = yy_m > 0 ? yy_accl[yy_ai] : 0;",
".        if (yy_m == 0) yy_m = 1;       /* no rule: the default takes a character */",
".        yy_l = yy_m;",
"T        switch (yy_tk[yy_rule]) {      /* trailing context: what yytext keeps */",
"T        case 1:",
"T            yy_l = yy_m - yy_tl[yy_rule];",
"T            break;",
"T        case 2:",
"T            yy_l = yy_tl[yy_rule];",
"T            break;",
"V        case 3:",
"V            for (yy_p = yy_m - (yy_ts[yy_rule] > 0 ? yy_ts[yy_rule] : 1); yy_p > 0; yy_p--)",
"V                if (yy_head[yy_sbuf[yy_p]] & (1 << yy_tl[yy_rule])) break;",
"V            if (yy_p > 0) yy_l = yy_p;",
"V            break;",
"T        }",
"T        if (yy_l == 0) {       /* r of r/s matched nothing: take the default */",
"T            yy_l = 1;",
"T            yy_rule = 0;",
"T        }",
".        for (yy_bp = yy_buf + yy_cp, yy_ep = yy_bp + yy_l; yy_bp < yy_ep; yy_bp++)",
".            if (*yy_bp == '\\n') yylineno++;",
".        yy_cp += yy_l;",
".        yyleng = yy_cp - yy_tok;",
".        yy_settext();",
".        if (yyleng > 0) yy_bol = yy_buf[yy_cp - 1] == '\\n';",
".        YY_USER_ACTION",
".        switch (yy_rule) {",
"%actions",
".        default:",
"D            ECHO;",
"d            YY_FATAL_ERROR(\"lex scanner jammed\");",
".            YY_BREAK",
".        }",
".        continue;",
"R    yy_reject:                 /* the next rule this state accepts, or a shorter match */",
"R        yy_unhold();",
"R        for (yy_i = yy_tok + yy_off; yy_i < yy_cp; yy_i++)",
"R            if (yy_buf[yy_i] == '\\n') yylineno--;",
"R        yy_cp = yy_tok + yy_off;",
"R        yy_ai++;",
"R        while (yy_m > 0 && yy_ai >= yy_acc[yy_sbuf[yy_m] + 1])",
"R            if (--yy_m > 0) yy_ai = yy_acc[yy_sbuf[yy_m]];",
"R        goto yy_found;",
".    }",
".}",
"M",
"Mint main(void)",
"M{",
"M    while (yylex() != 0)",
"M        ;",
"M    return 0;",
"M}",
0
};

static void puttext(FILE *f, char *s)
{
    if (!s) return;
    outs(f, s);
    if (*s && s[strlen(s) - 1] != '\n') putc('\n', f);
}

static void tables(FILE *f)
{
    int i, j, *v, nv, ntrail = 0;
    nv = csize + 1;                     /* the longest list v holds */
    if (ndfa + 1 > nv) nv = ndfa + 1;
    if (dacc[ndfa] > nv) nv = dacc[ndfa];
    if (nrules + 1 > nv) nv = nrules + 1;
    if (nv < NCHAR) nv = NCHAR;
    v = xalloc(nv * sizeof(int));
    if (ndfa > 32000 || csize > 32000) lexerr("the automaton is too big (over 32000 states or entries)", 0);
    fprintf(f, "\n/* the tables: %d states, %d character classes, %d transitions kept */\n",
            ndfa, nclass, ncomb);
    for (i = 0; i < NCHAR / 4; i++) {
        v[i] = 0;
        for (j = 3; j >= 0; j--) v[i] = (int) ((unsigned) v[i] << 8 | ec[4 * i + j]);
    }
    table(f, "yy_ec", v, NCHAR / 4, 1);
    for (i = 0; i < ndfa; i++) v[i] = cbase[i] << 16 | (cdef[i] + 1);
    table(f, "yy_bd", v, ndfa, 1);
    for (i = 0; i < csize; i++) v[i] = cchk[i] < 0 ? 0 : (cchk[i] + 1) << 16 | (cnxt[i] + 1);
    table(f, "yy_comb", v, csize, 1);
    if (reject) {
        for (i = 0; i <= ndfa; i++) v[i] = dacc[i];
        table(f, "yy_acc", v, ndfa + 1, 0);
        for (i = 0; i < dacc[ndfa]; i++) v[i] = dacclist[i] + 1;
        table(f, "yy_accl", v, dacc[ndfa], 0);
    } else {
        for (i = 0; i < ndfa; i++) v[i] = dacc[i] < dacc[i + 1] ? dacclist[dacc[i]] + 1 : 0;
        table(f, "yy_acc", v, ndfa, 0);
    }
    table(f, "yy_start_states", dstart, 2 * nsc, 0);
    for (i = 0; i < nrules; i++)
        if (rules[i].tkind != TK_NONE) ntrail++;
    if (ntrail) {
        nv = nrules + 1;
        v[0] = 0;
        for (i = 0; i < nrules; i++) v[i + 1] = rules[i].tkind;
        table(f, "yy_tk", v, nv, 0);
        for (i = 0; i < nrules; i++) v[i + 1] = rules[i].tlen;
        table(f, "yy_tl", v, nv, 0);
        if (nvar) {
            for (i = 0; i < nrules; i++) v[i + 1] = rules[i].tmin;
            table(f, "yy_ts", v, nv, 0);
            table(f, "yy_head", dhead, ndfa, 1);
        }
    }
    free(v);
}

/* does the word w (not part of a longer name) appear in s? */
static int hasword(char *s, char *w)
{
    char *p = s;
    int n = strlen(w);
    if (!s) return 0;
    while ((p = strstr(p, w)) != 0) {
        if ((p == s || !(isalnum(p[-1]) || p[-1] == '_')) && !(isalnum(p[n]) || p[n] == '_'))
            return 1;
        p += n;
    }
    return 0;
}

/* is a word used in the code of the specification? */
static int used(char *w, char *defcode, char *rulecode, char *usercode)
{
    int i;
    if (hasword(defcode, w) || hasword(rulecode, w) || hasword(usercode, w)) return 1;
    for (i = 0; i < nrules; i++)
        if (hasword(rules[i].action, w)) return 1;
    return 0;
}

static void actions(FILE *f)
{
    int i;
    for (i = 0; i < nrules; i++) {
        fprintf(f, "        case %d:\n", i + 1);
        if (!rules[i].action) {
            if (i == nrules - 1) {
                lineno = rules[i].line;
                lexerr("the last rule's action is |", 0);
            }
            continue;
        }
        outs(f, "            ");
        outs(f, rules[i].action);
        outs(f, "\n            YY_BREAK\n");
    }
}

void out_scanner(FILE *f, char *defcode, char *rulecode, char *usercode)
{
    int i, ntrail = 0, uinput, uunput, uless;
    char *l, t;
    compress();
    uinput = used("input", defcode, rulecode, usercode) || used("yyinput", defcode, rulecode, usercode);
    uunput = used("unput", defcode, rulecode, usercode) || used("yyunput", defcode, rulecode, usercode);
    uless = used("yyless", defcode, rulecode, usercode);
    for (i = 0; i < nrules; i++)
        if (rules[i].tkind != TK_NONE) ntrail++;
    fprintf(f, "/* lex.yy.c -- a scanner made by lex from %s */\n", filename);
    for (i = 0; (l = skel[i]) != 0; i++) {
        t = *l++;
        if (t == '%') {
            if (!strcmp(l, "sc")) {
                int k;
                for (k = 0; k < nsc; k++) fprintf(f, "#define %s %d\n", scname[k], k);
            } else if (!strcmp(l, "defcode"))
                puttext(f, defcode);
            else if (!strcmp(l, "tables"))
                tables(f);
            else if (!strcmp(l, "rulecode"))
                puttext(f, rulecode);
            else if (!strcmp(l, "actions"))
                actions(f);
            continue;
        }
        if ((t == 'A' && !arraymode) || (t == 'P' && arraymode) || (t == 'T' && !ntrail) ||
            (t == 'V' && !nvar) || (t == 'R' && !reject) || (t == 'W' && noyywrap) ||
            (t == 'w' && !noyywrap) || (t == 'D' && nodefault) || (t == 'd' && !nodefault) ||
            (t == 'M' && !genmain) || (t == 'r' && reject) || (t == 'I' && !uinput) ||
            (t == 'U' && !uunput) || (t == 'L' && !uless))
            continue;
        outs(f, l);
        putc('\n', f);
    }
    puttext(f, usercode);
}
