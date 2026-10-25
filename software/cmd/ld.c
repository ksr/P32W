/* ld -- the P32W linker on P32W Unix (M7, D24): assembler/p32ld.py in C
 * (its comment is the manual), writing byte for byte what it writes.
 *
 *   ld [-o OUT] [-e SYM] [-M MAP] [-s] [--format aout|bin] [--text ADDR] [--data ADDR] file.o ... lib.a ...
 *
 * An a.out (default): text at 0x1000, data on the next page, bss after it;
 * or a flat image (--format bin). Archive members are taken, in name order,
 * while they define a symbol still undefined.
 *
 * Memory (D39): an object file's text and data are not kept once it is read
 * (only its names, symbols and relocations); they are read again as the
 * output is put together, and the output goes straight to the file. So a
 * program as big as python (1.7 MB, 126 objects) links in about 3 MB, where
 * holding everything took more than P32W's RAM had to spare. */
#include "objfmt.h"

#define MAGIC 0x50333277u
#define PAGE 0x1000u
#define NHASH 2048

struct input { char *name; struct obj o; unsigned base[4]; int reread; };   /* reread: a file's text and data, read again */
static struct input *in;
static int nin, capin;

struct gsym { char *name; unsigned value; const char *where; struct gsym *next; };
static struct gsym *gh[NHASH];
struct need { char *name; struct need *next; };

static unsigned hashof(const char *s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % NHASH;
}

static struct gsym *gfind(const char *name)
{
    struct gsym *g;
    for (g = gh[hashof(name)]; g; g = g->next)
        if (!strcmp(g->name, name)) return g;
    return 0;
}

static struct gsym *gadd(const char *name, unsigned value, const char *where)
{
    unsigned h = hashof(name);
    struct gsym *g = xmalloc(sizeof *g);
    g->name = (char *)name;
    g->value = value;
    g->where = where;
    g->next = gh[h];
    gh[h] = g;
    return g;
}

/* n bytes to fd; 0 or -1 */
static int write_all(int fd, const unsigned char *p, unsigned n)
{
    while (n > 0) {
        int k = write(fd, p, n);
        if (k <= 0) return -1;
        p += k;
        n -= k;
    }
    return 0;
}

static void die(const char *fmt, const char *a)
{
    fprintf(stderr, "ld: ");
    fprintf(stderr, fmt, a);
    fputc('\n', stderr);
    exit(1);
}

static void note(struct obj *o);

static void add_input(const char *name, struct obj *o)
{
    if (nin == capin) {
        struct input *n;
        capin = capin ? capin * 2 : 64;
        n = xmalloc(sizeof *n * capin);
        if (nin) memcpy(n, in, sizeof *n * nin);
        free(in);
        in = n;
    }
    in[nin].name = xstrdup(name);
    in[nin].o = *o;
    in[nin].reread = 0;
    note(&in[nin].o);
    nin++;
}

/* an object file just decoded from b: its string table moved to a block of
   its own (the names point there now), so that b can go; its text and data
   are read again when the output is put together (reread_contents) */
static void keep_names(struct obj *o, const unsigned char *b)
{
    unsigned p = 48 + ((o->tsize + 3) & ~3u) + ((o->dsize + 3) & ~3u) + 12 * o->nsym + 16 * o->nrel;
    unsigned stsz = rword(b + 40);
    const char *tab = (const char *)b + p;
    char *nt = xmalloc(stsz + 1);
    int k;
    memcpy(nt, tab, stsz);
    for (k = 0; k < o->nsym; k++) o->syms[k].name = nt + (o->syms[k].name - tab);
    for (k = 0; k < o->nrel; k++) o->rels[k].sym = nt + (o->rels[k].sym - tab);
    o->source = nt + (o->source - tab);
    o->text = o->data = 0;
}

/* ---------------------------------------------------------------- which members to take */
static int is_section(const char *s) { return !strcmp(s, ".text") || !strcmp(s, ".data") || !strcmp(s, ".bss"); }

/* names the taken objects refer to, and the ones they define globally:
   a symbol is still needed when it is in the first set and not the second */
struct nent { const char *name; struct nent *next; };
static struct nent *refh[NHASH], *defh[NHASH];

static int nfind(struct nent **t, const char *name)
{
    struct nent *e;
    for (e = t[hashof(name)]; e; e = e->next)
        if (!strcmp(e->name, name)) return 1;
    return 0;
}

