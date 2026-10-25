/* awk.h -- the awk interpreter's shared declarations. awk is eight source
 * files so that each fits the native cc's memory on P32W:
 *
 *   awk.c       main: options, the program text, strings, errors
 *   awklex.c    the lexer: the program as an array of tokens
 *   awkparse.c  the parser: tokens to a syntax tree of Nodes
 *   awkrun.c    the interpreter: statements, expressions, functions
 *   awkvar.c    values (Cells), variables, arrays, fields, input, files
 *   awkfn.c     the built-in functions, printf, sub and gsub
 *   awknum.c    numbers: integers, softfp doubles, number <-> text
 *   awkre.c     regular expressions (POSIX ERE, awk's escapes)
 *
 * Numbers: P32W has no floating point, so a number (Num) is a 32-bit int
 * while it is a whole number that fits, and an IEEE double (softfp's fp64)
 * otherwise; every operation tries the int first. */
#ifndef AWK_H
#define AWK_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/wait.h>
#include <softfp.h>

/* ---------------------------------------------------------------- values */
typedef struct { int isint; int i; fp64 f; } Num;      /* f only when !isint */

/* a value: a variable, a field, an array element or an intermediate result */
enum {
    T_UNINIT,           /* never set: "" and 0 at once */
    T_NUM,              /* a number (n) */
    T_STR,              /* a string (s) */
    T_STRNUM,           /* input that looks like a number: s, and n */
    T_MAYBE,            /* input not yet looked at: becomes T_STRNUM or T_STR */
    T_ARR,              /* an array (p) */
    T_REF               /* a function's array parameter: the caller's Cell (p) */
};
typedef struct Cell Cell;
struct Cell {
    char t;
    char sv;            /* s: 0 none, 1 owned (sfree it), 2 borrowed (a field's text),
                           3 a constant's (kept for good) */
    char nv;            /* n valid (a T_STR's number, worked out once) */
    char pad;
    Num n;
    char *s;
    void *p;
};

/* (cinit, inline: what cfree and eval need of an empty Cell) */
#define CINIT(c) ((c)->t = T_UNINIT, (c)->sv = 0, (c)->nv = 0)

typedef struct Elem Elem;
struct Elem { Elem *next; Cell v; char *key; };
typedef struct Array { int n, size; Elem **tab; } Array;

/* a growing string */
typedef struct { char *s; int len, cap; } Buf;

/* ---------------------------------------------------------------- tokens */
enum {
    TK_EOF = 256, TK_NL, TK_NUMBER, TK_STRING, TK_ERE, TK_NAME, TK_FUNCNAME, TK_BUILTIN,
    TK_BEGIN, TK_END, TK_FUNCTION, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_DO, TK_BREAK,
    TK_CONTINUE, TK_NEXT, TK_NEXTFILE, TK_EXIT, TK_RETURN, TK_DELETE, TK_GETLINE, TK_PRINT,
    TK_PRINTF, TK_IN, TK_AND, TK_OR, TK_EQ, TK_NE, TK_LE, TK_GE, TK_APPEND, TK_NOMATCH,
    TK_INCR, TK_DECR, TK_POW, TK_ADDEQ, TK_SUBEQ, TK_MULEQ, TK_DIVEQ, TK_MODEQ, TK_POWEQ
};
typedef struct { int t, line, off, end, n; char *s; } Tok;     /* off, end: in the source text; n: a built-in */

/* built-in functions */
enum {
    B_LENGTH, B_SUBSTR, B_INDEX, B_SPLIT, B_SUB, B_GSUB, B_MATCH, B_SPRINTF, B_TOLOWER,
    B_TOUPPER, B_INT, B_SQRT, B_EXP, B_LOG, B_SIN, B_COS, B_ATAN2, B_RAND, B_SRAND, B_CLOSE,
    B_SYSTEM, B_FFLUSH
};

