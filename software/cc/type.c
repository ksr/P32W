/* type.c -- C types (cc/cty.py): char 8 (signed), short 16, int and long 32,
 * pointers 32; alignment = size up to 4. */
#include "cc.h"

struct type *ty_void, *ty_char, *ty_uchar, *ty_short, *ty_ushort, *ty_int, *ty_uint, *ty_long, *ty_ulong,
    *ty_llong, *ty_ullong, *ty_double, *ty_bool;

struct type *new_type(int kind, int size, int align, int uns, struct type *base)
{
    struct type *t = xmalloc(sizeof *t);
    memset(t, 0, sizeof *t);
    t->kind = kind;
    t->size = size;
    t->align = align;
    t->uns = uns;
    t->base = base;
    t->length = -1;
    return t;
}

void types_init(void)
{
    ty_void = new_type(K_VOID, 1, 1, 0, 0);
    ty_char = new_type(K_CHAR, 1, 1, 0, 0);
    ty_uchar = new_type(K_CHAR, 1, 1, 1, 0);
    ty_bool = new_type(K_CHAR, 1, 1, 1, 0);
    ty_short = new_type(K_SHORT, 2, 2, 0, 0);
    ty_ushort = new_type(K_SHORT, 2, 2, 1, 0);
    ty_int = new_type(K_INT, 4, 4, 0, 0);
    ty_uint = new_type(K_INT, 4, 4, 1, 0);
    ty_long = new_type(K_LONG, 4, 4, 0, 0);
    ty_ulong = new_type(K_LONG, 4, 4, 1, 0);
    ty_llong = new_type(K_LLONG, 8, 4, 0, 0);     /* (aligned to 4, kept in memory: D30) */
    ty_ullong = new_type(K_LLONG, 8, 4, 1, 0);
    ty_double = new_type(K_DOUBLE, 8, 4, 0, 0);    /* (float is the same type: D30) */
}

int is_integer(struct type *t)
{
    return t->kind == K_CHAR || t->kind == K_SHORT || t->kind == K_INT || t->kind == K_LONG || t->kind == K_ENUM ||
           t->kind == K_LLONG;
}

int is_arith(struct type *t) { return is_integer(t) || t->kind == K_DOUBLE; }

int is_scalar(struct type *t) { return is_arith(t) || t->kind == K_PTR; }

struct type *ptr_to(struct type *t) { return new_type(K_PTR, 4, 4, 1, t); }

struct type *array_of(struct type *t, int n)
{
    struct type *a = new_type(K_ARRAY, n >= 0 ? t->size * n : 0, t->align, 0, t);
    a->length = n;
    return a;
}

struct type *func_type(struct type *ret, struct type **params, int nparams, int variadic)
{
    struct type *f = new_type(K_FUNC, 1, 1, 0, ret);
    f->params = params;
    f->nparams = nparams;
    f->variadic = variadic;
    return f;
}

struct type *enum_type(void) { return new_type(K_ENUM, 4, 4, 0, 0); }

struct type *struct_type(int kind, char *tag)
{
    struct type *s = new_type(kind, 0, 1, 0, 0);
    s->tag = tag;
    return s;
}

/* offsets, size and alignment of a struct or union (as p32cc.py's layout).
   widths[i] >= 0: a bit-field, as System V lays them out: from the lowest bit
   of a unit the size of its type, never across a unit's boundary (one that
   does not fit starts the next unit); :0 ends the unit; an unnamed one does
   not add to the alignment. */
void layout(struct type *st, char **names, struct type **tys, int *widths, int n)
{
    int bits = 0, align = 1, i, off;    /* (bits: the struct's size so far, in bits) */
    st->members = xmalloc(sizeof(struct member) * (n ? n : 1));
    st->nmembers = 0;
    for (i = 0; i < n; i++) {
        int a = tys[i]->align, w = widths[i];
        struct member *m = &st->members[st->nmembers];
        if (w >= 0) {
            int unit = tys[i]->size * 8;
            if (names[i] && a > align) align = a;
            if (st->kind == K_UNION) {
                m->name = names[i]; m->ty = tys[i]; m->offset = 0; m->bit = 0; m->width = w;
                st->nmembers++;
                if (names[i] && tys[i]->size * 8 > bits) bits = tys[i]->size * 8;
                continue;
            }
            if (w == 0) { bits = (bits + unit - 1) / unit * unit; continue; }
            if (bits / unit != (bits + w - 1) / unit) bits = (bits + unit - 1) / unit * unit;
            m->name = names[i]; m->ty = tys[i];
            m->offset = bits / unit * tys[i]->size;
            m->bit = bits % unit;
            m->width = w;
            st->nmembers++;
            bits += w;
            continue;
        }
        if (a > align) align = a;
        m->name = names[i];
        m->ty = tys[i];
        m->bit = m->width = 0;
        if (st->kind == K_STRUCT) {
            off = ((bits + 7) / 8 + a - 1) / a * a;
            m->offset = off;
            bits = (off + tys[i]->size) * 8;
        } else {
            m->offset = 0;
            if (tys[i]->size * 8 > bits) bits = tys[i]->size * 8;
        }
        st->nmembers++;
    }
    off = (bits + 7) / 8;
    st->align = align;
    st->size = (off + align - 1) / align * align;
}

