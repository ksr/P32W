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
void out_val(struct bfile *bf, const char *s) { int n = SLEN(s); while (n-- > 0) out_char(bf, (unsigned char)*s++); }

/* ---------------------------------------------------------------- PRINT USING
 * A field of the format, as Microsoft's BASIC has them, and what it prints is always as wide as the field is
 * written (a % before it when the value does not fit):
 *
 *   [+] [** | **$ | $$] [# and ,] [. #] [^^^^ | ^^^^^] [+ | -]         (the sign after only without the + before)
 *
 * ** and $$ are two places and **$ three, one of them the $'s; a , among the #s is a place too. The sign of a
 * negative number takes a place before the point unless a + or - gives it one. */
struct ufield {
    int len;                            /* its characters */
    int plus, trail;                    /* a + before; '+' or '-' after (or 0) */
    int stars, dollar, comma;
    int before;                         /* places before the point: digits, the fill, the $, the commas */
    int decs;                           /* digits after the point; -1: no point */
    int expo;                           /* the exponent's digits: 2 (^^^^), 3 (^^^^^), or 0: none */
};

static int num_field(const char *f, struct ufield *u)   /* its length, 0 if there is none at f */
{
    const char *p = f;
    memset(u, 0, sizeof *u);
    u->decs = -1;
    if (*p == '+') { u->plus = 1; p++; }
    if (p[0] == '*' && p[1] == '*') {
        u->stars = 1; u->before = 2; p += 2;
        if (*p == '$') { u->dollar = 1; u->before++; p++; }
    } else if (p[0] == '$' && p[1] == '$') { u->dollar = 1; u->before = 2; p += 2; }
    if (*p == '#')
        for (; *p == '#' || *p == ','; p++) { if (*p == ',') u->comma = 1; u->before++; }
    if (*p == '.' && (p[1] == '#' || u->before)) {      /* (a . alone is text) */
        p++; u->decs = 0;
        while (*p == '#') { u->decs++; p++; }
    }
    if (!u->before && u->decs < 1) return 0;            /* (and so is a + with no number after it) */
    if (!strncmp(p, "^^^^", 4)) {
        p += 4; u->expo = 2;
        if (*p == '^') { p++; u->expo = 3; }
    }
    if (!u->plus && (*p == '+' || *p == '-')) u->trail = *p++;
    return u->len = p - f;
}

int using_field(const char *f)  /* the length of a field at f, 0 if f is not one */
{
    struct ufield u;
    const char *p = f;
    if (*p == '!' || *p == '&') return 1;
    if (*p == '\\') { p++; while (*p == ' ') p++; return *p == '\\' ? p - f + 1 : 0; }
    return num_field(f, &u);
}

/* The digits of a number: the 15 that PRINT shows (softfp(3) rounds the binary number to them), then rounded
   here to the first n of them as at school, a 5 upwards: so .125 through #.## is 0.13, and so is the .015 that
   is a hair under it in binary. d: the 15 digits, and room for one more; *e: the power of ten of the first;
   returns how many are left. n may be 0 (is the number half of that place or more? then a 1 in it) or less. */
#define SHOWN 15
static int round_digits(char *d, int *e, int n)
{
    int k;
    if (n >= SHOWN) return SHOWN;
    if (n < 0) return 0;
    if (d[n] >= '5') {
        for (k = n - 1; k >= 0 && d[k] == '9'; k--) d[k] = '0';
        if (k >= 0) d[k]++;
        else { memmove(d + 1, d, n); d[0] = '1'; (*e)++; n++; }        /* 9.99 to 10.0: a digit more */
    }
    d[n] = 0;
    return n;
}

#define UPLACES 24                      /* the places a field may have (Microsoft's limit) */