/* ---------------------------------------------------------------- the tree */
enum {
    /* expressions */
    N_NUM, N_STR, N_REGEX, N_VAR, N_LOCAL, N_FIELD, N_INDEX, N_ASSIGN, N_COND, N_OR, N_AND,
    N_IN, N_MATCH, N_CMP, N_CAT, N_ADD, N_SUB, N_MUL, N_DIV, N_MOD, N_POW, N_NEG, N_PLUS,
    N_NOT, N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC, N_CALL, N_BUILTIN, N_GETLINE,
    /* statements */
    S_EXPR, S_PRINT, S_PRINTF, S_IF, S_WHILE, S_DO, S_FOR, S_FORIN, S_BLOCK, S_NEXT,
    S_NEXTFILE, S_EXIT, S_RETURN, S_BREAK, S_CONTINUE, S_DELETE
};
/* N_CMP's n */
enum { C_LT, C_LE, C_GT, C_GE, C_EQ, C_NE };
/* N_GETLINE's n: getline [var] [< file], cmd | getline [var] */
enum { G_SIMPLE, G_FILE, G_CMD };
/* S_PRINT's n: where to */
enum { R_NONE, R_FILE, R_APPEND, R_PIPE };

typedef struct Node Node;
struct Node {
    int op, line, n;
    int pure;                   /* no side effects? 0 not known yet, 1 yes, 2 no */
    Node *a, *b, *c, *d;
    Node *next;                 /* lists: arguments, subscripts, statements */
    void *p;                    /* N_NUM, N_STR: the constant's Cell; N_REGEX: the Re; N_CALL: the Func */
};

typedef struct Func {
    char *name;
    int nparam;
    char **param;
    Node *body;
    int line;                   /* where it was first called (if never defined) */
} Func;

/* the rules */
typedef struct Rule Rule;
struct Rule { Node *pat1, *pat2, *action; int inrange; Rule *next; };

/* the special variables: the first globals, in this order */
enum {
    V_NF, V_NR, V_FNR, V_FS, V_OFS, V_ORS, V_RS, V_FILENAME, V_SUBSEP, V_RSTART, V_RLENGTH,
    V_CONVFMT, V_OFMT, V_ENVIRON, V_ARGC, V_ARGV, V_NSPECIAL
};

/* statement results */
enum { X_NORMAL, X_BREAK, X_CONTINUE, X_NEXT, X_NEXTFILE, X_EXIT, X_RETURN };

typedef struct Re Re;

/* ---------------------------------------------------------------- awk.c */
extern char *progtext;                  /* the program, all -f files joined */
extern int curline;                     /* the source line being run (for messages) */
extern Rule *begins, *ends, *rules;
extern Func **funcs;
extern int nfuncs;
extern char **gname;
extern Cell *gvar;
extern int ngvar;
extern int exitstatus;
void fatal(const char *fmt, ...);       /* a runtime error: message, where, exit 2 */
extern char *stacklimit;                /* recursion stops before the stack (1 MiB on P32W) runs out */
#define STACKCHECK(what) { char probe_; if (&probe_ < stacklimit) fatal("%s nested too deeply", what); }
void *emalloc(int n);
void *erealloc(void *p, int n);
char *snew(int n);                      /* room for n characters and a NUL */
void sfree(char *s);
void *anew(void);                       /* an Elem */
void afree(void *e);
char *sdup(const char *s);
char *sdupn(const char *s, int n);
void buf_init(Buf *b);
void buf_add(Buf *b, const char *s, int n);
void buf_addc(Buf *b, int c);
void buf_adds(Buf *b, const char *s);
char *buf_take(Buf *b);                 /* the string, as an snew string; b is emptied */
int globalvar(const char *name);        /* the index of a global, made if new */
int findglobal(const char *name);       /* -1 if there is none */
void unescape(char *d, const char *s);  /* -v and command-line values: \n \t ... */

/* ---------------------------------------------------------------- awklex.c */
extern Tok *toks;
extern int ntoks;
void lex(void);

/* ---------------------------------------------------------------- awkparse.c */
void parse(void);
void synerr(int ti, const char *msg);

/* ---------------------------------------------------------------- awkrun.c */
extern jmp_buf ruleout;                 /* next, nextfile, exit from inside a function */
void eval(Node *n, Cell *r);
Cell *evalref(Node *n, Cell *tmp);
char *evalstr(Node *n, Cell *tmp);
void evalnum(Node *n, Num *r);
int evalbool(Node *n);
int exec(Node *s);
Cell *lref(Node *n);
void lhook(Node *n);
void lassign(Node *n, Cell *v);
int cellcmp(Cell *a, Cell *b);
void run(void);

