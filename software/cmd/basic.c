/* basic -- a BASIC interpreter in the spirit of Microsoft BASIC (D27)
 *
 *   basic              the interactive editor: Ok, then lines and commands
 *   basic prog.bas     load prog.bas and run it; leave at its end
 *
 * Lines with a number are stored (a number alone deletes one); lines
 * without are run at once. Keywords are crunched into one-byte tokens
 * when a line is entered, LIST spells them out again, and SAVE writes
 * plain text (so a program can be edited with vi too).
 *
 * Numbers are whole numbers kept as 32-bit integers while they fit, and
 * IEEE double precision (softfp, about 15 digits) otherwise; A% is always
 * an integer, A$ a string. True is -1, false 0. man basic has the whole
 * language. */
#include "basic.h"

const char *const kw[] = {
    "END", "FOR", "NEXT", "DATA", "INPUT", "DIM", "READ", "LET", "GOTO", "RUN", "IF",
    "RESTORE", "GOSUB", "RETURN", "REM", "STOP", "ON", "PRINT", "CLEAR", "LIST", "NEW", "CONT",
    "DEF", "WHILE", "WEND", "ELSE", "THEN", "TO", "STEP", "FN", "USING", "TAB(", "SPC(", "NOT",
    "AND", "OR", "XOR", "MOD", "LOAD", "SAVE", "OPEN", "CLOSE", "AS", "OUTPUT", "APPEND", "LINE",
    "RANDOMIZE", "SWAP", "ERROR", "RESUME", "CLS", "SYSTEM", "SHELL", "DELETE", "RENUM", "TRON",
    "TROFF", "WRITE", "KILL", "NAME", "FILES", "EQV", "IMP", "ERASE", "SLEEP", "BEEP",
    "SQLOPEN", "SQLCLOSE", "SQLEXEC", "SQLQUERY", "SQLEND",
    "SCREEN", "PSET", "PRESET", "CIRCLE", "COLOR", "LOCATE",
    "ABS", "SGN", "INT", "FIX", "SQR", "SIN", "COS", "TAN", "ATN", "EXP", "LOG", "RND", "LEN",
    "VAL", "ASC", "CHR$", "STR$", "LEFT$", "RIGHT$", "MID$", "INSTR", "SPACE$", "STRING$", "HEX$",
    "OCT$", "UCASE$", "LCASE$", "EOF", "TIMER", "TIME$", "DATE$", "CINT", "CDBL", "ERR", "ERL",
    "POS", "FRE", "PI", "MAX", "MIN", "INKEY$", "POINT",
    "SQLNEXT", "SQLCOLS", "SQLNAME$", "SQLVAL$", "SQLVAL", "SQLNULL", "SQLCHANGES", "SQLERR$"
};

/* ---------------------------------------------------------------- the program */
struct line *prog;
int nlines, caplines;

jmp_buf on_error;                /* (an error unwinds to the statement loop) */
int cur;                         /* the line being run (index; -1: the direct line) */
unsigned char *pc;               /* where in it */
unsigned char direct[512];       /* the line typed without a number */
int running, tron, jumped;   /* (jumped: the statement moved cur / pc elsewhere) */
int err_code, err_line, err_handler = -1, in_handler;
int resume_line, cont_line = -1;
unsigned char *resume_pc, *cont_pc;
volatile int interrupted;

void basic_error(int e);

void *xmalloc(int n)
{
    void *p = malloc(n > 0 ? n : 1);
    if (!p) basic_error(E_MEMORY);
    return p;
}

char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

int find_line(int num)           /* the index of the first line >= num */
{
    int lo = 0, hi = nlines;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (prog[mid].num < num) lo = mid + 1; else hi = mid;
    }
    return lo;
}

void store_line(int num, const unsigned char *text, int len)
{
    int k = find_line(num);
    if (k < nlines && prog[k].num == num) {                /* replace, or delete */
        free(prog[k].text);
        if (!len) {
            memmove(prog + k, prog + k + 1, (nlines - k - 1) * sizeof *prog);
            nlines--;
            return;
        }
    } else {
        if (!len) return;
        if (nlines == caplines) {
            struct line *np;
            caplines = caplines ? caplines * 2 : 64;
            np = xmalloc(caplines * sizeof *np);
            if (prog) { memcpy(np, prog, nlines * sizeof *np); free(prog); }
            prog = np;
        }
        memmove(prog + k + 1, prog + k, (nlines - k) * sizeof *prog);
        nlines++;
    }
    prog[k].num = num;
    prog[k].len = len;
    prog[k].text = xmalloc(len + 1);
    memcpy(prog[k].text, text, len);
    prog[k].text[len] = 0;
}

/* ---------------------------------------------------------------- crunching */
int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int isalpha_(int c) { c = upper(c); return c >= 'A' && c <= 'Z'; }
int isdigit_(int c) { return c >= '0' && c <= '9'; }

