/* as -- the P32W assembler on P32W Unix (M7, D24): assembler/p32asm.py -c in
 * C. The same syntax (that file's comment is the manual), the same two
 * passes and the same rules for sizes, relocations and expressions, so it
 * writes byte for byte the object the host assembler writes.
 *
 *   as [-o file.o] file.s
 *
 * Differences: arithmetic is 32-bit (the host's is unbounded), so a few
 * out-of-range values that the host refuses are taken modulo 2^32; and only
 * relocatable objects are made (no flat ROM images: .org is refused). */
#include <ctype.h>
#include <p32isa.h>
#include "objfmt.h"

#define NHASH 2048
#define MAXARGS 64

/* ---------------------------------------------------------------- values */
/* sect: 0 a plain number; 1-3 relative to that section of this object;
   4 relative to the external sym. kind: 0, 'h' (hi()), 'l' (lo()). */
struct val { int sect; struct sym *ext; int add; int kind; };

struct sym {
    char *name;
    int defined, global, isext;
    struct val v;
    struct sym *next;
};

struct line {
    char *file;
    int num;
    char *label, *op;
    char **args;
    int nargs;
    int addr, size, sect;
};

static struct sym *hash[NHASH];
static struct line *lines;
static int nlines, cap_lines;
static int nerrors, passno = 1;
static int pcs[4];
static int cursect = S_TEXT;
static struct buf image[4];
static struct oreloc *rels;
static int nrels, cap_rels;
static struct line *cur;
static int woff;

static void error(const char *fmt, const char *a)
{
    fprintf(stderr, "%s:%d: error: ", cur ? cur->file : "as", cur ? cur->num : 0);
    fprintf(stderr, fmt, a);
    fputc('\n', stderr);
    nerrors++;
}

/* an error inside a line: report it and abandon the line */
#include <setjmp.h>
static jmp_buf line_err;
static void fail(const char *fmt, const char *a)
{
    error(fmt, a);
    longjmp(line_err, 1);
}

static struct sym *lookup(const char *name, int create)
{
    unsigned h = 5381;
    const char *s = name;
    struct sym *p;
    while (*s) h = h * 33 + (unsigned char)*s++;
    h %= NHASH;
    for (p = hash[h]; p; p = p->next)
        if (!strcmp(p->name, name)) return p;
    if (!create) return 0;
    p = xmalloc(sizeof *p);
    memset(p, 0, sizeof *p);
    p->name = xstrdup(name);
    p->next = hash[h];
    hash[h] = p;
    return p;
}

static int is_reg_name(const char *s)  /* r0-r15, zero, sp (as written: the host's REGS) */
{
    static const char *const names[] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
                                         "r11", "r12", "r13", "r14", "r15", "zero", "sp", 0 };
    int i;
    for (i = 0; names[i]; i++) if (!strcmp(s, names[i])) return 1;
    return 0;
}

static int reg(const char *s)
{
    char b[16];
    int i, n;
    while (*s == ' ' || *s == '\t') s++;
    for (i = 0; s[i] && i < 15; i++) b[i] = tolower((unsigned char)s[i]);
    b[i] = 0;
    while (i > 0 && (b[i - 1] == ' ' || b[i - 1] == '\t')) b[--i] = 0;
    if (!strcmp(b, "zero")) return 0;
    if (!strcmp(b, "sp")) return 15;
    if (b[0] == 'r' && isdigit((unsigned char)b[1])) {
        n = atoi(b + 1);
        if (n < 16 && ((n < 10 && !b[2]) || (n >= 10 && b[2] && !b[3]))) return n;
    }
    fail("expected a register, got '%s'", s);
    return 0;
}

/* ---------------------------------------------------------------- expressions */
static const char *ep;                  /* the text being evaluated */
static int here_addr;
static int need_defined;                /* 0: an undefined name makes the value unknown */
static int unknown;                     /* (set when that happened) */
static char tok[256];
static int toklen;

static int esc(int c)
{
    switch (c) {
    case 'n': return 10; case 'r': return 13; case 't': return 9; case '0': return 0;
    case '\\': return 92; case '\'': return 39; case '"': return 34;
    }
    fail("unknown escape", "");
    return 0;
}

