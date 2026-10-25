/* awk -- pattern scanning and processing language:
 *     awk [-F fs] [-v var=value] ... 'program' [var=value | file] ...
 *     awk [-F fs] [-v var=value] ... -f progfile ... [var=value | file] ...
 * POSIX awk, the language of "The AWK Programming Language" (1988): BEGIN
 * and END, patterns (expressions, /regex/, ranges), fields, associative
 * arrays (several subscripts, delete, in), user functions (arrays by
 * reference, locals as extra parameters, recursion), print and printf with
 * > >> | redirections, every form of getline, close, system, fflush, and
 * the built-ins length substr index split sub gsub match sprintf tolower
 * toupper int sqrt exp log sin cos atan2 rand srand. Also nextfile, delete
 * of a whole array, ** and **=, length of an array, ENVIRON, and RS as a
 * regular expression when it is longer than one character.
 *
 * Numbers are doubles as awk requires, but P32W has no floating point: whole
 * numbers that fit are 32-bit ints (fast), the rest softfp doubles. The
 * program is lexed (awklex.c) and parsed (awkparse.c) into a tree that
 * awkrun.c walks; see awk.h for the files. Status: 0, exit's value, 2 for
 * an error (syntax or at run time: the message says where). */
#include "awk.h"
#include <stdarg.h>

char *progtext;
int curline;
Rule *begins, *ends, *rules;
Func **funcs;
int nfuncs;
char **gname;
Cell *gvar;
int ngvar;
static int gcap;
int exitstatus;
char *stacklimit;

static void usage(void)
{
    fprintf(stderr, "usage: awk [-F fs] [-v var=value] ['prog' | -f progfile ...] [file ...]\n");
    exit(2);
}

/* ---------------------------------------------------------------- errors */
void fatal(const char *fmt, ...)
{
    va_list ap;
    char *fn;
    fflush(stdout);
    fprintf(stderr, "awk: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    if (nrec > 0 && gvar) {
        fn = gvar[V_FILENAME].t == T_STR || gvar[V_FILENAME].t == T_MAYBE ? gvar[V_FILENAME].s : "";
        fprintf(stderr, " input record number %d, file %s\n", nrec, fn);
    }
    if (curline > 0) fprintf(stderr, " source line number %d\n", curline);
    exit(2);
}

void *emalloc(int n)
{
    void *p = malloc(n ? n : 1);
    if (!p) fatal("out of memory");
    return p;
}

void *erealloc(void *p, int n)
{
    p = realloc(p, n ? n : 1);
    if (!p) fatal("out of memory");
    return p;
}

/* ---------------------------------------------------------------- strings */
/* Strings come and go all the time, so the short ones are carved out of
   4 KB slabs, by size class (16 to 256 bytes, the class in the word before
   the string), and kept on free lists by class instead of going back to
   malloc (whose free walks its list, and whose header would cost another
   8 bytes each). Array elements come from the slabs too (anew). */
#define NCLASS 9
static char *freel[NCLASS + 1];
static const short csize[NCLASS] = { 16, 24, 32, 48, 64, 96, 128, 192, 256 };
static char *slabp;
static int slableft;

static char *slab(int size)
{
    char *p;
    if (slableft < size) {
        slabp = emalloc(4096);
        slableft = 4096;
    }
    p = slabp;
    slabp += size;
    slableft -= size;
    return p;
}

char *snew(int n)
{
    int k;
    char *p;
    n += 5;                             /* (the NUL and the class) */
    if (n <= 16) k = 0;
    else if (n > 256) k = NCLASS;
    else for (k = 1; csize[k] < n; k++) ;
    if (k < NCLASS) {
        if ((p = freel[k]) != 0) {
            freel[k] = *(char **)p;
            return p;
        }
        p = slab(csize[k]);
    } else
        p = emalloc(n);
    *(int *)p = k;
    return p + 4;
}

void sfree(char *s)
{
    int k;
    if (!s) return;
    s -= 4;
    k = *(int *)s;
    if (k < NCLASS) {
        *(char **)(s + 4) = freel[k];
        freel[k] = s + 4;
    } else
        free(s);
}

/* an array element (from the slabs; they are 40 bytes) */
void *anew(void)
{
    char *p = freel[NCLASS];
    if (p) {
        freel[NCLASS] = *(char **)p;
        return p;
    }
    return slab((sizeof(Elem) + 7) & ~7);
}

void afree(void *e)
{
    *(char **)e = freel[NCLASS];
    freel[NCLASS] = e;
}