/* text -> tokens: keywords (outside strings, at the start of a word) become
   one byte; REM, ' and DATA keep the rest of the statement as written */
int crunch(const char *s, unsigned char *out, int max)
{
    unsigned char *o = out, *end = out + max - 2;
    int raw = 0;                        /* 1 after REM (to the end), 2 after DATA (to a :) */
    while (*s && o < end) {
        if (raw == 1) { *o++ = *s++; continue; }
        if (*s == '"') {
            *o++ = *s++;
            while (*s && *s != '"' && o < end) *o++ = *s++;
            if (*s == '"') *o++ = *s++;
            continue;
        }
        if (raw == 2) {
            if (*s == ':') raw = 0;
            *o++ = *s++;
            continue;
        }
        if (*s == '\'') { *o++ = T_REM; *o++ = '\''; s++; raw = 1; continue; }
        if (*s == '?') { *o++ = T_PRINT; s++; continue; }
        if (isalpha_(*s)) {
            int k, n = 0, best = -1, bestlen = 0;
            while (isalpha_(s[n]) || isdigit_(s[n])) n++;
            if (s[n] == '$' || s[n] == '%') n++;
            for (k = 0; k < NKW; k++) {
                const char *w = kw[k];
                int wl = strlen(w), j;
                for (j = 0; j < wl && upper(s[j]) == w[j]; j++) ;
                if (j != wl) continue;
                /* a whole word, or TAB( / SPC( / FN with a name after it */
                if (wl == n || w[wl - 1] == '(' || (k + T_END == T_FN && wl < n)) {
                    if (wl > bestlen) { best = k; bestlen = wl; }
                }
            }
            if (best >= 0) {
                *o++ = (unsigned char)(best + T_END);
                s += bestlen;
                if (best + T_END == T_REM) raw = 1;
                if (best + T_END == T_DATA) raw = 2;
                continue;
            }
            while (n-- > 0 && o < end) *o++ = upper(*s++);  /* (names are kept in capitals) */
            continue;
        }
        if (*s == ':') { *o++ = *s++; continue; }
        *o++ = *s++;
    }
    *o = 0;
    return o - out;
}

/* tokens -> text, for LIST and SAVE */
void detok(const unsigned char *t, char *out, int max)
{
    char *o = out, *end = out + max - 12;
    int q = 0;
    while (*t && o < end) {
        if (*t == '"') q = !q;
        if (*t >= T_END && *t < T_LAST && !q) {
            const char *w = kw[*t - T_END];
            int c = *t;
            /* a space between a keyword and a letter or digit on either side */
            if (o > out && (isalpha_(o[-1]) || isdigit_(o[-1]) || o[-1] == '$' || o[-1] == ')' || o[-1] == '"')) *o++ = ' ';
            while (*w && o < end) *o++ = *w++;
            t++;
            if (c == T_REM && *t == '\'') { o -= 3; *o++ = '\''; t++; }      /* (the ' form) */
            if (c != T_FN && (isalpha_(*t) || isdigit_(*t) || *t == '"' || *t == '.') && w[-1] != '(') *o++ = ' ';
            continue;
        }
        *o++ = *t++;
    }
    *o = 0;
}

const char *errmsg(int e)
{
    switch (e) {
    case E_NEXT: return "NEXT without FOR";
    case E_SYNTAX: return "Syntax error";
    case E_RETURN: return "RETURN without GOSUB";
    case E_DATA: return "Out of DATA";
    case E_ILLEGAL: return "Illegal function call";
    case E_OVERFLOW: return "Overflow";
    case E_MEMORY: return "Out of memory";
    case E_LINE: return "Undefined line number";
    case E_SUBSCRIPT: return "Subscript out of range";
    case E_REDIM: return "Duplicate definition";
    case E_DIV0: return "Division by zero";
    case E_TYPE: return "Type mismatch";
    case E_CONT: return "Can't continue";
    case E_UNDEFFN: return "Undefined user function";
    case E_NORESUME: return "No RESUME";
    case E_RESUME: return "RESUME without error";
    case E_FORNEXT: return "FOR without NEXT";
    case E_WHILE: return "WHILE without WEND";
    case E_WEND: return "WEND without WHILE";
    case E_BADFILE: return "Bad file number";
    case E_NOFILE: return "File not found";
    case E_FILEMODE: return "Bad file mode";
    case E_OPEN: return "File already open";
    case E_PAST: return "Input past end";
    case E_FILENAME: return "Bad file name";
    case E_DEVICE: return "Device unavailable";
    case E_SQL: return "SQL error";
    }
    return "Unprintable error";
}
/* ---------------------------------------------------------------- the editor's commands */
void list_range(int *from, int *to)     /* [n][-[m]] at pc */
{
    *from = 0;
    *to = 65535;
    sp();
    if (isdigit_(*pc)) *from = *to = line_number();
    if (match('-')) { sp(); *to = isdigit_(*pc) ? line_number() : 65535; }
}

