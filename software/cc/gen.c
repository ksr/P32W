/* gen.c -- P32W assembly from the tree (cc/cgen.py, whose comment explains the
 * ABI, the frame and the register use); every line it writes is the line
 * the Python writes. */
#include <setjmp.h>
#include "cc.h"

static struct vec out;                  /* lines (char *) */
static int printf_fp;                   /* a double passed to a variadic function: see gen_end */
static int nlab, nlab_start;
static char *fname, *retlab;
static unsigned busy, callee, reserved; /* register sets: bit r */
static int no_value;
static struct vec brks, conts;
static jmp_buf out_of_regs;
static int regvars[4] = { 13, 12, 11, 10 }, nregvars = 4;

/* The lines wait in out until the function is done (its prologue comes
   first and depends on the body). Their text is packed into 4 KB chunks,
   freed together when the lines are written (flush_out): a big function
   has thousands of lines, and a malloc each cost half as much again */
static struct vec chunks;
static char *chunk;
static int chunk_left;

static char *keep_line(const char *s)
{
    int n = strlen(s) + 1;
    char *p;
    if (n > chunk_left) {
        chunk_left = n > 4096 ? n : 4096;
        chunk = xmalloc(chunk_left);
        vpush(&chunks, chunk);
    }
    p = chunk;
    memcpy(p, s, n);
    chunk += n;
    chunk_left -= n;
    return p;
}

static void add_line(const char *s) { vpush(&out, keep_line(s)); }

static void emit(const char *f, ...);
#include <stdarg.h>
static void emit(const char *f, ...)
{
    char buf[512];
    va_list ap;
    strcpy(buf, "        ");
    va_start(ap, f);
    vsprintf(buf + 8, f, ap);
    va_end(ap);
    add_line(buf);
}

static void label(const char *l)
{
    char buf[512];
    sprintf(buf, "%.509s:", l);
    add_line(buf);
}

static FILE *outf;

static void flush_out(void)             /* the lines so far to the file (and out of memory) */
{
    int k;
    for (k = 0; k < out.n; k++) {
        fputs(out.a[k], outf);
        fputc('\n', outf);
    }
    free(out.a);
    memset(&out, 0, sizeof out);
    for (k = 0; k < chunks.n; k++) free(chunks.a[k]);
    free(chunks.a);
    memset(&chunks, 0, sizeof chunks);
    chunk_left = 0;
}

static char *newlab(void)
{
    nlab++;
    return fmt(".L%s_%d", fname, nlab);
}

static const char *sym(const char *name)       /* register names would read as registers */
{
    static const char *const regs[] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
                                        "r11", "r12", "r13", "r14", "r15", "zero", "sp", 0 };
    int i;
    for (i = 0; regs[i]; i++) if (!strcmp(name, regs[i])) return fmt("_C_%s", name);
    return name;
}

static int fits16(int v) { return v >= -32768 && v <= 32767; }

/* ---------------------------------------------------------------- registers */
#define HAS(set, r) (((set) >> (r)) & 1)

static int alloc(void)
{
    int r;
    for (r = 1; r < 14; r++)
        if (!HAS(busy, r) && !HAS(reserved, r)) {
            busy |= 1u << r;
            if (r >= 8) callee |= 1u << r;
            return r;
        }
    if (reserved) longjmp(out_of_regs, 1);    /* (function() tries again with fewer register variables) */
    error(0, "expression too complex (out of registers)");
    return 0;
}

static int regof(struct node *n)
{
    if (n->kind == N_VAR) return n->obj->reg;
    if (n->kind == N_VAROFF && n->off == 0) return n->var->reg;
    return 0;
}

static void zero(struct obj *var);

static int expr(struct node *n);
static int src(struct node *n) { int r = regof(n); return r ? r : expr(n); }

static int own(int r)
{
    if (HAS(reserved, r)) {
        int t = alloc();
        emit("mov   r%d, r%d", t, r);
        return t;
    }
    return r;
}

static int dest(int a) { return HAS(reserved, a) ? alloc() : a; }

static void freer(int r)
{
    if (r && !HAS(reserved, r) && r != 14) busy &= ~(1u << r);
}

static void effect(struct node *n)
{
    if (n->kind == N_ASSIGN || n->kind == N_OPASSIGN || n->kind == N_PREINC || n->kind == N_PREDEC ||
        n->kind == N_POSTINC || n->kind == N_POSTDEC)
        no_value = 1;
    freer(expr(n));
    no_value = 0;
}

static int take_no_value(void) { int v = no_value; no_value = 0; return v; }

/* ---------------------------------------------------------------- expressions */
static int unsigned_type(struct type *t) { return t->uns; }   /* (pointers are unsigned) */

/* an indexed access (D59) that addr_parts set up: the index's register
   (0: none) and the scale, for the next load / store */
static int x_reg, x_scale;
static int post_step(int base, int off, int *step);

/* fold: a pending *p++ step on this base goes into the instruction
   (post-increment, D59) */
static void load(int dst, int base, int off, struct type *ty, int fold)
{
    const char *ins;
    int step;
    if (x_reg) {
        ins = ty->size == 1 ? (ty->uns ? "lbux" : "lbx") : ty->size == 2 ? (ty->uns ? "lhux" : "lhx") : "lwx";
        emit("%-5s r%d, r%d, r%d, %d", ins, dst, base, x_reg, x_scale);
        freer(x_reg);
        x_reg = 0;
        return;
    }
    if (ty->kind == K_ARRAY || ty->kind == K_FUNC || is_memval(ty)) {      /* (by address) */
        if (dst != base || off) emit("addi  r%d, r%d, %d", dst, base, off);
        return;
    }
    ins = ty->size == 1 ? (ty->uns ? "lbu" : "lb") : ty->size == 2 ? (ty->uns ? "lhu" : "lh") : "lw";
    if (fold && dst != base && post_step(base, off, &step)) {
        char m[8];
        sprintf(m, "%sp", ins);
        emit("%-5s r%d, (r%d), %d", m, dst, base, step);
    } else
        emit("%-5s r%d, %d(r%d)", ins, dst, off, base);
}

static void store(int val, int base, int off, struct type *ty, int fold)
{
    const char *ins = ty->size == 1 ? "sb" : ty->size == 2 ? "sh" : "sw";
    int step;
    if (x_reg) {
        emit("%-5s r%d, r%d, r%d, %d", ty->size == 1 ? "sbx" : ty->size == 2 ? "shx" : "swx", val, base, x_reg, x_scale);
        freer(x_reg);
        x_reg = 0;
        return;
    }
    if (fold && post_step(base, off, &step)) {
        char m[8];
        sprintf(m, "%sp", ins);
        emit("%-5s r%d, (r%d), %d", m, val, base, step);
    } else
        emit("%-5s r%d, %d(r%d)", ins, val, off, base);
}