/* ---------------------------------------------------------------- awkvar.c */
extern Cell *fld;                       /* $1.. (fld[0] unused: $0 is getfield(0)) */
extern int nf, fldsplitdone;
extern int nrec;                        /* (for messages: the record number) */
void cinit(Cell *c);
void cfree(Cell *c);
void cset_num(Cell *c, Num *n);
void cset_int(Cell *c, int i);
void cset_str(Cell *c, char *s);        /* s (an snew string) is taken over */
void cset_strcopy(Cell *c, const char *s, int t);
void ccopy(Cell *d, Cell *s);
void cmove(Cell *d, Cell *s);           /* d's old value freed, s's taken over (s left T_UNINIT) */
char *cstr(Cell *c);                    /* the string, by CONVFMT (may be a static buffer) */
char *costr(Cell *c);                   /* the string for print, by OFMT */
void cnum(Cell *c, Num *n);
int cbool(Cell *c);
int cisnum(Cell *c);                    /* compares as a number? (resolves T_MAYBE) */
Cell *deref(Cell *c);
Array *carray(Cell *c);                 /* c's array (made if c is T_UNINIT) */
Array *newarray(void);
void freearray(Array *a);
void clearray(Array *a);
Cell *aget(Array *a, const char *key, int create);
void adelete(Array *a, const char *key);
char **akeys(Array *a, int *n);
Cell *getfield(int k);                  /* $k for reading */
Cell *setfieldref(int k);               /* $k for assigning (then fieldset) */
void fieldset(int k);
void fldsplit(void);
void nfset(void);
void setrecord(const char *s, int len);
char *getrec0(void);
void splitrec(void);
int splitstr(const char *s, Array *a, const char *fs, Re *re);
int mainrec(Buf *b);                    /* the next main-input record: 1, 0 at the end, -1 error */
void nextfile(void);
FILE *outstream(const char *name, int how);
int getlinefrom(const char *name, int how, Buf *b);    /* how: G_FILE or G_CMD */
int closestream(const char *name);
void closeall(void);
int flushstream(const char *name);
void flushall(void);
int runsystem(const char *cmd);
void initvars(int argc, char **argv, int first);
void cmdassign(const char *arg);        /* var=value */
int isassign(const char *arg);

/* ---------------------------------------------------------------- awkfn.c */
void builtin(Node *n, Cell *r);
void dosprintf(Buf *out, Node *args, const char *what);
void doprint(Node *s);

/* ---------------------------------------------------------------- awknum.c */
extern const fp64 FP_ZERO, FP_ONE;
void num_int(Num *n, int i);
void num_fp(Num *n, const fp64 *f);     /* kept as an int if it is one */
void num_tofp(const Num *n, fp64 *f);
int num_parse(const char *s, Num *n, int whole);   /* whole: 1 only if all of s is a number */
char *num_tostr(const Num *n, const char *fmt, char *buf, int size);
int num_isnum(const char *s, Num *n);
char *itoa_(int i, char *buf);          /* (sprintf's %d, but faster) */
void num_add(Num *r, const Num *a, const Num *b);
void num_sub(Num *r, const Num *a, const Num *b);
void num_mul(Num *r, const Num *a, const Num *b);
void num_div(Num *r, const Num *a, const Num *b);
void num_mod(Num *r, const Num *a, const Num *b);
void num_pow(Num *r, const Num *a, const Num *b);
void num_neg(Num *r, const Num *a);
int num_cmp(const Num *a, const Num *b);            /* -1 0 1, 2 for a NaN */
int num_true(const Num *n);
int num_toint(const Num *n);                        /* truncated, saturated */
void num_trunc(Num *r, const Num *a);
void fmt_number(Buf *out, int conv, const char *flags, int width, int prec, const Num *n);

/* ---------------------------------------------------------------- awkre.c */
Re *re_comp(const char *pat, char **err);
int re_exec(Re *re, const char *s, int len, int start, int *ms, int *me);
int re_test(Re *re, const char *s);
Re *re_dyn(const char *pat);            /* compiled once and kept (a small cache) */
#endif