void list_lines(FILE *f, int from, int to)
{
    static char buf[1200];
    int k;
    for (k = find_line(from); k < nlines && prog[k].num <= to; k++) {
        if (interrupted) { interrupted = 0; break; }
        detok(prog[k].text, buf, sizeof buf);
        fprintf(f, "%d %s\n", prog[k].num, buf);
    }
}

void new_program(void)
{
    int k;
    for (k = 0; k < nlines; k++) free(prog[k].text);
    nlines = 0;
    clear_vars();
    clear_fns();
    close_all();
    nstack = 0;
    data_line = -1;
    data_pc = 0;
    cont_line = -1;
    err_handler = -1;
    in_handler = 0;
}

void clear_run(void)             /* RUN and CLEAR: variables, stacks, files, DATA */
{
    clear_vars();
    clear_fns();
    close_all();
    nstack = 0;
    data_line = -1;
    data_pc = 0;
    err_handler = -1;
    in_handler = 0;
    err_code = err_line = 0;
}

char *file_name(char *buf)       /* "name" at pc, with .bas added when it has no . */
{
    struct val v;
    eval(&v);
    need_str(&v);
    if (!v.s[0] || strlen(v.s) > 200) { val_free(&v); basic_error(E_FILENAME); }
    strcpy(buf, v.s);
    val_free(&v);
    return buf;
}

void prog_name(char *buf)
{
    file_name(buf);
    {
        char *slash = strrchr(buf, '/'), *base = slash ? slash + 1 : buf;
        if (!strchr(base, '.')) strcat(buf, ".bas");
    }
}

int enter_line(char *text);

void load_file(const char *name)
{
    FILE *f = fopen(name, "r");
    char line[1024];
    if (!f) basic_error(E_NOFILE);
    new_program();
    while (read_line(f, line, sizeof line) >= 0) {
        char *p = line;
        while (*p == ' ') p++;
        if (!*p || (p[0] == '#' && p[1] == '!')) continue;       /* (blank lines, and a #! line) */
        if (!isdigit_(*p)) { fclose(f); basic_error(E_SYNTAX); }
        enter_line(p);
    }
    fclose(f);
}

void save_file(const char *name)
{
    FILE *f = fopen(name, "w");
    if (!f) basic_error(E_FILENAME);
    list_lines(f, 0, 65535);
    if (fclose(f)) basic_error(E_FILENAME);
}

/* RENUM [new][,[old][,step]]: the lines from old on become new, new+step, ...;
   GOTO, GOSUB, THEN, ELSE, RESTORE, RESUME, RUN and ON ... lists follow */
int renum_map(int *oldn, int *newn, int n, int num)
{
    int k;
    for (k = 0; k < n; k++) if (oldn[k] == num) return newn[k];
    return num;
}

void renumber(void)
{
    int start = 10, from = 0, step = 10, k, n = 0, *oldn, *newn, first;
    sp();
    if (isdigit_(*pc)) start = line_number();
    if (match(',')) { sp(); if (isdigit_(*pc)) from = line_number(); if (match(',')) step = line_number(); }
    if (step < 1) basic_error(E_ILLEGAL);
    first = find_line(from);
    if (first > 0 && prog[first - 1].num >= start) basic_error(E_ILLEGAL);
    if (start + (long)(nlines - first - 1) * step > 65529) basic_error(E_ILLEGAL);
    oldn = xmalloc((nlines + 1) * sizeof *oldn);
    newn = xmalloc((nlines + 1) * sizeof *newn);
    for (k = first; k < nlines; k++) { oldn[n] = prog[k].num; newn[n] = start + (k - first) * step; n++; }
    for (k = 0; k < nlines; k++) {      /* the references in every line */
        static unsigned char buf[1200];
        unsigned char *t = prog[k].text, *o = buf;
        int q = 0, refs = 0;
        while (*t && o < buf + sizeof buf - 12) {
            int c = *t++;
            *o++ = c;
            if (c == '"') { q = !q; continue; }
            if (q) continue;
            if (c == T_REM) { while (*t) *o++ = *t++; break; }
            if (c == T_GOTO || c == T_GOSUB || c == T_THEN || c == T_ELSE || c == T_RESTORE || c == T_RESUME || c == T_RUN) refs = 1;
            else if (c != ' ' && c != ',' && !isdigit_(c)) refs = 0;
            if (refs) {
                while (*t == ' ') *o++ = *t++;
                if (isdigit_(*t)) {
                    int num = 0;
                    while (isdigit_(*t)) num = num * 10 + (*t++ - '0');
                    o += sprintf((char *)o, "%d", renum_map(oldn, newn, n, num));
                }
            }
        }
        *o = 0;
        free(prog[k].text);
        prog[k].len = o - buf;
        prog[k].text = xmalloc(prog[k].len + 1);
        memcpy(prog[k].text, buf, prog[k].len + 1);
    }
    for (k = first; k < nlines; k++) prog[k].num = newn[k - first];
    free(oldn);
    free(newn);
    cont_line = -1;
}

