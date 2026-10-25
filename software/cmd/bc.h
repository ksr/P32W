/* bc.h -- bc's shared declarations (bc.c: the language, bcnum.c: the
 * numbers). bc is two files so that each fits the native cc's memory.
 *
 * A number is a sign and a string of decimal digits, one per byte (0-9),
 * the most significant first; scale of them are after the point. The
 * integer part has no leading zeros (so zero is all fraction, or nothing).
 * Numbers are shared, counted references: never changed once made. */
#ifndef BC_H
#define BC_H

typedef struct Num {
    int ref;                    /* references */
    int neg;                    /* 1: negative (never for zero) */
    int len;                    /* digits */
    int scale;                  /* of which after the point */
    char *d;                    /* the digits */
    int cls;                    /* its size class (bcnum.c), -1 if none */
} Num;

#define ILEN(n) ((n)->len - (n)->scale)
#define MAXSCALE 32767          /* scale and the digits of a result */

/* the program: a tree of nodes */
enum {
    /* expressions */
    E_NUM, E_VAR, E_ARR, E_SCALE, E_IBASE, E_OBASE, E_LAST, E_CALL, E_ARRARG,
    E_NEG, E_NOT, E_BIN, E_REL, E_AND, E_OR, E_ASSIGN, E_PRE, E_POST,
    E_FSCALE, E_SQRT, E_LENGTH, E_PAREN,
    /* statements */
    S_EXPR, S_STR, S_PRINT, S_BLOCK, S_IF, S_WHILE, S_FOR, S_BREAK, S_CONT,
    S_RETURN, S_HALT, S_NULL
};

typedef struct Node {
    int t;                      /* E_ or S_ */
    int op;                     /* the operator ('+', 'L' <=, ...), or a symbol */
    struct Node *a, *b, *c, *d; /* operands; for S_BLOCK and lists, a chain */
    struct Node *next;          /* the next in a list */
    char *s;                    /* a constant's digits, a string */
    Num *num;                   /* a constant's value in base base */
    int base;
} Node;

typedef struct Func {
    int defined;
    int np, na;                 /* parameters, autos */
    int *sym;                   /* np + na symbols */
    char *arr;                  /* and which are arrays */
    Node *body;
} Func;

/* bc.c */
extern int nsyms;
extern char **symname;

/* bcexec.c */
extern int scale, ibase, obase;
extern Func **funcs;
void outs(char *s, int esc);            /* a string (esc: with print's escapes) */
void growsyms(int n);
int exec(Node *s);                      /* 0, or BREAK CONTINUE RETURN */
void unwind(void);                      /* after an error: the variables of the calls */
void freenode(Node *n);
#define BREAK 1
#define CONTINUE 2
#define RETURN 3

/* bcnum.c */
Num *n_new(int ilen, int scale);
Num *n_ref(Num *n);
void n_free(Num *n);
Num *n_zero(void);
Num *n_int(long v);
long n_long(Num *n, int *ok);           /* the integer part; *ok 0 if too big */
int n_iszero(Num *n);
int n_cmp(Num *a, Num *b);
Num *n_neg(Num *a);
Num *n_trunc(Num *a, int scale);       /* at most scale digits after the point */
Num *n_add(Num *a, Num *b);
Num *n_sub(Num *a, Num *b);
Num *n_mul(Num *a, Num *b, int scale);
Num *n_div(Num *a, Num *b, int scale);  /* 0: division by zero */
Num *n_mod(Num *a, Num *b, int scale);  /* 0: division by zero */
Num *n_pow(Num *a, Num *b, int scale);
Num *n_sqrt(Num *a, int scale);         /* 0: negative */
int n_length(Num *n);
Num *n_parse(char *s, int base);
void n_print(Num *n, int base);

/* bc.c */
void bcerror(char *fmt, ...);           /* a message; abandons the statement */
void bcwarn(char *fmt, ...);            /* only a message */
void outc(int c);                       /* a character of a number (lines of 70) */
#endif