/* the member (in tmp when it is found inside an anonymous member) */
struct member *find_member(struct type *st, const char *name, struct member *tmp)
{
    int i;
    for (i = 0; st->members && i < st->nmembers; i++) {
        struct member *m = &st->members[i];
        if (m->name && !strcmp(m->name, name)) return m;
        if (!m->name && (m->ty->kind == K_STRUCT || m->ty->kind == K_UNION)) {
            struct member sub, *s = find_member(m->ty, name, &sub);
            if (s) {
                tmp->name = s->name;
                tmp->ty = s->ty;
                tmp->offset = m->offset + s->offset;
                tmp->bit = s->bit;
                tmp->width = s->width;
                return tmp;
            }
        }
    }
    return 0;
}

struct type *decay(struct type *t)
{
    if (t->kind == K_ARRAY) return ptr_to(t->base);
    if (t->kind == K_FUNC) return ptr_to(t);
    return t;
}

struct type *promote(struct type *t)
{
    if (t->kind == K_CHAR || t->kind == K_SHORT || t->kind == K_ENUM) return ty_int;
    return t;
}

struct type *usual(struct type *a, struct type *b)
{
    a = promote(a);
    b = promote(b);
    if (a->kind == K_DOUBLE || b->kind == K_DOUBLE) return ty_double;
    if (a->kind == K_LLONG || b->kind == K_LLONG)     /* (it holds every 32-bit value) */
        return (a->kind == K_LLONG && a->uns) || (b->kind == K_LLONG && b->uns) ? ty_ullong : ty_llong;
    if (a->uns || b->uns) return a->kind == K_INT && b->kind == K_INT ? ty_uint : ty_ulong;
    if (a->kind == K_LONG || b->kind == K_LONG) return ty_long;
    return ty_int;
}

int compatible(struct type *a, struct type *b)
{
    if (a == b) return 1;
    if (a->kind != b->kind)
        return is_integer(a) && is_integer(b) && a->size == b->size && a->uns == b->uns;
    if (a->kind == K_PTR || a->kind == K_ARRAY || a->kind == K_FUNC) return compatible(a->base, b->base);
    if (a->kind == K_STRUCT || a->kind == K_UNION) return a == b;
    return a->uns == b->uns;
}

static const char *const kname[] = { "void", "char", "short", "int", "long", "ptr", "array", "func",
                                     "struct", "union", "enum", "long long", "double" };

char *type_str(struct type *t)
{
    if (t->kind == K_PTR) return fmt("%s *", type_str(t->base));
    if (t->kind == K_ARRAY) return t->length < 0 ? fmt("%s[]", type_str(t->base)) : fmt("%s[%d]", type_str(t->base), t->length);
    if (t->kind == K_FUNC) return fmt("%s(...)", type_str(t->base));
    if (t->kind == K_STRUCT || t->kind == K_UNION) return fmt("%s %s", kname[t->kind], t->tag ? t->tag : "<anonymous>");
    return fmt("%s%s", t->uns ? "unsigned " : "", kname[t->kind]);
}

/* (as p32cc's cty) a value kept in memory, not in a register: a struct, a long long */
int is_memval(struct type *t) { return t->kind == K_STRUCT || t->kind == K_UNION || t->kind == K_LLONG || t->kind == K_DOUBLE; }

/* the argument words a value of type t takes */
int words(struct type *t) { return is_memval(t) ? (t->size + 3) / 4 : 1; }

/* Does a call pass every argument on the stack? A variadic function, or one
   with a struct parameter (each struct copied into words of its own); else
   arguments 1-4 go in r1-r4. A function returning a struct gets a hidden
   first argument either way: where to put the result (its value in r1). */
int stack_args(struct type *ft)
{
    int k;
    if (ft->variadic) return 1;
    for (k = 0; ft->params && k < ft->nparams; k++) if (is_memval(ft->params[k])) return 1;
    return 0;
}
