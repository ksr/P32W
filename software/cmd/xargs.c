/* xargs -- build and run commands from the standard input:
 *   xargs [-0rtx] [-E eof] [-I repl | -L n | -n n] [-s size] [command [arg ...]]
 * The input's words (separated by blanks and newlines; '...', "..." and
 * \c quote) are added to command (echo by default) as arguments, as many
 * to each run as fit: P32W Unix gives a program at most 249 arguments and
 * 4096 bytes of argument and environment strings (each with its NUL).
 * Exit status: 0; 123 if a run exited 1-254; 124 if one exited 255;
 * 125 if one was killed by a signal; 126 if the command could not be run;
 * 127 if it was not found; 1 for xargs's own errors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define MAXARGS 249             /* argv entries a program may have (the kernel's NCARGS less 1) */
#define ARGBYTES 4096           /* bytes of argument and environment strings (MAXARG) */

static int nflag, lflag, tflag, zflag, rflag, xflag;
static char *repl, *eofstr;
static int size;                /* bytes of the command line (strings and NULs) allowed */
static char **base;             /* the command and its own arguments */
static int nbase, basesize;
static char **av;               /* the command line being built */
static int nav, avsize, lines, ran, status;
static char *tok;               /* the word being read */
static int tokn, tokcap;

static void nomem(void)
{
    fprintf(stderr, "xargs: out of memory\n");
    exit(1);
}

static void usage(void)
{
    fprintf(stderr, "usage: xargs [-0rtx] [-E eof] [-I repl | -L n | -n n] [-s size] [command [arg ...]]\n");
    exit(1);
}

static void addc(int c)
{
    if (tokn + 1 >= tokcap) {
        tokcap = tokcap ? 2 * tokcap : 256;
        if (!(tok = realloc(tok, tokcap))) nomem();
    }
    tok[tokn++] = c;
}

/* the next word into tok: 1, or 0 at the end of the input. *eol: the word
 * ends a line (for -L: a line whose last character is a blank goes on to
 * the next). With -I a whole line, less its leading blanks, is the word. */
static int getword(int *eol)
{
    int c, q = 0, have = 0;
    tokn = 0;
    *eol = 0;
    if (zflag) {                        /* -0: NUL-separated, nothing quoted */
        while ((c = getchar()) != EOF && c != 0) addc(c);
        if (c == EOF && tokn == 0) return 0;
        addc(0);
        *eol = 1;
        return 1;
    }
    for (;;) {                          /* blanks and empty lines before it */
        c = getchar();
        if (c != ' ' && c != '\t' && c != '\n') break;
    }
    for (;; c = getchar()) {
        if (c == EOF) {
            if (q) {
                fprintf(stderr, "xargs: unmatched %s quote\n", q == '"' ? "double" : "single");
                exit(1);
            }
            if (!have) return 0;
            *eol = 1;
            break;
        }
        if (q) {
            if (c == q) q = 0;
            else if (c == '\n') {
                fprintf(stderr, "xargs: unmatched %s quote\n", q == '"' ? "double" : "single");
                exit(1);
            } else addc(c);
            continue;
        }
        if (c == '\n') { *eol = 1; break; }
        if ((c == ' ' || c == '\t') && !repl) {
            while ((c = getchar()) == ' ' || c == '\t')
                ;
            if (c == '\n') {            /* a trailing blank: the line goes on */
                if (!lflag) *eol = 1;
            } else if (c != EOF) ungetc(c, stdin);
            else *eol = 1;
            break;
        }
        have = 1;
        if (c == '\'' || c == '"') q = c;
        else if (c == '\\') {
            if ((c = getchar()) == EOF) break;
            addc(c);
        } else addc(c);
    }
    addc(0);
    return 1;
}

/* a file that can be run: 1; else 0 and errno */
static int runnable(const char *path)
{
    struct stat st;
    if (access(path, X_OK) < 0 || stat(path, &st) < 0) return 0;
    if (S_ISREG(st.st_mode)) return 1;
    errno = EACCES;
    return 0;
}

/* where the command is: 0 and its path in path, or 126 / 127 */
static int findcmd(const char *name, char *path, int pathsize)
{
    const char *dirs, *p;
    int denied = 0, n;
    if (strchr(name, '/')) {
        strncpy(path, name, pathsize - 1);
        path[pathsize - 1] = 0;
        if (runnable(path)) return 0;
        return errno == ENOENT || errno == ENOTDIR ? 127 : 126;
    }
    if (!(dirs = getenv("PATH"))) dirs = "/bin:/usr/bin";
    for (;;) {
        p = strchr(dirs, ':');
        n = p ? p - dirs : (int)strlen(dirs);
        if (n + strlen(name) + 2 <= (size_t)pathsize) {
            memcpy(path, dirs, n);
            if (n) path[n++] = '/';
            strcpy(path + n, name);
            if (runnable(path)) return 0;
            if (errno != ENOENT && errno != ENOTDIR) denied = 1;
        }
        if (!p) break;
        dirs = p + 1;
    }
    errno = denied ? EACCES : ENOENT;
    return denied ? 126 : 127;
}