void run_shell(const char *cmd)
{
    int pid, st;
    fflush(stdout);
    key_mode(0);
    if ((pid = fork()) < 0) basic_error(E_MEMORY);
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        if (cmd) execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        else execl("/bin/sh", "sh", (char *)0);
        _exit(127);
    }
    signal(SIGINT, SIG_IGN);
    while (wait(&st) != pid) ;
    signal(SIGINT, on_int);             /* (^C is BASIC's again: it stayed ignored after the first SHELL or FILES) */
    col = 0;
}

/* ---------------------------------------------------------------- statements */
unsigned char *stmt_pc;          /* where the statement being run began */
void statement(void);

void do_if(void)
{
    struct val c;
    int truth, depth;
    eval(&c);
    truth = c.type == V_STR ? (val_free(&c), basic_error(E_TYPE), 0) : (c.type == V_INT ? c.i != 0 : !fp_iszero(&c.f));
    sp();
    if (*pc == T_THEN) pc++;
    else if (*pc != T_GOTO) basic_error(E_SYNTAX);
    jumped = 1;                         /* (what follows THEN or ELSE runs as statements of their own) */
    if (truth) {
        sp();
        if (isdigit_(*pc)) go_line(line_number());
        return;                         /* (the statements after THEN run; an ELSE then ends the line) */
    }
    /* false: to the matching ELSE, or the end of the line */
    depth = 0;
    while (*pc) {
        if (*pc == '"') { pc++; while (*pc && *pc != '"') pc++; if (*pc) pc++; continue; }
        if (*pc == T_REM) { while (*pc) pc++; break; }
        if (*pc == T_IF) depth++;
        if (*pc == T_ELSE) {
            if (depth-- == 0) {
                pc++;
                sp();
                if (isdigit_(*pc)) go_line(line_number());
                return;
            }
        }
        pc++;
    }
}

void do_for(void)
{
    struct lval lv;
    struct val start, lim, step, cur_v;
    struct frame *f;
    int k, cmp_r, sgn;
    unsigned char *save;
    get_lval(&lv);
    if (lv.type == V_STR) basic_error(E_TYPE);
    expect('=');
    eval_num(&start);
    if (!match(T_TO)) basic_error(E_SYNTAX);
    eval_num(&lim);
    set_int(&step, 1);
    if (match(T_STEP)) eval_num(&step);
    store(&lv, &start);
    for (k = nstack - 1; k >= 0 && stack[k].kind == F_FOR; k--)     /* the same variable again: that loop ends */
        if (stack[k].var.n == lv.n) { nstack = k; break; }
    /* already past the limit: the body is skipped */
    load(&lv, &cur_v);
    {
        struct val a = cur_v, b = lim, z;
        set_int(&z, 0);
        { struct val s2 = step; sgn = compare(&s2, &z); }
        cmp_r = compare(&a, &b);
    }
    if ((sgn >= 0 && cmp_r > 0) || (sgn < 0 && cmp_r < 0)) {
        save = pc;
        (void)save;
        skip_block(T_FOR, T_NEXT, E_FORNEXT);
        return;
    }
    f = push(F_FOR);
    f->var = lv;
    f->limit = lim;
    f->step = step;
    skip_statement();
    f->pc = pc;
}

void do_next(void)
{
    char name[NAMELEN + 2];
    for (;;) {
        struct lval lv;
        struct val v, st, lim, z;
        int k, sgn, c, have = 0;
        unsigned char *save = pc;
        if (get_name(name)) {
            pc = save;
            get_lval(&lv);
            have = 1;
        }
        for (k = nstack - 1; k >= 0 && stack[k].kind == F_FOR; k--)
            if (!have || stack[k].var.n == lv.n) break;
        if (k < 0 || stack[k].kind != F_FOR) basic_error(E_NEXT);
        nstack = k + 1;
        load(&stack[k].var, &v);
        st = stack[k].step;
        arith('+', &v, &st);
        store(&stack[k].var, &v);
        load(&stack[k].var, &v);
        lim = stack[k].limit;
        set_int(&z, 0);
        { struct val s2 = stack[k].step; sgn = compare(&s2, &z); }
        c = compare(&v, &lim);
        if ((sgn >= 0 && c <= 0) || (sgn < 0 && c >= 0)) {     /* round again */
            cur = stack[k].line;
            pc = stack[k].pc;
            jumped = 1;
            return;
        }
        nstack = k;                     /* done: on to the next name, or the next statement */
        if (!have || !match(',')) return;
    }
}

