/* bexpr.c -- P32W BASIC: numbers, variables, arrays, user functions, files
 * and the expression evaluator (basic.h) */
#include "basic.h"

/* ---------------------------------------------------------------- numbers */
const fp64 F_ONE = { 0, 0x3FF00000 };

void val_free(struct val *v) { if (v->type == V_STR && v->s) free(v->s); v->s = 0; }
void set_int(struct val *v, int i) { v->type = V_INT; v->i = i; v->s = 0; }
void set_str(struct val *v, char *s) { v->type = V_STR; v->s = s; }      /* (s is taken over) */

/* a float result: kept as an integer when it is a whole number that fits */
void set_flt(struct val *v, const fp64 *f)
{
    int ok, i;
    v->s = 0;
    if (fp_isnan(f) || fp_isinf(f)) basic_error(E_OVERFLOW);
    if (fp_isint(f)) {
        i = fp_toint(f, &ok);
        if (ok) { v->type = V_INT; v->i = i; return; }
    }
    v->type = V_FLT;
    v->f = *f;
}

void need_num(const struct val *v) { if (v->type == V_STR) basic_error(E_TYPE); }
void need_str(const struct val *v) { if (v->type != V_STR) basic_error(E_TYPE); }

void to_fp(const struct val *v, fp64 *f)
{
    need_num(v);
    if (v->type == V_INT) fp_fromint(f, v->i); else *f = v->f;
}

int to_int(const struct val *v)    /* rounded; Overflow outside 32 bits */
{
    fp64 r;
    int ok, i;
    need_num(v);
    if (v->type == V_INT) return v->i;
    fp_round(&r, &v->f);
    i = fp_toint(&r, &ok);
    if (!ok) basic_error(E_OVERFLOW);
    return i;
}

/* a number as PRINT shows it (without the leading space): integers as they
   are, others to 15 digits, .5 and -.5 without a 0, 1E+20 beyond 15 digits */
char *num_text(const struct val *v, char *buf)
{
    char d[20], *o = buf;
    int x, n, i;
    if (v->type == V_INT) { sprintf(buf, "%d", v->i); return buf; }
    if (v->f.hi >> 31) *o++ = '-';
    fp_digits(&v->f, 15, d, &x);
    for (n = 15; n > 1 && d[n - 1] == '0'; n--) ;
    if (x >= 15 || x < -5) {            /* d.dddE+xx */
        *o++ = d[0];
        if (n > 1) { *o++ = '.'; for (i = 1; i < n; i++) *o++ = d[i]; }
        sprintf(o, "E%c%02d", x < 0 ? '-' : '+', x < 0 ? -x : x);
        return buf;
    }
    if (x >= 0) {
        for (i = 0; i <= x; i++) *o++ = i < n ? d[i] : '0';
        if (n > x + 1) { *o++ = '.'; for (i = x + 1; i < n; i++) *o++ = d[i]; }
    } else {
        *o++ = '.';
        for (i = -1; i > x; i--) *o++ = '0';
        for (i = 0; i < n; i++) *o++ = d[i];
    }
    *o = 0;
    return buf;
}

/* parse a number at s ("12", "1.5E-3", "&HFF", "&O17"): its end, or 0 */
const char *parse_num(const char *s, struct val *v)
{
    const char *t = s;
    fp64 f;
    if (*s == '&' && (upper(s[1]) == 'H' || upper(s[1]) == 'O')) {
        unsigned u = 0;
        int base = upper(s[1]) == 'H' ? 16 : 8, d;
        for (t = s + 2; ; t++) {
            int c = upper(*t);
            d = isdigit_(c) ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (d >= base) break;
            u = u * base + d;
        }
        if (t == s + 2) return 0;
        set_int(v, (int)u);
        return t;
    }
    {                                   /* a plain integer: quickly */
        unsigned u = 0;
        int big = 0;
        while (isdigit_(*t)) { if (u > 214748364u) big = 1; u = u * 10 + (*t++ - '0'); }
        if (t > s && *t != '.' && upper(*t) != 'E' && !big && u <= 2147483647u) { set_int(v, (int)u); return t; }
    }
    t = fp_parse(&f, s);
    if (!t) return 0;
    if (fp_isinf(&f)) basic_error(E_OVERFLOW);
    set_flt(v, &f);
    return t;
}