static void run(void)
{
    static char path[300];
    int pid, w, st, k, r;
    av[nav] = 0;
    ran = 1;
    if (tflag) {
        for (k = 0; k < nav; k++) fprintf(stderr, k ? " %s" : "%s", av[k]);
        fputc('\n', stderr);
    }
    if ((r = findcmd(av[0], path, sizeof path)) != 0) {
        fprintf(stderr, "xargs: %s: %s\n", av[0], strerror(errno));
        exit(r);
    }
    fflush(stdout);
    if ((pid = fork()) < 0) {
        fprintf(stderr, "xargs: cannot fork: %s\n", strerror(errno));
        exit(1);
    }
    if (pid == 0) {
        close(0);                       /* the command's input: /dev/null, not ours */
        open("/dev/null", O_RDONLY);
        execv(path, av);
        fprintf(stderr, "xargs: %s: %s\n", av[0], strerror(errno));
        _exit(126);
    }
    while ((w = wait(&st)) != pid)
        if (w < 0) { fprintf(stderr, "xargs: lost the command\n"); exit(1); }
    if (WIFSIGNALED(st)) {
        fprintf(stderr, "xargs: %s: terminated by signal %d\n", av[0], WTERMSIG(st));
        exit(125);
    }
    st = WEXITSTATUS(st);
    if (st == 255) {
        fprintf(stderr, "xargs: %s: exited with status 255; stopping\n", av[0]);
        exit(124);
    }
    if (st) status = 123;
}

/* start a new command line: the command and its own arguments */
static void reset(void)
{
    int k;
    for (k = nbase; k < nav; k++) free(av[k]);
    for (k = 0; k < nbase; k++) av[k] = base[k];
    nav = nbase;
    avsize = basesize;
    lines = 0;
}

static void toolong(void)
{
    fprintf(stderr, "xargs: argument list too long\n");
    exit(1);
}

/* -I: run the command with each occurrence of repl in its arguments
 * replaced by the word */
static void insert(const char *w)
{
    int k, rl = strlen(repl), wl = strlen(w), total = 0;
    for (k = 0; k < nbase; k++) {
        const char *s = base[k], *m;
        int len = 0, nrep = 0;
        char *d;
        for (m = s; (m = strstr(m, repl)) != 0; m += rl) nrep++;
        len = strlen(s) + nrep * (wl - rl);
        if (!(d = malloc(len + 1))) nomem();
        av[k] = d;
        while ((m = strstr(s, repl)) != 0) {
            memcpy(d, s, m - s);
            d += m - s;
            memcpy(d, w, wl);
            d += wl;
            s = m + rl;
        }
        strcpy(d, s);
        total += len + 1;
    }
    nav = nbase;
    if (total > size) toolong();
    run();
    for (k = 0; k < nbase; k++) free(av[k]);
}

static int number(const char *s, char opt)
{
    int n = 0;
    const char *p = s;
    while (*p >= '0' && *p <= '9' && n < 1000000) n = n * 10 + *p++ - '0';
    if (*p || p == s || n < 1) {
        fprintf(stderr, "xargs: -%c: bad number: %s\n", opt, s);
        exit(1);
    }
    return n;
}

int main(int argc, char **argv)
{
    static char *echo[] = { "echo", 0 };
    int i, eol, k, envsize = 0, len, maxargs;
    extern char **environ;
    char *sizearg = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p, *arg;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++) {
            if (strchr("0rtx", *p)) {
                if (*p == '0') zflag = 1;
                else if (*p == 'r') rflag = 1;
                else if (*p == 't') tflag = 1;
                else xflag = 1;
                continue;
            }
            if (!strchr("EILns", *p)) { fprintf(stderr, "xargs: unknown option -%c\n", *p); usage(); }
            if (p[1]) arg = p + 1;
            else if (i + 1 < argc) arg = argv[++i];
            else { fprintf(stderr, "xargs: -%c needs an argument\n", *p); usage(); }
            switch (*p) {
            case 'E': eofstr = arg; break;
            case 'I': repl = arg; lflag = nflag = 0; break;
            case 'L': lflag = number(arg, 'L'); repl = 0; nflag = 0; break;
            case 'n': nflag = number(arg, 'n'); repl = 0; lflag = 0; break;
            case 's': sizearg = arg; break;
            }
            break;
        }
    }
    if (repl && !*repl) { fprintf(stderr, "xargs: -I needs a non-empty string\n"); usage(); }
    if (i < argc) { base = argv + i; nbase = argc - i; }
    else { base = echo; nbase = 1; }
    for (k = 0; environ && environ[k]; k++) envsize += strlen(environ[k]) + 1;
    size = ARGBYTES - envsize;
    if (sizearg) {
        k = number(sizearg, 's');
        if (k < size) size = k;
    }
    for (k = 0; k < nbase; k++) basesize += strlen(base[k]) + 1;
    maxargs = MAXARGS;
    if (nbase > maxargs || basesize > size) toolong();
    if (!(av = malloc((MAXARGS + 1) * sizeof *av))) nomem();
    reset();
    while (getword(&eol)) {
        if (eofstr && !strcmp(tok, eofstr)) break;
        if (repl) { insert(tok); continue; }
        len = tokn;                     /* (with its NUL) */
        if (basesize + len > size) toolong();
        if (nav > nbase && (avsize + len > size || nav >= maxargs)) {
            if (xflag && (nflag || lflag)) toolong();
            run();
            reset();
        }
        if (!(av[nav] = malloc(len))) nomem();
        memcpy(av[nav++], tok, len);
        avsize += len;
        if (eol) lines++;
        if ((nflag && nav - nbase >= nflag) || (lflag && lines >= lflag)) {
            run();
            reset();
        }
    }
    if (nav > nbase || (!ran && !rflag && !repl)) run();
    return status;
}
