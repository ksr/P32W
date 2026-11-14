/* bio.c -- P32W BASIC: PRINT (and USING), WRITE, INPUT, INKEY$'s keyboard,
 * DATA / READ, the control stack and moving around the program (basic.h) */
#include "basic.h"
#include <sys/select.h>
#ifdef __P32__
#include <sgtty.h>
#endif

/* ---------------------------------------------------------------- output */
void out_char(struct bfile *bf, int c)      /* bf 0: the screen */
{
    if (bf) { putc(c, bf->f); bf->col = c == '\n' ? 0 : bf->col + 1; return; }
    if (gr_text) { gr_char(c); return; }
    putchar(c);
    col = c == '\n' ? 0 : c == '\b' ? (col ? col - 1 : 0) : col + 1;
}
void out_str(struct bfile *bf, const char *s) { while (*s) out_char(bf, *s++); }

/* PRINT USING: one value through the field at *fmt (which moves past it) */
int using_field(const char *f)  /* the length of a field at f, 0 if f is not one */
{
    const char *p = f;
    if (*p == '!' || *p == '&') return 1;
    if (*p == '\\') { p++; while (*p == ' ') p++; return *p == '\\' ? p - f + 1 : 0; }
    if ((p[0] == '*' && p[1] == '*') || (p[0] == '$' && p[1] == '$')) p += 2;
    if (*p == '$' && f[0] == '*') p++;
    if (*p == '+' && p == f) p++;
    if (*p != '#' && !(*p == '.' && p[1] == '#') && p == f) return 0;
    while (*p == '#' || *p == ',') p++;
    if (*p == '.') { p++; while (*p == '#') p++; }
    if (!strncmp(p, "^^^^", 4)) p += 4;
    if (*p == '+' || *p == '-') p++;
    return p - f;
}