void do_on(void)
{
    struct val v;
    int n, k, t, num = -1;
    sp();
    if (*pc == T_ERROR) {               /* ON ERROR GOTO n */
        pc++;
        if (!match(T_GOTO)) basic_error(E_SYNTAX);
        n = line_number();
        if (n == 0) {
            err_handler = -1;
            if (in_handler) basic_error(err_code);    /* (inside a handler: the error stands) */
            return;
        }
        k = find_line(n);
        if (k >= nlines || prog[k].num != n) basic_error(E_LINE);
        err_handler = n;
        return;
    }
    eval(&v);
    n = to_int(&v);
    if (n < 0 || n > 255) basic_error(E_ILLEGAL);
    sp();
    t = *pc;
    if (t != T_GOTO && t != T_GOSUB) basic_error(E_SYNTAX);
    pc++;
    for (k = 1; ; k++) {
        int ln = line_number();
        if (k == n) num = ln;
        if (!match(',')) break;
    }
    if (num < 0) return;                /* (out of range: on to the next statement) */
    if (t == T_GOSUB) {
        struct frame *f = push(F_GOSUB);
        f->pc = pc;
    }
    go_line(num);
}

void do_dim(void)
{
    do {
        char name[NAMELEN + 2];
        int d[MAXDIM], n = 0;
        if (!get_name(name)) basic_error(E_SYNTAX);
        expect('(');
        do { if (n == MAXDIM) basic_error(E_SUBSCRIPT); d[n++] = int_arg(); } while (match(','));
        expect(')');
        dim_array(lookup(name, 1), n, d);
    } while (match(','));
}

void do_open(void)
{
    char name[256];
    int mode = 0, n;
    const char *m;
    file_name(name);
    if (!match(T_FOR)) basic_error(E_SYNTAX);
    sp();
    if (*pc == T_INPUT) mode = 'I';
    else if (*pc == T_OUTPUT) mode = 'O';
    else if (*pc == T_APPEND) mode = 'A';
    else basic_error(E_SYNTAX);
    pc++;
    if (!match(T_AS)) basic_error(E_SYNTAX);
    match('#');
    n = int_arg();
    if (n < 1 || n >= NFILES) basic_error(E_BADFILE);
    if (files[n].f) basic_error(E_OPEN);
    m = mode == 'I' ? "r" : mode == 'O' ? "w" : "a";
    if (!(files[n].f = fopen(name, m))) basic_error(mode == 'I' ? E_NOFILE : E_FILENAME);
    files[n].mode = mode;
    files[n].col = 0;
}

void do_close(void)
{
    if (at_end()) { close_all(); return; }
    do {
        int n;
        match('#');
        n = int_arg();
        if (n < 1 || n >= NFILES) basic_error(E_BADFILE);
        if (files[n].f) { fclose(files[n].f); files[n].f = 0; files[n].mode = 0; }
    } while (match(','));
}

void do_def(void)
{
    char name[NAMELEN + 2];
    struct fn *f;
    if (!match(T_FN)) basic_error(E_SYNTAX);
    if (!get_name(name)) basic_error(E_SYNTAX);
    for (f = fns; f && strcmp(f->name, name); f = f->next) ;
    if (!f) {
        f = xmalloc(sizeof *f);
        memset(f, 0, sizeof *f);
        strcpy(f->name, name);
        f->next = fns;
        fns = f;
    }
    f->nparam = 0;
    if (match('(')) {
        do {
            if (f->nparam == 8 || !get_name(f->param[f->nparam])) basic_error(E_SYNTAX);
            f->nparam++;
        } while (match(','));
        expect(')');
    }
    expect('=');
    f->line = cur;
    f->body = pc;
    if (cur < 0) basic_error(E_ILLEGAL);   /* (a definition lives in the program) */
    skip_statement();
}

void do_resume(void)
{
    if (!in_handler) basic_error(E_RESUME);
    in_handler = 0;
    sp();
    if (*pc == T_NEXT) {
        cur = resume_line;
        pc = resume_pc;
        jumped = 1;
        skip_statement();
        return;
    }
    if (isdigit_(*pc)) {
        int n = line_number();
        if (n) { go_line(n); return; }
    }
    cur = resume_line;
    pc = resume_pc;
    jumped = 1;
}

