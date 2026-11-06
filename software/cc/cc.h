/* cc.h -- the P32W C compiler in C (M7, D24): cc/p32cc.py rewritten, stage
 * for stage, so the two write the same assembly for the same source (the
 * Python files' comments describe each stage; this is their C).
 *
 *   lex.c    tokens                     (cc/clex.py)
 *   cpp.c    the preprocessor            (cc/cpp.py)
 *   type.c   types                       (cc/cty.py)
 *   parse.c  the typed syntax tree       (cc/cparse.py)
 *   gen.c    P32W assembly                (cc/cgen.py)
 *   main.c   the driver                  (cc/p32cc.py)
 *
 * Integers are 32 bits here as on P32W: a constant is its 32 bits and the
 * signedness of its type (the Python works with unbounded integers and
 * wraps them to the type).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- memory, strings */
void *xmalloc(unsigned n);
void *xrealloc(void *p, unsigned n);
char *xstrdup(const char *s);
char *intern(const char *s);            /* one copy of each string: compare pointers */
char *fmt(const char *f, ...);          /* a new formatted string */

struct vec { void **a; int n, cap; };   /* a growing array of pointers */
void vpush(struct vec *v, void *p);
#define VPUSH(v, p) do { if ((v)->n < (v)->cap) (v)->a[(v)->n++] = (p); else vpush((v), (p)); } while (0)

/* ---------------------------------------------------------------- tokens */
enum { T_ID, T_NUM, T_CHAR, T_STR, T_PUNCT, T_EOF };

struct hide { const char *name; struct hide *next; };     /* a macro hideset */

/* 28 bytes on P32W, from slabs of their own (tok_new, tok_free): a big
   file has tens of thousands at once. A slab whose tokens are all freed
   goes back to the heap (D33) */
struct tok {
    unsigned kind : 8, bol : 1, space : 1, slab : 22;  /* (slab: tok_new's) */
    char *text;                         /* interned */
    char *file;
    int line;
    union { int cval; unsigned char *sval; };   /* T_CHAR: its value (char is signed); T_STR: its bytes */
    int slen;
    struct hide *hide;
};

void error(struct tok *t, const char *f, ...);
void warning(struct tok *t, const char *f, ...);
extern int warnings;
struct tok *tok_new(void);              /* zeroed */
void tok_free(struct tok *t);
struct tok *tokcopy(struct tok *t);
struct tok **tokenize(const char *src, const char *fname, int *ntoks);
int parse_int(struct tok *t, unsigned *v, int *uns, int *lng, int *nondec);
int parse_int64(struct tok *t, unsigned long long *v, int *uns, int *lng, int *nondec, int *over);
/* its text is s. The first character inline: nearly every test fails there, and
   a parser makes dozens for each token (t and s go in more than once) */
#define is(t, s) ((t)->text[0] == (s)[0] && !strcmp((t)->text, (s)))

/* ---------------------------------------------------------------- the preprocessor */
void cpp_init(char **dirs, int ndirs, char **defs, int ndefs);
struct tok **preprocess(const char *path, int *n);
char *detok(struct tok **t, int n);

/* ---------------------------------------------------------------- types */
enum { K_VOID, K_CHAR, K_SHORT, K_INT, K_LONG, K_PTR, K_ARRAY, K_FUNC, K_STRUCT, K_UNION, K_ENUM, K_LLONG, K_DOUBLE };

struct member { char *name; struct type *ty; int offset, bit, width; };    /* width: a bit-field (bits bit.. of the unit at offset) */

struct type {
    int kind, size, align, uns;
    struct type *base;
    int length;                         /* array: -1 = incomplete */
    struct type **params;               /* func: NULL = unprototyped */
    int nparams, variadic;
    struct tok **param_names;
    int *param_vol;                     /* func: which parameters are volatile (kept in memory), or NULL */
    struct member *members;             /* struct / union: NULL = incomplete */
    int nmembers;
    char *tag;
};

extern struct type *ty_void, *ty_char, *ty_uchar, *ty_short, *ty_ushort, *ty_int, *ty_uint,
    *ty_long, *ty_ulong, *ty_llong, *ty_ullong, *ty_double,
    *ty_bool;                           /* _Bool (D35): an unsigned char that only ever holds 0 or 1 */
