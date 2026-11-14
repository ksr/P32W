/* basic.h -- the P32W BASIC interpreter's shared declarations (basic.c,
 * bexpr.c, bio.c, bgfx.c). BASIC is several source files so that each fits
 * the native cc's memory on P32W (a whole 2400-line file did not). */
#ifndef BASIC_H
#define BASIC_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <softfp.h>
#include <sqldb.h>

/* ---------------------------------------------------------------- tokens */
enum {
    T_END = 0x80, T_FOR, T_NEXT, T_DATA, T_INPUT, T_DIM, T_READ, T_LET, T_GOTO, T_RUN, T_IF,
    T_RESTORE, T_GOSUB, T_RETURN, T_REM, T_STOP, T_ON, T_PRINT, T_CLEAR, T_LIST, T_NEW, T_CONT,
    T_DEF, T_WHILE, T_WEND, T_ELSE, T_THEN, T_TO, T_STEP, T_FN, T_USING, T_TAB, T_SPC, T_NOT,
    T_AND, T_OR, T_XOR, T_MOD, T_LOAD, T_SAVE, T_OPEN, T_CLOSE, T_AS, T_OUTPUT, T_APPEND, T_LINE,
    T_RANDOMIZE, T_SWAP, T_ERROR, T_RESUME, T_CLS, T_SYSTEM, T_SHELL, T_DELETE, T_RENUM, T_TRON,
    T_TROFF, T_WRITE, T_KILL, T_NAME, T_FILES, T_EQV, T_IMP, T_ERASE, T_SLEEP, T_BEEP,
    T_SQLOPEN, T_SQLCLOSE, T_SQLEXEC, T_SQLQUERY, T_SQLEND,
    T_SCREEN, T_PSET, T_PRESET, T_CIRCLE, T_COLOR, T_LOCATE,
    /* functions */
    T_ABS, T_SGN, T_INT, T_FIX, T_SQR, T_SIN, T_COS, T_TAN, T_ATN, T_EXP, T_LOG, T_RND, T_LEN,
    T_VAL, T_ASC, T_CHRS, T_STRS, T_LEFTS, T_RIGHTS, T_MIDS, T_INSTR, T_SPACES, T_STRINGS, T_HEXS,
    T_OCTS, T_UCASES, T_LCASES, T_EOF, T_TIMER, T_TIMES, T_DATES, T_CINT, T_CDBL, T_ERR, T_ERL,
    T_POS, T_FRE, T_PI, T_MAX, T_MIN, T_INKEYS, T_POINT,
    T_SQLNEXT, T_SQLCOLS, T_SQLNAMES, T_SQLVALS, T_SQLVAL, T_SQLNULL, T_SQLCHANGES, T_SQLERRS, T_LAST
};
extern const char *const kw[];
#define NKW (T_LAST - T_END)            /* (128: a token is a byte from 0x80 up, and they are all taken) */
typedef char tokens_fit_a_byte[T_LAST <= 0x100 ? 1 : -1];

/* ---------------------------------------------------------------- values */
enum { V_INT, V_FLT, V_STR };
struct val { int type; int i; fp64 f; char *s; };          /* (s: owned by the value) */
struct num { int isint; int i; fp64 f; };                   /* a numeric variable or element */

/* errors (Microsoft's numbers) */
enum {
    E_NEXT = 1, E_SYNTAX = 2, E_RETURN = 3, E_DATA = 4, E_ILLEGAL = 5, E_OVERFLOW = 6, E_MEMORY = 7,
    E_LINE = 8, E_SUBSCRIPT = 9, E_REDIM = 10, E_DIV0 = 11, E_TYPE = 13, E_CONT = 17, E_UNDEFFN = 18,
    E_NORESUME = 19, E_RESUME = 20, E_FORNEXT = 26, E_WHILE = 29, E_WEND = 30, E_BADFILE = 52,
    E_NOFILE = 53, E_FILEMODE = 54, E_OPEN = 55, E_PAST = 62, E_FILENAME = 64, E_DEVICE = 68, E_SQL = 70,
    E_BREAK = 255
};

/* ---------------------------------------------------------------- shared types */
#define NAMELEN 40
#define MAXDIM 8
struct var {
    char name[NAMELEN + 2];
    int isarr, type;                    /* V_STR for X$, V_INT for X%, else V_FLT */
    struct num n;                       /* a scalar number */
    char *s;                            /* a scalar string */
    int ndim, dim[MAXDIM], total;       /* an array: upper bounds (from 0) */
    struct num *na;
    char **sa;
    struct var *next;
};

/* an assignable place: a scalar or an element */
struct lval { int type; struct num *n; char **s; };

struct fn { char name[NAMELEN + 2]; int nparam; char param[8][NAMELEN + 2]; int line; unsigned char *body; struct fn *next; };

#define NFILES 16
struct bfile { FILE *f; int mode; int col; };           /* mode: 'I' 'O' 'A' (0: closed) */

enum { F_FOR = 1, F_GOSUB, F_WHILE };
struct frame {
    int kind, line;
    unsigned char *pc;                  /* FOR: after the FOR statement; GOSUB: after GOSUB; WHILE: at WHILE */
    struct lval var;
    struct val limit, step;
};
#define NFRAME 64

/* ---------------------------------------------------------------- shared state */
struct line { int num, len; unsigned char *text; };         /* text: crunched, NUL-ended */