void statement(void)
{
    int t;
    sp();
    stmt_pc = pc;
    t = *pc;
    if (t < T_END || t >= T_ABS) {      /* an assignment without LET (or MID$...) */
        struct lval lv;
        struct val v;
        if (t == T_MIDS) basic_error(E_SYNTAX);
        get_lval(&lv);
        expect('=');
        eval(&v);
        store(&lv, &v);
        return;
    }
    pc++;
    switch (t) {
    case T_LET: { struct lval lv; struct val v; get_lval(&lv); expect('='); eval(&v); store(&lv, &v); return; }
    case T_PRINT: do_print(0); return;
    case T_WRITE: do_print(1); return;
    case T_REM: while (*pc) pc++; return;
    case T_DATA: skip_statement(); return;
    case T_IF: do_if(); return;
    case T_ELSE: while (*pc) pc++; return;                 /* (after a THEN part ran) */
    case T_GOTO: go_line(line_number()); return;
    case T_GOSUB: {
        int n = line_number();
        struct frame *f = push(F_GOSUB);
        f->pc = pc;
        go_line(n);
        return;
    }
    case T_RETURN: {
        int k;
        for (k = nstack - 1; k >= 0 && stack[k].kind != F_GOSUB; k--) ;
        if (k < 0) basic_error(E_RETURN);
        nstack = k;
        cur = stack[k].line;
        pc = stack[k].pc;
        jumped = 1;
        sp();
        if (isdigit_(*pc)) go_line(line_number());
        return;
    }
    case T_FOR: do_for(); return;
    case T_NEXT: do_next(); return;
    case T_WHILE: {
        struct val c;
        unsigned char *at = stmt_pc;
        int truth;
        eval(&c);
        if (c.type == V_STR) { val_free(&c); basic_error(E_TYPE); }
        truth = c.type == V_INT ? c.i != 0 : !fp_iszero(&c.f);
        if (truth) {
            struct frame *f;
            if (nstack && stack[nstack - 1].kind == F_WHILE && stack[nstack - 1].pc == at) f = &stack[nstack - 1];
            else f = push(F_WHILE);
            f->line = cur;
            f->pc = at;
        } else {
            if (nstack && stack[nstack - 1].kind == F_WHILE && stack[nstack - 1].pc == at) nstack--;
            skip_block(T_WHILE, T_WEND, E_WHILE);
        }
        return;
    }
    case T_WEND:
        if (!nstack || stack[nstack - 1].kind != F_WHILE) basic_error(E_WEND);
        cur = stack[nstack - 1].line;
        pc = stack[nstack - 1].pc;
        jumped = 1;
        return;
    case T_ON: do_on(); return;
    case T_INPUT: do_input(); return;
    case T_LINE: if (match(T_INPUT)) do_line_input(); else gr_statement(T_LINE); return;
    case T_SCREEN: case T_PSET: case T_PRESET: case T_CIRCLE: case T_COLOR: case T_LOCATE: gr_statement(t); return;
    case T_READ:
        do {
            struct lval lv;
            get_lval(&lv);
            if (text_to(&lv, next_data()) < 0) basic_error(E_SYNTAX);
        } while (match(','));
        return;
    case T_RESTORE:
        sp();
        restore_to(isdigit_(*pc) ? line_number() : -1);
        return;
    case T_DIM: do_dim(); return;
    case T_ERASE:
        do {
            char name[NAMELEN + 2];
            struct var *a;
            if (!get_name(name)) basic_error(E_SYNTAX);
            a = lookup(name, 1);
            if (a->na) free(a->na);
            if (a->sa) { int k; for (k = 0; k < a->total; k++) if (a->sa[k]) free(a->sa[k]); free(a->sa); }
            a->na = 0; a->sa = 0; a->ndim = a->total = 0;
        } while (match(','));
        return;
    case T_DEF: do_def(); return;
    case T_END:
        close_all();
        running = 0;
        cur = -1;
        pc = direct;
        direct[0] = 0;
        jumped = 1;
        return;
    case T_STOP: basic_error(E_BREAK); return;
    case T_SWAP: {
        struct lval a, b;
        struct val x, y;
        get_lval(&a);
        expect(',');
        get_lval(&b);
        if ((a.type == V_STR) != (b.type == V_STR)) basic_error(E_TYPE);
        load(&a, &x);
        load(&b, &y);
        store(&a, &y);
        store(&b, &x);
        return;
    }
    case T_RANDOMIZE: {
        struct val v;
        if (at_end()) rnd_state = (unsigned)time(0) * 2654435761u;
        else { eval(&v); { fp64 f; to_fp(&v, &f); rnd_state = f.lo ^ f.hi * 2654435761u; } }
        if (!rnd_state) rnd_state = 1;
        return;
    }
    case T_CLS: if (gr_on) gr_statement(T_CLS); else { out_str(0, "\033[H\033[2J"); col = 0; } return;
    case T_BEEP: out_char(0, 7); col--; return;
    case T_SLEEP: { int n = at_end() ? 1 : int_arg(); fflush(stdout); gr_show(); if (n > 0) sleep(n); return; }
    case T_OPEN: do_open(); return;
    case T_SQLOPEN: {
        char name[256];
        file_name(name);
        sql_shut();
        if (!(bdb = sql_open(name, 1))) sql_fail(sql_errmsg(0));
        return;
    }
    case T_SQLCLOSE: sql_shut(); return;
    case T_SQLEXEC: {
        struct val v;
        int r;
        eval(&v);
        need_str(&v);
        if (!bdb) { val_free(&v); sql_fail("no database is open (SQLOPEN)"); }
        r = sql_exec(bdb, v.s);
        val_free(&v);
        if (r < 0) sql_fail(sql_errmsg(bdb));
        return;
    }
    case T_SQLQUERY: {
        struct val v;
        int q = int_arg();
        if (q < 1 || q > 4) basic_error(E_ILLEGAL);
        expect(',');
        eval(&v);
        need_str(&v);
        if (!bdb) { val_free(&v); sql_fail("no database is open (SQLOPEN)"); }
        if (bq[q]) { sql_finish(bq[q]); bq[q] = 0; }
        bq[q] = sql_query(bdb, v.s);
        val_free(&v);
        if (!bq[q]) sql_fail(sql_errmsg(bdb));
        return;
    }
    case T_SQLEND: {
        int q = int_arg();
        if (q < 1 || q > 4) basic_error(E_ILLEGAL);
        if (bq[q]) { sql_finish(bq[q]); bq[q] = 0; }
        return;
    }
    case T_CLOSE: do_close(); return;
    case T_ERROR: { int n = int_arg(); if (n < 1 || n > 255) basic_error(E_ILLEGAL); basic_error(n); return; }
    case T_RESUME: do_resume(); return;
    case T_KILL: { char name[256]; file_name(name); if (unlink(name) < 0) basic_error(E_NOFILE); return; }
    case T_NAME: {
        char a[256], b[256];
        file_name(a);
        if (!match(T_AS)) basic_error(E_SYNTAX);
        file_name(b);
        if (rename(a, b) < 0) basic_error(E_NOFILE);
        return;
    }
    case T_FILES: {
        char name[256], cmd[300];
        if (at_end()) strcpy(name, "");
        else file_name(name);
        sprintf(cmd, "ls -C %s", name);
        run_shell(cmd);
        return;
    }
    case T_SHELL: {
        char cmd[256];
        if (at_end()) run_shell(0);
        else run_shell(file_name(cmd));
        return;
    }
    case T_SYSTEM: fflush(stdout); exit(0);
    case T_TRON: tron = 1; return;
    case T_TROFF: tron = 0; return;
    case T_CLEAR: clear_run(); while (*pc && *pc != ':') pc++; return;
    /* the editor's commands (in a program too, as in Microsoft BASIC) */
    case T_NEW: new_program(); running = 0; cur = -1; pc = direct; direct[0] = 0; jumped = 1; return;
    case T_LIST: { int a, b; list_range(&a, &b); list_lines(stdout, a, b); col = 0; return; }
    case T_DELETE: {
        int a, b, k;
        list_range(&a, &b);
        if (a == 0 && b == 65535) basic_error(E_ILLEGAL);
        for (k = find_line(a); k < nlines && prog[k].num <= b; ) store_line(prog[k].num, (unsigned char *)"", 0);
        cont_line = -1;
        return;
    }
    case T_RENUM: renumber(); return;
    case T_SAVE: { char name[256]; prog_name(name); save_file(name); return; }
    case T_LOAD: {
        char name[256];
        prog_name(name);
        load_file(name);
        running = 0; cur = -1; pc = direct; direct[0] = 0; jumped = 1;
        return;
    }
    case T_RUN: {
        sp();
        if (*pc == '"') {
            char name[256];
            prog_name(name);
            load_file(name);
            clear_run();
            if (!nlines) { running = 0; cur = -1; pc = direct; direct[0] = 0; jumped = 1; return; }
            cur = 0; pc = prog[0].text; running = 1; jumped = 1;
            return;
        }
        clear_run();
        restore_to(-1);
        if (isdigit_(*pc)) { go_line(line_number()); return; }
        if (!nlines) { running = 0; cur = -1; pc = direct; direct[0] = 0; jumped = 1; return; }
        cur = 0;
        pc = prog[0].text; jumped = 1;
        running = 1;
        return;
    }
    case T_CONT:
        if (cont_line < 0) basic_error(E_CONT);
        cur = cont_line;
        pc = cont_pc;
        jumped = 1;
        running = 1;
        cont_line = -1;
        return;
    }
    basic_error(E_SYNTAX);
}

