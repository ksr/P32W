/* yacc.h -- the parser generator's shared declarations. yacc is three files
 * so that each fits the native cc's memory:
 *   yacc.c   the options, reading the grammar, its actions
 *   ylr.c    the LR(0) automaton, LALR(1) lookaheads (DeRemer and Pennello),
 *            conflicts and each state's actions, y.output
 *   yout.c   compressing the tables, writing y.tab.c and y.tab.h
 *
 * The grammar, once read, is in arrays of numbers:
 *   symbols  0 .. ntokens-1 are the terminals: 0 $end, 1 error,
 *            2 $undefined (any token number yylex returns that the grammar
 *            does not know), then the grammar's tokens; ntokens .. nsyms-1
 *            the nonterminals, the first ($accept) the augmented start.
 *   rules    0 is $accept : start $end; then the grammar's, in order (a
 *            mid-rule action's empty rule just before the rule it is in).
 *   ritem    every rule's right side, each followed by -1-r (r its rule):
 *            an item (a dotted rule) is an index into ritem, the symbol
 *            after the dot ritem[i] (< 0: the dot is at the end). */
#ifndef YACC_H
#define YACC_H

#define S_UNKNOWN 0             /* struct sym class: used, not yet known */
#define S_TOKEN   1
#define S_NTERM   2

#define A_NONE    0             /* associativity (prec 0: none) */
#define A_LEFT    1
#define A_RIGHT   2
#define A_NONASSOC 3

struct sym {
    char *name;                 /* as written ('+' for a character token) */
    int class;                  /* S_UNKNOWN, S_TOKEN, S_NTERM */
    int value;                  /* a token's number (-1: not yet given) */
    int prec, assoc;            /* %left %right %nonassoc: level from 1 */
    int tag;                    /* its <type>: index in tags[] + 1, or 0 */
    int num;                    /* its symbol number, once numbered */
    int line;                   /* where first seen */
    int isrule;                 /* has rules (a nonterminal) */
    struct sym *next;           /* hash chain */
};

struct rule {
    int lhs;                    /* symbol number */
    int rhs;                    /* its first item in ritem */
    int len;                    /* symbols on the right */
    int prec, assoc;            /* from its last token or %prec */
    char *act;                  /* the action's C code, translated, or 0 */
    int line;                   /* where the rule (or its action) starts */
    int used;                   /* reduced in some state */
};

struct state {
    int nk, *kern;              /* the kernel items, ascending */
    int sym;                    /* the symbol by which it is reached */
    int nt, *trans;             /* transitions: target states, by symbol */
    int nr, *red;               /* reductions: rules, ascending */
    int la;                     /* the lookahead set of red[0] (LA[la + k]) */
    int nact;                   /* actions on terminals other than the default */
    short *asym, *aval;         /* the terminal; >0 shift, <0 reduce, 0 error */
    int defrule;                /* default reduction (0: none, an error) */
    struct state *hnext;        /* hash chain */
};

/* yacc.c */
extern char *infile;            /* the grammar's file name */
extern int lflag, tflag, vflag, dflag;
extern char *fileprefix, *symprefix;
extern struct sym **symtab;     /* by symbol number */
extern int nsyms, ntokens, nnterms;
extern struct rule *rules;
extern int nrules;
extern int *ritem, nitems;
extern char **tags;
extern int ntags;
extern char *prologue;          /* %{ %} code and the %union typedef, in order */
extern int haveunion;
extern char *unionbody;         /* "{ ... }" of %union, for y.tab.h */
extern int unionline;
extern char *epilogue;          /* after the second %% */
extern int epiline;
extern int start;               /* the start symbol's number */
void *xmalloc(int n);
void *xrealloc(void *p, int n);
char *xstrdup(char *s);
void fatal(char *fmt, char *a);
void warn(char *fmt, char *a);

/* ylr.c */
extern struct state **states;
extern int nstates;
extern int finalstate;          /* entered by shifting $end: accept */
extern int tsize;               /* words in a set of terminals */
extern unsigned *LA;            /* lookahead sets, tsize words each */
extern int nsr, nrr;            /* unresolved conflicts */
extern int nunused;             /* rules never reduced */
extern FILE *vout;              /* y.output, or 0 */
extern int ngotos;
extern int *gotomap, *fromst, *tost;
void lr0(void);
void lalr(void);
void actions(void);

/* yout.c */
void output(FILE *c, FILE *h);

#endif