static void narrow(int r, struct type *ty)
{
    int bits;
    if (ty->kind == K_VOID || ty->size == 4 || ty->kind == K_PTR || ty->kind == K_ARRAY || ty->kind == K_FUNC ||
        is_memval(ty))
        return;
    bits = ty->size * 8;
    if (ty->uns) emit("andi  r%d, r%d, %d", r, r, (1 << bits) - 1);
    else {
        emit("slli  r%d, r%d, %d", r, r, 32 - bits);
        emit("srai  r%d, r%d, %d", r, r, 32 - bits);
    }
}

/* (base 14, offset) of a local scalar lvalue: 1 and *off, or 0 */
static int local_mem(struct node *n, int *off)
{
    if (n->kind == N_VAR && n->obj->local && fits16(n->obj->offset)) { *off = n->obj->offset; return 1; }
    if (n->kind == N_VAROFF && fits16(n->var->offset + n->off)) { *off = n->var->offset + n->off; return 1; }
    return 0;
}

static int addr(struct node *n);

/* the pointer increment a *p++ access left to follow it */
static int after_reg, after_step;
static void flush_after(void)
{
    if (after_reg) emit("addi  r%d, r%d, %d", after_reg, after_reg, after_step);
    after_reg = 0;
}

/* the step of the pending *p++ on base, taken over by the access (it is
   then no longer pending): 1 and *step, or 0 */
static int post_step(int base, int off, int *step)
{
    if (!after_reg || off != 0 || after_reg != base || after_step < -128 || after_step > 127) return 0;
    *step = after_step;
    after_reg = 0;
    return 1;
}

/* a constant pointer that reaches as an offset from r0 (the I/O page at the
   top of the address space, low memory): 1 and its offset, else 0 */
static int const_addr(struct node *e, int *off)
{
    if (e->kind == N_CAST && e->ty->kind == K_PTR) e = e->expr;
    if (e->kind == N_NUM && fits16((int)e->val)) { *off = (int)e->val; return 1; }
    return 0;
}

/* the base register and offset of an lvalue; the base may be borrowed (a
   register variable, r14 for a local, r0 for a constant address). A
   constant added to a pointer (p[2], p + 3) goes into the offset. */
static int addr_parts_x(struct node *n, int *off, int index_ok);
static int addr_parts(struct node *n, int *off) { return addr_parts_x(n, off, 0); }

/* with index_ok, a scalar p[i] (p + i * 1, 2, 4 or 8) sets x_reg and
   x_scale for an indexed access (D59) */
static int addr_parts_x(struct node *n, int *off, int index_ok)
{
    if (n->kind == N_DEREF) {
        struct node *e = n->expr;
        int rv, d;
        if (index_ok && e->kind == N_BINOP && !strcmp(e->op, "+") && e->ty->kind == K_PTR &&
            e->lhs->ty->kind == K_PTR && e->rhs->kind != N_NUM && is_scalar(n->ty) && !is_memval(n->ty) &&
            (n->ty->size == 1 || n->ty->size == 2 || n->ty->size == 4)) {
            struct node *i = e->rhs;
            int scale = 1;
            if (i->kind == N_BINOP && !strcmp(i->op, "*") && i->rhs->kind == N_NUM &&
                (i->rhs->val == 2 || i->rhs->val == 4 || i->rhs->val == 8)) {
                scale = (int)i->rhs->val;
                i = i->lhs;
            }
            if (is_integer(i->ty) && i->ty->size == 4) {
                int b = src(e->lhs);
                x_reg = src(i);
                x_scale = scale;
                *off = 0;
                return b;
            }
        }
        rv = regof(e);
        if (rv) { *off = 0; return rv; }
        if (const_addr(e, off)) return 0;
        if ((e->kind == N_POSTINC || e->kind == N_POSTDEC) && e->expr->ty->kind == K_PTR && (rv = regof(e->expr))) {
            /* *p++ on a register pointer: the access through p, then p moves */
            after_reg = rv;
            after_step = e->kind == N_POSTINC ? e->step : -e->step;
            *off = 0;
            return rv;
        }
        if (e->kind == N_BINOP && (!strcmp(e->op, "+") || !strcmp(e->op, "-")) && e->ty->kind == K_PTR &&
            e->rhs->kind == N_NUM) {
            d = !strcmp(e->op, "+") ? (int)e->rhs->val : -(int)e->rhs->val;
            if (fits16(d)) { *off = d; return src(e->lhs); }
        }
    }
    if (n->kind == N_MEMBER) {
        int o, b = addr_parts(n->expr, &o), r;
        if (fits16(o + n->mem->offset)) { *off = o + n->mem->offset; return b; }
        r = b != 0 && b != 14 ? dest(b) : alloc();
        emit("addi  r%d, r%d, %d", r, b, o + n->mem->offset);
        *off = 0;
        return r;
    }
    if (n->kind == N_VAR && n->obj->local && fits16(n->obj->offset) && !regof(n)) { *off = n->obj->offset; return 14; }
    *off = 0;
    return addr(n);
}

/* temporary v's value into register variable rv (and v freed): the last
   instruction rewritten to write rv when it wrote v */
static void retarget(int v, int rv)
{
    char *last = out.n ? out.a[out.n - 1] : "";
    char *p = last, *mn, *rest, want[8];
    int mnlen;
    while (*p == ' ' || *p == '\t') p++;
    mn = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    mnlen = p - mn;
    while (*p == ' ' || *p == '\t') p++;
    rest = p;
    sprintf(want, "r%d,", v);
    if (mnlen && *rest &&
        !(mnlen == 2 && (!strncmp(mn, "sw", 2) || !strncmp(mn, "sh", 2) || !strncmp(mn, "sb", 2) || !strncmp(mn, "jr", 2))) &&
        !(mnlen == 4 && !strncmp(mn, "call", 4)) && !(mnlen == 5 && !strncmp(mn, "callr", 5)) &&
        !(mnlen == 3 && !strncmp(mn, "jmp", 3)) && mn[0] != 'b' && !strncmp(rest, want, strlen(want))) {
        char *n = fmt("%.*sr%d%s", (int)(rest - last), last, rv, rest + strlen(want) - 1);
        out.a[out.n - 1] = keep_line(n);
        free(n);
    } else
        emit("mov   r%d, r%d", rv, v);
    freer(v);
}