/* ---------------------------------------------------------------- running */
int handler_jump;

void basic_error(int e)
{
    int num = cur >= 0 && cur < nlines ? prog[cur].num : -1;
    fflush(stdout);
    if (e != E_BREAK && running && err_handler >= 0 && !in_handler) {    /* ON ERROR GOTO */
        err_code = e;
        err_line = num < 0 ? 0 : num;
        resume_line = cur;
        resume_pc = stmt_pc;
        in_handler = 1;
        go_line(err_handler);
        handler_jump = 1;
        longjmp(on_error, 2);
    }
    if (col) { putchar('\n'); col = 0; }
    if (e == E_BREAK) {
        if (num >= 0) printf("Break in %d\n", num); else printf("Break\n");
        if (running) { cont_line = cur; cont_pc = pc; while (*cont_pc && *cont_pc != ':') cont_pc++; }
    } else {
        if (e == E_SQL) printf("SQL error: %s", sqlerr);
        else printf("%s", errmsg(e));
        if (num >= 0 && running) printf(" in %d", num);
        putchar('\n');
        cont_line = -1;
        err_code = e;
        err_line = num < 0 ? 0 : num;
    }
    running = 0;
    in_handler = 0;
    nstack = running ? nstack : nstack;
    cur = -1;
    pc = direct;
    direct[0] = 0;
    longjmp(on_error, 1);
}