void using_value(struct bfile *bf, const char *f, int len, struct val *v)
{
    char d[20], out[80], digits[64];
    int lead_sign = 0, trail = 0, stars = 0, dollar = 0, comma = 0, ints = 0, decs = -1, expo = 0, neg, k, e, n, w;
    const char *p = f, *end = f + len;
    if (*f == '!' || *f == '&' || *f == '\\') {    /* string fields */
        need_str(v);
        if (*f == '&') out_str(bf, v->s);
        else {
            int wdt = *f == '!' ? 1 : len;
            for (k = 0; k < wdt; k++) out_char(bf, k < (int)strlen(v->s) ? v->s[k] : ' ');
        }
        val_free(v);
        return;
    }
    need_num(v);
    if (p[0] == '*' && p[1] == '*') { stars = 1; ints += 2; p += 2; if (*p == '$') { dollar = 1; p++; } }
    else if (p[0] == '$' && p[1] == '$') { dollar = 1; ints += 1; p += 2; }
    if (*p == '+') { lead_sign = 1; p++; }
    for (; p < end && (*p == '#' || *p == ','); p++) { if (*p == ',') comma = 1; ints++; }
    if (p < end && *p == '.') { p++; decs = 0; while (p < end && *p == '#') { decs++; p++; } }
    if (p + 4 <= end && !strncmp(p, "^^^^", 4)) { expo = 1; p += 4; }
    if (p < end && (*p == '+' || *p == '-')) trail = *p++;
    {
        fp64 x;
        to_fp(v, &x);
        neg = x.hi >> 31 && !fp_iszero(&x);
        fp_abs(&x, &x);
        n = 0;
        if (expo) {                     /* d.ddddE+xx (one # stays for the sign) */
            int lead = ints - (lead_sign || trail ? 0 : 1), nd;
            if (lead < 1) lead = 1;
            ints = lead + (lead_sign || trail ? 0 : 1);
            nd = lead + (decs > 0 ? decs : 0);
            if (nd > 17) nd = 17;
            if (fp_iszero(&x)) { memset(d, '0', nd); d[nd] = 0; e = 0; } else fp_digits(&x, nd, d, &e);
            k = 0;
            for (; k < lead; k++) digits[n++] = d[k];
            if (decs >= 0) { digits[n++] = '.'; for (; k < nd; k++) digits[n++] = d[k]; }
            e -= lead - 1;
            n += sprintf(digits + n, "E%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
        } else {
            int dec = decs > 0 ? decs : 0, nd, ipart;
            if (fp_iszero(&x)) e = -1, nd = 0;
            else {
                fp_digits(&x, 17, d, &e);
                nd = e + 1 + dec;
                if (nd > 17) nd = 17;
                if (nd >= 1) fp_digits(&x, nd, d, &e);
                nd = e + 1 + dec;
                if (nd > 17) nd = 17;
            }
            if (nd < 1) { e = -1; nd = 0; }
            ipart = e + 1;              /* integer digits */
            if (ipart <= 0) digits[n++] = '0';
            for (k = 0; k < ipart; k++) {
                if (comma && k && (ipart - k) % 3 == 0) digits[n++] = ',';
                digits[n++] = k < nd ? d[k] : '0';
            }
            if (decs >= 0) {
                digits[n++] = '.';
                for (k = 0; k < dec; k++) {
                    int pos = ipart + k;
                    digits[n++] = pos >= 0 && pos < nd ? d[pos] : '0';
                }
            }
            if (ipart <= 0 && ints == 0 && n > 1 && digits[0] == '0') { memmove(digits, digits + 1, n - 1); n--; }
        }
        digits[n] = 0;
    }
    /* the integer field's width: sign and $ in it unless at the end */
    w = ints + (decs >= 0 ? decs + 1 : 0) + (expo ? 4 : 0);
    {
        char *o = out;
        int need = n + (dollar ? 1 : 0) + ((neg && !trail) || lead_sign ? 1 : 0);
        if (need > w + (lead_sign ? 1 : 0)) *o++ = '%';
        for (k = need; k < w + (lead_sign ? 1 : 0); k++) *o++ = stars ? '*' : ' ';
        if (lead_sign) *o++ = neg ? '-' : '+';
        else if (neg && !trail) *o++ = '-';
        if (dollar) *o++ = '$';
        strcpy(o, digits);
        o += strlen(o);
        if (trail == '+') *o++ = neg ? '-' : '+';
        else if (trail == '-') *o++ = neg ? '-' : ' ';
        *o = 0;
        out_str(bf, out);
    }
    val_free(v);
}

/* ---------------------------------------------------------------- the control stack */
struct frame stack[NFRAME];
int nstack;

struct frame *push(int kind)
{
    if (nstack == NFRAME) basic_error(E_MEMORY);
    memset(&stack[nstack], 0, sizeof stack[0]);
    stack[nstack].kind = kind;
    stack[nstack].line = cur;
    return &stack[nstack++];
}

/* ---------------------------------------------------------------- moving around */
unsigned char *line_text(int k) { return k < 0 ? direct : prog[k].text; }

void go_line(int num)            /* GOTO num */
{
    int k = find_line(num);
    if (k >= nlines || prog[k].num != num) basic_error(E_LINE);
    cur = k;
    pc = prog[k].text;
    running = 1;
    jumped = 1;
    if (tron) { printf("[%d]", num); col += 3; }
}

int line_number(void)            /* a line number at pc */
{
    int n = 0, any = 0;
    sp();
    while (isdigit_(*pc)) { n = n * 10 + (*pc++ - '0'); any = 1; if (n > 65529) basic_error(E_SYNTAX); }
    if (!any) basic_error(E_SYNTAX);
    return n;
}

/* past the rest of this statement (strings skipped); stops at : or the end */
void skip_statement(void)
{
    while (*pc && *pc != ':') {
        if (*pc == '"') { pc++; while (*pc && *pc != '"') pc++; if (*pc) pc++; continue; }
        if (*pc == T_REM) { while (*pc) pc++; return; }
        pc += TLEN(pc);
    }
}

/* forward to the statement after the matching close (WEND for WHILE, NEXT for FOR) */
void skip_block(int open, int closer, int err)
{
    int depth = 1;
    for (;;) {
        while (*pc) {
            int t = *pc;
            if (t == '"') { pc++; while (*pc && *pc != '"') pc++; if (*pc) pc++; continue; }
            if (t == T_REM) { while (*pc) pc++; break; }
            if (t == T_DATA) { while (*pc && *pc != ':') pc++; continue; }
            pc += TLEN(pc);
            if (t == open) depth++;
            else if (t == closer) {
                if (closer == T_NEXT) {   /* NEXT I, J closes as many as it names */
                    char name[NAMELEN + 2];
                    unsigned char *save = pc;
                    int names = 0;
                    while (get_name(name)) { names++; if (!match(',')) break; }
                    if (!names) { names = 1; pc = save; }
                    depth -= names;
                } else
                    depth--;
                if (depth <= 0) return;
            }
        }
        if (cur < 0 || cur + 1 >= nlines) basic_error(err);
        cur++;
        pc = prog[cur].text;
    }
}

/* ---------------------------------------------------------------- DATA and READ */
int data_line = -1;
unsigned char *data_pc;

void restore_to(int num)
{
    data_line = num < 0 ? 0 : find_line(num);
    data_pc = 0;
}

/* the next DATA item, as text (owned by the caller) */
char *next_data(void)
{
    unsigned char *p = data_pc;
    char *s, *o;
    for (;;) {
        if (p && *p == ',') p++;
        else if (p && *p && *p != ':') ;
        else {
            if (p && *p == ':') p++;
            /* look for the next DATA */
            for (;;) {
                if (data_line < 0) data_line = 0;
                if (!p) { if (data_line >= nlines) basic_error(E_DATA); p = prog[data_line].text; }
                while (*p && *p != T_DATA) {
                    if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; continue; }
                    if (*p == T_REM) { while (*p) p++; break; }
                    p += TLEN(p);
                }
                if (*p == T_DATA) { p++; break; }
                data_line++;
                p = 0;
            }
        }
        break;
    }
    while (*p == ' ') p++;
    o = s = xmalloc(256);
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && o < s + 255) *o++ = *p++;
        if (*p == '"') p++;
        while (*p == ' ') p++;
    } else {
        while (*p && *p != ',' && *p != ':' && o < s + 255) *o++ = *p++;
        while (o > s && o[-1] == ' ') o--;
    }
    *o = 0;
    data_pc = p;
    return s;
}

