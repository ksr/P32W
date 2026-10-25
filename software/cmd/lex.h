/* lex.h -- the parts of lex: lex.c (the command, reading the specification),
 * lexre.c (patterns: syntax trees, character sets, the NFA), lexdfa.c (the
 * DFA: character classes, subset construction, minimisation) and lexout.c
 * (the compressed tables and the scanner, lex.yy.c). */

#define NCHAR 256

/* a set of characters: 256 bits */
typedef struct { unsigned w[8]; } Cset;
#define CS_HAS(s, c) ((s)->w[(c) >> 5] & (1u << ((c) & 31)))
#define CS_ADD(s, c) ((s)->w[(c) >> 5] |= 1u << ((c) & 31))

/* a pattern's syntax tree; min and max are the lengths it can match (max
 * -1: unbounded). Trees are never freed and may be shared ({name}). */
enum { R_SET, R_CAT, R_ALT, R_STAR, R_PLUS, R_OPT, R_REP, R_EMPTY };
typedef struct re Re;
struct re {
    int op;
    Re *a, *b;
    int set;                    /* R_SET: the character set's number */
    int n, m;                   /* R_REP: {n,m}, m -1 for {n,} */
    int min, max;
};

/* trailing context r/s: how yytext's length is found */
enum { TK_NONE, TK_BACK, TK_HEAD, TK_VAR };
#define MAXVAR 31               /* rules with both r and s of varying length */

typedef struct {
    Re *re, *trail;             /* r and s (s 0: no trailing context) */
    int bol;                    /* ^: only at the start of a line */
    char *sc;                   /* the start conditions it is active in (nsc flags) */
    char *action;               /* its code; 0 for |, the next rule's */
    int line;                   /* where it was, for messages */
    int tkind, tlen, tmin;      /* TK_BACK: s's length; TK_HEAD: r's; TK_VAR: the bit, s's least */
    int used;                   /* it can be matched (some DFA state accepts it first) */
} Rule;

/* NFA nodes (lexre.c) */
enum { N_EPS, N_SET, N_ACC, N_HEAD };

/* lex.c */
extern Rule *rules;
extern int nrules;
extern char **scname;
extern char *scexcl;
extern int nsc;
extern int caseless, reject, arraymode, noyywrap, genmain, nodefault, vflag;
extern int lineno;
extern char *filename;
void lexerr(char *fmt, char *a);
void lexwarn(char *fmt, char *a);
void *xalloc(int n);
void *xrealloc(void *p, int n);
char *xstrdup(char *s);
char *finddef(char *name);

/* lexre.c */
extern Cset *csets;
extern int ncset;
extern int nnfa;
extern int *nfa_op, *nfa_arg, *nfa_out1, *nfa_out2;
extern int *rulestart;
extern int nvar;
void re_rule(Rule *r, char *pat);
void re_nfa(void);

/* lexdfa.c */
extern int nclass, ndfa, ndfa0;
extern int ec[NCHAR];
extern int *dtrans;             /* ndfa x nclass: the next state, -1 none */
extern int *dacc, *dacclist;    /* state s accepts dacclist[dacc[s] .. dacc[s+1]) */
extern int *dhead;              /* the TK_VAR heads in each state (bits) */
extern int *dstart;             /* 2 * nsc start states: [2 * sc + at the start of a line] */
void dfa_build(void);

/* lexout.c */
void out_scanner(FILE *f, char *defcode, char *rulecode, char *usercode);
extern int ncomb;