void run_loop(void)              /* until the program (or the direct line) ends */
{
    int n = 0;
    for (;;) {
        if (interrupted) { interrupted = 0; basic_error(E_BREAK); }
        if (gr_on && !(++n & 15)) gr_show();    /* (a picture being drawn is seen as it grows) */
        sp();
        if (*pc == ':') { pc++; continue; }
        if (!*pc) {
            if (cur < 0 || !running) return;
            if (++cur >= nlines) { running = 0; close_all(); return; }
            pc = prog[cur].text;
            if (tron) { printf("[%d]", prog[cur].num); col += 3; }
            continue;
        }
        jumped = 0;
        statement();
        sp();
        if (!jumped && *pc && *pc != ':' && *pc != T_ELSE && !(*pc == T_REM && pc[1] == '\'')) basic_error(E_SYNTAX);
    }                                   /* (a ' remark may follow a statement with no colon) */
}

int go(void)                     /* run what is at cur / pc: 0, or 1 after an error */
{
    for (;;) {
        int r = setjmp(on_error);
        if (r == 0) { run_loop(); return 0; }
        if (r == 2) continue;           /* into an error handler */
        return 1;
    }
}

/* a line typed or loaded: with a number it is stored, without it is run */
int enter_line(char *text)
{
    static unsigned char buf[1024];
    int n;
    char *p = text;
    while (*p == ' ') p++;
    if (isdigit_(*p)) {
        int num = 0;
        while (isdigit_(*p)) { num = num * 10 + (*p++ - '0'); if (num > 65529) { printf("Syntax error\n"); return 1; } }
        if (*p == ' ') p++;
        n = crunch(p, buf, sizeof buf);
        while (n && buf[n - 1] == ' ') buf[--n] = 0;
        store_line(num, buf, n);
        cont_line = -1;
        return 0;
    }
    n = crunch(p, direct, sizeof direct);
    cur = -1;
    pc = direct;
    running = 0;
    return go();
}

void on_int(int sig) { (void)sig; signal(SIGINT, on_int); interrupted = 1; }

int main(int argc, char **argv)
{
    char line[1024];
    t0_time = time(0);
    {
        struct tms tb;
        t0_ticks = times(&tb);
    }
    rnd_state = 0x2545F491;
    signal(SIGINT, on_int);
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        fprintf(stderr, "usage: basic [program.bas]\n");
        return 2;
    }
    if (argc == 2) {                    /* basic prog.bas: run it, then leave */
        int bad;
        if (setjmp(on_error)) return 1;
        load_file(argv[1]);
        if (!nlines) return 0;
        clear_run();
        cur = 0;
        pc = prog[0].text; jumped = 1;
        running = 1;
        bad = go();
        fflush(stdout);
        return bad;
    }
    printf("P32W BASIC (D27): integers and double precision. HELP is man basic; SYSTEM leaves.\nOk\n");
    for (;;) {
        int k;
        key_mode(0);                    /* (lines again, and echo, before anything that looks like a prompt is out) */
        fflush(stdout);
        gr_show();
        interrupted = 0;
        k = read_line(stdin, line, sizeof line);
        gr_typed();
        if (k < 0) {
            if (interrupted || errno == EINTR) { clearerr(stdin); interrupted = 0; putchar('\n'); continue; }
            break;
        }
        col = 0;
        {
            char *p = line;
            while (*p == ' ') p++;
            if (!*p) continue;
            if (isdigit_(*p)) { enter_line(line); continue; }
        }
        enter_line(line);
        key_mode(0);
        if (col) putchar('\n');
        col = 0;
        printf("Ok\n");
    }
    putchar('\n');
    return 0;
}