/* a op b, numbers (or + on strings); the result in a */
void arith(int op, struct val *a, struct val *b)
{
    fp64 x, y, r;
    if (a->type == V_STR || b->type == V_STR) {
        if (op != '+' || a->type != b->type) basic_error(E_TYPE);
        {
            char *s = xmalloc(strlen(a->s) + strlen(b->s) + 1);
            strcpy(s, a->s);
            strcat(s, b->s);
            val_free(a);
            val_free(b);
            set_str(a, s);
        }
        return;
    }
    if (a->type == V_INT && b->type == V_INT) {
        int p = a->i, q = b->i, s;
        switch (op) {
        case '+': s = (int)((unsigned)p + (unsigned)q); if (((p ^ s) & (q ^ s)) >= 0) { a->i = s; return; } break;
        case '-': s = (int)((unsigned)p - (unsigned)q); if (((p ^ q) & (p ^ s)) >= 0) { a->i = s; return; } break;
        case '*':
            if (p > -46341 && p < 46341 && q > -46341 && q < 46341) { a->i = p * q; return; }
            break;
        case '/':
            if (!q) basic_error(E_DIV0);
            if (!(p == (int)0x80000000 && q == -1) && p % q == 0) { a->i = p / q; return; }
            break;
        case '^':
            if (q >= 0 && q < 32) {     /* a small whole power, while it fits */
                int k, ok = 1;
                fp64 acc;
                fp_fromint(&acc, 1);
                fp_fromint(&x, p);
                for (k = 0; k < q; k++) fp_mul(&acc, &acc, &x);
                set_flt(a, &acc);
                (void)ok;
                return;
            }
            break;
        }
    }
    to_fp(a, &x);
    to_fp(b, &y);
    switch (op) {
    case '+': fp_add(&r, &x, &y); break;
    case '-': fp_sub(&r, &x, &y); break;
    case '*': fp_mul(&r, &x, &y); break;
    case '/': if (fp_iszero(&y)) basic_error(E_DIV0); fp_div(&r, &x, &y); break;
    case '^':
        fp_pow(&r, &x, &y);
        if (fp_isnan(&r)) basic_error(E_ILLEGAL);
        break;
    default: basic_error(E_SYNTAX);
    }
    set_flt(a, &r);
}

int compare(struct val *a, struct val *b)   /* -1 0 1 */
{
    int c;
    if (a->type == V_STR || b->type == V_STR) {
        if (a->type != b->type) basic_error(E_TYPE);
        c = strcmp(a->s, b->s);
        val_free(a);
        val_free(b);
        return c < 0 ? -1 : c > 0;
    }
    if (a->type == V_INT && b->type == V_INT) return a->i < b->i ? -1 : a->i > b->i;
    {
        fp64 x, y;
        to_fp(a, &x);
        to_fp(b, &y);
        c = fp_cmp(&x, &y);
        return c == 2 ? 0 : c;
    }
}

/* ---------------------------------------------------------------- variables */
struct var *vtab[211];

int hash(const char *s) { unsigned h = 0; while (*s) h = h * 31 + (unsigned char)*s++; return h % 211; }

int name_type(const char *name)
{
    int n = strlen(name);
    return name[n - 1] == '$' ? V_STR : name[n - 1] == '%' ? V_INT : V_FLT;
}

struct var *lookup(const char *name, int isarr)
{
    int h = hash(name);
    struct var *v;
    for (v = vtab[h]; v; v = v->next)
        if (v->isarr == isarr && !strcmp(v->name, name)) return v;
    v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    strcpy(v->name, name);
    v->isarr = isarr;
    v->type = name_type(name);
    v->n.isint = 1;
    v->next = vtab[h];
    vtab[h] = v;
    return v;
}

void clear_vars(void)
{
    int h, k;
    for (h = 0; h < 211; h++) {
        struct var *v = vtab[h], *nx;
        for (; v; v = nx) {
            nx = v->next;
            if (v->s) free(v->s);
            if (v->na) free(v->na);
            if (v->sa) { for (k = 0; k < v->total; k++) if (v->sa[k]) free(v->sa[k]); free(v->sa); }
            free(v);
        }
        vtab[h] = 0;
    }
}

void sp(void) { while (*pc == ' ' || *pc == '\t') pc++; }