/* the next token into tok (empty at the end) */
static void next(void)
{
    const char *s = ep;
    toklen = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) { tok[0] = 0; ep = s; return; }
    if (isdigit((unsigned char)*s)) {
        while (isalnum((unsigned char)*s) || *s == '_') { if (toklen < 250) tok[toklen++] = *s; s++; }
    } else if (*s == '\'') {
        tok[toklen++] = *s++;
        if (*s == '\\') tok[toklen++] = *s++;
        if (*s) tok[toklen++] = *s++;
        if (*s != '\'') fail("bad character constant", "");
        tok[toklen++] = *s++;
    } else if (isalpha((unsigned char)*s) || *s == '_' || *s == '.') {
        while (isalnum((unsigned char)*s) || *s == '_' || *s == '.') { if (toklen < 250) tok[toklen++] = *s; s++; }
    } else if ((s[0] == '<' && s[1] == '<') || (s[0] == '>' && s[1] == '>')) {
        tok[toklen++] = *s++; tok[toklen++] = *s++;
    } else if (strchr("-+*/%&|^~()", *s)) {
        tok[toklen++] = *s++;
    } else
        fail("bad expression near '%s'", s);
    tok[toklen] = 0;
    ep = s;
}

static void num(struct val *v, int n) { v->sect = 0; v->ext = 0; v->add = n; v->kind = 0; }

static void plain(struct val *v)
{
    if (v->kind) fail("hi()/lo() of a relocatable value must be the whole operand", "");
}

static void binary(int lvl, struct val *v);