void types_init(void);
struct type *new_type(int kind, int size, int align, int uns, struct type *base);
struct type *ptr_to(struct type *t);
struct type *array_of(struct type *t, int n);
struct type *func_type(struct type *ret, struct type **params, int nparams, int variadic);
struct type *enum_type(void);
struct type *struct_type(int kind, char *tag);
void layout(struct type *st, char **names, struct type **tys, int *widths, int n);
struct member *find_member(struct type *st, const char *name, struct member *tmp);
struct type *decay(struct type *t);
struct type *promote(struct type *t);
struct type *usual(struct type *a, struct type *b);
int compatible(struct type *a, struct type *b);
int is_integer(struct type *t);
int is_arith(struct type *t);            /* an integer or a double */
int is_scalar(struct type *t);
char *type_str(struct type *t);

/* ---------------------------------------------------------------- the tree */
enum {
    N_NUM, N_STR, N_VAR, N_VAROFF, N_ADDR, N_DEREF, N_MEMBER, N_CAST, N_NEG, N_BITNOT, N_NOT,
    N_BINOP, N_CMP, N_LOGAND, N_LOGOR, N_COND, N_COMMA, N_ASSIGN, N_OPASSIGN,
    N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC, N_CALL,
    N_BLOCK, N_EXPRSTMT, N_IF, N_FOR, N_DO, N_SWITCH, N_CASELABEL, N_BREAK, N_CONTINUE,
    N_RETURN, N_GOTO, N_LABEL, N_MEMZERO, N_STMTEXPR, N_MEMADDR
};

struct obj;
/* A node's fields overlap where no kind of node uses two of them (memory:
   a function's whole tree is in memory, and P32W gives a process 8 MiB). */
struct node {
    int kind;
    struct tok *tok;
    short bfbit, bfwidth;               /* a bit-field lvalue (N_MEMBER, N_VAROFF): its bits */
    struct type *ty;
    union { unsigned val; int uns; int off; int step; int dlen; };   /* NUM; CMP; VAROFF; inc/dec; STR */
    union { char *op; unsigned char *data; };                       /* BINOP / CMP / OPASSIGN; STR */
    union { struct node *lhs, *expr, *fn, *init; };                 /* slot 0 */
    union { struct node *rhs, *cond; };                             /* slot 1 */
    union { struct node *a, *then, *inc; };                         /* slot 2 */
    union { struct node *b, *els, *body; };                         /* slot 3 */
    union { struct obj *obj, *var, *direct; struct member *mem; struct type *opty; };
    union { struct type *ftype; char *label, *dflt; };              /* CALL; STR / labels; SWITCH */
    union { struct vec list, cases; };                              /* BLOCK / CALL; SWITCH */
};
struct casepair { unsigned val; char *label; };

struct reloc_init { int off; char *label; int add; };
struct obj {
    char *name;
    struct type *ty;
    int local, is_static, is_def, ext, tentative, used, uses, addr_taken;
    int reg;                            /* the code generator: its register, or 0 */
    char *label;
    int offset;
    unsigned char *init;                /* globals with an initialiser: ty->size bytes (or init_size) */
    int init_size;                      /* 0, or more than ty->size: a flexible array member's elements (D35) */
    int has_init;
    struct reloc_init *irel;
    int nirel;
    struct node *body;
    struct vec params, locals;          /* functions: struct obj * */
    struct obj *retptr;                 /* a function returning a struct: its hidden argument 0 */
    int is_llconst;                     /* a long long constant's object: its value */
    unsigned long long llval;
    int is_dconst;                      /* a double constant's object: its value */
    double dval;
};

struct program { struct vec globals; struct vec strings; /* struct node * (N_STR) */ };
int is_memval(struct type *t);         /* kept in memory, handled by its address (a struct) */
int words(struct type *t);             /* the argument words a value takes */
int stack_args(struct type *ft);       /* every argument on the stack (variadic, or a struct one) */
struct program *parse(struct tok **toks, int n);
extern void (*function_done)(struct obj *fn);  /* called as each function's definition ends */
int parser_labels(void);                /* (the parser's label counter) */

/* ---------------------------------------------------------------- code */
void gen_begin(FILE *f);
void gen_function(struct obj *fn);
int gen_end(struct program *p);
extern int annotate;                    /* -S: source lines as comments */   /* the assembly, written to f as it is made; 0 or -1 */