/* what is at pc: a character, a keyword of one byte, or one of the second table (0x100 up); *len: its bytes */
int token(int *len)
{
    if (*pc == T_EXT && pc[1] >= 0x80 && pc[1] - 0x80 < NKWX) { *len = 2; return 0x100 + pc[1] - 0x80; }
    *len = 1;
    return *pc;
}

int match(int c) { sp(); if (*pc == c) { pc++; return 1; } return 0; }
void expect(int c) { if (!match(c)) basic_error(E_SYNTAX); }

/* a variable name at pc (letters, digits, then $ or %): its length, or 0 */
int get_name(char *buf)
{
    int n = 0;
    sp();
    if (!isalpha_(*pc)) return 0;
    while ((isalpha_(*pc) || isdigit_(*pc)) && n < NAMELEN) buf[n++] = *pc++;
    while (isalpha_(*pc) || isdigit_(*pc)) pc++;
    if (*pc == '$' || *pc == '%') buf[n++] = *pc++;
    buf[n] = 0;
    return n;
}

void eval(struct val *v);

void dim_array(struct var *a, int ndim, const int *dim)
{
    int k, total = 1;
    if (a->na || a->sa) basic_error(E_REDIM);
    for (k = 0; k < ndim; k++) {
        if (dim[k] < 0) basic_error(E_ILLEGAL);
        a->dim[k] = dim[k];
        if (total > 4000000 / (dim[k] + 1)) basic_error(E_MEMORY);
        total *= dim[k] + 1;
    }
    a->ndim = ndim;
    a->total = total;
    if (a->type == V_STR) {
        a->sa = xmalloc(total * sizeof *a->sa);
        memset(a->sa, 0, total * sizeof *a->sa);
    } else {
        a->na = xmalloc(total * sizeof *a->na);
        memset(a->na, 0, total * sizeof *a->na);
        for (k = 0; k < total; k++) a->na[k].isint = 1;
    }
}

/* the element named by (i, j, ...) at pc: its index in the array */
int subscript(struct var *a)
{
    int idx[MAXDIM], n = 0, k, off = 0;
    struct val v;
    expect('(');
    do {
        if (n == MAXDIM) basic_error(E_SUBSCRIPT);
        eval(&v);
        idx[n++] = to_int(&v);
    } while (match(','));
    expect(')');
    if (!a->na && !a->sa) {             /* used before DIM: 0 to 10 in each dimension */
        int d[MAXDIM];
        for (k = 0; k < n; k++) d[k] = 10;
        dim_array(a, n, d);
    }
    if (n != a->ndim) basic_error(E_SUBSCRIPT);
    for (k = 0; k < n; k++) {
        if (idx[k] < 0 || idx[k] > a->dim[k]) basic_error(E_SUBSCRIPT);
        off = off * (a->dim[k] + 1) + idx[k];
    }
    return off;
}


void get_lval(struct lval *lv)
{
    char name[NAMELEN + 2];
    struct var *v;
    if (!get_name(name)) basic_error(E_SYNTAX);
    sp();
    if (*pc == '(') {
        int off;
        v = lookup(name, 1);
        off = subscript(v);
        lv->type = v->type;
        lv->n = v->na ? &v->na[off] : 0;
        lv->s = v->sa ? &v->sa[off] : 0;
        return;
    }
    v = lookup(name, 0);
    lv->type = v->type;
    lv->n = &v->n;
    lv->s = &v->s;
}

void store(struct lval *lv, struct val *v)  /* (v's string is taken over) */
{
    if (lv->type == V_STR) {
        need_str(v);
        if (*lv->s) free(*lv->s);
        *lv->s = v->s;
        v->s = 0;
        return;
    }
    need_num(v);
    if (lv->type == V_INT) { lv->n->isint = 1; lv->n->i = to_int(v); return; }
    lv->n->isint = v->type == V_INT;
    lv->n->i = v->i;
    lv->n->f = v->f;
}

/* MID$(v$, start [, n]) = s$: the characters of v$ from start on become the first of s$, n of them at most
   and never past the end of v$, which keeps its length (D115) */