static void unary(struct val *v)
{
    char t[256];
    if (!tok[0]) fail("expression ends early", "");
    strcpy(t, tok);
    next();
    if (!strcmp(t, "-") || !strcmp(t, "+") || !strcmp(t, "~")) {
        unary(v);
        if (t[0] == '+') return;
        if (v->sect) fail("only + and - work on a relocatable value", "");
        v->add = t[0] == '-' ? -v->add : ~v->add;
        return;
    }
    if (!strcmp(t, "(")) {
        binary(0, v);
        if (strcmp(tok, ")")) fail("missing ')'", "");
        next();
        return;
    }
    if ((tolower((unsigned char)t[0]) == 'h' && tolower((unsigned char)t[1]) == 'i' && !t[2]) ||
        (tolower((unsigned char)t[0]) == 'l' && tolower((unsigned char)t[1]) == 'o' && !t[2])) {
        if (!strcmp(tok, "(")) {
            int hi = tolower((unsigned char)t[0]) == 'h';
            next();
            binary(0, v);
            if (strcmp(tok, ")")) fail("missing ')'", "");
            next();
            if (v->sect) { plain(v); v->kind = hi ? 'h' : 'l'; return; }
            num(v, hi ? (int)(((unsigned)v->add >> 16) & 0xFFFF) : v->add & 0xFFFF);
            return;
        }
    }
    if (isdigit((unsigned char)t[0])) {
        char clean[256];
        int i, k = 0;
        unsigned n;
        for (i = 0; t[i]; i++) if (t[i] != '_') clean[k++] = t[i];
        clean[k] = 0;
        if (clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X')) n = strtoul(clean + 2, 0, 16);
        else if (clean[0] == '0' && (clean[1] == 'b' || clean[1] == 'B')) n = strtoul(clean + 2, 0, 2);
        else n = strtoul(clean, 0, 10);
        num(v, (int)n);
        return;
    }
    if (t[0] == '\'') { num(v, t[1] == '\\' ? esc(t[2]) : (unsigned char)t[1]); return; }
    if (!strcmp(t, ".")) { num(v, here_addr); v->sect = cur->sect; return; }
    {
        struct sym *s = lookup(t, 0);
        if (s && s->defined) { *v = s->v; return; }
        if (passno == 2 && strncmp(t, ".L", 2)) {    /* an external */
            s = lookup(t, 1);
            s->isext = 1;
            num(v, 0);
            v->sect = 4;
            v->ext = s;
            return;
        }
        if (need_defined) fail("undefined symbol '%s'", t);
        unknown = 1;
        num(v, 0);
    }
}

static int level_of(const char *t)
{
    if (!strcmp(t, "|")) return 0;
    if (!strcmp(t, "^")) return 1;
    if (!strcmp(t, "&")) return 2;
    if (!strcmp(t, "<<") || !strcmp(t, ">>")) return 3;
    if (!strcmp(t, "+") || !strcmp(t, "-")) return 4;
    if (!strcmp(t, "*") || !strcmp(t, "/") || !strcmp(t, "%")) return 5;
    return -1;
}

static int same_base(struct val *a, struct val *b)
{
    return a->sect == b->sect && (a->sect != 4 || a->ext == b->ext);
}

static void binary(int lvl, struct val *v)
{
    struct val r;
    if (lvl == 6) { unary(v); return; }
    binary(lvl + 1, v);
    while (tok[0] && level_of(tok) == lvl) {
        char op[4];
        strcpy(op, tok);
        next();
        binary(lvl + 1, &r);
        if (op[0] == '+') {
            if (v->sect && r.sect) fail("cannot add two relocatable values", "");
            if (v->sect) plain(v);
            if (r.sect) { plain(&r); r.add += v->add; *v = r; }
            else v->add += r.add;
        } else if (op[0] == '-') {
            if (r.sect) {
                if (v->sect && same_base(v, &r) && !r.kind) { plain(v); num(v, v->add - r.add); }
                else if (!v->sect) fail("cannot subtract a relocatable value from a number", "");
                else fail("cannot subtract values from different sections", "");
            } else {
                if (v->sect) plain(v);
                v->add -= r.add;
            }
        } else {
            if (v->sect || r.sect) fail("only + and - work on a relocatable value", "");
            switch (op[0]) {
            case '|': v->add |= r.add; break;
            case '^': v->add ^= r.add; break;
            case '&': v->add &= r.add; break;
            case '<': v->add = (int)((unsigned)v->add << r.add); break;
            case '>': v->add >>= r.add; break;
            case '*': v->add *= r.add; break;
            case '/': if (!r.add) fail("division by zero in expression", ""); v->add /= r.add; break;
            case '%': if (!r.add) fail("division by zero in expression", ""); v->add %= r.add; break;
            }
        }
    }
}

/* the value of text into v; *known = 0 if (need == 0 and) a name is not yet defined */
static void eval(const char *text, int need, int *known, struct val *v)
{
    ep = text;
    need_defined = need;
    unknown = 0;
    here_addr = cur->addr;
    next();
    if (!tok[0]) fail("missing expression", "");
    binary(0, v);
    if (tok[0]) fail("unexpected '%s' in expression", tok);
    if (known) *known = !unknown;
}

static int evnum(const char *text)          /* a plain number */
{
    struct val v;
    eval(text, 1, 0, &v);
    if (v.sect) fail("only + and - work on a relocatable value", "");
    return v.add;
}

/* ---------------------------------------------------------------- lines */
static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void strip_comment(char *s)
{
    int q = 0, i;
    for (i = 0; s[i]; i++) {
        if (q) { if (s[i] == q && (i == 0 || s[i - 1] != '\\')) q = 0; }
        else if (s[i] == '"' || s[i] == '\'') q = s[i];
        else if (s[i] == ';') { s[i] = 0; return; }
    }
}

/* operands split at commas outside quotes and parentheses */
static int split_operands(char *s, char **out)
{
    int n = 0, depth = 0, q = 0;
    char *start = s, *p;
    for (p = s; *p; p++) {
        if (q) { if (*p == q && !(p > start && p[-1] == '\\')) q = 0; }
        else if (*p == '"' || *p == '\'') q = *p;
        else if (*p == '(') depth++;
        else if (*p == ')') depth--;
        else if (*p == ',' && depth == 0) {
            *p = 0;
            if (n < MAXARGS) out[n++] = trim(start);
            start = p + 1;
        }
    }
    if (*trim(start) && n < MAXARGS) out[n++] = trim(start);
    return n;
}

/* a "string": its bytes into out (at most 1024); the length */
static int parse_string(const char *s, unsigned char *out)
{
    int n = strlen(s), i, k = 0;
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') fail("expected a \"string\"", "");
    for (i = 1; i < n - 1; i++) {
        if (k >= 1024) fail("string too long", "");
        if (s[i] == '\\') { i++; out[k++] = esc(s[i]); }
        else out[k++] = s[i];
    }
    return k;
}

static int is_name(const char *s)
{
    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    for (s++; *s; s++) if (!(isalnum((unsigned char)*s) || *s == '_' || *s == '.')) return 0;
    return 1;
}

static void define(const char *name, struct val *v)
{
    struct sym *s;
    if (is_reg_name(name)) fail("'%s' is a reserved name", name);
    s = lookup(name, 1);
    if (s->defined) fail("'%s' defined twice", name);
    s->defined = 1;
    s->v = *v;
}

static const struct p32op *opcode(const char *m)
{
    int lo = 0, hi = P32_NOPS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(m, p32_ops[mid].name);
        if (!c) return &p32_ops[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

static int pseudo_size(const char *op)
{
    static const char *const four[] = { "NOP", "MOV", "NEG", "NOT", "BGT", "BLE", "BGTU", "BLEU", "BEQZ", "BNEZ", 0 };
    int i;
    for (i = 0; four[i]; i++) if (!strcmp(op, four[i])) return 4;
    if (!strcmp(op, "PUSH") || !strcmp(op, "POP") || !strcmp(op, "LA")) return 8;
    return 0;
}

static int li_count(unsigned v)                 /* instructions LI needs for v */
{
    int s = (int)v;
    if (s >= -32768 && s <= 32767) return 1;
    if (v <= 0xFFFF) return 1;
    if ((v & 0xFFFF) == 0) return 1;
    return 2;
}

static int size_line(struct line *ln, int pc)
{
    const char *op = ln->op;
    char **a = ln->args;
    if (!strcmp(op, ".equ")) {
        if (ln->nargs != 2) fail(".equ needs NAME, expr", "");
        struct val v;
        eval(a[1], 1, 0, &v);
        define(a[0], &v);
        return pc;
    }
    if (!strcmp(op, ".org")) fail(".org is not allowed in relocatable mode", "");
    if (!strcmp(op, ".align")) {
        int n = evnum(a[0]);
        if (n <= 0 || (n & (n - 1))) fail(".align needs a power of 2", "");
        ln->size = (n - pc % n) % n;
    } else if (!strcmp(op, ".space")) ln->size = evnum(a[0]);
    else if (!strcmp(op, ".word")) ln->size = 4 * ln->nargs;
    else if (!strcmp(op, ".half")) ln->size = 2 * ln->nargs;
    else if (!strcmp(op, ".byte")) {
        int i;
        unsigned char tmp[1024];
        ln->size = 0;
        for (i = 0; i < ln->nargs; i++) ln->size += a[i][0] == '"' ? parse_string(a[i], tmp) : 1;
    } else if (!strcmp(op, ".ascii") || !strcmp(op, ".asciz")) {
        unsigned char tmp[1024];
        if (ln->nargs < 1) fail("%s needs a string", op);
        ln->size = parse_string(a[0], tmp) + (op[5] == 'z');
    } else if (op[0] == '.') fail("unknown directive %s", op);
    else {
        if (pc % 4) fail("instruction at an unaligned address (use .align 4)", "");
        if (opcode(op)) ln->size = 4;
        else if (pseudo_size(op)) ln->size = pseudo_size(op);
        else if (!strcmp(op, "LI")) {
            int known;
            struct val v;
            if (ln->nargs != 2) fail("LI needs rd, expr", "");
            eval(a[1], 0, &known, &v);
            ln->size = (!known || v.sect) ? 8 : 4 * li_count((unsigned)v.add);
        } else fail("unknown instruction %s", op);
    }
    return pc + ln->size;
}

static void add_line(struct line *ln)
{
    if (nlines == cap_lines) {
        struct line *n;
        cap_lines = cap_lines ? cap_lines * 2 : 1024;
        n = xmalloc(sizeof *n * cap_lines);
        if (nlines) memcpy(n, lines, sizeof *n * nlines);
        free(lines);
        lines = n;
    }
    lines[nlines++] = *ln;
}

static void pass1(const char *fname, char *src, int depth)
{
    char *p = src;
    int lineno = 0;
    while (*p) {
        char *eol = strchr(p, '\n'), *text, *colon;
        struct line ln;
        if (eol) *eol = 0;
        lineno++;
        memset(&ln, 0, sizeof ln);
        ln.file = (char *)fname;
        ln.num = lineno;
        ln.addr = pcs[cursect];
        ln.sect = cursect;
        cur = &ln;
        strip_comment(p);
        text = trim(p);
        if (setjmp(line_err) == 0) {
            /* a label: name ':' (the name may start with '.') */
            colon = text;
            if (*colon == '.') colon++;
            if (isalpha((unsigned char)*colon) || *colon == '_') {
                while (isalnum((unsigned char)*colon) || *colon == '_' || *colon == '.') colon++;
                {
                    char *c2 = colon;
                    while (*c2 == ' ' || *c2 == '\t') c2++;
                    if (*c2 == ':') {
                        *colon = 0;
                        ln.label = text;
                        text = trim(c2 + 1);
                    }
                }
            }
            if (ln.label) {
                struct val v;
                num(&v, pcs[cursect]);
                v.sect = cursect;
                define(ln.label, &v);
            }
            /* NAME = expr */
            {
                char *e = text;
                if (isalpha((unsigned char)*e) || *e == '_') {
                    while (isalnum((unsigned char)*e) || *e == '_' || *e == '.') e++;
                    {
                        char *e2 = e;
                        while (*e2 == ' ' || *e2 == '\t') e2++;
                        if (*e2 == '=' && e2[1] != '=') {
                            struct val v;
                            *e = 0;
                            eval(trim(e2 + 1), 1, 0, &v);
                            define(text, &v);
                            text = "";
                        }
                    }
                }
            }
            if (*text) {
                char *rest = text, *args[MAXARGS];
                int i;
                while (*rest && *rest != ' ' && *rest != '\t') rest++;
                if (*rest) *rest++ = 0;
                if (text[0] == '.') for (i = 0; text[i]; i++) text[i] = tolower((unsigned char)text[i]);
                else for (i = 0; text[i]; i++) text[i] = toupper((unsigned char)text[i]);
                ln.op = text;
                ln.nargs = split_operands(rest, args);
                ln.args = xmalloc(sizeof(char *) * (ln.nargs ? ln.nargs : 1));
                for (i = 0; i < ln.nargs; i++) ln.args[i] = args[i];
                if (!strcmp(ln.op, ".include")) {
                    unsigned char name[1024];
                    char path[1200], *slash;
                    unsigned len;
                    char *inc;
                    int n;
                    if (ln.nargs != 1) fail(".include needs one \"file\"", "");
                    if (depth >= 8) fail(".include nested too deeply", "");
                    n = parse_string(ln.args[0], name);
                    name[n] = 0;
                    strcpy(path, fname);
                    slash = strrchr(path, '/');
                    if (slash) strcpy(slash + 1, (char *)name); else strcpy(path, (char *)name);
                    if (!(inc = (char *)read_file(path, &len))) fail("cannot include %s", path);
                    ln.op = 0;
                    add_line(&ln);
                    pass1(xstrdup(path), inc, depth + 1);
                    p = eol ? eol + 1 : p + strlen(p);
                    continue;
                }
                if (!strcmp(ln.op, ".text") || !strcmp(ln.op, ".data") || !strcmp(ln.op, ".bss")) {
                    if (ln.nargs) fail("%s takes no operands", ln.op);
                    cursect = ln.op[1] == 't' ? S_TEXT : ln.op[1] == 'd' ? S_DATA : S_BSS;
                    ln.op = 0;
                } else if (!strcmp(ln.op, ".global") || !strcmp(ln.op, ".globl") || !strcmp(ln.op, ".extern")) {
                    for (i = 0; i < ln.nargs; i++) {
                        struct sym *s;
                        if (!is_name(ln.args[i])) fail("bad symbol name '%s'", ln.args[i]);
                        s = lookup(ln.args[i], 1);
                        if (ln.op[1] == 'g') s->global = 1; else s->isext = 1;
                    }
                    ln.op = 0;
                } else
                    pcs[cursect] = size_line(&ln, pcs[cursect]);
            }
        }
        add_line(&ln);
        p = eol ? eol + 1 : p + strlen(p);
    }
}

/* ---------------------------------------------------------------- pass 2 */
static void add_reloc(int kind, int off, struct val *v)
{
    struct oreloc *r;
    if (nrels == cap_rels) {
        struct oreloc *n;
        cap_rels = cap_rels ? cap_rels * 2 : 256;
        n = xmalloc(sizeof *n * cap_rels);
        if (nrels) memcpy(n, rels, sizeof *n * nrels);
        free(rels);
        rels = n;
    }
    r = &rels[nrels++];
    r->sect = cur->sect;
    r->kind = kind;
    r->offset = off;
    r->sym = v->sect == 4 ? v->ext->name : (char *)sect_name[v->sect];
    r->addend = v->add;
}

static int rel(const char *text, int bits)
{
    struct val t;
    int delta, off;
    eval(text, 1, 0, &t);
    if (t.sect) {
        if (t.sect != cur->sect || t.kind) fail("branch to '%s' leaves the section (use JMP)", text);
    }
    delta = t.add - (woff + 4);
    if (delta % 4) fail("branch target is not word-aligned", "");
    off = delta / 4;
    if (off < -(1 << (bits - 1)) || off >= (1 << (bits - 1))) fail("branch target out of range", "");
    return off;
}

static void memop(const char *s, char *off, int *base)
{
    int n = strlen(s), i, depth = 0;
    char r[32];
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n < 3 || s[n - 1] != ')') fail("expected imm(reg), got '%s'", s);
    for (i = n - 1; i >= 0; i--) {
        if (s[i] == ')') depth++;
        if (s[i] == '(' && --depth == 0) break;
    }
    if (i < 0 || n - i - 2 >= (int)sizeof r) fail("expected imm(reg), got '%s'", s);
    memcpy(r, s + i + 1, n - i - 2);
    r[n - i - 2] = 0;
    memcpy(off, s, i);
    off[i] = 0;
    if (!*trim(off)) strcpy(off, "0");
    *base = reg(r);
}

static unsigned ins(const char *m, char **a, int na)
{
    const struct p32op *o = opcode(m);
    static const int counts[] = { 0, 3, 3, 3, 3, 2, 2, 2, 3, 1, 1, 0, 2 };
    unsigned w;
    struct val v;
    char tmp[16];
    if (!o) fail("unknown instruction %s", m);
    w = (unsigned)o->op << 24;
    if (na != counts[o->fmt]) { sprintf(tmp, "%d", counts[o->fmt]); fail("instruction takes %s operand(s)", tmp); }
    switch (o->fmt) {
    case F_R:
        return w | reg(a[0]) << 20 | reg(a[1]) << 16 | reg(a[2]) << 12;
    case F_I: case F_IU: case F_SH: {
        int lo, hi;
        eval(a[2], 1, 0, &v);
        if (v.sect) {
            if (strcmp(m, "ORI") || v.kind != 'l') fail("a relocatable immediate works only as lo() in ORI", "");
            add_reloc(R_LO16, woff, &v);
            v.add = 0;
        }
        lo = o->fmt == F_I ? -32768 : 0;
        hi = o->fmt == F_I ? 32767 : o->fmt == F_IU ? 0xFFFF : 31;
        if (v.add < lo || v.add > hi) fail("immediate out of range (%s)", a[2]);
        return w | reg(a[0]) << 20 | reg(a[1]) << 16 | (v.add & 0xFFFF);
    }
    case F_LUI:
        eval(a[1], 1, 0, &v);
        if (v.sect) {
            if (v.kind != 'h') fail("a relocatable LUI operand must be hi()", "");
            add_reloc(R_HI16, woff, &v);
            v.add = 0;
        }
        if (v.add < 0 || v.add > 0xFFFF) fail("LUI immediate out of range (%s)", a[1]);
        return w | reg(a[0]) << 20 | v.add;
    case F_LD: case F_ST: {
        char off[256];
        int base;
        if (strlen(a[1]) >= sizeof off) fail("operand too long", "");
        memop(a[1], off, &base);
        eval(off, 1, 0, &v);
        if (v.sect) fail("a load/store offset cannot be relocatable (LA the address first)", "");
        if (v.add < -32768 || v.add > 32767) fail("offset out of range (%s)", off);
        return w | reg(a[0]) << 20 | base << 16 | (v.add & 0xFFFF);
    }
    case F_BR:
        return w | reg(a[0]) << 20 | reg(a[1]) << 16 | (rel(a[2], 16) & 0xFFFF);
    case F_J:
        eval(a[0], 1, 0, &v);
        if (v.sect && (v.sect != cur->sect || v.kind)) {
            if (v.kind) fail("a jump target cannot be hi()/lo()", "");
            add_reloc(R_J24, woff, &v);
            return w;
        }
        return w | (rel(a[0], 24) & 0xFFFFFF);
    case F_JR:
        return w | reg(a[0]) << 16;
    case F_CR: {
        const char *gpr = !strcmp(m, "MFCR") ? a[0] : a[1], *cr = !strcmp(m, "MFCR") ? a[1] : a[0];
        char u[32];
        int i, n = -1;
        for (i = 0; cr[i] && i < 31; i++) u[i] = toupper((unsigned char)cr[i]);
        u[i] = 0;
        for (i = 0; i < P32_NCREGS; i++) if (!strcmp(trim(u), p32_cregs[i])) n = i;
        if (n < 0) {
            n = evnum(cr);
            if (n < 0 || n > 15) fail("control register out of range", "");
        }
        return w | reg(gpr) << 20 | n << 16;
    }
    }
    return w;                           /* N */
}

static void emit_word(struct buf *out, unsigned w)
{
    bword(out, w);
    woff += 4;
}

/* one (pseudo-)instruction's words into out */
static void encode_ins(struct line *ln, struct buf *out)
{
    const char *op = ln->op;
    char **a = ln->args;
    int na = ln->nargs;
    char *x[4];
    char b1[300], b2[300];
#define NEED(n) do { if (na != (n)) fail("wrong number of operands", ""); } while (0)
    woff = ln->addr;
    if (opcode(op)) { emit_word(out, ins(op, a, na)); return; }
    if (!strcmp(op, "NOP")) { NEED(0); x[0] = "r0"; x[1] = "r0"; x[2] = "r0"; emit_word(out, ins("ADD", x, 3)); return; }
    if (!strcmp(op, "MOV")) { NEED(2); x[0] = a[0]; x[1] = a[1]; x[2] = "r0"; emit_word(out, ins("ADD", x, 3)); return; }
    if (!strcmp(op, "NEG")) { NEED(2); x[0] = a[0]; x[1] = "r0"; x[2] = a[1]; emit_word(out, ins("SUB", x, 3)); return; }
    if (!strcmp(op, "NOT")) { NEED(2); x[0] = a[0]; x[1] = a[1]; x[2] = "r0"; emit_word(out, ins("NOR", x, 3)); return; }
    if (!strcmp(op, "PUSH")) {
        NEED(1);
        x[0] = "sp"; x[1] = "sp"; x[2] = "-4"; emit_word(out, ins("ADDI", x, 3));
        x[0] = a[0]; x[1] = "0(sp)"; emit_word(out, ins("SW", x, 2));
        return;
    }
    if (!strcmp(op, "POP")) {
        NEED(1);
        x[0] = a[0]; x[1] = "0(sp)"; emit_word(out, ins("LW", x, 2));
        x[0] = "sp"; x[1] = "sp"; x[2] = "4"; emit_word(out, ins("ADDI", x, 3));
        return;
    }
    if (!strcmp(op, "BGT") || !strcmp(op, "BLE") || !strcmp(op, "BGTU") || !strcmp(op, "BLEU")) {
        NEED(3);
        x[0] = a[1]; x[1] = a[0]; x[2] = a[2];
        emit_word(out, ins(!strcmp(op, "BGT") ? "BLT" : !strcmp(op, "BLE") ? "BGE" : !strcmp(op, "BGTU") ? "BLTU" : "BGEU", x, 3));
        return;
    }
    if (!strcmp(op, "BEQZ") || !strcmp(op, "BNEZ")) {
        NEED(2);
        x[0] = a[0]; x[1] = "r0"; x[2] = a[1];
        emit_word(out, ins(op[1] == 'E' ? "BEQ" : "BNE", x, 3));
        return;
    }
    if (!strcmp(op, "LI") || !strcmp(op, "LA")) {
        struct val v;
        unsigned u;
        NEED(2);
        eval(a[1], 1, 0, &v);
        if (strlen(a[1]) > 280) fail("operand too long", "");
        if (v.sect) {
            sprintf(b1, "hi(%s)", a[1]);
            x[0] = a[0]; x[1] = b1; emit_word(out, ins("LUI", x, 2));
            sprintf(b2, "lo(%s)", a[1]);
            x[0] = a[0]; x[1] = a[0]; x[2] = b2; emit_word(out, ins("ORI", x, 3));
            return;
        }
        u = (unsigned)v.add;
        if (ln->size == 4) {                    /* li_parts: one instruction */
            int s = (int)u;
            if (s >= -32768 && s <= 32767) { sprintf(b1, "%d", s); x[0] = a[0]; x[1] = "r0"; x[2] = b1; emit_word(out, ins("ADDI", x, 3)); }
            else if (u <= 0xFFFF) { sprintf(b1, "%u", u); x[0] = a[0]; x[1] = "r0"; x[2] = b1; emit_word(out, ins("ORI", x, 3)); }
            else { sprintf(b1, "%u", u >> 16); x[0] = a[0]; x[1] = b1; emit_word(out, ins("LUI", x, 2)); }
        } else {
            sprintf(b1, "%u", u >> 16); x[0] = a[0]; x[1] = b1; emit_word(out, ins("LUI", x, 2));
            sprintf(b2, "%u", u & 0xFFFF); x[0] = a[0]; x[1] = a[0]; x[2] = b2; emit_word(out, ins("ORI", x, 3));
        }
        return;
    }
    fail("unknown instruction %s", op);
}

static void encode(struct line *ln, struct buf *out)
{
    const char *op = ln->op;
    int i;
    if (!strcmp(op, ".align") || !strcmp(op, ".space")) { bput(out, 0, ln->size); return; }
    if (!strcmp(op, ".word") || !strcmp(op, ".half") || !strcmp(op, ".byte")) {
        int n = op[1] == 'w' ? 4 : op[1] == 'h' ? 2 : 1;
        for (i = 0; i < ln->nargs; i++) {
            struct val v;
            if (n == 1 && ln->args[i][0] == '"') {
                unsigned char s[1024];
                int k = parse_string(ln->args[i], s);
                bput(out, s, k);
                continue;
            }
            eval(ln->args[i], 1, 0, &v);
            if (v.sect) {
                if (n != 4 || v.kind) fail("a relocatable value needs .word", "");
                add_reloc(R_ABS32, ln->addr + out->n, &v);
                bword(out, 0);
                continue;
            }
            if (n == 4) bword(out, (unsigned)v.add);
            else {
                unsigned char b[2];
                if (n == 1 && (v.add < -128 || v.add > 255)) fail("value does not fit in .byte (%s)", ln->args[i]);
                if (n == 2 && (v.add < -32768 || v.add > 65535)) fail("value does not fit in .half (%s)", ln->args[i]);
                b[0] = v.add; b[1] = v.add >> 8;
                bput(out, b, n);
            }
        }
        return;
    }
    if (!strcmp(op, ".ascii") || !strcmp(op, ".asciz")) {
        unsigned char s[1024];
        int k = parse_string(ln->args[0], s);
        bput(out, s, k);
        if (op[5] == 'z') bput(out, 0, 1);
        return;
    }
    encode_ins(ln, out);
}

static void pass2(void)
{
    int i;
    passno = 2;
    for (i = 0; i < nlines; i++) {
        struct line *ln = &lines[i];
        struct buf out;
        struct buf *img;
        if (!ln->op || !strcmp(ln->op, ".equ")) continue;
        cur = ln;
        memset(&out, 0, sizeof out);
        if (setjmp(line_err) == 0) {
            if (ln->sect == S_BSS && strcmp(ln->op, ".space") && strcmp(ln->op, ".align"))
                fail(".bss holds only .space and .align", "");
            encode(ln, &out);
            if ((int)out.n != ln->size)
                error("internal: size changed between passes", "");
            else if (ln->sect != S_BSS) {
                img = &image[ln->sect];
                if (img->n < (unsigned)(ln->addr + out.n)) bput(img, 0, ln->addr + out.n - img->n);
                if (out.n) memcpy(img->p + ln->addr, out.p, out.n);
            }
        }
        free(out.p);
    }
}

/* ---------------------------------------------------------------- the object */
static int write_object(const char *src, const char *path)
{
    struct obj o;
    struct buf out;
    int i, n = 0;
    const char *base = strrchr(src, '/');
    memset(&o, 0, sizeof o);
    o.source = (char *)(base ? base + 1 : src);
    for (i = 0; i < NHASH; i++) {
        struct sym *s;
        for (s = hash[i]; s; s = s->next) {
            if (s->global && !s->defined) {
                cur = 0;
                fprintf(stderr, "%s: error: .global %s is not defined\n", src, s->name);
                nerrors++;
            }
            if (s->defined && ((s->v.sect >= 1 && s->v.sect <= 3 && strncmp(s->name, ".L", 2)) ||
                               (s->v.sect == 0 && s->global))) n++;
        }
    }
    if (nerrors) return -1;
    o.syms = xmalloc(sizeof *o.syms * (n ? n : 1));
    for (i = 0; i < NHASH; i++) {
        struct sym *s;
        for (s = hash[i]; s; s = s->next)
            if (s->defined && ((s->v.sect >= 1 && s->v.sect <= 3 && strncmp(s->name, ".L", 2)) ||
                               (s->v.sect == 0 && s->global))) {
                o.syms[o.nsym].name = s->name;
                o.syms[o.nsym].value = (unsigned)s->v.add;
                o.syms[o.nsym].sect = s->v.sect;
                o.syms[o.nsym].global = s->global;
                o.nsym++;
            }
    }
    if (image[S_TEXT].n < (unsigned)pcs[S_TEXT]) bput(&image[S_TEXT], 0, pcs[S_TEXT] - image[S_TEXT].n);
    if (image[S_DATA].n < (unsigned)pcs[S_DATA]) bput(&image[S_DATA], 0, pcs[S_DATA] - image[S_DATA].n);
    o.text = image[S_TEXT].p; o.tsize = pcs[S_TEXT];
    o.data = image[S_DATA].p; o.dsize = pcs[S_DATA];
    o.bsize = pcs[S_BSS];
    o.talign = o.dalign = o.balign = 4;
    o.rels = rels; o.nrel = nrels;
    memset(&out, 0, sizeof out);
    obj_encode(&o, &out);
    if (write_file(path, out.p, out.n) < 0) { perror(path); return -1; }
    return 0;
}

int main(int argc, char **argv)
{
    const char *src = 0, *out = 0;
    char defout[300];
    unsigned len;
    char *text;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-c")) ;     /* (always relocatable) */
        else if (argv[i][0] == '-') { fprintf(stderr, "usage: as [-o file.o] file.s\n"); return 2; }
        else if (src) { fprintf(stderr, "as: one source file at a time\n"); return 2; }
        else src = argv[i];
    }
    if (!src) { fprintf(stderr, "usage: as [-o file.o] file.s\n"); return 2; }
    if (!(text = (char *)read_file(src, &len))) { perror(src); return 1; }
    if (!out) {
        const char *b = strrchr(src, '/');
        char *dot;
        strncpy(defout, b ? b + 1 : src, sizeof defout - 3);
        defout[sizeof defout - 3] = 0;          /* (room for .o) */
        dot = strrchr(defout, '.');
        if (dot) strcpy(dot, ".o"); else strcat(defout, ".o");
        out = defout;
    }
    pass1(src, text, 0);
    if (!nerrors) pass2();
    if (nerrors || write_object(src, out) < 0) return 1;
    return 0;
}