char *sdupn(const char *s, int n)
{
    char *p = snew(n);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *sdup(const char *s)
{
    return sdupn(s, strlen(s));
}

void buf_init(Buf *b)
{
    b->s = 0;
    b->len = b->cap = 0;
}

void buf_add(Buf *b, const char *s, int n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2 + 32;
        b->s = erealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

void buf_addc(Buf *b, int c)
{
    char ch = c;
    if (b->len + 2 <= b->cap) {
        b->s[b->len++] = ch;
        b->s[b->len] = 0;
    } else
        buf_add(b, &ch, 1);
}

void buf_adds(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

char *buf_take(Buf *b)
{
    char *s = sdupn(b->s ? b->s : "", b->len);
    free(b->s);
    buf_init(b);
    return s;
}

/* ---------------------------------------------------------------- globals */
static const char *specials[] = { "NF", "NR", "FNR", "FS", "OFS", "ORS", "RS", "FILENAME", "SUBSEP",
                                  "RSTART", "RLENGTH", "CONVFMT", "OFMT", "ENVIRON", "ARGC", "ARGV", 0 };

int findglobal(const char *name)
{
    int i;
    for (i = 0; i < ngvar; i++)
        if (!strcmp(gname[i], name)) return i;
    return -1;
}

int globalvar(const char *name)
{
    int i = findglobal(name);
    if (i >= 0) return i;
    if (gvar) fatal("internal: a new variable %s while running", name);
    if (ngvar >= gcap) {
        gcap = gcap * 2 + 32;
        gname = erealloc(gname, gcap * sizeof(char *));
    }
    gname[ngvar] = sdup(name);
    return ngvar++;
}

/* \n \t \\ \" \/ \a \b \f \r \v \ooo in s, into d (no longer than s) */
void unescape(char *d, const char *s)
{
    int c, n;
    while (*s) {
        if (*s != '\\' || !s[1]) { *d++ = *s++; continue; }
        s++;
        switch (c = *s++) {
        case 'n': *d++ = '\n'; break;
        case 't': *d++ = '\t'; break;
        case 'r': *d++ = '\r'; break;
        case 'f': *d++ = '\f'; break;
        case 'b': *d++ = '\b'; break;
        case 'v': *d++ = '\v'; break;
        case 'a': *d++ = '\007'; break;
        case '\\': *d++ = '\\'; break;
        case '"': *d++ = '"'; break;
        case '/': *d++ = '/'; break;
        default:
            if (c >= '0' && c <= '7') {
                n = c - '0';
                if (*s >= '0' && *s <= '7') { n = n * 8 + *s++ - '0'; if (*s >= '0' && *s <= '7') n = n * 8 + *s++ - '0'; }
                *d++ = n;
            } else {
                *d++ = '\\';
                *d++ = c;
            }
        }
    }
    *d = 0;
}

/* ---------------------------------------------------------------- main */
static char *readfile(const char *name)
{
    Buf b;
    char tmp[512];
    int fd, n;
    fd = strcmp(name, "-") ? open(name, O_RDONLY) : 0;
    if (fd < 0) {
        fprintf(stderr, "awk: can't open file %s\n", name);
        exit(2);
    }
    buf_init(&b);
    while ((n = read(fd, tmp, sizeof tmp)) > 0) buf_add(&b, tmp, n);
    if (fd) close(fd);
    buf_addc(&b, '\n');
    return b.s;
}

int main(int argc, char **argv)
{
    Buf prog;
    char **vassign, *fs = 0, *a;
    int nv = 0, i, nf = 0, first;

#ifdef __P32__
    stacklimit = (char *)&prog - 1008 * 1024;  /* (P32W Unix gives a process 1 MiB of stack, D41) */
#else
    stacklimit = (char *)&prog - 6 * 1024 * 1024;
#endif
    vassign = emalloc(argc * sizeof(char *));
    buf_init(&prog);
    for (i = 0; specials[i]; i++) globalvar(specials[i]);
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        a = argv[i];
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-version") || !strcmp(a, "--version")) {
            printf("awk version 20261014 (P32W)\n");
            return 0;
        }
        if (a[1] == 'f' || a[1] == 'v' || a[1] == 'F') {
            char *val = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : 0);
            if (!val) usage();
            if (a[1] == 'f') {
                char *t = readfile(val);
                buf_adds(&prog, t);
                free(t);
                nf++;
            } else if (a[1] == 'v') {
                if (!isassign(val)) {
                    fprintf(stderr, "awk: invalid -v argument: %s\n", val);
                    exit(2);
                }
                vassign[nv++] = val;
            } else
                fs = val;
            continue;
        }
        if (!strcmp(a, "-d") || !strcmp(a, "-safe")) continue;
        fprintf(stderr, "awk: unknown option %s ignored\n", a);
    }
    if (!nf) {
        if (i >= argc) usage();
        buf_adds(&prog, argv[i++]);
        buf_addc(&prog, '\n');
    }
    first = i;
    progtext = prog.s ? prog.s : "";
    lex();
    parse();
    gvar = emalloc(ngvar * sizeof(Cell));
    for (i = 0; i < ngvar; i++) cinit(&gvar[i]);
    initvars(argc, argv, first);
    if (fs) {
        char *t = snew(strlen(fs));
        if (!strcmp(fs, "t")) strcpy(t, "\t");
        else unescape(t, fs);
        cset_str(&gvar[V_FS], t);
    }
    for (i = 0; i < nv; i++) cmdassign(vassign[i]);
    free(vassign);
    curline = 1;
    run();
    closeall();
    return exitstatus;
}