static int addr(struct node *n)
{
    int r;
    if (regof(n)) error(n->tok, "internal: the address of a register variable");
    switch (n->kind) {
    case N_VAR:
        r = alloc();
        if (n->obj->local) emit("addi  r%d, r14, %d", r, n->obj->offset);
        else emit("la    r%d, %s", r, sym(n->obj->label));
        return r;
    case N_VAROFF:
        r = alloc();
        emit("addi  r%d, r14, %d", r, n->var->offset + n->off);
        return r;
    case N_STR:
        r = alloc();
        emit("la    r%d, %s", r, n->label);
        return r;
    case N_DEREF:
        return expr(n->expr);
    case N_MEMBER:
        r = addr(n->expr);
        if (n->mem->offset) emit("addi  r%d, r%d, %d", r, r, n->mem->offset);
        return r;
    }
    error(n->tok, "not an lvalue");
    return 0;
}

/* an immediate instruction for `reg op v` (v the constant node): 1 with *ins and *imm, or 0 */
static int imm_op(const char *op, int uns, struct node *c, const char **ins, int *imm)
{
    unsigned u = c->val;
    int cu = unsigned_type(c->ty), s = (int)u;
    int pos = cu ? u != 0 : s > 0;
    int pow2 = pos && (u & (u - 1)) == 0, lg = 0;
    if (pow2) { unsigned x = u; while (x > 1) { x >>= 1; lg++; } }
    if (!strcmp(op, "+") && (cu ? u <= 32767 : fits16(s))) { *ins = "addi"; *imm = s; return 1; }
    if (!strcmp(op, "-") && (cu ? u <= 32768 : (s >= -32767 && s <= 32768))) { *ins = "addi"; *imm = -s; return 1; }
    if ((!strcmp(op, "&") || !strcmp(op, "|") || !strcmp(op, "^")) && (cu ? u <= 0xFFFF : (s >= 0 && s <= 0xFFFF))) {
        *ins = op[0] == '&' ? "andi" : op[0] == '|' ? "ori" : "xori";
        *imm = s;
        return 1;
    }
    if (!strcmp(op, "<<") && (cu ? u <= 31 : (s >= 0 && s <= 31))) { *ins = "slli"; *imm = s; return 1; }
    if (!strcmp(op, ">>") && (cu ? u <= 31 : (s >= 0 && s <= 31))) { *ins = uns ? "srli" : "srai"; *imm = s; return 1; }
    if (!strcmp(op, "*") && pow2) { *ins = "slli"; *imm = lg; return 1; }
    if (!strcmp(op, "/") && uns && pow2) { *ins = "srli"; *imm = lg; return 1; }
    if (!strcmp(op, "%") && uns && pow2 && u - 1 <= 0xFFFF) { *ins = "andi"; *imm = (int)(u - 1); return 1; }
    return 0;
}

static void arith(const char *op, int uns, int a, int b, int d)
{
    const char *ins;
    if (!strcmp(op, "+")) ins = "add";
    else if (!strcmp(op, "-")) ins = "sub";
    else if (!strcmp(op, "*")) ins = "mul";
    else if (!strcmp(op, "&")) ins = "and";
    else if (!strcmp(op, "|")) ins = "or";
    else if (!strcmp(op, "^")) ins = "xor";
    else if (!strcmp(op, "<<")) ins = "sll";
    else if (!strcmp(op, ">>")) ins = uns ? "srl" : "sra";
    else if (!strcmp(op, "/")) ins = uns ? "divu" : "div";
    else if (!strcmp(op, "%")) ins = uns ? "remu" : "rem";
    else { error(0, "internal: op %s", op); ins = ""; }
    emit("%-5s r%d, r%d, r%d", ins, d ? d : a, a, b);
}

static int binop(struct node *n)
{
    int a = src(n->lhs), uns, b, d, imm;
    const char *ins;
    uns = !strcmp(n->op, ">>") ? n->lhs->ty->uns : n->ty->uns;
    if (n->rhs->kind == N_NUM && imm_op(n->op, uns, n->rhs, &ins, &imm)) {
        d = dest(a);
        emit("%-5s r%d, r%d, %d", ins, d, a, imm);
        return d;
    }
    b = src(n->rhs);
    d = dest(a);
    arith(n->op, uns, a, b, d);
    freer(b);
    return d;
}

static int cmp(struct node *n)
{
    int a = src(n->lhs), b = src(n->rhs);
    const char *u = n->uns ? "u" : "", *op = n->op;
    if (HAS(reserved, a)) {
        int d = alloc();
        emit("mov   r%d, r%d", d, a);
        a = d;
    }
    if (!strcmp(op, "==")) {
        emit("xor   r%d, r%d, r%d", a, a, b);
        emit("sltiu r%d, r%d, 1", a, a);
    } else if (!strcmp(op, "!=")) {
        emit("xor   r%d, r%d, r%d", a, a, b);
        emit("sltu  r%d, r0, r%d", a, a);
    } else if (!strcmp(op, "<")) emit("slt%s  r%d, r%d, r%d", u, a, a, b);
    else if (!strcmp(op, ">")) emit("slt%s  r%d, r%d, r%d", u, a, b, a);
    else if (!strcmp(op, "<=")) {
        emit("slt%s  r%d, r%d, r%d", u, a, b, a);
        emit("xori  r%d, r%d, 1", a, a);
    } else {
        emit("slt%s  r%d, r%d, r%d", u, a, a, b);
        emit("xori  r%d, r%d, 1", a, a);
    }
    freer(b);
    return a;
}

/* copy a struct of type ty, in the widest pieces its size and its alignment
   allow (a 2-aligned struct may lie at an odd halfword) */
static void copy_struct(int dst, int s, struct type *ty)
{
    int t = alloc(), o, size = ty->size;
    int unit = size % 4 == 0 && ty->align >= 4 ? 4 : size % 2 == 0 && ty->align >= 2 ? 2 : 1;
    const char *ld = unit == 4 ? "lw   " : unit == 2 ? "lhu  " : "lbu  ";
    const char *st = unit == 4 ? "sw   " : unit == 2 ? "sh   " : "sb   ";
    if (size > 64) {                    /* (a loop, not a long run of copies) */
        int a = alloc(), b = alloc(), e = alloc();
        char *lp = newlab();
        emit("mov   r%d, r%d", a, s);
        emit("mov   r%d, r%d", b, dst);
        emit("addi  r%d, r%d, %d", e, dst, size);
        label(lp);
        emit("%s r%d, 0(r%d)", ld, t, a);
        emit("%s r%d, 0(r%d)", st, t, b);
        emit("addi  r%d, r%d, %d", a, a, unit);
        emit("addi  r%d, r%d, %d", b, b, unit);
        emit("bne   r%d, r%d, %s", b, e, lp);
        freer(a); freer(b); freer(e);
    } else
        for (o = 0; o < size; o += unit) {
            emit("%s r%d, %d(r%d)", ld, t, o, s);
            emit("%s r%d, %d(r%d)", st, t, o, dst);
        }
    freer(t);
}