void do_mid(void)
{
    struct lval lv;
    struct val v;
    int start, n = 0x7FFFFFFF, len, k;
    char *d;
    expect('(');
    get_lval(&lv);
    if (lv.type != V_STR) basic_error(E_TYPE);
    expect(',');
    start = int_arg();
    if (match(',')) n = int_arg();
    expect(')');
    expect('=');
    eval(&v);
    if (v.type != V_STR) basic_error(E_TYPE);
    d = *lv.s;
    len = d ? strlen(d) : 0;
    if (start < 1 || start > len || n < 0) { val_free(&v); basic_error(E_ILLEGAL); }
    k = strlen(v.s);
    if (n > k) n = k;
    if (n > len - start + 1) n = len - start + 1;
    memcpy(d + start - 1, v.s, n);     /* (v.s is a copy, whatever the expression was: MID$(A$, 2) = A$ is safe) */
    val_free(&v);
}

void load(struct lval *lv, struct val *v)
{
    if (lv->type == V_STR) { set_str(v, xstrdup(*lv->s ? *lv->s : "")); return; }
    if (lv->n->isint) set_int(v, lv->n->i);
    else { v->type = V_FLT; v->f = lv->n->f; v->s = 0; }
}

/* ---------------------------------------------------------------- user functions (DEF FN) */
struct fn *fns;

void clear_fns(void)
{
    while (fns) { struct fn *n = fns->next; free(fns); fns = n; }
}

/* ---------------------------------------------------------------- files */
struct bfile files[NFILES];
int col;                         /* the screen's column (for , TAB and POS) */

/* ---- the SQL database */
sqldb *bdb;
sqlstmt *bq[5];
char sqlerr[200];

void sql_fail(const char *msg)          /* error 70, with the database's words in SQLERR$ */
{
    strncpy(sqlerr, msg ? msg : "", sizeof sqlerr - 1);
    sqlerr[sizeof sqlerr - 1] = 0;
    basic_error(E_SQL);
}

void sql_shut(void)                     /* the queries finished, the database closed */
{
    int k;
    for (k = 1; k < 5; k++) if (bq[k]) { sql_finish(bq[k]); bq[k] = 0; }
    if (bdb) { sql_close(bdb); bdb = 0; }
}

static sqlstmt *query_of(int q)
{
    if (q < 1 || q > 4) basic_error(E_ILLEGAL);
    if (!bq[q]) sql_fail("no such query (SQLQUERY)");
    return bq[q];
}

static int column_of(sqlstmt *st, int i)
{
    if (i < 1 || i > sql_ncols(st)) basic_error(E_ILLEGAL);
    return i - 1;
}

void close_all(void)
{
    sql_shut();
    int k;
    for (k = 1; k < NFILES; k++) if (files[k].f) { fclose(files[k].f); files[k].f = 0; files[k].mode = 0; }
}

struct bfile *file_of(int n, int mode)      /* mode 0: any open one */
{
    if (n < 1 || n >= NFILES) basic_error(E_BADFILE);
    if (!files[n].f) basic_error(E_BADFILE);
    if (mode == 'I' && files[n].mode != 'I') basic_error(E_FILEMODE);
    if (mode == 'O' && files[n].mode == 'I') basic_error(E_FILEMODE);
    return &files[n];
}

/* ---------------------------------------------------------------- expressions */
unsigned rnd_state = 0x2545F491;
fp64 rnd_last;
long t0_ticks;
time_t t0_time;

void next_rnd(fp64 *r)           /* [0, 1), 53 random bits */
{
    unsigned a, b;
    fp64 x, y;
    rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; a = rnd_state >> 6;  /* 26 bits */
    rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; b = rnd_state >> 5;  /* 27 bits */
    fp_fromuint(&x, a);
    fp_ldexp(&x, &x, 27);
    fp_fromuint(&y, b);
    fp_add(&x, &x, &y);
    fp_ldexp(r, &x, -53);
}