/* text into a variable: a number for a numeric one (or an error) */
int text_to(struct lval *lv, char *text)
{
    struct val v;
    if (lv->type == V_STR) { set_str(&v, text); store(lv, &v); return 0; }
    {
        const char *p = text, *e;
        int neg = 0;
        while (*p == ' ') p++;
        if (*p == '-' || *p == '+') neg = *p++ == '-';
        if (!*p) { free(text); set_int(&v, 0); store(lv, &v); return 0; }
        e = parse_num(p, &v);
        if (!e) { free(text); return -1; }
        while (*e == ' ') e++;
        if (*e) { free(text); return -1; }
        if (neg) { struct val z; set_int(&z, 0); arith('-', &z, &v); v = z; }
        free(text);
        store(lv, &v);
        return 0;
    }
}

/* ---------------------------------------------------------------- INKEY$ (D113)
   A terminal gives BASIC whole lines and echoes what is typed. INKEY$ wants a key the moment it is pressed, and
   unseen: so the first INKEY$ puts the terminal in CBREAK without ECHO (^C still breaks), and BASIC puts it
   back as it found it whenever it wants a line again (its prompt, INPUT), runs a shell, or leaves. */
static int keys;                        /* the terminal is in that mode */
#ifdef __P32__
static struct sgttyb found;             /* ... and was like this */

static void key_exit(void) { key_mode(0); }
static void key_term(int sig) { key_mode(0); signal(sig, SIG_DFL); kill(getpid(), sig); }
#endif

void key_mode(int on)
{
#ifdef __P32__
    static int armed;
    struct sgttyb b;
    if (on == keys) return;
    if (on) {
        if (gtty(0, &found) < 0) return;
        if (!armed) { atexit(key_exit); signal(SIGTERM, key_term); signal(SIGHUP, key_term); armed = 1; }
        b = found;
        b.sg_flags = (b.sg_flags | CBREAK) & ~ECHO;
        stty(0, &b);
    } else stty(0, &found);
    keys = on;
#else
    (void)on;                           /* (BASIC built where the system is built: for the tests' results, no terminal) */
#endif
}

static int at_terminal(void)            /* is standard input a terminal? (asked once) */
{
    static int term = -1;
    if (term < 0) term = isatty(0);
    return term;
}

int key_get(void)
{
    fd_set r;
    struct timeval tv;
    unsigned char c;
    if (!at_terminal()) { int k = getc(stdin); return k == EOF ? -1 : k; }     /* (a file or a pipe: its next character) */
    key_mode(1);                        /* (first: a prompt still to be written is then seen with the keys unechoed) */
    fflush(stdout);
    FD_ZERO(&r);
    FD_SET(0, &r);
    tv.tv_sec = 0; tv.tv_usec = 0;
    if (select(1, &r, 0, 0, &tv) <= 0 || read(0, &c, 1) != 1) return -1;   /* (select with no time: is one waiting?) */
    return c;
}

/* INPUT$(n [, #f]): n characters from the keyboard, waited for and not shown (or from file f). Where INKEY$ in
   a loop keeps the processor, this sleeps in the read until a key comes. */
