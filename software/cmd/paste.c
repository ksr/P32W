/* paste -- merge lines of files:  paste [-s] [-d list] [file ...]
 * Without -s the files' first lines are joined into one line, then their
 * second lines, and so on; with -s each file's lines are joined into one
 * line. The joins are by the characters of list in turn (a tab by
 * default; \n \t \\ and \0, which is no character, may be used). A file
 * - is the standard input (each - takes the next line of it); no file
 * means -. Lines may be of any length. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct buf { char *s; int n, cap; };
static int *dl;                 /* the delimiters; -1 for \0, none */
static int ndl;

static void nomem(void)
{
    fprintf(stderr, "paste: out of memory\n");
    exit(1);
}

static void usage(void)
{
    fprintf(stderr, "usage: paste [-s] [-d list] [file ...]\n");
    exit(1);
}

/* the next line (without its newline) into b: 1, or 0 at the end */
static int getln(FILE *f, struct buf *b)
{
    int c;
    b->n = 0;
    while ((c = getc(f)) != EOF && c != '\n') {
        if (b->n >= b->cap) {
            b->cap = b->cap ? 2 * b->cap : 256;
            if (!(b->s = realloc(b->s, b->cap))) nomem();
        }
        b->s[b->n++] = c;
    }
    return !(c == EOF && b->n == 0);
}

static void delims(const char *p)
{
    if (!(dl = malloc((strlen(p) + 1) * sizeof *dl))) nomem();
    if (!*p) { dl[ndl++] = -1; return; }       /* -d '': nothing between */
    while (*p) {
        int c = (unsigned char)*p++;
        if (c == '\\' && *p) {
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == '0') c = -1;
        }
        dl[ndl++] = c;
    }
}

static void putdelim(int k)
{
    if (dl[k % ndl] >= 0) putchar(dl[k % ndl]);
}

static FILE *open1(const char *name)
{
    FILE *f;
    if (!strcmp(name, "-")) return stdin;
    if (!(f = fopen(name, "r"))) {
        fflush(stdout);
        fprintf(stderr, "paste: %s: %s\n", name, strerror(errno));
    }
    return f;
}

int main(int argc, char **argv)
{
    int i, k, sflag = 0, status = 0, nf;
    char *d = "\\t", *dash = "-";
    char **names;
    FILE **fp;
    struct buf *b;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *p;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++) {
            if (*p == 's') sflag = 1;
            else if (*p == 'd') {
                if (p[1]) d = p + 1;
                else if (i + 1 < argc) d = argv[++i];
                else { fprintf(stderr, "paste: -d needs an argument\n"); usage(); }
                break;
            } else { fprintf(stderr, "paste: unknown option -%c\n", *p); usage(); }
        }
    }
    delims(d);
    names = argv + i;
    nf = argc - i;
    if (nf == 0) { names = &dash; nf = 1; }
    if (sflag) {
        struct buf one;
        one.s = 0;
        one.cap = 0;
        for (i = 0; i < nf; i++) {
            FILE *f = open1(names[i]);
            if (!f) { status = 1; continue; }
            for (k = 0; getln(f, &one); k++) {
                if (k) putdelim(k - 1);
                fwrite(one.s, 1, one.n, stdout);
            }
            putchar('\n');
            if (f != stdin) fclose(f);
        }
    } else {
        int more = 1;
        if (!(fp = malloc(nf * sizeof *fp)) || !(b = calloc(nf, sizeof *b))) nomem();
        for (i = 0; i < nf; i++)
            if (!(fp[i] = open1(names[i]))) exit(1);
        for (;;) {
            more = 0;
            for (i = 0; i < nf; i++) {          /* a line from each, or none */
                if (fp[i] && !getln(fp[i], &b[i])) {
                    b[i].n = 0;
                    if (fp[i] != stdin) fclose(fp[i]);
                    fp[i] = 0;
                }
                if (fp[i]) more = 1;
            }
            if (!more) break;
            for (i = 0; i < nf; i++) {
                if (fp[i]) fwrite(b[i].s, 1, b[i].n, stdout);
                if (i < nf - 1) putdelim(i);
            }
            putchar('\n');
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) { fprintf(stderr, "paste: write error\n"); return 1; }
    return status;
}