static void nadd(struct nent **t, const char *name)
{
    unsigned h = hashof(name);
    struct nent *e;
    if (nfind(t, name)) return;
    e = xmalloc(sizeof *e);
    e->name = name;
    e->next = t[h];
    t[h] = e;
}

static void note(struct obj *o)
{
    int k;
    for (k = 0; k < o->nsym; k++) if (o->syms[k].global) nadd(defh, o->syms[k].name);
    for (k = 0; k < o->nrel; k++) nadd(refh, o->rels[k].sym);
}

static int needed(const char *name) { return nfind(refh, name) && !nfind(defh, name); }

/* p32ld.py's gather: archives again and again, members in name order, each
   taken when it defines a symbol still needed, until a pass takes none */
struct archive { char *path; struct member *m; int n; char *taken; };

static void gather(struct archive *ar, int nar)
{
    int added, a, i, k;
    do {
        added = 0;
        for (a = 0; a < nar; a++)
            for (i = 0; i < ar[a].n; i++) {
                struct member *m = &ar[a].m[i];
                if (ar[a].taken[i]) continue;
                for (k = 0; k < m->o.nsym; k++)
                    if (m->o.syms[k].global && needed(m->o.syms[k].name)) break;
                if (k < m->o.nsym) {
                    char key[600];
                    if (strlen(ar[a].path) + strlen(m->name) + 3 > sizeof key) die("name too long: %s", m->name);
                    sprintf(key, "%s(%s)", ar[a].path, m->name);
                    add_input(key, &m->o);
                    ar[a].taken[i] = 1;
                    added = 1;
                }
            }
    } while (added);
}

/* ---------------------------------------------------------------- linking */
static unsigned align(unsigned x, unsigned n) { return (x + n - 1) / n * n; }

static unsigned place(int sect, unsigned start)
{
    unsigned a = start;
    int i;
    for (i = 0; i < nin; i++) {
        struct obj *o = &in[i].o;
        unsigned al = sect == S_TEXT ? o->talign : sect == S_DATA ? o->dalign : o->balign;
        a = align(a, al ? al : 4);
        in[i].base[sect] = a;
        a += sect == S_TEXT ? o->tsize : sect == S_DATA ? o->dsize : o->bsize;
    }
    return a;
}

struct mapent { const char *name; unsigned value; };

/* the a.out's symbol table (p32ld.py's comment gives the format) */
struct tabent { unsigned value; const char *name; int type; };
static int tabcmp(const void *a, const void *b)
{
    const struct tabent *x = a, *y = b;
    int c;
    if (x->value != y->value) return x->value < y->value ? -1 : 1;
    if ((c = bytecmp(x->name, y->name)) != 0) return c;
    return x->type - y->type;
}
static int mapcmp(const void *a, const void *b)
{
    const struct mapent *x = a, *y = b;
    if (x->value != y->value) return x->value < y->value ? -1 : 1;
    return bytecmp(x->name, y->name);
}

