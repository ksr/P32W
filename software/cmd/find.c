/* find path ... [expression] -- walk directory trees, testing each path
   primaries: -name pat  -type c  -newer file  -print  -exec cmd ... {} ... ;
   operators: ! expr, expr [-a] expr, expr -o expr, ( expr ) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
#define NNODE 100
#define NARG 64

enum { AND, OR, NOT, NAME, TYPE, NEWER, PRINT, EXEC };
struct node {
    int op, l, r;                       /* operands: node numbers */
    char *s;                            /* -name's pattern, -type's letter */
    int t;                              /* -newer's time */
    char **av;                          /* -exec's words (up to the ;) */
    int ac;
};
static struct node nodes[NNODE];
static int nnode, status;
static char **args;
static int nargs, ap, action;

/* the names in a directory but . and .., read all at once and the directory
   closed, so a deep tree does not run out of descriptors; NULL on error */
static char **names(const char *path, int *np)
{
    DIR *d;
    struct dirent *e;
    char **v = malloc(16 * sizeof(char *)), **nv;
    int n = 0, max = 16;
    if (!v) { errno = ENOMEM; return NULL; }
    if (!(d = opendir(path))) { free(v); return NULL; }
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == max && (nv = realloc(v, 2 * max * sizeof(char *))) != NULL) { v = nv; max *= 2; }
        if (n == max || !(v[n] = strdup(e->d_name))) {
            while (n > 0) free(v[--n]);
            free(v);
            closedir(d);
            errno = ENOMEM;
            return NULL;
        }
        n++;
    }
    closedir(d);
    *np = n;
    return v;
}

static void freenames(char **v, int n)
{
    while (n > 0) free(v[--n]);
    free(v);
}

static void syntax(const char *msg, const char *arg)
{
    fprintf(stderr, "find: %s%s\n", msg, arg ? arg : "");
    exit(2);
}

static int mk(int op, int l, int r)
{
    if (nnode == NNODE) syntax("expression too long", NULL);
    nodes[nnode].op = op;
    nodes[nnode].l = l;
    nodes[nnode].r = r;
    return nnode++;
}

static int por(void);

static int prim(void)
{
    char *a;
    int n;
    struct stat st;
    if (ap >= nargs) syntax("expression expected", NULL);
    a = args[ap++];
    if (!strcmp(a, "(")) {
        n = por();
        if (ap >= nargs || strcmp(args[ap], ")")) syntax("missing )", NULL);
        ap++;
        return n;
    }
    if (!strcmp(a, "-print")) { action = 1; return mk(PRINT, 0, 0); }
    if (!strcmp(a, "-exec")) {
        int first = ap;
        while (ap < nargs && strcmp(args[ap], ";")) ap++;
        if (ap >= nargs) syntax("-exec needs a ; at the end (\\; to the shell)", NULL);
        if (ap == first) syntax("-exec needs a command", NULL);
        if (ap - first >= NARG) syntax("-exec: too many words", NULL);
        n = mk(EXEC, 0, 0);
        nodes[n].av = args + first;
        nodes[n].ac = ap - first;
        ap++;
        action = 1;
        return n;
    }
    if (!strcmp(a, "-name") || !strcmp(a, "-type") || !strcmp(a, "-newer")) {
        if (ap >= nargs) syntax(a, " needs an argument");
        if (!strcmp(a, "-name")) { n = mk(NAME, 0, 0); nodes[n].s = args[ap++]; return n; }
        if (!strcmp(a, "-type")) {
            if (!strchr("fdcbp", args[ap][0]) || !args[ap][0] || args[ap][1]) syntax("-type takes f, d, c, b or p, not ", args[ap]);
            n = mk(TYPE, 0, 0);
            nodes[n].s = args[ap++];
            return n;
        }
        if (stat(args[ap], &st) < 0) { fprintf(stderr, "find: %s: %s\n", args[ap], strerror(errno)); exit(2); }
        n = mk(NEWER, 0, 0);
        nodes[n].t = st.st_mtime;
        ap++;
        return n;
    }
    if (!strcmp(a, ")") || !strcmp(a, "-o") || !strcmp(a, "-a")) syntax("misplaced ", a);
    syntax("unknown predicate ", a);
    return 0;
}

static int pnot(void)
{
    if (ap < nargs && !strcmp(args[ap], "!")) { ap++; return mk(NOT, pnot(), 0); }
    return prim();
}

static int pand(void)
{
    int l = pnot();
    while (ap < nargs && strcmp(args[ap], "-o") && strcmp(args[ap], ")")) {
        if (!strcmp(args[ap], "-a")) ap++;
        l = mk(AND, l, pnot());
    }
    return l;
}

static int por(void)
{
    int l = pand();
    while (ap < nargs && !strcmp(args[ap], "-o")) {
        ap++;
        l = mk(OR, l, pand());
    }
    return l;
}

