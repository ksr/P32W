/* main.c -- cc, the driver (cc/p32cc.py): preprocess, parse, generate (a function at a time); then
 * as and ld for objects and programs.
 *
 *   cc [-c | -S | -E] [-o OUT] [-I DIR] [-D NAME[=VAL]] [-lNAME] [-s] [-v] [-w] files...
 *
 * .c files are compiled, .s files assembled, .o / .a files passed to the
 * linker; the default links a P32W Unix program (a.out) with
 * /usr/lib/crt0.o and /usr/lib/libc.a. Headers: -I dirs, then /usr/include.
 * -lNAME links /usr/lib/libNAME.a, -s leaves out the symbol table, -v shows
 * the commands run; -S -o - (or -E) writes to the screen; -h lists all this. */
#include <unistd.h>
#include <sys/wait.h>
#include "cc.h"

#ifndef INCLUDE_DIR
#define INCLUDE_DIR "/usr/include"
#endif
#ifndef LIB_DIR
#define LIB_DIR "/usr/lib"
#endif
#ifndef BIN_DIR
#define BIN_DIR "/bin"
#endif

static void *heap0;
static int annotate_s;                  /* -S: the generator writes the source lines too */
static int verbose;

static void usage(FILE *f)
{
    fprintf(f, "usage: cc [options] file...      (.c compiled, .s assembled, .o .a linked)\n"
               "  -o OUT      the output's name (default a.out; - with -S or -E: the screen)\n"
               "  -c          compile (and assemble) to .o files only, no linking\n"
               "  -S          compile to assembly (.s files) only, each statement's line above its code\n"
               "  -E          preprocess only, to the screen\n"
               "  -I DIR      look for #include files in DIR too (before /usr/include)\n"
               "  -D NAME[=V] define a macro, as #define NAME V\n"
               "  -lNAME      link the library /usr/lib/libNAME.a\n"
               "  -s          leave the symbol table out of the program (smaller; db sees no names)\n"
               "  -v          show each step's command\n"
               "  -w          no warnings\n"
               "  -h          this list\n");
}                     /* (CCMEM: the heap's start, to report its growth) */