/* r holds a bit-field's unit: leave the field's value, sign- or zero-extended */
static void extract(int r, int bit, int width, struct type *ty)
{
    if (32 - bit - width) emit("slli  r%d, r%d, %d", r, r, 32 - bit - width);
    if (32 - width) emit("%s  r%d, r%d, %d", ty->uns ? "srli" : "srai", r, r, 32 - width);
}

/* a bit-field's unit = (unit & ~mask) | (value << bit & mask); the value the
   field now holds is the result (as p32cc.py's assign_bits) */
static int assign_bits(struct node *n)
{
    struct node *lhs = n->lhs;
    struct type *ty = n->ty;
    int bit = lhs->bfbit, width = lhs->bfwidth, novalue, v, b, off, u, m;
    unsigned mask = (width == 32 ? 0xFFFFFFFFu : (1u << width) - 1) << bit;
    novalue = take_no_value();
    v = own(src(n->rhs));
    if (lhs->kind == N_VAROFF && local_mem(lhs, &off)) b = 14;
    else b = addr_parts(lhs, &off);
    u = alloc();
    m = alloc();
    load(u, b, off, ty, 0);
    emit("li    r%d, %d", m, (int)mask);
    emit("nor   r%d, r%d, r0", m, m);
    emit("and   r%d, r%d, r%d", u, u, m);
    emit("slli  r%d, r%d, %d", m, v, 32 - width);
    if (32 - width - bit) emit("srli  r%d, r%d, %d", m, m, 32 - width - bit);
    emit("or    r%d, r%d, r%d", u, u, m);
    store(u, b, off, ty, 0);
    freer(m);
    freer(u);
    flush_after();
    freer(b);
    if (novalue) { freer(v); return 0; }
    extract(v, 0, width, ty);
    return v;
}

static int assign(struct node *n)
{
    struct node *lhs = n->lhs;
    struct type *ty = n->ty;
    int novalue, rv, v, o;
    if (lhs->bfwidth) return assign_bits(n);
    if (is_memval(ty)) {
        int k = n->rhs->kind, s, d;
        take_no_value();                /* (else it reaches the operands: m[k++] = x lost k's value) */
        s = (k == N_VAR || k == N_DEREF || k == N_MEMBER || k == N_VAROFF) ? addr(n->rhs) : expr(n->rhs);
        d = addr(lhs);
        copy_struct(d, s, ty);
        freer(s);
        return d;
    }
    novalue = take_no_value();
    rv = regof(lhs);
    if (rv) {
        if (rv != regof(n->rhs) || !novalue) {
            v = src(n->rhs);
            if (HAS(reserved, v)) emit("mov   r%d, r%d", rv, v);
            else retarget(v, rv);
        }
        return novalue ? 0 : own(rv);
    }
    v = src(n->rhs);
    if (local_mem(lhs, &o))
        store(v, 14, o, ty, 0);
    else {
        int off, b = addr_parts_x(lhs, &off, 1);
        store(v, b, off, ty, 1);
        flush_after();
        freer(b);
    }
    if (novalue) { freer(v); return 0; }
    return own(v);
}

static int opassign(struct node *n)
{
    struct node *lhs = n->lhs;
    struct type *ty = n->ty;
    int rv = regof(lhs), novalue = take_no_value(), uns, imm, base, off, a = 0, v, b, o;
    const char *ins;
    uns = ty->kind != K_PTR ? (!strcmp(n->op, ">>") ? ty->uns : n->opty->uns) : 1;
    if (rv) {
        if (n->rhs->kind == N_NUM && imm_op(n->op, uns, n->rhs, &ins, &imm))
            emit("%-5s r%d, r%d, %d", ins, rv, rv, imm);
        else {
            b = expr(n->rhs);
            arith(n->op, uns, rv, b, 0);
            freer(b);
        }
        narrow(rv, ty);
        return novalue ? 0 : own(rv);
    }
    if (local_mem(lhs, &o)) { base = 14; off = o; }
    else { a = addr(lhs); base = a; off = 0; }
    v = alloc();
    load(v, base, off, ty, 0);
    if (n->rhs->kind == N_NUM && imm_op(n->op, uns, n->rhs, &ins, &imm))
        emit("%-5s r%d, r%d, %d", ins, v, v, imm);
    else {
        b = expr(n->rhs);
        arith(n->op, uns, v, b, 0);
        freer(b);
    }
    narrow(v, ty);
    store(v, base, off, ty, 0);
    freer(a);
    return v;
}

static int incdec(struct node *n)
{
    struct node *e = n->expr;
    struct type *ty = n->ty;
    int rv = regof(e), novalue = take_no_value(), step, base, off, a = 0, v, t, o;
    int inc = n->kind == N_PREINC || n->kind == N_POSTINC, pre = n->kind == N_PREINC || n->kind == N_PREDEC;
    step = inc ? n->step : -n->step;
    if (rv) {
        v = 0;
        if (!pre && !novalue) {
            v = alloc();
            emit("mov   r%d, r%d", v, rv);
        }
        emit("addi  r%d, r%d, %d", rv, rv, step);
        narrow(rv, ty);
        if (pre && !novalue) v = own(rv);
        return v;
    }
    if (local_mem(e, &o)) { base = 14; off = o; }
    else { a = addr(e); base = a; off = 0; }
    v = alloc();
    load(v, base, off, ty, 0);
    if (pre) {
        emit("addi  r%d, r%d, %d", v, v, step);
        narrow(v, ty);
        store(v, base, off, ty, 0);
        freer(a);
        return v;
    }
    t = alloc();
    emit("addi  r%d, r%d, %d", t, v, step);
    narrow(t, ty);
    store(t, base, off, ty, 0);
    freer(t);
    freer(a);
    return v;
}

