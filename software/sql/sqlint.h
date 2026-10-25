/* sqlint.h -- the inside of the SQL library (sqldb(3)): what its files share.
 *
 * The library is in six files, each small enough for cc to compile on P32W
 * (cc keeps a whole file in memory while it works):
 *
 *   sqlstore.c  memory, errors, the pager (page cache, journal, lock),
 *               table files (rows in slotted pages)
 *   sqlbtree.c  indexes (B+trees)
 *   sqlparse.c  SQL text to statement trees
 *   sqleval.c   values, expressions, names, the planner, table cursors
 *   sqlselect.c SELECT: joins, grouping, sorting
 *   sqldb.c     the other statements, and the API
 *
 * Names shared between the files start with sq_ (the macros below keep the
 * code readable), so a program linked with libsql.a can use any other name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "sqldb.h"

/* ---------------------------------------------------------------- limits */
#define PGSZ     2048               /* the page size of every file */
#define NFRAME   32                 /* pages kept in the cache (64 KiB) */
#define MAXNAME  20                 /* longest table, index or column name */
#define MAXCOL   32                 /* columns in a table */
#define MAXIDX   16                 /* indexes on a table */
#define MAXTEXT  1000               /* longest TEXT value in a table */
#define MAXSTR   4000               /* longest string while computing */
#define MAXREC   (PGSZ - 12)        /* longest row: a page less its header and a slot */
#define RECBUF   (MAXREC + MAXTEXT + 16)    /* room to build one */
#define TKEY     40                 /* bytes of a TEXT value kept in an index key */
#define MAXORDER 16                 /* ORDER BY terms */
#define LOCKWAIT 10                 /* seconds to wait for another writer */

/* ---------------------------------------------------------------- file formats
 * Every file is an array of PGSZ-byte pages; page 0 is the header, which
 * starts with a magic number and the number of pages. Integers are 32 bits,
 * little-endian, except in index keys (below). */
#define H_MAGIC   0                 /* "P32T" table, "P32I" index */
#define H_NPAGES  4
/* table header (NAME.tbl page 0) */
#define T_NROWS   8
#define T_AUTO    12                /* next value for an INTEGER PRIMARY KEY */
#define T_HINT    16                /* a page that may have room */
#define T_NCOLS   20
#define T_NIDX    24
#define T_NAME    28                /* 24 bytes */
#define T_COLS    52                /* MAXCOL entries of CE bytes: */
#define CE        28                /*   name[21] type flags pad maxlen(2) */
#define T_IDX     (T_COLS + MAXCOL * CE)    /* MAXIDX entries of IE bytes: */
#define IE        32                /*   name[25] column unique */
/* index header (NAME.idx page 0) */
#define I_ROOT    8
#define I_KS      12                /* key size: 4 INTEGER, TKEY TEXT */
#define I_UNIQUE  16
#define I_COL     20
#define I_TABLE   24                /* 24 bytes */
#define I_NAME    48                /* 28 bytes */
/* data page: nslots(2) top(2) 'D' pad(3), then slots of off(2) len(2);
   rows are packed at the end of the page, top is the lowest; len 0: free */
#define D_TOP     2
#define D_SLOTS   8
/* B+tree node: type(1: leaf, 2: inner) pad n(2) link(4), then entries.
   A leaf's link is the next leaf; an inner node's is its first child.
   Entry: key(ks) rowid(4), and in an inner node the child(4) holding
   entries >= it. Keys compare with memcmp: an INTEGER is stored
   big-endian with its sign bit flipped, a TEXT as its first TKEY bytes
   padded with zeros, the rowid big-endian. */
#define B_LEAF    1
#define B_INNER   2
#define B_N       2
#define B_LINK    4
#define B_ENT     8

/* ---------------------------------------------------------------- values */
typedef struct val {                /* a value: SQL_NULL, SQL_INTEGER or SQL_TEXT */
    int t, i;
    char *s;                        /* TEXT: NUL-terminated */
} val;

/* memory: arenas, freed all at once */
struct chunk { struct chunk *next; int size, used; };
typedef struct arena { struct chunk *head; } arena;
struct mark { struct chunk *c; int used; };
struct vec { void **v; int n, cap; };       /* a growing array of pointers */

/* ---------------------------------------------------------------- storage */
struct pfile {                      /* an open file of the database */
    struct pfile *next;
    char name[32];
    int fd, rdonly;
    int orig;                       /* pages when the transaction first wrote it; -1 */
    unsigned char *jbits;           /* pages already saved in the journal */
};
struct frame {                      /* a page in the cache */
    struct pfile *f;                /* 0: empty */
    int pg, dirty;
    unsigned stamp;                 /* when last used */
    unsigned char *data;
};
#define C_NOTNULL 1                 /* column flags */
#define C_PK      2
#define C_UNIQUE  4
struct column { char name[MAXNAME + 1]; int type, flags, maxlen; };
struct index {
    char name[MAXNAME + 5];         /* the file is NAME.idx */
    int col, unique, ks;
    struct pfile *pf;               /* 0 until used */
};
struct table {
    struct table *next;
    char name[MAXNAME + 1];         /* the file is NAME.tbl */
    struct pfile *pf;
    int ncols, nidx;
    struct column col[MAXCOL];
    struct index idx[MAXIDX];
};
struct dname { struct dname *next; char name[32]; };
struct btcur {                      /* a place in an index */
    struct index *ix;
    int pg, pos, steps;
    unsigned char ent[TKEY + 8];    /* the entry last read */
};