/* glob match: * any string, ? any character, [...] a class (a-z ranges, ! or ^ to negate) */
static int gmatch(const char *p, const char *s)
{
    for (;; p++, s++) {
        if (*p == '*') {
            p++;
            for (;;) {
                if (gmatch(p, s)) return 1;
                if (!*s++) return 0;
            }
        }
        if (!*p) return !*s;
        if (!*s) return 0;
        if (*p == '[') {
            const char *q = p + 1;
            int neg = 0, ok = 0;
            if (*q == '!' || *q == '^') { neg = 1; q++; }
            do {
                if (q[1] == '-' && q[2] && q[2] != ']') { if (*s >= *q && *s <= q[2]) ok = 1; q += 3; }
                else { if (*s == *q) ok = 1; q++; }
            } while (*q && *q != ']');
            if (*q != ']') { if (*p != *s) return 0; continue; }     /* no ]: a plain [ */
            if (ok == neg) return 0;
            p = q;
            continue;
        }
        if (*p != '?' && *p != *s) return 0;
    }
}

static int run(struct node *n, const char *path)
{
    char *av[NARG + 1];
    int i, pid, w, st;
    for (i = 0; i < n->ac; i++) av[i] = strcmp(n->av[i], "{}") ? n->av[i] : (char *)path;
    av[i] = NULL;
    fflush(stdout);
    if ((pid = fork()) < 0) { fprintf(stderr, "find: cannot fork\n"); status = 1; return 0; }
    if (pid == 0) {
        execvp(av[0], av);
        fprintf(stderr, "find: %s: %s\n", av[0], strerror(errno));
        _exit(127);
    }
    while ((w = wait(&st)) != pid && w != -1)
        ;
    return w == pid && st == 0;
}

static int eval(int k, const char *path, const char *base, struct stat *sp)
{
    struct node *n = &nodes[k];
    int m = sp->st_mode & S_IFMT;
    switch (n->op) {
    case AND: return eval(n->l, path, base, sp) && eval(n->r, path, base, sp);
    case OR: return eval(n->l, path, base, sp) || eval(n->r, path, base, sp);
    case NOT: return !eval(n->l, path, base, sp);
    case NAME: return gmatch(n->s, base);
    case TYPE:
        switch (n->s[0]) {
        case 'f': return m == S_IFREG;
        case 'd': return m == S_IFDIR;
        case 'c': return m == S_IFCHR;
        case 'b': return m == S_IFBLK;
        default: return m == S_IFIFO;
        }
    case NEWER: return sp->st_mtime > n->t;
    case PRINT: puts(path); return 1;
    case EXEC: return run(n, path);
    }
    return 0;
}

static int root = -1;

static void walk(const char *path, const char *base)
{
    struct stat st;
    char sub[PATHMAX], **v;
    int len, slash, i, nv;
    if (stat(path, &st) < 0) { fprintf(stderr, "find: %s: %s\n", path, strerror(errno)); status = 1; return; }
    if (root < 0) puts(path);
    else eval(root, path, base, &st);
    if (!S_ISDIR(st.st_mode)) return;
    if (!(v = names(path, &nv))) { fprintf(stderr, "find: %s: %s\n", path, strerror(errno)); status = 1; return; }
    len = strlen(path);
    slash = len > 0 && path[len - 1] != '/';
    for (i = 0; i < nv; i++) {
        if (len + slash + strlen(v[i]) >= PATHMAX) {
            fprintf(stderr, "find: %s/%s: name too long\n", path, v[i]);
            status = 1;
            continue;
        }
        strcpy(sub, path);
        if (slash) strcat(sub, "/");
        strcat(sub, v[i]);
        walk(sub, v[i]);
    }
    freenames(v, nv);
}

int main(int argc, char **argv)
{
    int i, npath;
    for (npath = 1; npath < argc; npath++)
        if (argv[npath][0] == '-' || !strcmp(argv[npath], "!") || !strcmp(argv[npath], "(")) break;
    args = argv + npath;
    nargs = argc - npath;
    if (nargs > 0) {
        root = por();
        if (ap < nargs) syntax("misplaced ", args[ap]);
        if (!action) root = mk(AND, root, mk(PRINT, 0, 0));
    }
    if (npath == 1) walk(".", ".");
    for (i = 1; i < npath; i++) {
        char base[PATHMAX];
        int e = strlen(argv[i]), s;
        while (e > 1 && argv[i][e - 1] == '/') e--;      /* the last component: -name tests it too */
        s = e;
        while (s > 0 && argv[i][s - 1] != '/') s--;
        if (e - s >= PATHMAX) e = s + PATHMAX - 1;
        memcpy(base, argv[i] + s, e - s);
        base[e - s] = 0;
        walk(argv[i], base);
    }
    return status;
}