static int call(struct node *n)
{
    int live[8], nlive = 0, r, k, nargs = n->list.n, nreg, f = 0, res = 0, nwords = 0, *slots;
    for (r = 1; r <= 7; r++) if (HAS(busy, r)) live[nlive++] = r;
    if (nlive) {
        emit("addi  sp, sp, %d", -4 * nlive);
        for (k = 0; k < nlive; k++) emit("sw    r%d, %d(sp)", live[k], 4 * k);
        for (k = 0; k < nlive; k++) busy &= ~(1u << live[k]);
    }
    for (k = 0; k < nargs && n->ftype->variadic; k++)
        if (((struct node *)n->list.a[k])->ty->kind == K_DOUBLE) printf_fp = 1;
    nreg = stack_args(n->ftype) ? 0 : (nargs < 4 ? nargs : 4);
    /* the outgoing arguments, left to right, into slots reserved first (a
       word each, a struct as many as it needs; nested calls leave sp as
       they found it) */
    slots = xmalloc(sizeof(int) * (nargs ? nargs : 1));
    for (k = 0; k < nargs; k++) {
        slots[k] = nwords;
        nwords += words(((struct node *)n->list.a[k])->ty);
    }
    if (nwords) emit("addi  sp, sp, %d", -4 * nwords);
    for (k = 0; k < nargs; k++) {
        struct node *a = n->list.a[k];
        r = expr(a);
        if (is_memval(a->ty)) {         /* (r: its address; a copy of its own) */
            int d = alloc();
            emit("addi  r%d, sp, %d", d, 4 * slots[k]);
            copy_struct(d, r, a->ty);
            freer(d);
        } else
            emit("sw    r%d, %d(sp)", r, 4 * slots[k]);
        freer(r);
    }
    free(slots);
    if (!n->direct) {
        f = expr(n->fn);
        if (f <= 4) {
            emit("mov   r5, r%d", f);
            freer(f);
            f = 5;
        }
    }
    for (k = 0; k < nreg; k++) emit("lw    r%d, %d(sp)", k + 1, 4 * k);
    if (nreg) emit("addi  sp, sp, %d", 4 * nreg);
    if (n->direct) emit("call  %s", sym(n->direct->label));
    else {
        emit("callr r%d", f);
        freer(f);
    }
    if (nwords - nreg) emit("addi  sp, sp, %d", 4 * (nwords - nreg));
    if (n->ty->kind != K_VOID) {
        for (r = 1; r < 14; r++) {
            int in_live = 0;
            for (k = 0; k < nlive; k++) if (live[k] == r) in_live = 1;
            if (!HAS(busy, r) && !in_live) { res = r; break; }
        }
        busy |= 1u << res;
        if (res >= 8) callee |= 1u << res;
        if (res != 1) emit("mov   r%d, r1", res);
        narrow(res, n->ty);
    }
    if (nlive) {
        for (k = 0; k < nlive; k++) {
            emit("lw    r%d, %d(sp)", live[k], 4 * k);
            busy |= 1u << live[k];
        }
        emit("addi  sp, sp, %d", 4 * nlive);
    }
    return res;
}

static void branch(struct node *n, const char *target, int when);

static int expr(struct node *n)
{
    int r, o;
    switch (n->kind) {
    case N_NUM:
        r = alloc();
        if (unsigned_type(n->ty)) emit("li    r%d, %u", r, n->val);
        else emit("li    r%d, %d", r, (int)n->val);
        return r;
    case N_STR:
        return addr(n);
    case N_VAR: case N_VAROFF: {
        int rv = regof(n);
        if (rv) return own(rv);
        if (local_mem(n, &o) && n->ty->kind != K_ARRAY && n->ty->kind != K_FUNC && !is_memval(n->ty)) {
            r = alloc();
            load(r, 14, o, n->ty, 0);
            return r;
        }
        if (n->kind == N_VAR && n->ty->kind == K_FUNC) {
            r = alloc();
            emit("la    r%d, %s", r, sym(n->obj->label));
            return r;
        }
        r = addr(n);
        load(r, r, 0, n->ty, 0);
        return r;
    }
    case N_ADDR:
        if (n->expr->kind == N_VAR && n->expr->ty->kind == K_FUNC) {
            r = alloc();
            emit("la    r%d, %s", r, sym(n->expr->obj->label));
            return r;
        }
        return addr(n->expr);
    case N_DEREF: case N_MEMBER: {
        int b, off;
        if (n->kind == N_MEMBER && n->expr->kind != N_DEREF && n->expr->kind != N_VAR && n->expr->kind != N_MEMBER &&
            n->expr->kind != N_VAROFF)
            error(n->tok, "member of a non-lvalue struct is not supported");
        b = addr_parts_x(n, &off, !n->bfwidth);
        r = b != 0 && b != 14 ? dest(b) : alloc();
        load(r, b, off, n->ty, !n->bfwidth);
        flush_after();
        if (n->bfwidth) extract(r, n->bfbit, n->bfwidth, n->ty);
        return r;
    }
    case N_CAST: {
        struct type *s = n->expr->ty;
        r = expr(n->expr);
        if (n->ty->kind == K_VOID) { freer(r); return 0; }
        if (is_integer(n->ty) && (n->ty->size < 4 || n->ty->size < s->size))
            if (!(s->size <= n->ty->size && s->uns == n->ty->uns && is_integer(s))) narrow(r, n->ty);
        return r;
    }
    case N_NEG:
        r = expr(n->expr);
        emit("sub   r%d, r0, r%d", r, r);
        return r;
    case N_BITNOT:
        r = expr(n->expr);
        emit("nor   r%d, r%d, r0", r, r);
        return r;
    case N_NOT:
        r = expr(n->expr);
        emit("sltiu r%d, r%d, 1", r, r);
        return r;
    case N_BINOP: return binop(n);
    case N_CMP: return cmp(n);
    case N_LOGAND: case N_LOGOR: {
        char *end;
        r = alloc();
        end = newlab();
        if (n->kind == N_LOGAND) {
            emit("li    r%d, 0", r);
            branch(n->lhs, end, 0);
            branch(n->rhs, end, 0);
            emit("li    r%d, 1", r);
        } else {
            emit("li    r%d, 1", r);
            branch(n->lhs, end, 1);
            branch(n->rhs, end, 1);
            emit("li    r%d, 0", r);
        }
        label(end);
        return r;
    }
    case N_COND: {
        char *els = newlab(), *end = newlab();
        int r2;
        branch(n->cond, els, 0);
        if (n->ty->kind == K_VOID) {
            freer(expr(n->a));
            emit("jmp   %s", end);
            label(els);
            freer(expr(n->b));
            label(end);
            return 0;
        }
        r = expr(n->a);
        emit("jmp   %s", end);
        label(els);
        r2 = expr(n->b);
        if (r2 != r) {
            emit("mov   r%d, r%d", r, r2);
            freer(r2);
        }
        label(end);
        return r;
    }
    case N_COMMA:
        freer(expr(n->lhs));
        return expr(n->rhs);
    case N_MEMADDR: return expr(n->expr);   /* (a long long's address, as a pointer) */
    case N_STMTEXPR: {                  /* (a compound literal: its initialisation, then its address;
                                           only these two kinds, run among live temporaries) */
        int k;
        for (k = 0; k < n->list.n; k++) {
            struct node *s = n->list.a[k];
            if (s->kind == N_MEMZERO) zero(s->var);
            else freer(expr(s->expr));
        }
        return expr(n->expr);
    }
    case N_ASSIGN: return assign(n);
    case N_OPASSIGN: return opassign(n);
    case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: return incdec(n);
    case N_CALL: return call(n);
    }
    error(n->tok, "internal: expression %d", n->kind);
    return 0;
}