struct sqldb {
    char dir[200];
    char err[240];                  /* the last error */
    jmp_buf *jb;                    /* where fail() goes */
    struct frame fr[NFRAME];
    unsigned char *mem;             /* the frames' pages */
    unsigned clock;
    struct pfile *files;
    struct table *tables;           /* loaded for the statement (or transaction) */
    struct dname *dels;             /* files dropped by the transaction */
    int jfd;                        /* the journal while writing, else -1 */
    int locked, intxn, nactive, gen, ddl;
    int stmtw, stmtmod;             /* the statement writes, has written */
    int changes, delseq;
};

/* ---------------------------------------------------------------- statements */
enum {                              /* expression operators */
    E_LIT, E_COL, E_NEG, E_NOT, E_ADD, E_SUB, E_MUL, E_DIV, E_MOD, E_CAT,
    E_EQ, E_NE, E_LT, E_LE, E_GT, E_GE, E_AND, E_OR, E_ISNULL, E_NOTNULL,
    E_LIKE, E_BETWEEN, E_IN, E_FUNC, E_AGG, E_STAR
};
enum {                              /* functions, then aggregates */
    F_UPPER, F_LOWER, F_LENGTH, F_SUBSTR, F_ABS, F_COALESCE, F_IFNULL,
    A_COUNT, A_SUM, A_MIN, A_MAX, A_AVG
};
struct expr {
    int op, not, fn;
    struct expr *l, *r;
    struct expr **list;             /* E_IN items, E_FUNC arguments, E_BETWEEN bounds */
    int n;
    val v;                          /* E_LIT */
    char *name, *qual;              /* E_COL [qual.]name; E_STAR qual.* */
    int slot;                       /* E_COL: its place in the row; E_AGG: its accumulator */
    int tab, col;                   /* E_COL: the table (0, 1) and column */
    int start, end;                 /* its text in the statement */
};
struct src {                        /* a table in FROM */
    char *name, *alias;
    struct table *t;
    int base;                       /* where its columns start in the row */
    int join;                       /* the second table: 1 inner, 2 LEFT */
    struct expr *on;
};
struct access {                     /* how a table is read */
    struct index *ix;               /* 0: every row */
    struct expr *lo, *hi;           /* bounds on the index's column (inclusive) */
};
struct select {
    int distinct, explain, grouped;
    int nres; struct expr **res; char **alias; char **cname;
    int nsrc; struct src src[2];
    int ord[2];                     /* the loops: outer table, inner table */
    struct access acc[2];
    struct expr *where, *having, *limit, *offset;
    int ngroup; struct expr **group;
    int norder; struct expr *order[MAXORDER]; int desc[MAXORDER], opos[MAXORDER];
    struct vec agg;                 /* the aggregate calls */
};
enum {                              /* statements */
    K_SELECT, K_INSERT, K_UPDATE, K_DELETE, K_CREATE, K_CREATEIX, K_DROP, K_DROPIX,
    K_BEGIN, K_COMMIT, K_ROLLBACK, K_TABLES, K_INDEXES, K_DESCRIBE
};
struct stmt {
    int kind;
    struct select *sel;
    char *table, *name;             /* the table; an index's name */
    int ifex, unique;               /* IF [NOT] EXISTS; UNIQUE */
    int ncol; struct column *cols;  /* CREATE TABLE */
    struct vec names;               /* INSERT's columns; UPDATE's SET columns; the index column */
    struct vec rows;                /* INSERT's rows (each a struct vec of exprs); SET values */
    struct expr *where;
};

struct tcur {                       /* a cursor reading one table */
    struct table *t;
    int base;
    int mode;                       /* 0 done, 1 every row, 2 by index */
    int pg, slot, rid;
    struct btcur bc;
    unsigned char hik[TKEY + 8];
    int hashi;
    unsigned char rec[MAXREC];
};
struct acc { int n, sum, cap; val m; char *buf; };    /* an aggregate's state */
struct group {
    struct group *next, *lnext;     /* hash chain; all groups in order */
    val *key, *row;
    struct acc *acc;
};
struct ctx { val *row; struct group *g; };   /* what an expression reads */