static void using_num(struct bfile *bf, const char *f, struct val *v)
{
    struct ufield u;
    char d[SHOWN + 5], body[2 * UPLACES + 24], out[2 * UPLACES + 40], *o = out;
    const char *b = body;
    fp64 x;
    int neg, have = 0, e = -1, n = 0, k, dec, sign = 0, len;

    if (v->type == V_STR) { val_free(v); basic_error(E_TYPE); }
    num_field(f, &u);
    dec = u.decs > 0 ? u.decs : 0;
    if (u.before + dec > UPLACES) basic_error(E_ILLEGAL);
    to_fp(v, &x);
    neg = fp_sign(&x) < 0;
    fp_abs(&x, &x);
    if (!fp_iszero(&x)) fp_digits(&x, SHOWN, d, &e);
    if (u.plus) sign = neg ? '-' : '+';
    else if (neg && !u.trail) sign = '-';

    if (u.expo) {       /* the digits from the left of the field, the exponent what that leaves */
        int lead = u.before - u.dollar - (u.plus || u.trail ? 0 : 1), ex = 0;
        if (lead < 0) lead = 0;
        if (lead + dec == 0) lead = 1;                  /* (Microsoft's prints no digit at all then) */
        if (!fp_iszero(&x)) { have = round_digits(d, &e, lead + dec); ex = e + 1 - lead; }
        for (k = 0; k < lead; k++) body[n++] = k < have ? d[k] : '0';
        if (u.decs >= 0) body[n++] = '.';
        for (; k < lead + dec; k++) body[n++] = k < have ? d[k] : '0';
        sprintf(body + n, u.expo == 3 ? "E%c%03d" : "E%c%02d", ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
    } else {
        int ip;                                         /* the digits before the point */
        if (!fp_iszero(&x)) have = round_digits(d, &e, e + 1 + dec);
        ip = have ? e + 1 : 0;
        if (ip > UPLACES) {                             /* more than any field holds: as PRINT has it */
            *o++ = '%';
            if (neg) *o++ = '-';
            v->type = V_FLT; v->f = x;
            num_text(v, o);
            out_str(bf, out);
            return;
        }
        if (ip <= 0) body[n++] = '0';
        for (k = 0; k < ip; k++) {
            if (u.comma && k && (ip - k) % 3 == 0) body[n++] = ',';
            body[n++] = k < have ? d[k] : '0';
        }
        if (u.decs >= 0) body[n++] = '.';
        for (k = ip; k < ip + dec; k++) body[n++] = k >= 0 && k < have ? d[k] : '0';
        body[n] = 0;
    }
    len = (sign != 0) + u.dollar + (int)strlen(body) + (u.trail != 0);
    if (len > u.len && !u.expo && body[0] == '0' && body[1] == '.' && body[2]) { b++; len--; }   /* .50 for 0.50 */
    if (len > u.len) *o++ = '%';
    for (k = len; k < u.len; k++) *o++ = u.stars ? '*' : ' ';
    if (sign) *o++ = (char)sign;
    if (u.dollar) *o++ = '$';
    strcpy(o, b);
    o += strlen(o);
    if (u.trail) *o++ = neg ? '-' : u.trail == '+' ? '+' : ' ';
    *o = 0;
    out_str(bf, out);
}

/* one value through the field at f */
void using_value(struct bfile *bf, const char *f, int len, struct val *v)
{
    int k, w;
    if (*f != '!' && *f != '&' && *f != '\\') { using_num(bf, f, v); return; }
    need_str(v);
    if (*f == '&') out_val(bf, v->s);
    else {
        w = *f == '!' ? 1 : len;
        for (k = 0; k < w; k++) out_char(bf, k < SLEN(v->s) ? (unsigned char)v->s[k] : ' ');
    }
    val_free(v);
}

static char *fmt;                       /* PRINT USING's format (kept until the next: an error in a value leaves) */

/* is there a field anywhere in the format f (and so somewhere for a value to go)? */
static int using_any(const char *f)
{
    for (; *f; f++) {
        if (*f == '_' && f[1]) { f++; continue; }
        if (using_field(f)) return 1;
    }
    return 0;
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

/* text (from malloc, and freed here) into a variable: a number for a numeric one (or an error) */
int text_to(struct lval *lv, char *text)
{
    struct val v;
    if (lv->type == V_STR) { set_str(&v, sstr(text)); free(text); store(lv, &v); return 0; }
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
    s = snew(n);
    if (!bf && at_terminal()) { key_mode(1); fflush(stdout); gr_show(); }
    for (k = 0; k < n; k++) {
        if (bf || !at_terminal()) r = getc(bf ? bf->f : stdin);
        else {
            while ((r = read(0, &c, 1)) < 0 && errno == EINTR && !interrupted)
                ;
            r = r == 1 ? c : EOF;
        }
        if (r == EOF) {
            sfree(s);
            if (interrupted) { interrupted = 0; basic_error(E_BREAK); }
            basic_error(E_PAST);
        }
        s[k] = (char)r;
    }
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
            out_val(0, pv.s);
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
    set_str(&v, sstr(line));
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
        const char *f;
        int any;
        eval(&fv);
        need_str(&fv);
        sfree(fmt);
        f = fmt = fv.s;
        if (!match(';')) expect(',');
        any = using_any(fmt);
        while (!at_end()) {
            struct val v;
            int len;
            eval(&v);
            if (!any) { val_free(&v); basic_error(E_ILLEGAL); }        /* (a value, and no field for it) */
            for (;;) {                  /* text up to the next field (round again at the end) */
                if (!*f) f = fmt;
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
                if (write) { out_char(bf, '"'); out_val(bf, v.s); out_char(bf, '"'); }
                else out_val(bf, v.s);
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