/* ---------------------------------------------------------------- conditions */
static void branch(struct node *n, const char *target, int when)
{
    switch (n->kind) {
    case N_NUM:
        if ((n->val != 0) == when) emit("jmp   %s", target);
        return;
    case N_NOT:
        branch(n->expr, target, !when);
        return;
    case N_LOGAND:
        if (!when) { branch(n->lhs, target, 0); branch(n->rhs, target, 0); }
        else {
            char *skip = newlab();
            branch(n->lhs, skip, 0);
            branch(n->rhs, target, 1);
            label(skip);
        }
        return;
    case N_LOGOR:
        if (when) { branch(n->lhs, target, 1); branch(n->rhs, target, 1); }
        else {
            char *skip = newlab();
            branch(n->lhs, skip, 1);
            branch(n->rhs, target, 0);
            label(skip);
        }
        return;
    case N_CMP: {
        const char *op = n->op, *ins;
        char insb[8];
        int a, b, t;
        if (!when) {
            op = !strcmp(op, "==") ? "!=" : !strcmp(op, "!=") ? "==" : !strcmp(op, "<") ? ">=" :
                 !strcmp(op, ">=") ? "<" : !strcmp(op, ">") ? "<=" : ">";
        }
        a = src(n->lhs);
        b = (n->rhs->kind == N_NUM && n->rhs->val == 0) ? 0 : src(n->rhs);
        if (!strcmp(op, ">") || !strcmp(op, "<=")) {
            t = a; a = b; b = t;
            op = !strcmp(op, ">") ? "<" : ">=";
        }
        ins = !strcmp(op, "==") ? "beq" : !strcmp(op, "!=") ? "bne" : !strcmp(op, "<") ? "blt" : "bge";
        sprintf(insb, "%s%s", ins, n->uns && (!strcmp(op, "<") || !strcmp(op, ">=")) ? "u" : "");
        emit("%-5s r%d, r%d, %s", insb, a, b, target);
        freer(a);
        freer(b);
        return;
    }
    }
    {
        int r = src(n);
        emit("%-5s r%d, r0, %s", when ? "bne" : "beq", r, target);
        freer(r);
    }
}

/* ---------------------------------------------------------------- statements */
static void zero(struct obj *var)
{
    int size = (var->ty->size + 3) / 4 * 4, o, a, e;
    char *lp;
    if (size <= 64) {
        for (o = 0; o < size; o += 4) emit("sw    r0, %d(r14)", var->offset + o);
        return;
    }
    a = alloc();
    e = alloc();
    lp = newlab();
    emit("addi  r%d, r14, %d", a, var->offset);
    emit("addi  r%d, r%d, %d", e, a, size);
    label(lp);
    emit("sw    r0, 0(r%d)", a);
    emit("addi  r%d, r%d, 4", a, a);
    emit("bne   r%d, r%d, %s", a, e, lp);
    freer(a);
    freer(e);
}

/* -S: each statement's source line as a comment (cc/cgen.py writes the same) */
int annotate;
static char *noted_file;
static int noted_line;
static char *src_path[4], *src_text[4];
static int src_next;