int main(int argc, char **argv)
{
    const char *out = "a.out", *entry = "_start", *mapname = 0;
    int bin = 0, have_text = 0, have_data = 0, strip = 0, i, k, nar = 0, missing = 0;
    unsigned text_at = 0, data_at = 0, t0, t_end, d0, d_end, b0, b_end, end, data_load;
    struct archive *ar = xmalloc(sizeof *ar * argc);
    unsigned char *text, *data;
    struct gsym *g;
    static const char *const lsyms[] = { "_text", "_etext", "_data", "_edata", "_data_load", "_bss", "_end" };
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "-e") && i + 1 < argc) entry = argv[++i];
        else if (!strcmp(a, "-M") && i + 1 < argc) mapname = argv[++i];
        else if (!strcmp(a, "-s")) strip = 1;
        else if (!strcmp(a, "--format") && i + 1 < argc) {
            i++;
            if (!strcmp(argv[i], "bin")) bin = 1;
            else if (strcmp(argv[i], "aout")) die("unknown format %s", argv[i]);
        } else if (!strcmp(a, "--text") && i + 1 < argc) { text_at = strtoul(argv[++i], 0, 0); have_text = 1; }
        else if (!strcmp(a, "--data") && i + 1 < argc) { data_at = strtoul(argv[++i], 0, 0); have_data = 1; }
        else if (a[0] == '-') die("unknown option %s", a);
        else {
            unsigned len;
            unsigned char *b = read_file(a, &len);
            if (!b) die("cannot read %s", a);
            if (len >= 4 && !memcmp(b, "P32A", 4)) {
                ar[nar].path = (char *)a;
                ar[nar].n = ar_decode(b, len, &ar[nar].m);
                if (ar[nar].n < 0) die("%s: a damaged archive", a);
                ar[nar].taken = xmalloc(ar[nar].n + 1);
                memset(ar[nar].taken, 0, ar[nar].n + 1);
                nar++;
            } else {
                struct obj o;
                if (obj_decode(b, len, &o) < 0) die("%s: not a P32W object or archive", a);
                keep_names(&o, b);
                free(b);
                add_input(a, &o);
                in[nin - 1].reread = 1;
            }
        }
    }
    gather(ar, nar);
    if (!nin) die("nothing to link%s", "");
    /* layout */
    t0 = bin || have_text ? text_at : 0x1000;
    t_end = place(S_TEXT, t0);
    if (!bin) d0 = have_data ? data_at : align(t_end, PAGE);
    else d0 = have_data ? data_at : align(t_end, 4);
    d_end = place(S_DATA, d0);
    b0 = align(d_end, 4);
    b_end = place(S_BSS, b0);
    end = align(b_end, 4);
    data_load = bin ? align(t_end, 4) : d0;
    /* symbols */
    {
        unsigned v[7];
        v[0] = t0; v[1] = t_end; v[2] = d0; v[3] = d_end; v[4] = data_load; v[5] = b0; v[6] = end;
        for (k = 0; k < 7; k++) gadd(lsyms[k], v[k], "the linker");
    }
    for (i = 0; i < nin; i++)
        for (k = 0; k < in[i].o.nsym; k++) {
            struct osym *s = &in[i].o.syms[k];
            unsigned v;
            if (!s->global) continue;
            v = s->sect == S_ABS ? s->value : in[i].base[s->sect] + s->value;
            if ((g = gfind(s->name)) != 0) {
                fprintf(stderr, "ld: %s defined twice (%s and %s)\n", s->name, g->where, in[i].name);
                return 1;
            }
            gadd(s->name, v, in[i].name);
        }
    /* contents and relocations */
    text = xmalloc(t_end - t0 + 1);
    data = xmalloc(d_end - d0 + 1);
    memset(text, 0, t_end - t0 + 1);
    memset(data, 0, d_end - d0 + 1);
    for (i = 0; i < nin; i++) {
        struct obj *o = &in[i].o;
        unsigned char *b = 0;
        if (in[i].reread) {             /* (an object file: its contents again) */
            unsigned len;
            if (!(b = read_file(in[i].name, &len)) || len < 48 + ((o->tsize + 3) & ~3u) + o->dsize ||
                rword(b + 8) != o->tsize || rword(b + 12) != o->dsize)
                die("%s changed while linking", in[i].name);
            o->text = b + 48;
            o->data = b + 48 + ((o->tsize + 3) & ~3u);
        }
        memcpy(text + in[i].base[S_TEXT] - t0, o->text, o->tsize);
        memcpy(data + in[i].base[S_DATA] - d0, o->data, o->dsize);
        if (b) { free(b); o->text = o->data = 0; }
    }
    for (i = 0; i < nin; i++) {
        struct obj *o = &in[i].o;
        for (k = 0; k < o->nrel; k++) {
            struct oreloc *r = &o->rels[k];
            unsigned char *buf = r->sect == S_TEXT ? text : data, *p;
            unsigned start = r->sect == S_TEXT ? t0 : d0;
            unsigned P = in[i].base[r->sect] + r->offset, S, val, w;
            if (is_section(r->sym))
                S = in[i].base[r->sym[1] == 't' ? S_TEXT : r->sym[1] == 'd' ? S_DATA : S_BSS];
            else if ((g = gfind(r->sym)) != 0)
                S = g->value;
            else {
                fprintf(stderr, "ld: undefined: %s (in %s)\n", r->sym, o->source);
                missing = 1;
                continue;
            }
            val = S + (unsigned)r->addend;
            p = buf + (P - start);
            w = rword(p);
            if (r->kind == R_ABS32) w = val;
            else if (r->kind == R_HI16) w = (w & 0xFFFF0000u) | (val >> 16 & 0xFFFF);
            else if (r->kind == R_LO16) w = (w & 0xFFFF0000u) | (val & 0xFFFF);
            else if (r->kind == R_J24) {
                int d = (int)(val - (P + 4));
                if ((d & 3) || d < -(1 << 25) || d >= (1 << 25)) die("a jump to %s is out of range or unaligned", r->sym);
                w = (w & 0xFF000000u) | ((unsigned)(d / 4) & 0xFFFFFF);
            } else die("unknown relocation kind in %s", in[i].name);
            p[0] = w; p[1] = w >> 8; p[2] = w >> 16; p[3] = w >> 24;
        }
    }
    if (missing) return 1;
    if (!bin && !gfind(entry)) die("entry symbol %s is not defined", entry);
    /* the output, straight to the file */
    {
        struct buf o;
        struct buf st;
        int fd;
        memset(&o, 0, sizeof o);
        memset(&st, 0, sizeof st);
        if (!bin && !strip) {           /* every symbol of every object, and the linker's */
            int n = 7, j = 0;
            struct tabent *tab;
            for (i = 0; i < nin; i++) n += in[i].o.nsym;
            tab = xmalloc(sizeof *tab * n);
            for (k = 0; k < 7; k++) { tab[j].value = gfind(lsyms[k])->value; tab[j].name = lsyms[k]; tab[j].type = 'A'; j++; }
            for (i = 0; i < nin; i++)
                for (k = 0; k < in[i].o.nsym; k++) {
                    struct osym *s = &in[i].o.syms[k];
                    tab[j].value = s->sect == S_ABS ? s->value : in[i].base[s->sect] + s->value;
                    tab[j].name = s->name;
                    tab[j].type = "ATDB"[s->sect] + (s->global ? 0 : 32);
                    j++;
                }
            qsort(tab, j, sizeof *tab, tabcmp);
            for (k = 0; k < j; k++) {
                unsigned char e[4 + 2 + 255 + 3];
                int len = strlen(tab[k].name), el;
                if (len > 255) len = 255;
                e[0] = tab[k].value; e[1] = tab[k].value >> 8; e[2] = tab[k].value >> 16; e[3] = tab[k].value >> 24;
                e[4] = tab[k].type;
                e[5] = len;
                memcpy(e + 6, tab[k].name, len);
                el = 6 + len;
                while (el & 3) e[el++] = 0;
                bput(&st, e, el);
            }
            free(tab);
        }
        if (!bin) {
            bword(&o, MAGIC); bword(&o, t_end - t0); bword(&o, d_end - d0); bword(&o, b_end - b0);
            bword(&o, gfind(entry)->value); bword(&o, 0); bword(&o, st.n); bword(&o, 0);
        }
        if ((fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0 ||
            write_all(fd, o.p, o.n) < 0 || write_all(fd, text, t_end - t0) < 0) { perror(out); return 1; }
        if (bin && data_load - t0 > t_end - t0) {       /* (a flat image: zeros up to the data) */
            struct buf z;
            memset(&z, 0, sizeof z);
            bput(&z, 0, data_load - t_end);
            if (write_all(fd, z.p, z.n) < 0) { perror(out); return 1; }
        }
        if (write_all(fd, data, d_end - d0) < 0 || write_all(fd, st.p, st.n) < 0 || close(fd) < 0) { perror(out); return 1; }
        if (!bin) {                     /* an a.out can be run: x as the umask allows */
            int mask = umask(0);
            umask(mask);
            chmod(out, 0777 & ~mask);
        }
    }
    if (mapname) {
        FILE *f = fopen(mapname, "w");
        struct mapent *me;
        int n = 0, j;
        if (!f) { perror(mapname); return 1; }
        fprintf(f, "%-5s %08X-%08X  %u bytes\n", "text", t0, t_end, t_end - t0);
        fprintf(f, "%-5s %08X-%08X  %u bytes\n", "data", d0, d_end, d_end - d0);
        fprintf(f, "%-5s %08X-%08X  %u bytes\n", "bss", b0, b_end, b_end - b0);
        fprintf(f, "data stored at %08X\n\n", data_load);
        for (i = 0; i < nin; i++)
            fprintf(f, "%-40s text %08X data %08X bss %08X\n", in[i].name, in[i].base[S_TEXT],
                    in[i].base[S_DATA], in[i].base[S_BSS]);
        fputc('\n', f);
        for (j = 0; j < NHASH; j++) for (g = gh[j]; g; g = g->next) n++;
        me = xmalloc(sizeof *me * (n + 1));
        n = 0;
        for (j = 0; j < NHASH; j++) for (g = gh[j]; g; g = g->next) { me[n].name = g->name; me[n].value = g->value; n++; }
        qsort(me, n, sizeof *me, mapcmp);
        for (j = 0; j < n; j++) fprintf(f, "%08X  %s\n", me[j].value, me[j].name);
        fclose(f);
    }
    return 0;
}
