/* ar -- P32W object archives on P32W Unix (M7, D24): assembler/p32ar.py in C,
 * writing byte for byte what it writes, with V7-style keys besides.
 *
 *   ar LIB.a file.o ...     make LIB.a of exactly these objects (a later one
 *                           of the same name wins) -- as p32ar.py does
 *   ar [-]key[v] LIB.a [file ...]   key is one of
 *       r  replace members (or add them); LIB.a is made if it does not exist
 *       q  quick append: the same as r here (members are unique, in name order)
 *       t  list the members and the symbols each defines (all, or those named)
 *       x  extract members into files of their names (all, or those named)
 *       d  delete the members named
 *     v (verbose) says what is done to each member; c and u are accepted.
 */
#include "objfmt.h"

static int verbose;

static const char *basename_(const char *p)
{
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

/* is s a key (V7 style: one of r q t x d, modifiers v c u, an optional -)?
 * its operation letter, or 0 */
static int key(const char *s, int dash_needed)
{
    int op = 0;
    if (*s == '-') s++;
    else if (dash_needed) return 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (strchr("rqtxd", *s)) { if (op) return 0; op = *s; }
        else if (*s == 'v') verbose = 1;
        else if (*s != 'c' && *s != 'u') return 0;
    }
    return op;
}

static int named(const char *name, int nf, char **files, int *used)
{
    int i;
    for (i = 0; i < nf; i++)
        if (!strcmp(basename_(files[i]), name)) { if (used) used[i] = 1; return 1; }
    return 0;
}

/* an object file as a member (its name without the directory); 0 if bad */
static int load_member(const char *path, struct member *x)
{
    unsigned len;
    unsigned char *b = read_file(path, &len);
    if (!b) { fprintf(stderr, "ar: %s: cannot open\n", path); return 0; }
    memset(x, 0, sizeof *x);
    if (obj_decode(b, len, &x->o) < 0) { fprintf(stderr, "ar: %s is not an object\n", path); return 0; }
    x->name = (char *)basename_(path);
    x->raw = b;
    x->rawlen = len;
    return 1;
}

static int write_archive(const char *path, struct member *m, int n)
{
    struct buf out;
    memset(&out, 0, sizeof out);
    ar_encode(m, n, &out);
    if (write_file(path, out.p, out.n) < 0) { perror(path); return 1; }
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: ar LIB.a file.o ...  |  ar r|q|t|x|d[v] LIB.a [file ...]\n");
    return 2;
}

int main(int argc, char **argv)
{
    struct member *m;
    int n = 0, i, k, op, st = 0, nf;
    char *lib, **files;
    int *used;
    unsigned len;
    unsigned char *b;

    if (argc < 2) return usage();
    op = key(argv[1], 0);
    if (!op && argv[1][0] == '-') return usage();
    if (!op) {                          /* ar LIB.a file.o ...: a new archive of these */
        if (argc < 3) return usage();
        m = xmalloc(sizeof *m * argc);
        for (i = 2; i < argc; i++) {
            struct member x;
            if (!load_member(argv[i], &x)) return 1;
            for (k = 0; k < n; k++)
                if (!strcmp(m[k].name, x.name)) break;
            m[k] = x;                   /* (replaces one of the same name) */
            if (k == n) n++;
        }
        return write_archive(argv[1], m, n);
    }
    if (argc < 3) return usage();
    lib = argv[2];
    files = argv + 3;
    nf = argc - 3;
    used = xmalloc(sizeof *used * (nf + 1));
    memset(used, 0, sizeof *used * (nf + 1));

    /* the archive as it is (none yet is all right for r and q) */
    b = read_file(lib, &len);
    if (!b) {
        if (op != 'r' && op != 'q') { fprintf(stderr, "ar: %s: cannot open\n", lib); return 1; }
        n = 0;
        m = 0;
    } else if ((n = ar_decode(b, len, &m)) < 0) {
        fprintf(stderr, "ar: %s: not an archive\n", lib);
        return 1;
    }

    switch (op) {
    case 't':
        for (i = 0; i < n; i++) {
            if (nf && !named(m[i].name, nf, files, used)) continue;
            /* (the symbols in name order: they are stored sorted) */
            if (verbose) printf("%6u ", m[i].rawlen);
            printf("%s:", m[i].name);
            for (k = 0; k < m[i].o.nsym; k++)
                if (m[i].o.syms[k].global) printf(" %s", m[i].o.syms[k].name);
            putchar('\n');
        }
        break;
    case 'x':
        for (i = 0; i < n; i++) {
            if (nf && !named(m[i].name, nf, files, used)) continue;
            if (verbose) printf("x - %s\n", m[i].name);
            if (write_file(m[i].name, m[i].raw, m[i].rawlen) < 0) { perror(m[i].name); st = 1; }
        }
        break;
    case 'd':
        for (i = k = 0; i < n; i++) {
            if (named(m[i].name, nf, files, used)) {
                if (verbose) printf("d - %s\n", m[i].name);
                continue;
            }
            m[k] = m[i];
            k++;
        }
        n = k;
        break;
    case 'r': case 'q': {
        struct member *all = xmalloc(sizeof *all * (n + nf + 1));
        for (i = 0; i < n; i++) all[i] = m[i];
        for (i = 0; i < nf; i++) {
            struct member x;
            used[i] = 1;
            if (!load_member(files[i], &x)) return 1;   /* (the archive is left as it was) */
            for (k = 0; k < n; k++)
                if (!strcmp(all[k].name, x.name)) break;
            if (verbose) printf("%c - %s\n", k < n ? 'r' : 'a', x.name);
            all[k] = x;
            if (k == n) n++;
        }
        m = all;
        break;
    }
    }
    for (i = 0; i < nf; i++)
        if (!used[i]) { fprintf(stderr, "ar: %s: not in archive\n", files[i]); st = 1; }
    if (op == 'd' || op == 'r' || op == 'q')
        if (write_archive(lib, m, n)) return 1;
    return st;
}