static const char *source_text(const char *path)        /* a file's text, kept (four at most) */
{
    int k;
    FILE *f;
    char *buf;
    int n = 0, cap = 4096, got;
    for (k = 0; k < 4; k++) if (src_path[k] && !strcmp(src_path[k], path)) return src_text[k];
    k = src_next++ % 4;
    if (src_path[k]) { free(src_path[k]); free(src_text[k]); }
    src_path[k] = xstrdup(path);
    src_text[k] = 0;
    if (!(f = fopen(path, "r"))) return 0;
    buf = xmalloc(cap);
    while ((got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (n + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
    }
    buf[n] = 0;
    fclose(f);
    return src_text[k] = buf;
}

static void note_line(struct tok *t)
{
    const char *text, *base, *p, *e;
    char line[104];
    int k, n;
    if (!t || !t->file) return;
    if (noted_file && t->line == noted_line && !strcmp(noted_file, t->file)) return;
    noted_file = t->file;
    noted_line = t->line;
    line[0] = 0;
    if ((text = source_text(t->file)) != 0) {
        for (p = text, k = 1; *p && k < t->line; p++) if (*p == '\n') k++;
        if (k == t->line) {
            while (*p == ' ' || *p == '\t') p++;
            for (e = p; *e && *e != '\n'; e++) ;
            while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
            n = e - p > 100 ? 100 : e - p;
            memcpy(line, p, n);
            line[n] = 0;
        }
    }
    base = strrchr(t->file, '/');
    base = base ? base + 1 : t->file;
    {
        char *a = fmt("; %s:%d: %s", base, t->line, line);
        add_line(a);
        free(a);
    }
}

/* ---------------------------------------------------------------- switch */
/* a switch becomes a jump table when it has at least SWITCH_MIN cases and
   the table (one word per value from the lowest case to the highest) is at
   most SWITCH_DENSITY times as long as the case list */
#define SWITCH_MIN 4
#define SWITCH_DENSITY 3

static long long caseval(struct casepair *c, int uns) { return uns ? (long long)c->val : (long long)(int)c->val; }

static void table_range(struct node *n, int uns, long long *lo, long long *hi)
{
    int k;
    *lo = *hi = caseval(n->cases.a[0], uns);
    for (k = 1; k < n->cases.n; k++) {
        long long v = caseval(n->cases.a[k], uns);
        if (v < *lo) *lo = v;
        if (v > *hi) *hi = v;
    }
}

static int dense(struct node *n, int uns)
{
    long long lo, hi;
    if (n->cases.n < SWITCH_MIN) return 0;
    table_range(n, uns, &lo, &hi);
    return hi - lo + 1 <= (long long)SWITCH_DENSITY * n->cases.n;
}

/* a sparse switch: compare with each case in turn */
static void compare_chain(struct node *n, int v, int t, char *brk, int uns)
{
    int k;
    for (k = 0; k < n->cases.n; k++) {
        struct casepair *c = n->cases.a[k];
        if (c->val == 0) emit("beq   r%d, r0, %s", v, c->label);
        else {
            if (uns) emit("li    r%d, %u", t, c->val);
            else emit("li    r%d, %d", t, (int)c->val);
            emit("beq   r%d, r%d, %s", v, t, c->label);
        }
    }
    emit("jmp   %s", n->dflt ? n->dflt : brk);
}

/* a dense switch: one unsigned compare against the table's size (which
   also catches values below the lowest case), then a jump through a table
   of case addresses that follows the JR in .text */
static void jump_table(struct node *n, int v, int t, char *brk, int uns)
{
    long long lo, hi, w;
    char *other = n->dflt ? n->dflt : brk, *tab;
    int d = alloc(), x = v, k;
    tab = newlab();
    table_range(n, uns, &lo, &hi);
    if (lo) {
        if (lo >= -32767 && lo <= 32768) emit("addi  r%d, r%d, %d", d, v, (int)-lo);
        else {
            emit("li    r%d, %d", d, (int)(unsigned)lo);
            emit("sub   r%d, r%d, r%d", d, v, d);
        }
        x = d;
    }
    emit("li    r%d, %d", t, (int)(hi - lo + 1));
    emit("bgeu  r%d, r%d, %s", x, t, other);
    emit("slli  r%d, r%d, 2", d, x);
    emit("la    r%d, %s", t, tab);
    emit("add   r%d, r%d, r%d", d, d, t);
    emit("lw    r%d, 0(r%d)", d, d);
    emit("jr    r%d", d);
    freer(d);
    label(tab);
    for (w = lo; w <= hi; w++) {
        char *lab = other;
        for (k = 0; k < n->cases.n; k++)
            if (caseval(n->cases.a[k], uns) == w) lab = ((struct casepair *)n->cases.a[k])->label;
        emit(".word %s", lab);
    }
}

static void stmt(struct node *n)
{
    int k;
    if (!n) return;
    if (annotate && n->kind != N_BLOCK) note_line(n->tok);
    switch (n->kind) {
    case N_BLOCK:
        for (k = 0; k < n->list.n; k++) stmt(n->list.a[k]);
        return;
    case N_EXPRSTMT:
        effect(n->expr);
        return;
    case N_IF: {
        char *els = newlab(), *end = newlab();
        branch(n->cond, els, 0);
        stmt(n->then);
        if (n->els) emit("jmp   %s", end);
        label(els);
        if (n->els) {
            stmt(n->els);
            label(end);
        }
        return;
    }
    case N_FOR: {
        char *top = newlab(), *cont = newlab(), *brk = newlab(), *test;
        if (n->init) stmt(n->init);
        test = newlab();
        emit("jmp   %s", test);
        label(top);
        vpush(&brks, brk); vpush(&conts, cont);
        stmt(n->body);
        brks.n--; conts.n--;
        label(cont);
        if (n->inc) effect(n->inc);
        label(test);
        if (n->cond) branch(n->cond, top, 1);
        else emit("jmp   %s", top);
        label(brk);
        return;
    }
    case N_DO: {
        char *top = newlab(), *cont = newlab(), *brk = newlab();
        label(top);
        vpush(&brks, brk); vpush(&conts, cont);
        stmt(n->body);
        brks.n--; conts.n--;
        label(cont);
        branch(n->cond, top, 1);
        label(brk);
        return;
    }
    case N_SWITCH: {
        char *brk = newlab();
        int v = expr(n->expr), t = alloc(), uns = promote(n->expr->ty)->uns;
        if (dense(n, uns)) jump_table(n, v, t, brk, uns);
        else compare_chain(n, v, t, brk, uns);
        freer(t);
        freer(v);
        vpush(&brks, brk);
        stmt(n->body);
        brks.n--;
        label(brk);
        return;
    }
    case N_CASELABEL:
        label(n->label);
        stmt(n->body);
        return;
    case N_BREAK:
        emit("jmp   %s", (char *)brks.a[brks.n - 1]);
        return;
    case N_CONTINUE:
        emit("jmp   %s", (char *)conts.a[conts.n - 1]);
        return;
    case N_RETURN:
        if (n->expr) {
            int r = expr(n->expr);
            if (r != 1) emit("mov   r1, r%d", r);
            freer(r);
        }
        emit("jmp   %s", retlab);
        return;
    case N_GOTO:
        emit("jmp   .L%s_u_%s", fname, n->label);
        return;
    case N_LABEL:
        label(fmt(".L%s_u_%s", fname, n->label));
        stmt(n->body);
        return;
    case N_MEMZERO:
        zero(n->var);
        return;
    }
    error(n->tok, "internal: statement %d", n->kind);
}

/* ---------------------------------------------------------------- functions */
static void function_with(struct obj *fn, struct obj **rv, int nrv)
{
    noted_file = 0;
    int k, off = 0, locals_size, frame, nsaved = 0, saved[16], stack, w = 0;
    struct vec stores, moves, body, main_out;
    fname = fn->label;
    while (*fname == '.') fname++;
    busy = callee = reserved = 0;
    after_reg = 0;
    for (k = 0; k < fn->params.n; k++) ((struct obj *)fn->params.a[k])->reg = 0;
    for (k = 0; k < fn->locals.n; k++) ((struct obj *)fn->locals.a[k])->reg = 0;
    for (k = 0; k < nrv; k++) {
        rv[k]->reg = regvars[k];
        reserved |= 1u << regvars[k];
    }
    callee |= reserved;
    memset(&brks, 0, sizeof brks);
    memset(&conts, 0, sizeof conts);
    nlab_start = nlab;
    memset(&stores, 0, sizeof stores);
    memset(&moves, 0, sizeof moves);
    stack = stack_args(fn->ty);         /* (every argument on the stack: variadic, or a struct) */
    for (k = 0; k < fn->params.n; k++) {
        struct obj *p = fn->params.a[k];
        if (stack || k >= 4) {
            p->offset = 8 + 4 * (stack ? w : k - 4);
            if (p->reg) vpush(&moves, fmt("lw    r%d, %d(r14)", p->reg, p->offset));
        } else if (p->reg)
            vpush(&moves, fmt("mov   r%d, r%d", p->reg, k + 1));
        else {
            off += 4;
            p->offset = -off;
            vpush(&stores, fmt("sw    r%d, %d(r14)", k + 1, p->offset));
        }
        w += words(p->ty);
    }
    for (k = 0; k < fn->locals.n; k++) {
        struct obj *v = fn->locals.a[k];
        int a;
        if (v->reg) continue;
        a = v->ty->align > 4 ? v->ty->align : 4;        /* word-aligned: memzero uses sw */
        off = (off + v->ty->size + a - 1) / a * a;
        v->offset = -off;
    }
    locals_size = (off + 3) / 4 * 4;
    retlab = fmt(".L%s_ret", fname);
    main_out = out;
    memset(&out, 0, sizeof out);
    stmt(fn->body);                     /* (out of registers: longjmp to function()) */
    body = out;
    out = main_out;
    for (k = 1; k < 16; k++) if (HAS(callee, k)) saved[nsaved++] = k;
    frame = locals_size + 4 * nsaved;
    if (!fn->is_static) emit(".global %s", sym(fn->label));
    label(sym(fn->label));
    emit("addi  sp, sp, -4");
    emit("sw    r14, 0(sp)");
    emit("mov   r14, sp");
    if (frame) {
        if (fits16(-frame)) emit("addi  sp, sp, %d", -frame);
        else {
            emit("li    r6, %d", frame);
            emit("sub   sp, sp, r6");
        }
    }
    for (k = 0; k < nsaved; k++) emit("sw    r%d, %d(r14)", saved[k], -(locals_size + 4 * (k + 1)));
    for (k = 0; k < stores.n; k++) emit("%s", (char *)stores.a[k]);
    for (k = 0; k < moves.n; k++) emit("%s", (char *)moves.a[k]);
    for (k = 0; k < body.n; k++) vpush(&out, body.a[k]);
    label(retlab);
    for (k = 0; k < nsaved; k++) emit("lw    r%d, %d(r14)", saved[k], -(locals_size + 4 * (k + 1)));
    emit("mov   sp, r14");
    emit("lw    r14, 0(sp)");
    emit("addi  sp, sp, 4");
    emit("ret");
}

static void function(struct obj *fn)
{
    struct obj *cands[256];
    int nc = 0, k, j, n;
    static struct vec saved_out;
    for (k = 0; k < fn->params.n + fn->locals.n; k++) {
        struct obj *v = k < fn->params.n ? fn->params.a[k] : fn->locals.a[k - fn->params.n];
        int kd = v->ty->kind;
        if (!v->addr_taken && v->uses >= 3 && (kd == K_CHAR || kd == K_SHORT || kd == K_INT || kd == K_LONG ||
                                              kd == K_ENUM || kd == K_PTR) && nc < 256)
            cands[nc++] = v;
    }
    for (k = 1; k < nc; k++) {          /* most used first; a stable sort, as Python's */
        struct obj *x = cands[k];
        for (j = k; j > 0 && cands[j - 1]->uses < x->uses; j--) cands[j] = cands[j - 1];
        cands[j] = x;
    }
    n = nc < nregvars ? nc : nregvars;
    saved_out = out;
    for (;;) {
        if (setjmp(out_of_regs) == 0) {
            function_with(fn, cands, n);
            flush_out();
            return;
        }
        out = saved_out;                /* (the half-made body is dropped) */
        nlab = nlab_start;
        n--;
    }
}

/* ---------------------------------------------------------------- the program */
static void bytes_(const unsigned char *b, int n)
{
    int k, j;
    for (k = 0; k < n; k += 16) {
        char line[128];
        int p = sprintf(line, ".byte ");
        for (j = k; j < n && j < k + 16; j++) p += sprintf(line + p, j > k ? ",%d" : "%d", b[j]);
        emit("%s", line);
    }
}

static void global_data(struct obj *o)
{
    int k = 0, size = o->init_size ? o->init_size : o->ty->size, j;
    unsigned char *run = xmalloc(size + 1);
    int nrun = 0;
    emit(".align %d", o->ty->align > 1 ? o->ty->align : 1);
    if (!o->is_static) emit(".global %s", sym(o->label));
    label(sym(o->label));
    while (k < size) {
        struct reloc_init *r = 0;
        for (j = 0; j < o->nirel; j++) if (o->irel[j].off == k) r = &o->irel[j];
        if (r) {
            if (nrun) { bytes_(run, nrun); nrun = 0; }
            if (r->add) emit(".word %s%+d", sym(r->label), r->add);
            else emit(".word %s", sym(r->label));
            k += 4;
        } else
            run[nrun++] = o->init[k++];
    }
    if (nrun) bytes_(run, nrun);
    free(run);
}

/* the file in three steps: gen_begin; gen_function for each function, as
   the parser finishes it (in the order defined); gen_end, the data */
void gen_begin(FILE *f)
{
    printf_fp = 0;
    if (getenv("P32CC_NOREGVARS")) nregvars = 0;
    outf = f;
    memset(&out, 0, sizeof out);
    nlab = 0;                           /* each file afresh (as p32cc.py's new Gen) */
    no_value = 0;
    fputs("; generated by p32cc\n        .text\n", f);
}

void gen_function(struct obj *fn) { function(fn); }

int gen_end(struct program *p)
{
    struct vec data, bss;
    int k;
    FILE *f = outf;
    memset(&data, 0, sizeof data);
    memset(&bss, 0, sizeof bss);
    for (k = 0; k < p->globals.n; k++) {
        struct obj *o = p->globals.a[k];
        if (o->ty->kind == K_FUNC || (o->ext && !o->is_def)) continue;
        if (o->has_init) vpush(&data, o);
        else if (o->is_def || o->tentative || o->is_static) vpush(&bss, o);
    }
    if (data.n || p->strings.n) {
        add_line("        .data");
        for (k = 0; k < data.n; k++) global_data(data.a[k]);
        for (k = 0; k < p->strings.n; k++) {
            struct node *s = p->strings.a[k];
            label(s->label);
            bytes_(s->data, s->dlen);
        }
    }
    if (printf_fp) {                    /* (libc's printf formats doubles only in programs that ask: fphook.c) */
        add_line("        .data");
        emit(".align 4");
        emit(".word __printf_fp");
    }
    if (bss.n) {
        add_line("        .bss");
        for (k = 0; k < bss.n; k++) {
            struct obj *o = bss.a[k];
            if (o->ty->size == 0) error(0, "%s has an incomplete type", o->name);
            emit(".align %d", o->ty->align > 1 ? o->ty->align : 1);
            if (!o->is_static) emit(".global %s", sym(o->label));
            label(sym(o->label));
            emit(".space %d", o->ty->size);
        }
    }
    flush_out();
    return ferror(f) ? -1 : 0;
}