void input_chars(struct val *v)
{
    struct bfile *bf = 0;
    unsigned char c;
    char *s;
    int n, k, r;
    expect('(');
    n = int_arg();
    if (match(',')) { match('#'); bf = file_of(int_arg(), 'I'); }
    expect(')');
    if (n < 1 || n > 255) basic_error(E_ILLEGAL);
    s = xmalloc(n + 1);
    if (!bf && at_terminal()) { key_mode(1); fflush(stdout); gr_show(); }
    for (k = 0; k < n; k++) {
        if (bf || !at_terminal()) r = getc(bf ? bf->f : stdin);
        else {
            while ((r = read(0, &c, 1)) < 0 && errno == EINTR && !interrupted)
                ;
            r = r == 1 ? c : EOF;
        }
        if (r == EOF) {
            free(s);
            if (interrupted) { interrupted = 0; basic_error(E_BREAK); }
            basic_error(E_PAST);
        }
        s[k] = (char)r;
    }
    s[n] = 0;
    set_str(v, s);
}

/* ---------------------------------------------------------------- input */
int read_line(FILE *f, char *buf, int size)      /* without the newline; -1 at the end */
{
    int n = 0, c;
    for (;;) {
        c = getc(f);
        if (c == EOF) {
            if (interrupted) basic_error(E_BREAK);
            if (!n) return -1;
            break;
        }
        if (c == '\n') break;
        if (c != '\r' && n < size - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

/* split a typed line into fields (commas; quotes keep them) */
int split_fields(char *line, char **field, int max)
{
    int n = 0;
    char *p = line;
    for (;;) {
        char *o;
        while (*p == ' ') p++;
        if (n == max) return -1;
        field[n++] = o = p;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') *o++ = *p++;
            if (*p == '"') p++;
            while (*p == ' ') p++;
            if (*p && *p != ',') return -1;
        } else
            while (*p && *p != ',') *o++ = *p++;
        if (*p == ',') { *o = 0; if (o != p) ; p++; *o = 0; continue; }
        *o = 0;
        break;
    }
    return n;
}

void do_input(void)
{
    struct lval lv[32];
    char line[512], *field[32], prompt[256];
    int n = 0, k, qmark = 1;
    prompt[0] = 0;
    gr_show();
    sp();
    if (*pc == '#') {                   /* INPUT #n, a, b */
        struct bfile *bf;
        pc++;
        bf = file_of(int_arg(), 'I');
        expect(',');
        do {
            struct lval one;
            int c;
            char *s, *o;
            get_lval(&one);
            /* a field: up to a comma or the end of the line (quotes as in INPUT) */
            while ((c = getc(bf->f)) == ' ' || c == '\n' || c == '\r') ;
            if (c == EOF) basic_error(E_PAST);
            o = s = xmalloc(256);
            if (c == '"') {
                while ((c = getc(bf->f)) != EOF && c != '"') if (o < s + 255) *o++ = c;
                while ((c = getc(bf->f)) == ' ') ;
            } else {                    /* (a number also ends at a space) */
                do { if (o < s + 255) *o++ = c; }
                while ((c = getc(bf->f)) != EOF && c != ',' && c != '\n' && !(c == ' ' && one.type != V_STR));
                while (o > s && (o[-1] == ' ' || o[-1] == '\r')) o--;
                if (c == ' ') { while ((c = getc(bf->f)) == ' ') ; if (c != ',' && c != EOF) ungetc(c, bf->f); }
            }
            *o = 0;
            if (text_to(&one, s) < 0) basic_error(E_TYPE);
        } while (match(','));
        return;
    }
    if (*pc == '"') {
        struct val pv;
        atom(&pv);
        strncpy(prompt, pv.s, sizeof prompt - 1);
        prompt[sizeof prompt - 1] = 0;
        val_free(&pv);
        sp();
        if (*pc == ',') qmark = 0;
        else if (*pc != ';') basic_error(E_SYNTAX);
        pc++;
    }
    do {
        if (n == 32) basic_error(E_SYNTAX);
        get_lval(&lv[n++]);
    } while (match(','));
    key_mode(0);
    for (;;) {
        out_str(0, prompt);
        if (qmark) out_str(0, "? ");
        fflush(stdout);
        if (read_line(stdin, line, sizeof line) < 0) basic_error(E_PAST);
        col = 0;
        gr_text = 0;                    /* (the Return typed ends a line that LOCATE aimed at the picture) */
        k = split_fields(line, field, 32);
        if (k == n) {
            int bad = 0, j;
            for (j = 0; j < n && !bad; j++)
                if (lv[j].type != V_STR) {
                    struct val t;
                    const char *p = field[j], *e;
                    while (*p == ' ') p++;
                    if (*p == '-' || *p == '+') p++;
                    e = *p ? parse_num(p, &t) : p;
                    if (!e) bad = 1;
                    else { while (*e == ' ') e++; if (*e) bad = 1; }
                }
            if (!bad) {
                for (j = 0; j < n; j++) text_to(&lv[j], xstrdup(field[j]));
                return;
            }
        }
        out_str(0, "?Redo from start\n");
    }
}

void do_line_input(void)
{
    struct lval lv;
    char line[1024];
    struct val v;
    gr_show();
    sp();
    if (*pc == '#') {
        struct bfile *bf;
        pc++;
        bf = file_of(int_arg(), 'I');
        expect(',');
        get_lval(&lv);
        if (lv.type != V_STR) basic_error(E_TYPE);
        if (read_line(bf->f, line, sizeof line) < 0) basic_error(E_PAST);
    } else {
        if (*pc == '"') {
            struct val pv;
            atom(&pv);
            out_str(0, pv.s);
            val_free(&pv);
            if (!match(';') && !match(',')) basic_error(E_SYNTAX);
        }
        get_lval(&lv);
        if (lv.type != V_STR) basic_error(E_TYPE);
        key_mode(0);
        fflush(stdout);
        if (read_line(stdin, line, sizeof line) < 0) basic_error(E_PAST);
        col = 0;
        gr_text = 0;
    }
    set_str(&v, xstrdup(line));
    store(&lv, &v);
}

/* ---------------------------------------------------------------- PRINT and WRITE */
int at_end(void) { sp(); return !*pc || *pc == ':' || *pc == T_ELSE || (*pc == T_REM && pc[1] == '\''); }

void do_print(int write)
{
    struct bfile *bf = 0;
    char buf[64];
    int newline = 1;
    sp();
    if (*pc == '#') {
        pc++;
        bf = file_of(int_arg(), 'O');
        if (!match(',')) { if (!at_end()) basic_error(E_SYNTAX); }
    }
    if (!write && match(T_USING)) {     /* PRINT USING fmt; values */
        struct val fv;
        const char *f, *start;
        eval(&fv);
        need_str(&fv);
        if (!match(';')) expect(',');
        f = start = fv.s;
        while (!at_end()) {
            struct val v;
            int len;
            eval(&v);
            for (;;) {                  /* text up to the next field (round again at the end) */
                if (!*f) { f = start; if (!using_field(f) && !strpbrk(start, "#!&\\")) { val_free(&v); basic_error(E_ILLEGAL); } }
                if (*f == '_' && f[1]) { out_char(bf, f[1]); f += 2; continue; }
                if ((len = using_field(f)) != 0) break;
                out_char(bf, *f++);
            }
            using_value(bf, f, len, &v);
            f += len;
            newline = 1;
            if (match(';') || match(',')) { newline = 0; if (at_end()) break; newline = 1; continue; }
            break;
        }
        while (*f && !using_field(f)) { if (*f == '_' && f[1]) f++; out_char(bf, *f++); }
        if (newline) out_char(bf, '\n');
        free(fv.s);
        return;
    }
    while (!at_end()) {
        int c = *pc;
        newline = 1;
        if (c == ';') { pc++; newline = 0; continue; }
        if (c == ',') {
            pc++;
            newline = 0;
            if (write) out_char(bf, ',');
            else { int cl = bf ? bf->col : col; do out_char(bf, ' '); while ((++cl) % 14); }
            continue;
        }
        if (c == T_TAB || c == T_SPC) {
            int n, cl;
            pc++;
            n = int_arg();
            expect(')');
            cl = bf ? bf->col : col;
            if (c == T_TAB) { if (n - 1 < cl) out_char(bf, '\n'); cl = bf ? bf->col : col; while (cl++ < n - 1) out_char(bf, ' '); }
            else while (n-- > 0) out_char(bf, ' ');
            newline = 0;
            sp();
            if (*pc == ';') { pc++; }
            continue;
        }
        {
            struct val v;
            eval(&v);
            if (v.type == V_STR) {
                if (write) { out_char(bf, '"'); out_str(bf, v.s); out_char(bf, '"'); }
                else out_str(bf, v.s);
                val_free(&v);
            } else {
                num_text(&v, buf);
                if (write) out_str(bf, buf);
                else {
                    if (buf[0] != '-') out_char(bf, ' ');
                    out_str(bf, buf);
                    out_char(bf, ' ');
                }
            }
        }
        sp();
        if (write && *pc == ',') { pc++; out_char(bf, ','); newline = 1; continue; }
        if (*pc != ';' && *pc != ',' && !at_end()) continue;
    }
    if (newline || write) out_char(bf, '\n');
}