/* basic.c: the program, where it is running, errors */
extern struct line *prog;
extern int nlines, caplines;
extern jmp_buf on_error;
extern int cur;                         /* the line being run (index; -1: the direct line) */
extern unsigned char *pc;               /* where in it */
extern unsigned char direct[512];       /* the line typed without a number */
extern int running, tron, jumped;
extern int err_code, err_line, err_handler, in_handler;
extern int resume_line, cont_line;
extern unsigned char *resume_pc, *cont_pc;
extern volatile int interrupted;
extern unsigned char *stmt_pc;
extern int handler_jump;

/* bexpr.c: variables, functions, files, random numbers, the clock */
extern const fp64 F_ONE;
extern struct var *vtab[211];
extern struct fn *fns;
extern struct bfile files[NFILES];
extern int col;                         /* the screen's column */
extern unsigned rnd_state;
extern fp64 rnd_last;
extern long t0_ticks;
extern time_t t0_time;

/* bexpr.c: the SQL database (SQLOPEN ... ): one open, four queries */
extern sqldb *bdb;
extern sqlstmt *bq[5];
extern char sqlerr[200];
void sql_fail(const char *msg);
void sql_shut(void);

/* bgfx.c: the graphics statements, on the video card's screen (SCREEN 1) */
extern int gr_on;                       /* SCREEN 1 is on */
extern int gr_text;                     /* LOCATE has aimed this line of PRINT at the picture */
void gr_statement(int t);               /* SCREEN, PSET, PRESET, LINE, CIRCLE, COLOR, LOCATE, and CLS in SCREEN 1 */
void gr_point(struct val *v);           /* POINT(x, y) */
void gr_char(int c);                    /* a character of PRINT, into the picture */
void gr_show(void);                     /* what has been drawn so far, shown: before BASIC waits for anything */
void gr_typed(void);                    /* a line has been typed at the prompt */

/* bio.c: the control stack, DATA */
extern struct frame stack[NFRAME];
extern int nstack;
extern int data_line;
extern unsigned char *data_pc;

/* ---------------------------------------------------------------- functions */
void val_free(struct val *v);
void set_int(struct val *v, int i);
void set_str(struct val *v, char *s);
void set_flt(struct val *v, const fp64 *f);
void need_num(const struct val *v);
void need_str(const struct val *v);
void to_fp(const struct val *v, fp64 *f);
int to_int(const struct val *v);
char *num_text(const struct val *v, char *buf);
const char *parse_num(const char *s, struct val *v);
void arith(int op, struct val *a, struct val *b);
int compare(struct val *a, struct val *b);
int hash(const char *s);
int name_type(const char *name);
struct var *lookup(const char *name, int isarr);
void clear_vars(void);
void sp(void);
int match(int c);
void expect(int c);
int get_name(char *buf);
void dim_array(struct var *a, int ndim, const int *dim);
int subscript(struct var *a);
void get_lval(struct lval *lv);
void store(struct lval *lv, struct val *v);
void load(struct lval *lv, struct val *v);
void clear_fns(void);
void close_all(void);
struct bfile *file_of(int n, int mode);
void next_rnd(fp64 *r);
void timer_now(fp64 *r);
char *str_of(int c, int n);
void args(struct val *v, int n);
int int_arg(void);
void function(int t, struct val *v);
void atom(struct val *v);
void power(struct val *v);
void unary(struct val *v);
void mul(struct val *v);
void idiv(struct val *v);
void add(struct val *v);
void relation(struct val *v);
void logic_not(struct val *v);
void logic(struct val *v, int level);
void eval(struct val *v);
void eval_num(struct val *v);
void call_fn(struct val *out);
void out_char(struct bfile *bf, int c);
void out_str(struct bfile *bf, const char *s);
int using_field(const char *f);
void using_value(struct bfile *bf, const char *f, int len, struct val *v);
struct frame *push(int kind);
unsigned char *line_text(int k);
void go_line(int num);
int line_number(void);
void skip_statement(void);
void skip_block(int open, int closer, int err);
void restore_to(int num);
char *next_data(void);
int text_to(struct lval *lv, char *text);
int read_line(FILE *f, char *buf, int size);
int split_fields(char *line, char **field, int max);
void do_input(void);
void do_line_input(void);
int at_end(void);
void do_print(int write);
void *xmalloc(int n);
char *xstrdup(const char *s);
int find_line(int num);
void store_line(int num, const unsigned char *text, int len);
int upper(int c);
int isalpha_(int c);
int isdigit_(int c);
int crunch(const char *s, unsigned char *out, int max);
void detok(const unsigned char *t, char *out, int max);
const char *errmsg(int e);
void list_range(int *from, int *to);
void list_lines(FILE *f, int from, int to);
void new_program(void);
void clear_run(void);
char *file_name(char *buf);
void prog_name(char *buf);
void load_file(const char *name);
void save_file(const char *name);
int renum_map(int *oldn, int *newn, int n, int num);
void renumber(void);
void run_shell(const char *cmd);
void do_if(void);
void do_for(void);
void do_next(void);
void do_on(void);
void do_dim(void);
void do_open(void);
void do_close(void);
void do_def(void);
void do_resume(void);
void statement(void);
void basic_error(int e);
void run_loop(void);
int go(void);
int enter_line(char *text);
void on_int(int sig);
int main(int argc, char **argv);
#endif