struct sqlstmt {
    sqldb *db;
    const char *src;                /* the statement text */
    arena a;                        /* trees, kept rows */
    arena t;                        /* temporary values of one row */
    struct mark tm;
    int gen, active, err, done, started;
    struct stmt *s;
    int ncols; char **cname;
    val *out;                       /* the current row */
    char *txt;                      /* sql_text's numbers */
    /* reading a SELECT */
    struct tcur *cur;               /* two */
    val row[2 * MAXCOL];
    int state, matched, limit, skip, nout, nslot;
    int mat, emitted;               /* the rows are in rows[]; rows returned */
    struct group **ghash, *glist, **gtail;
    val *gkey;
    void **dhash;                   /* DISTINCT: rows seen */
    val **rows; int nrows, cap, pos;    /* the result, when it is made whole first */
};

enum { T_EOF, T_ID, T_QID, T_INT, T_STR, T_OP };    /* tokens */

struct parser {
    const char *src;
    int pos;                        /* where the next token starts looking */
    int t, op, start, end, lastend; /* the token: kind, operator, its text */
    unsigned ival;
    char *s;
};

/* ---------------------------------------------------------------- shared names */
#define D        sq_D
#define S        sq_S
#define P        sq_P
#define fail     sq_fail
#define aalloc   sq_aalloc
#define zalloc   sq_zalloc
#define amark    sq_amark
#define arelease sq_arelease
#define afree    sq_afree
#define astrndup sq_astrndup
#define vpush    sq_vpush
#define ieq      sq_ieq
#define get16    sq_get16
#define put16    sq_put16
#define get32    sq_get32
#define put32    sq_put32
#define getbe    sq_getbe

extern sqldb *D;                    /* the database being worked on */
extern struct sqlstmt *S;           /* the statement being worked on */
extern struct parser *P;

/* sqlstore.c */
void fail(const char *fmt, ...);
void *aalloc(arena *a, int n);
void *zalloc(int n);                /* zeroed, from the statement's arena */
void amark(arena *a, struct mark *m);
void arelease(arena *a, struct mark *m);
void afree(arena *a);
char *astrndup(arena *a, const char *s, int n);
void vpush(struct vec *v, void *x);
int ieq(const char *a, const char *b);
int get16(const unsigned char *p);
void put16(unsigned char *p, int v);
int get32(const unsigned char *p);
void put32(unsigned char *p, int v);
int getbe(const unsigned char *p);
void sq_dpath(char *buf, const char *name);
struct pfile *sq_pfopen(const char *name);
void sq_pfclose(struct pfile *f);
unsigned char *sq_pg(struct pfile *f, int pg);
unsigned char *sq_touch(struct pfile *f, int pg);
int sq_npages(struct pfile *f);
int sq_newpage(struct pfile *f);
struct pfile *sq_create(const char *name, const char *magic);
void sq_dropfile(const char *name);
void sq_lock(void);
void sq_unlock(void);
void sq_commit(void);
void sq_rollback(void);
void sq_reset(void);
void sq_recover(int dels);
struct table *sq_table(const char *name, int must);
void sq_savetable(struct table *t);
int sq_th(struct table *t, int off);
void sq_thset(struct table *t, int off, int v);
int sq_insrow(struct table *t, unsigned char *rec, int len);
int sq_fetch(struct table *t, int rid, unsigned char *buf);
void sq_delrow(struct table *t, int rid);
int sq_updrow(struct table *t, int rid, unsigned char *rec, int len);
unsigned char *sq_dpage(struct table *t, int pg);
int sq_mkrec(struct table *t, val *v, unsigned char *buf);
void sq_readrec(struct table *t, unsigned char *rec, int len, val *v);

/* sqlbtree.c */
struct pfile *sq_ixfile(struct index *ix);
void sq_key(struct index *ix, val *v, int rid, unsigned char *k);
void sq_btinit(struct index *ix);
void sq_btinsert(struct index *ix, val *v, int rid);
void sq_btdelete(struct index *ix, val *v, int rid);
void sq_btseek(struct btcur *c, struct index *ix, unsigned char *key);
int sq_btnext(struct btcur *c);

/* sqlparse.c */
struct stmt *sq_parse(void);
void sq_lexinit(struct parser *p, const char *src);
void sq_lex(void);
int sq_isop(int o);
int sq_op(int o);

/* sqlselect.c */
int sq_vsame(val *a, val *b);
void sq_keep(struct sqlstmt *st, val *o);   /* add a row to the result */
void sq_startsel(struct sqlstmt *st);
int sq_nextrow(struct sqlstmt *st);

/* sqleval.c */
char *sq_text(val *v);              /* any value as text (NULL: "") */
int sq_toint(const char *s, int *out);
int sq_int(val *v);                 /* a number, or an error */
int sq_cmp(val *a, val *b);         /* both not NULL */
int sq_truth(val *v);
void sq_eval(struct ctx *c, struct expr *e, val *r);
void sq_bind(struct select *q, struct src *src, int nsrc, struct expr *e, int aggok);
void sq_plan(struct select *q, struct expr *where);
void sq_plan1(struct src *s, struct expr *where, struct access *a);
void sq_tcopen(struct tcur *c, struct src *s, struct access *a, val *row);
int sq_tcnext(struct tcur *c, val *row);
struct index *sq_findix(struct table *t, int col);