void timer_now(fp64 *r)          /* seconds since midnight, to a tick */
{
    struct tms tb;
    long ticks = times(&tb) - t0_ticks;
    time_t t = t0_time + ticks / 50;
    struct tm *tm = localtime(&t);
    fp64 frac, hz;
    fp_fromint(r, tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
    fp_fromint(&frac, (int)(ticks % 50));
    fp_fromint(&hz, 50);
    fp_div(&frac, &frac, &hz);
    fp_add(r, r, &frac);
}

char *str_of(int c, int n)
{
    char *s;
    if (n < 0 || n > 65535) basic_error(E_ILLEGAL);
    s = xmalloc(n + 1);
    memset(s, c, n);
    s[n] = 0;
    return s;
}

void args(struct val *v, int n)  /* (a, b, ...) into v[0..n-1] */
{
    int k;
    expect('(');
    for (k = 0; k < n; k++) { if (k) expect(','); eval(&v[k]); }
    expect(')');
}

int int_arg(void) { struct val v; eval(&v); return to_int(&v); }

void call_fn(struct val *out);

void function(int t, struct val *v)
{
    struct val a[3];
    fp64 f, r;
    char *s;
    int n, k;
    switch (t) {
    case T_ABS: case T_SGN: case T_INT: case T_FIX: case T_SQR: case T_SIN: case T_COS: case T_TAN:
    case T_ATN: case T_EXP: case T_LOG: case T_CINT: case T_CDBL:
        args(a, 1);
        need_num(&a[0]);
        if (a[0].type == V_INT) {       /* (the quick and exact cases) */
            int i = a[0].i;
            if (t == T_ABS && i != (int)0x80000000) { set_int(v, i < 0 ? -i : i); return; }
            if (t == T_SGN) { set_int(v, (i > 0) - (i < 0)); return; }
            if (t == T_INT || t == T_FIX || t == T_CINT || t == T_CDBL) { *v = a[0]; return; }
        }
        to_fp(&a[0], &f);
        switch (t) {
        case T_ABS: fp_abs(&r, &f); break;
        case T_SGN: set_int(v, fp_sign(&f)); return;
        case T_INT: fp_floor(&r, &f); break;
        case T_FIX: fp_trunc(&r, &f); break;
        case T_CINT: set_int(v, to_int(&a[0])); return;
        case T_CDBL: r = f; break;
        case T_SQR: if (f.hi >> 31 && !fp_iszero(&f)) basic_error(E_ILLEGAL); fp_sqrt(&r, &f); break;
        case T_SIN: fp_sin(&r, &f); break;
        case T_COS: fp_cos(&r, &f); break;
        case T_TAN: fp_tan(&r, &f); break;
        case T_ATN: fp_atan(&r, &f); break;
        case T_EXP: fp_exp(&r, &f); if (fp_isinf(&r)) basic_error(E_OVERFLOW); break;
        case T_LOG: if (fp_sign(&f) <= 0) basic_error(E_ILLEGAL); fp_log(&r, &f); break;
        }
        if (t == T_CDBL) { v->type = V_FLT; v->f = r; v->s = 0; return; }   /* (CDBL keeps the float) */
        set_flt(v, &r);
        return;
    case T_RND:
        sp();
        if (*pc == '(') {
            args(a, 1);
            to_fp(&a[0], &f);
            if (fp_iszero(&f)) { v->type = V_FLT; v->f = rnd_last; v->s = 0; return; }   /* RND(0): the last one again */
            if (f.hi >> 31) { rnd_state = f.lo ^ f.hi ^ 0x9E3779B9; if (!rnd_state) rnd_state = 1; }
        }
        next_rnd(&rnd_last);
        v->type = V_FLT; v->f = rnd_last; v->s = 0;
        if (fp_iszero(&rnd_last)) set_int(v, 0);
        return;
    case T_PI: { fp64 one, four; fp_fromint(&one, 1); fp_atan(&r, &one); fp_fromint(&four, 4); fp_mul(&r, &r, &four); v->type = V_FLT; v->f = r; v->s = 0; return; }
    case T_TIMER: timer_now(&r); set_flt(v, &r); return;
    case T_ERR: set_int(v, err_code); return;
    case T_ERL: set_int(v, err_line); return;
    case T_FRE: if (*pc == '(') { args(a, 1); val_free(&a[0]); } set_int(v, 0); return;
    case T_POS: args(a, 1); set_int(v, col + 1); return;
    case T_INKEYS: n = key_get(); s = xmalloc(2); s[0] = n < 0 ? 0 : (char)n; s[1] = 0; set_str(v, s); return;
    case T_POINT: gr_point(v); return;
    case T_INPUTS: input_chars(v); return;
    case T_TIMES: case T_DATES: {
        time_t t2 = time(0);
        struct tm *tm = localtime(&t2);
        s = xmalloc(16);
        if (t == T_TIMES) sprintf(s, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
        else sprintf(s, "%02d-%02d-%04d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
        set_str(v, s);
        return;
    }
    case T_MAX: case T_MIN:
        args(a, 2);
        need_num(&a[0]);
        need_num(&a[1]);
        k = compare(&a[0], &a[1]);
        if ((t == T_MAX) == (k >= 0)) *v = a[0]; else *v = a[1];
        return;
    case T_LEN: args(a, 1); need_str(&a[0]); set_int(v, strlen(a[0].s)); val_free(&a[0]); return;
    case T_ASC:
        args(a, 1); need_str(&a[0]);
        if (!a[0].s[0]) basic_error(E_ILLEGAL);
        set_int(v, (unsigned char)a[0].s[0]); val_free(&a[0]); return;
    case T_VAL: {
        const char *p;
        args(a, 1); need_str(&a[0]);
        for (p = a[0].s; *p == ' '; p++) ;
        {
            int neg = *p == '-';
            if (*p == '-' || *p == '+') p++;
            if (!parse_num(p, v)) set_int(v, 0);
            else if (neg) { struct val z; set_int(&z, 0); arith('-', &z, v); *v = z; }
        }
        val_free(&a[0]);
        return;
    }
    case T_CHRS:
        args(a, 1); n = to_int(&a[0]);
        if (n < 0 || n > 255) basic_error(E_ILLEGAL);
        s = xmalloc(2); s[0] = n; s[1] = 0; set_str(v, s); return;
    case T_STRS: {
        char b[40];
        args(a, 1); need_num(&a[0]);
        num_text(&a[0], b + 1);
        b[0] = ' ';
        set_str(v, xstrdup(b[1] == '-' ? b + 1 : b));
        return;
    }
    case T_HEXS: case T_OCTS: {
        char b[16];
        args(a, 1);
        sprintf(b, t == T_HEXS ? "%X" : "%o", (unsigned)to_int(&a[0]));
        set_str(v, xstrdup(b));
        return;
    }
    case T_SPACES: args(a, 1); set_str(v, str_of(' ', to_int(&a[0]))); return;
    case T_STRINGS:
        args(a, 2);
        if (a[1].type == V_STR) { n = (unsigned char)a[1].s[0]; val_free(&a[1]); } else n = to_int(&a[1]);
        set_str(v, str_of(n, to_int(&a[0])));
        return;
    case T_UCASES: case T_LCASES:
        args(a, 1); need_str(&a[0]);
        for (s = a[0].s; *s; s++)
            if (t == T_UCASES) *s = upper(*s);
            else if (*s >= 'A' && *s <= 'Z') *s += 32;
        *v = a[0];
        return;
    case T_LEFTS: case T_RIGHTS:
        args(a, 2); need_str(&a[0]);
        n = to_int(&a[1]);
        if (n < 0) basic_error(E_ILLEGAL);
        k = strlen(a[0].s);
        if (n > k) n = k;
        s = xmalloc(n + 1);
        memcpy(s, t == T_LEFTS ? a[0].s : a[0].s + k - n, n);
        s[n] = 0;
        val_free(&a[0]);
        set_str(v, s);
        return;
    case T_MIDS: {
        int start, len;
        expect('(');
        eval(&a[0]); need_str(&a[0]);
        expect(',');
        start = int_arg();
        len = 0x7FFFFFFF;
        if (match(',')) len = int_arg();
        expect(')');
        if (start < 1 || len < 0) basic_error(E_ILLEGAL);
        k = strlen(a[0].s);
        if (start > k) len = 0;
        else if (len > k - start + 1) len = k - start + 1;
        s = xmalloc(len + 1);
        memcpy(s, a[0].s + (start > k ? 0 : start - 1), len);
        s[len] = 0;
        val_free(&a[0]);
        set_str(v, s);
        return;
    }
    case T_INSTR: {
        int start = 1;
        char *f2;
        expect('(');
        eval(&a[0]);
        if (a[0].type != V_STR) { start = to_int(&a[0]); expect(','); eval(&a[0]); }
        expect(',');
        eval(&a[1]);
        expect(')');
        need_str(&a[0]); need_str(&a[1]);
        if (start < 1) basic_error(E_ILLEGAL);
        n = 0;
        if (start <= (int)strlen(a[0].s) + 1 && (f2 = strstr(a[0].s + start - 1, a[1].s)) != 0) n = f2 - a[0].s + 1;
        if (start > (int)strlen(a[0].s)) n = 0;
        val_free(&a[0]); val_free(&a[1]);
        set_int(v, n);
        return;
    }
    case T_EOF: {
        struct bfile *bf;
        int c;
        args(a, 1);
        bf = file_of(to_int(&a[0]), 'I');
        c = getc(bf->f);
        if (c != EOF) ungetc(c, bf->f);
        set_int(v, c == EOF ? -1 : 0);
        return;
    }
    case T_FN: call_fn(v); return;
    case T_SQLNEXT: {
        int r;
        args(a, 1);
        r = sql_next(query_of(to_int(&a[0])));
        if (r < 0) sql_fail(sql_errmsg(bdb));
        set_int(v, r == 1 ? -1 : 0);
        return;
    }
    case T_SQLCOLS: args(a, 1); set_int(v, sql_ncols(query_of(to_int(&a[0])))); return;
    case T_SQLNAMES: case T_SQLVALS: case T_SQLVAL: case T_SQLNULL: {
        sqlstmt *st;
        int c;
        args(a, 2);
        st = query_of(to_int(&a[0]));
        c = column_of(st, to_int(&a[1]));
        if (t == T_SQLNAMES) { set_str(v, xstrdup(sql_colname(st, c))); return; }
        if (t == T_SQLNULL) { set_int(v, sql_isnull(st, c) ? -1 : 0); return; }
        if (t == T_SQLVALS) { set_str(v, xstrdup(sql_isnull(st, c) ? "" : sql_text(st, c))); return; }
        if (sql_isnull(st, c)) { set_int(v, 0); return; }          /* SQLVAL: the number */
        if (sql_coltype(st, c) == SQL_INTEGER) { set_int(v, sql_int(st, c)); return; }
        if (!parse_num(sql_text(st, c), v)) set_int(v, 0);
        return;
    }
    case T_SQLCHANGES: set_int(v, bdb ? sql_changes(bdb) : 0); return;
    case T_SQLERRS: set_str(v, xstrdup(sqlerr)); return;
    }
    basic_error(E_SYNTAX);
}

void atom(struct val *v)
{
    char name[NAMELEN + 2];
    int t, n;
    sp();
    t = token(&n);
    if (t == '"') {
        const unsigned char *e = ++pc;
        char *s;
        while (*e && *e != '"') e++;
        s = xmalloc(e - pc + 1);
        memcpy(s, pc, e - pc);
        s[e - pc] = 0;
        pc = (unsigned char *)(*e ? e + 1 : e);
        set_str(v, s);
        return;
    }
    if (isdigit_(t) || t == '.' || t == '&') {
        const char *e = parse_num((const char *)pc, v);
        if (!e) basic_error(E_SYNTAX);
        pc = (unsigned char *)e;
        return;
    }
    if (t == '(') { pc++; eval(v); expect(')'); return; }
    if (IS_FN(t)) { pc += n; function(t, v); return; }
    if (t == T_FN) { pc++; function(t, v); return; }
    if (get_name(name)) {
        struct lval lv;
        sp();
        if (*pc == '(') {
            struct var *a = lookup(name, 1);
            int off = subscript(a);
            lv.type = a->type;
            lv.n = a->na ? &a->na[off] : 0;
            lv.s = a->sa ? &a->sa[off] : 0;
        } else {
            struct var *s1 = lookup(name, 0);
            lv.type = s1->type;
            lv.n = &s1->n;
            lv.s = &s1->s;
        }
        load(&lv, v);
        return;
    }
    basic_error(E_SYNTAX);
}

void unary(struct val *v);

void power(struct val *v)        /* a ^ b ^ c, left to right */
{
    atom(v);
    while (match('^')) {
        struct val b;
        unary(&b);
        arith('^', v, &b);
    }
}

void unary(struct val *v)
{
    sp();
    if (*pc == '-') {
        struct val z;
        pc++;
        unary(v);
        need_num(v);
        if (v->type == V_INT && v->i != (int)0x80000000) { v->i = -v->i; return; }
        set_int(&z, 0);
        arith('-', &z, v);
        *v = z;
        return;
    }
    if (*pc == '+') { pc++; unary(v); need_num(v); return; }
    power(v);
}

void mul(struct val *v)
{
    unary(v);
    for (;;) {
        struct val b;
        int op;
        sp();
        op = *pc;
        if (op != '*' && op != '/') return;
        pc++;
        unary(&b);
        arith(op, v, &b);
    }
}

void idiv(struct val *v)         /* \ and MOD: on integers */
{
    mul(v);
    for (;;) {
        struct val b;
        int op, x, y;
        sp();
        op = *pc;
        if (op != '\\' && op != T_MOD) return;
        pc++;
        mul(&b);
        x = to_int(v);
        y = to_int(&b);
        if (!y) basic_error(E_DIV0);
        if (x == (int)0x80000000 && y == -1) basic_error(E_OVERFLOW);
        set_int(v, op == '\\' ? x / y : x % y);
    }
}

void add(struct val *v)
{
    idiv(v);
    for (;;) {
        struct val b;
        int op;
        sp();
        op = *pc;
        if (op != '+' && op != '-') return;
        pc++;
        idiv(&b);
        arith(op, v, &b);
    }
}

void relation(struct val *v)
{
    add(v);
    for (;;) {
        struct val b;
        int lt = 0, eq = 0, gt = 0, c;
        sp();
        while (*pc == '<' || *pc == '=' || *pc == '>') {
            if (*pc == '<') lt = 1; else if (*pc == '=') eq = 1; else gt = 1;
            pc++;
            sp();
        }
        if (!lt && !eq && !gt) return;
        add(&b);
        c = compare(v, &b);
        set_int(v, ((c < 0 && lt) || (c == 0 && eq) || (c > 0 && gt)) ? -1 : 0);
    }
}

void logic_not(struct val *v)
{
    sp();
    if (*pc == T_NOT) { pc++; logic_not(v); set_int(v, ~to_int(v)); return; }
    relation(v);
}

void logic(struct val *v, int level)     /* AND, OR, XOR, EQV, IMP: on integers, bit by bit */
{
    static const int ops[] = { T_AND, T_OR, T_XOR, T_EQV, T_IMP };
    if (level < 0) { logic_not(v); return; }
    logic(v, level - 1);
    for (;;) {
        struct val b;
        int x, y;
        sp();
        if (*pc != ops[level]) return;
        pc++;
        logic(&b, level - 1);
        x = to_int(v);
        y = to_int(&b);
        switch (ops[level]) {
        case T_AND: x &= y; break;
        case T_OR: x |= y; break;
        case T_XOR: x ^= y; break;
        case T_EQV: x = ~(x ^ y); break;
        case T_IMP: x = ~x | y; break;
        }
        set_int(v, x);
    }
}

void eval(struct val *v) { logic(v, 4); }

void eval_num(struct val *v) { eval(v); need_num(v); }

/* FNname(args): the parameters take the arguments' values for the time of the call */
void call_fn(struct val *out)
{
    char name[NAMELEN + 2];
    struct fn *f;
    struct val av[8];
    struct lval saved_lv[8];
    struct val saved[8];
    unsigned char *save_pc;
    int k, n = 0, save_cur;
    if (!get_name(name)) basic_error(E_SYNTAX);
    for (f = fns; f && strcmp(f->name, name); f = f->next) ;
    if (!f) basic_error(E_UNDEFFN);
    if (match('(')) {
        do { if (n == 8) basic_error(E_SYNTAX); eval(&av[n++]); } while (match(','));
        expect(')');
    }
    if (n != f->nparam) basic_error(E_SYNTAX);
    for (k = 0; k < n; k++) {           /* save the parameters, give them the arguments */
        struct var *pv = lookup(f->param[k], 0);
        saved_lv[k].type = pv->type; saved_lv[k].n = &pv->n; saved_lv[k].s = &pv->s;
        load(&saved_lv[k], &saved[k]);
        store(&saved_lv[k], &av[k]);
    }
    save_pc = pc;
    save_cur = cur;
    pc = f->body;
    eval(out);
    pc = save_pc;
    cur = save_cur;
    for (k = 0; k < n; k++) store(&saved_lv[k], &saved[k]);
    if (name_type(name) == V_STR) need_str(out); else need_num(out);
}