static int run(char **argv)
{
    int pid, st;
    if (verbose) {                      /* (before the fork, or both would print it) */
        char **a;
        for (a = argv; *a; a++) fprintf(stderr, "%s%s", a == argv ? "" : " ", *a);
        fputc('\n', stderr);
    }
    pid = fork();
    if (pid < 0) { perror("cc: fork"); return -1; }
    if (pid == 0) {
        execv(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    while (wait(&st) != pid) ;
    return st == 0 ? 0 : -1;
}

static char *base_of(const char *path, const char *ext)  /* dir/x.c -> x.EXT */
{
    const char *b = strrchr(path, '/');
    char *s = xstrdup(b ? b + 1 : path), *dot = strrchr(s, '.');
    if (dot) *dot = 0;
    return fmt("%s%s", s, ext);
}

static int ends(const char *s, const char *e)
{
    int n = strlen(s), k = strlen(e);
    return n >= k && !strcmp(s + n - k, e);
}

static int write_text(const char *path, const char *text)
{
    FILE *f;
    if (path && !strcmp(path, "-")) path = 0;     /* (-o -: the screen) */
    f = path ? fopen(path, "w") : stdout;
    if (!f) { perror(path); return -1; }
    fputs(text, f);
    if (path && fclose(f)) { perror(path); return -1; }
    return 0;
}

/* one .c file to assembly, in a process of its own (as cc1: each file
   starts with all of memory, and nothing is left over for the next) */
static int compile(const char *src, const char *spath, char **dirs, int ndirs, char **defs, int ndefs)
{
    int pid, st;
    fflush(stdout);
    fflush(stderr);
    if ((pid = fork()) < 0) { perror("cc: fork"); return -1; }
    if (pid == 0) {
        int n, r;
        struct tok **t;
        FILE *f;
        cpp_init(dirs, ndirs, defs, ndefs);
        annotate = annotate_s;
        t = preprocess(src, &n);
        if (!strcmp(spath, "-")) f = stdout;
        else if (!(f = fopen(spath, "w"))) { perror(spath); _exit(1); }
        gen_begin(f);
        function_done = gen_function;
        r = gen_end(parse(t, n));
        if ((f == stdout ? fflush(f) : fclose(f)) || r < 0) { perror(spath); _exit(1); }
        if (getenv("CCMEM")) fprintf(stderr, "cc: %s: %u bytes of heap\n", src, (unsigned)((char *)sbrk(0) - (char *)heap0));
        _exit(0);
    }
    while (wait(&st) != pid) ;
    return st == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    char *out = 0, *dirs[64], *defs[64], *files[256], *link[300], *tmp[256];
    int ndirs = 0, ndefs = 0, nfiles = 0, nlink = 0, ntmp = 0, mode = 0, i, st = 0, strip = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "-c") || !strcmp(a, "-S") || !strcmp(a, "-E")) { mode = a[1]; annotate_s = mode == 'S'; }
        else if (!strcmp(a, "-w")) warnings = 0;
        else if (!strcmp(a, "-s")) strip = 1;
        else if (!strcmp(a, "-v")) verbose = 1;
        else if (!strcmp(a, "-h") || !strcmp(a, "-?") || !strcmp(a, "--help")) { usage(stdout); return 0; }
        else if (!strncmp(a, "-l", 2) && a[2] && nfiles < 256) files[nfiles++] = fmt("%s/lib%s.a", LIB_DIR, a + 2);
        else if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else if (!strncmp(a, "-I", 2) && ndirs < 63) dirs[ndirs++] = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : "");
        else if (!strncmp(a, "-D", 2) && ndefs < 63) defs[ndefs++] = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : "");
        else if (a[0] == '-') { fprintf(stderr, "cc: unknown option %s\n", a); usage(stderr); return 2; }
        else if (nfiles < 256) files[nfiles++] = a;
    }
    if (!nfiles) { usage(stderr); return 2; }
    dirs[ndirs++] = INCLUDE_DIR;
    heap0 = sbrk(0);
    types_init();
    if (mode == 'E') {
        int n;
        struct tok **t;
        cpp_init(dirs, ndirs, defs, ndefs);
        t = preprocess(files[0], &n);
        return write_text(out, detok(t, n)) < 0;
    }
    link[nlink++] = BIN_DIR "/ld";
    if (strip) link[nlink++] = "-s";
    link[nlink++] = "-o";
    link[nlink++] = out ? out : "a.out";
    link[nlink++] = LIB_DIR "/crt0.o";
    for (i = 0; i < nfiles; i++) {
        char *f = files[i];
        if (ends(f, ".c")) {
            char *spath, *opath;
            spath = mode == 'S' ? (out && nfiles == 1 ? out : base_of(f, ".s")) : fmt("/tmp/cc%d_%d.s", (int)getpid(), i);
            if (compile(f, spath, dirs, ndirs, defs, ndefs) < 0) { st = 1; break; }
            if (mode == 'S') continue;
            opath = mode == 'c' ? (out && nfiles == 1 ? out : base_of(f, ".o")) : fmt("/tmp/cc%d_%d.o", (int)getpid(), i);
            {
                char *av[6];
                av[0] = BIN_DIR "/as"; av[1] = "-o"; av[2] = opath; av[3] = spath; av[4] = 0;
                if (run(av) < 0) { unlink(spath); st = 1; break; }
            }
            unlink(spath);
            if (mode != 'c') { link[nlink++] = opath; tmp[ntmp++] = opath; }
        } else if (ends(f, ".s")) {
            char *opath = mode == 'c' ? (out && nfiles == 1 ? out : base_of(f, ".o")) : fmt("/tmp/cc%d_%d.o", (int)getpid(), i);
            char *av[6];
            if (mode == 'S') continue;
            av[0] = BIN_DIR "/as"; av[1] = "-o"; av[2] = opath; av[3] = f; av[4] = 0;
            if (run(av) < 0) { st = 1; break; }
            if (mode != 'c') { link[nlink++] = opath; tmp[ntmp++] = opath; }
        } else
            link[nlink++] = f;                      /* .o / .a */
    }
    if (st || mode == 'c' || mode == 'S') {        /* (a failure: the temporary objects go too) */
        for (i = 0; i < ntmp; i++) unlink(tmp[i]);
        return st;
    }
    link[nlink++] = LIB_DIR "/libc.a";
    link[nlink] = 0;
    if (run(link) < 0) st = 1;
    for (i = 0; i < ntmp; i++) unlink(tmp[i]);
    return st;
}
