/* sed -- the stream editor:
 *     sed [-nEs] [-i[suffix]] [-e script] ... [-f file] ... [script] [file ...]
 * POSIX sed with the common GNU extensions: one-line a/i/c, -E, -i, -s,
 * first~step, addr,+N, addr,~N, 0,/re/, I flags, T, Q, z, \n \t \w \< \>
 * and \U \L \u \l \E in replacements. Regular expressions are in sedre.c.
 * Lines may be of any length; a last line without a newline is written
 * without one. Status: 0, 1 for a usage or script error, 2 if an input file
 * could not be read, 4 for an output error, or q's exit code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "sed.h"

typedef struct { char *s; int len, cap; } Buf;
typedef struct { char *name; FILE *fp; int nonl; } Out;   /* nonl: a newline is owed */

enum { A_NONE, A_LINE, A_LAST, A_RE, A_STEP, A_PLUS, A_MULT };
struct addr { int t; long n, step; Re *re; };

struct cmd {
    struct addr a1, a2;
    int neg, range;             /* !; the range is active */
    long rend;                  /* the line a numbered range ends on */
    int c;                      /* the command letter */
    char *text;                 /* a i c: the text; b t T :: the label; r: the file */
    int tlen;
    Re *re;                     /* s: the regex (0: the last one used) */
    int gflag, nth, pflag;
    Out *wout;                  /* w, s///w */
    int n;                      /* q Q: exit code; l: width; b t T {: jump */
    unsigned char *ymap;
};

static struct cmd *cmds;
static int ncmd, cmdcap;
static int nflag, ext, separate, inplace;
static char *suffix;
static int status, quit, qcode;

/* the script: every -e and -f piece, each ending in a newline */
static Buf script;
struct piece { int start, num; char *file; };
static struct piece pieces[64];
static int npieces, nexpr;
static char *S;
static int sp;

static Out sout, serr, *out, iout;
static Out *wouts[32];
static int nwouts;

static Buf ps, hs, tmp, look;
static int nonl, looknonl, havelook, tflag;
static long lineno;
static Re *lastre;

static char **files;
static int nfiles, fi;
static FILE *in;

struct app { int isfile; char *s; int len; };
static struct app *apps;
static int napps, appcap;

static void die(char *fmt, char *a, char *b, int st)
{
    fflush(stdout);
    fprintf(stderr, "sed: ");
    fprintf(stderr, fmt, a, b);
    fprintf(stderr, "\n");
    exit(st);
}

static void *xrealloc(void *p, int n)
{
    p = realloc(p, n);
    if (!p) die("out of memory", 0, 0, 4);
    return p;
}

static void bgrow(Buf *b, int n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2 + 32;
        b->s = xrealloc(b->s, b->cap);
    }
}

static void badd(Buf *b, const char *p, int n)
{
    bgrow(b, n);
    memcpy(b->s + b->len, p, n);
    b->len += n;
    b->s[b->len] = 0;
}

static void bputc(Buf *b, int c)
{
    bgrow(b, 1);
    b->s[b->len++] = c;
    b->s[b->len] = 0;
}

/* ---- parsing the script ---- */

static void bad(char *msg, int arg)
{
    char m[200];
    int k = npieces - 1, i, line = 1;
    sprintf(m, msg, arg);
    while (k > 0 && pieces[k].start > sp) k--;
    fflush(stdout);
    if (pieces[k].file) {
        for (i = pieces[k].start; i < sp && S[i]; i++)
            if (S[i] == '\n') line++;
        fprintf(stderr, "sed: file %s line %d: %s\n", pieces[k].file, line, m);
    } else
        fprintf(stderr, "sed: -e expression #%d, char %d: %s\n", pieces[k].num, sp - pieces[k].start, m);
    exit(1);
}

static void skipws(void)
{
    while (S[sp] == ' ' || S[sp] == '\t') sp++;
}

static long number(void)
{
    long n = 0;
    while (isdigit(S[sp])) n = n * 10 + S[sp++] - '0';
    return n;
}

static Re *compile(Buf *b, int icase)
{
    char *err;
    Re *re;
    if (b->len == 0) return 0;          /* the empty regex: the last one used */
    re = recomp(b->s, (ext ? RE_EXT : 0) | (icase ? RE_ICASE : 0), &err);
    if (!re) bad(err, 0);
    return re;
}

/* a regex up to the delimiter d into b: \d becomes d; a bracket
 * expression is copied whole (a d inside it is an ordinary character) */
static int readre(int d, Buf *b)
{
    int c;
    b->len = 0;
    bputc(b, 0);
    b->len = 0;
    for (;;) {
        c = S[sp];
        if (!c || c == '\n') return 0;
        sp++;
        if (c == d) return 1;
        if (c == '\\') {
            c = S[sp];
            if (!c) return 0;
            sp++;
            if (c == d && d != 'n') bputc(b, c);
            else if (c == '\n') bputc(b, c);
            else { bputc(b, '\\'); bputc(b, c); }
            continue;
        }
        bputc(b, c);
        if (c != '[') continue;
        if (S[sp] == '^') bputc(b, S[sp++]);
        if (S[sp] == ']') bputc(b, S[sp++]);
        while (S[sp] != ']') {
            c = S[sp];
            if (!c || c == '\n') return 0;
            if (c == '[' && (S[sp + 1] == ':' || S[sp + 1] == '.' || S[sp + 1] == '=')) {
                int e = S[sp + 1];
                bputc(b, S[sp++]);
                bputc(b, S[sp++]);
                while (S[sp] && S[sp] != '\n' && !(S[sp] == e && S[sp + 1] == ']')) bputc(b, S[sp++]);
                if (S[sp] != e) return 0;
                bputc(b, S[sp++]);
            } else if (c == '\\' && S[sp + 1] == d) {
                sp++;
            }
            bputc(b, S[sp++]);
        }
        bputc(b, S[sp++]);
    }
}

/* the replacement of s: \0-\9 (& is \0), \U \L \u \l \E, and \\ stay
 * escaped; \n \t, \& and \d (the delimiter) become plain characters */
static int readrepl(int d, Buf *b, int *maxref)
{
    int c;
    b->len = 0;
    bputc(b, 0);
    b->len = 0;
    for (;;) {
        c = S[sp];
        if (!c || c == '\n') return 0;
        sp++;
        if (c == d) return 1;
        if (c == '&') {
            badd(b, "\\0", 2);
        } else if (c == '\\') {
            c = S[sp];
            if (!c) return 0;
            sp++;
            if (isdigit(c) || strchr("ULulE\\", c)) {
                bputc(b, '\\');
                if (isdigit(c) && c - '0' > *maxref) *maxref = c - '0';
            } else if (c == 'n' && d != 'n')
                c = '\n';
            else if (c == 't' && d != 't')
                c = '\t';
            bputc(b, c);
        } else
            bputc(b, c);
    }
}

static Out *wfile(char *name)
{
    int i;
    Out *o;
    if (!strcmp(name, "/dev/stdout")) return &sout;
    if (!strcmp(name, "/dev/stderr")) return &serr;
    for (i = 0; i < nwouts; i++)
        if (!strcmp(wouts[i]->name, name)) return wouts[i];
    if (nwouts == 32) bad("too many w files", 0);
    o = xrealloc(0, sizeof(Out));
    o->name = strdup(name);
    o->nonl = 0;
    o->fp = fopen(name, "w");
    if (!o->fp) die("couldn't open file %s: %s", name, strerror(errno), 4);
    wouts[nwouts++] = o;
    return o;
}

/* the rest of the line, leading blanks skipped (a file name) */
static char *restofline(void)
{
    int b;
    skipws();
    b = sp;
    while (S[sp] && S[sp] != '\n') sp++;
    if (sp == b) bad("missing filename in r/R/w/W commands", 0);
    tmp.len = 0;
    badd(&tmp, S + b, sp - b);
    return strdup(tmp.s);
}

/* a label: up to a ; or the end of the line, trailing blanks dropped */
static char *label(void)
{
    int b, e;
    skipws();
    b = sp;
    while (S[sp] && S[sp] != '\n' && S[sp] != ';') sp++;
    e = sp;
    while (e > b && (S[e - 1] == ' ' || S[e - 1] == '\t')) e--;
    tmp.len = 0;
    badd(&tmp, S + b, e - b);
    bputc(&tmp, 0);
    return strdup(tmp.s);
}

/* the text of a, i or c: "a\" newline text, "a\text" or "a text"; a
 * backslash-newline continues the text on the next line */
static void text(struct cmd *c)
{
    skipws();
    if (S[sp] == '\\') {
        sp++;
        if (S[sp] == '\n') sp++;
    } else if (!S[sp] || S[sp] == '\n')
        bad("expected \\ after `a', `c' or `i'", 0);
    tmp.len = 0;
    while (S[sp] && S[sp] != '\n') {
        if (S[sp] == '\\' && S[sp + 1]) sp++;
        bputc(&tmp, S[sp++]);
    }
    bputc(&tmp, '\n');
    c->text = xrealloc(0, tmp.len + 1);
    memcpy(c->text, tmp.s, tmp.len + 1);
    c->tlen = tmp.len;
}

static int addr(struct addr *a)
{
    int c = S[sp], d, icase = 0;
    Buf b;
    if (isdigit(c)) {
        a->n = number();
        a->t = A_LINE;
        if (S[sp] == '~') {
            sp++;
            a->step = number();
            a->t = A_STEP;
        }
    } else if (c == '$') {
        sp++;
        a->t = A_LAST;
    } else if (c == '/' || c == '\\') {
        sp++;
        d = '/';
        if (c == '\\') {
            d = S[sp++];
            if (!d || d == '\n' || d == '\\') bad("unterminated address regex", 0);
        }
        b.s = 0; b.len = b.cap = 0;
        if (!readre(d, &b)) bad("unterminated address regex", 0);
        while (S[sp] == 'I') { icase = 1; sp++; }
        a->re = compile(&b, icase);
        free(b.s);
        a->t = A_RE;
    } else
        return 0;
    return 1;
}

static void parse_s(struct cmd *c)
{
    int d = S[sp], maxref = 0, icase = 0;
    Buf re, rp;
    re.s = rp.s = 0;
    re.len = re.cap = rp.len = rp.cap = 0;
    if (!d || d == '\n' || d == '\\') bad("unterminated `s' command", 0);
    sp++;
    if (!readre(d, &re) || !readrepl(d, &rp, &maxref)) bad("unterminated `s' command", 0);
    c->nth = 0;
    for (;;) {
        d = S[sp];
        if (d == 'g') {
            c->gflag = 1;
        } else if (d == 'p') {
            if (c->pflag) bad("multiple `p' options to `s' command", 0);
            c->pflag = 1;
        } else if (d == 'i' || d == 'I') {
            icase = 1;
        } else if (isdigit(d)) {
            if (c->nth) bad("multiple number options to `s' command", 0);
            c->nth = number();
            if (!c->nth) bad("number option to `s' command may not be zero", 0);
            continue;
        } else if (d == 'w') {
            sp++;
            c->wout = wfile(restofline());
            break;
        } else if (d == ' ' || d == '\t' || d == ';' || d == '\n' || d == '}' || d == '#' || !d) {
            break;
        } else
            bad("unknown option to `s'", 0);
        sp++;
    }
    if (!c->nth) c->nth = 1;
    c->re = compile(&re, icase);
    if (c->re && maxref > rensub(c->re)) bad("invalid reference \\%d on `s' command's RHS", maxref);
    c->text = rp.s;
    c->tlen = rp.len;
    free(re.s);
}

/* the strings of y: \\, \n and \d (the delimiter) are escapes */
static int ystr(int d, Buf *b)
{
    int c;
    b->len = 0;
    for (;;) {
        c = S[sp];
        if (!c || c == '\n') return 0;
        sp++;
        if (c == d) return 1;
        if (c == '\\') {
            c = S[sp++];
            if (c == 'n') c = '\n';
            else if (c != d && c != '\\') bad("unknown option to `y'", 0);
        }
        bputc(b, c);
    }
}

static void parse_y(struct cmd *c)
{
    int d = S[sp], i;
    Buf a, b;
    a.s = b.s = 0;
    a.len = a.cap = b.len = b.cap = 0;
    if (!d || d == '\n' || d == '\\') bad("unterminated `y' command", 0);
    sp++;
    if (!ystr(d, &a) || !ystr(d, &b)) bad("unterminated `y' command", 0);
    if (a.len != b.len) bad("strings for `y' command are different lengths", 0);
    c->ymap = xrealloc(0, 256);
    for (i = 0; i < 256; i++) c->ymap[i] = i;
    for (i = 0; i < a.len; i++) c->ymap[(unsigned char)a.s[i]] = b.s[i];
    free(a.s);
    free(b.s);
}

static void parse(void)
{
    int open[64], nopen = 0, i, j, k;
    struct cmd *c;
    S = script.s;
    sp = 0;
    if (S[0] == '#' && S[1] == 'n' && S[2] == '\n') nflag = 1;
    for (;;) {
        while (S[sp] == ' ' || S[sp] == '\t' || S[sp] == '\n' || S[sp] == ';') sp++;
        if (!S[sp]) break;
        if (S[sp] == '#') {
            while (S[sp] && S[sp] != '\n') sp++;
            continue;
        }
        if (ncmd >= cmdcap) {
            cmdcap = cmdcap * 2 + 16;
            cmds = xrealloc(cmds, cmdcap * sizeof(struct cmd));
        }
        c = &cmds[ncmd];
        memset(c, 0, sizeof(struct cmd));
        if (addr(&c->a1)) {
            if (S[sp] == ',') {
                sp++;
                skipws();
                if (S[sp] == '+' || S[sp] == '~') {
                    c->a2.t = S[sp++] == '+' ? A_PLUS : A_MULT;
                    if (!isdigit(S[sp])) bad("expected newer version of sed", 0);
                    c->a2.n = number();
                } else if (!addr(&c->a2))
                    bad("unexpected `,'", 0);
            }
            if (c->a1.t == A_LINE && c->a1.n == 0) {
                if (c->a2.t != A_RE) bad("invalid usage of line address 0", 0);
                c->range = 1;
            }
        }
        skipws();
        while (S[sp] == '!') {
            c->neg = 1;
            sp++;
            skipws();
        }
        c->c = S[sp];
        if (!c->c || c->c == '\n' || c->c == ';') bad("missing command", 0);
        sp++;
        switch (c->c) {
        case '{':
            if (nopen == 64) bad("too many {", 0);
            open[nopen++] = ncmd++;
            continue;
        case '}':
            if (!nopen) bad("unexpected `}'", 0);
            if (c->a1.t) bad("} doesn't want any addresses", 0);
            cmds[open[--nopen]].n = ncmd;
            break;
        case '=': case 'd': case 'D': case 'g': case 'G': case 'h': case 'H':
        case 'n': case 'N': case 'p': case 'P': case 'x': case 'z':
            break;
        case 'l': case 'q': case 'Q':
            skipws();
            c->n = isdigit(S[sp]) ? number() : c->c == 'l' ? 70 : 0;
            if (c->c != 'l' && c->a2.t) bad("command only uses one address", 0);
            break;
        case ':':
            if (c->a1.t) bad(": doesn't want any addresses", 0);
            c->text = label();
            if (!c->text[0]) bad("\":\" lacks a label", 0);
            break;
        case 'b': case 't': case 'T':
            c->text = label();
            break;
        case 'a': case 'i': case 'c':
            text(c);
            break;
        case 'r':
            c->text = restofline();
            break;
        case 'w':
            c->wout = wfile(restofline());
            break;
        case 's':
            parse_s(c);
            break;
        case 'y':
            parse_y(c);
            break;
        default:
            bad("unknown command: `%c'", c->c);
        }
        ncmd++;
        skipws();
        if (S[sp] == ';' || S[sp] == '\n') sp++;
        else if (S[sp] && S[sp] != '}' && S[sp] != '#') bad("extra characters after command", 0);
    }
    if (nopen) {
        sp = 0;
        bad("unmatched `{'", 0);
    }
    for (i = 0; i < ncmd; i++) {
        k = cmds[i].c;
        if (k != 'b' && k != 't' && k != 'T') continue;
        cmds[i].n = ncmd;
        if (!cmds[i].text[0]) continue;
        for (j = 0; j < ncmd; j++)
            if (cmds[j].c == ':' && !strcmp(cmds[j].text, cmds[i].text)) break;
        if (j == ncmd) die("can't find label for jump to `%s'", cmds[i].text, 0, 1);
        cmds[i].n = j;
    }
}

/* ---- input ---- */

static void openinput(int one)
{
    char *name;
    while (fi < nfiles) {
        name = files[fi++];
        if (!strcmp(name, "-")) {
            in = stdin;
            return;
        }
        in = fopen(name, "r");
        if (in) return;
        fflush(stdout);
        fprintf(stderr, "sed: can't read %s: %s\n", name, strerror(errno));
        status = 2;
        if (one) return;
    }
}

static void closeinput(void)
{
    if (in && in != stdin) fclose(in);
    in = 0;
}

/* one line of the current file into b (without its newline) */
static int rawline(Buf *b, int *missing)
{
    int c = EOF;
    b->len = 0;
    if (!in) return 0;
    for (;;) {
        c = getc(in);
        if (c == EOF || c == '\n') break;
        bgrow(b, 1);
        b->s[b->len++] = c;
    }
    bputc(b, 0);
    b->len--;
    if (c == EOF && b->len == 0) return 0;
    *missing = c == EOF;
    return 1;
}

/* read ahead a line, so that $ can be known; with -s or -i, files are
 * separate (the caller opens the next) */
static void fill(void)
{
    for (;;) {
        if (rawline(&look, &looknonl)) {
            havelook = 1;
            return;
        }
        closeinput();
        havelook = 0;
        if (separate || fi >= nfiles) return;
        openinput(0);
    }
}

static int nextline(int append)
{
    Buf t;
    if (!havelook) return 0;
    if (append) {
        bputc(&ps, '\n');
        badd(&ps, look.s, look.len);
    } else {
        t = ps; ps = look; look = t;
    }
    nonl = looknonl;
    lineno++;
    tflag = 0;
    fill();
    return 1;
}

/* ---- output ---- */

static void owrite(Out *o, const char *s, int n)
{
    if (o->nonl) {
        putc('\n', o->fp);
        o->nonl = 0;
    }
    if (o == &serr) fflush(stdout);
    fwrite(s, 1, n, o->fp);
}

static void oline(Out *o, const char *s, int n, int missing)
{
    owrite(o, s, n);
    if (missing) o->nonl = 1;
    else putc('\n', o->fp);
}

static void queue(int isfile, char *s, int len)
{
    if (napps >= appcap) {
        appcap = appcap * 2 + 8;
        apps = xrealloc(apps, appcap * sizeof(struct app));
    }
    apps[napps].isfile = isfile;
    apps[napps].s = s;
    apps[napps].len = len;
    napps++;
}

static void flushapps(void)
{
    int i, n;
    char buf[512];
    FILE *f;
    for (i = 0; i < napps; i++) {
        if (!apps[i].isfile) {
            owrite(out, apps[i].s, apps[i].len);
            continue;
        }
        f = fopen(apps[i].s, "r");
        if (!f) continue;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) owrite(out, buf, n);
        fclose(f);
    }
    napps = 0;
}

static void list(int width)
{
    char ob[8];
    int i, k, w = 0, c;
    for (i = 0; i < ps.len; i++) {
        c = (unsigned char)ps.s[i];
        ob[0] = '\\';
        ob[2] = 0;
        switch (c) {
        case '\\': ob[1] = '\\'; break;
        case '\a': ob[1] = 'a'; break;
        case '\b': ob[1] = 'b'; break;
        case '\f': ob[1] = 'f'; break;
        case '\n': ob[1] = 'n'; break;
        case '\r': ob[1] = 'r'; break;
        case '\t': ob[1] = 't'; break;
        case '\v': ob[1] = 'v'; break;
        default:
            if (c < 32 || c >= 127) sprintf(ob, "\\%03o", c);
            else { ob[0] = c; ob[1] = 0; }
        }
        k = strlen(ob);
        if (width > 1 && w + k > width - 1) {
            owrite(out, "\\\n", 2);
            w = 0;
        }
        owrite(out, ob, k);
        w += k;
    }
    owrite(out, "$\n", 2);
}

/* ---- running the script ---- */

static int match(Re *re, int start, int *sub)
{
    if (!re) re = lastre;
    if (!re) die("no previous regular expression", 0, 0, 1);
    lastre = re;
    return reexec(re, ps.s, ps.len, start, sub);
}

static int amatch(struct addr *a)
{
    int sub[2 * RE_NSUB];
    switch (a->t) {
    case A_LINE: return lineno == a->n;
    case A_LAST: return !havelook;
    case A_RE: return match(a->re, 0, sub);
    case A_STEP:
        if (a->step <= 0) return lineno == a->n;
        return lineno >= a->n && (lineno - a->n) % a->step == 0;
    }
    return 0;
}

static int selected(struct cmd *c)
{
    int r = 1;
    if (!c->a1.t) return 1;
    if (!c->a2.t) {
        r = amatch(&c->a1);
    } else if (c->range) {
        switch (c->a2.t) {
        case A_LINE: case A_PLUS:
            if (lineno >= c->rend) c->range = 0;
            break;
        case A_MULT:
            if (c->a2.n <= 0 || lineno % c->a2.n == 0) c->range = 0;
            break;
        default:
            if (amatch(&c->a2)) c->range = 0;
        }
    } else if (amatch(&c->a1)) {
        c->range = 1;
        switch (c->a2.t) {
        case A_LINE:
            c->rend = c->a2.n;
            if (c->rend <= lineno) c->range = 0;
            break;
        case A_PLUS:
            c->rend = lineno + c->a2.n;
            if (c->a2.n == 0) c->range = 0;
            break;
        case A_MULT:
            if (c->a2.n <= 0 || lineno % c->a2.n == 0) c->range = 0;
            break;
        case A_LAST:
            if (!havelook) c->range = 0;
            break;
        }
    } else
        r = 0;
    return r != c->neg;
}

static void addc(int c, int *mode, int *one)
{
    if (*one) {
        c = *one == 'u' ? toupper(c) : tolower(c);
        *one = 0;
    } else if (*mode)
        c = *mode == 'U' ? toupper(c) : tolower(c);
    bputc(&tmp, c);
}

static void putrepl(struct cmd *c, int *sub)
{
    char *r = c->text;
    int i, k, mode = 0, one = 0;
    for (i = 0; i < c->tlen; i++) {
        if (r[i] != '\\') {
            addc(r[i], &mode, &one);
            continue;
        }
        i++;
        if (isdigit(r[i])) {
            k = r[i] - '0';
            if (sub[2 * k] >= 0 && sub[2 * k + 1] >= sub[2 * k])
                for (k = sub[2 * k]; k < sub[2 * (r[i] - '0') + 1]; k++)
                    addc(ps.s[k], &mode, &one);
        } else if (r[i] == 'U' || r[i] == 'L') {
            mode = r[i];
            one = 0;
        } else if (r[i] == 'E') {
            mode = one = 0;
        } else if (r[i] == 'u' || r[i] == 'l') {
            one = r[i];
        } else
            addc(r[i], &mode, &one);
    }
}

static void subst(struct cmd *c)
{
    int sub[2 * RE_NSUB], pos = 0, count = 0, prevend = -1, done = 0, ms, me;
    Buf t;
    tmp.len = 0;
    while (pos <= ps.len && match(c->re, pos, sub)) {
        ms = sub[0];
        me = sub[1];
        if (ms == me && ms == prevend) {        /* empty, just after a match */
            if (ms >= ps.len) break;
            badd(&tmp, ps.s + pos, ms + 1 - pos);
            pos = ms + 1;
            continue;
        }
        count++;
        badd(&tmp, ps.s + pos, ms - pos);
        if (count >= c->nth && (c->gflag || count == c->nth)) {
            putrepl(c, sub);
            done = 1;
        } else
            badd(&tmp, ps.s + ms, me - ms);
        prevend = me;
        pos = me;
        if (ms == me) {
            if (ms >= ps.len) break;
            bputc(&tmp, ps.s[ms]);
            pos++;
        }
        if (done && !c->gflag) break;
    }
    if (!done) return;
    if (pos < ps.len) badd(&tmp, ps.s + pos, ps.len - pos);
    bputc(&tmp, 0);
    tmp.len--;
    t = ps; ps = tmp; tmp = t;
    tflag = 1;
    if (c->pflag) oline(out, ps.s, ps.len, nonl);
    if (c->wout) oline(c->wout, ps.s, ps.len, nonl);
}

enum { END, DEL, AGAIN, QUIT };

/* run the script on every line of the current input */
static void run(void)
{
    int pc, act, again = 0, i;
    char nb[16], *p;
    struct cmd *c;
    Buf t;
    while (again || nextline(0)) {
        again = 0;
        act = END;
        for (pc = 0; pc < ncmd && act == END; ) {
            c = &cmds[pc];
            if (!selected(c)) {
                pc = c->c == '{' ? c->n + 1 : pc + 1;
                continue;
            }
            pc++;
            switch (c->c) {
            case '=':
                sprintf(nb, "%ld\n", lineno);
                owrite(out, nb, strlen(nb));
                break;
            case 'a': queue(0, c->text, c->tlen); break;
            case 'r': queue(1, c->text, 0); break;
            case 'i': owrite(out, c->text, c->tlen); break;
            case 'b': pc = c->n; break;
            case 't': if (tflag) { tflag = 0; pc = c->n; } break;
            case 'T': if (!tflag) pc = c->n; else tflag = 0; break;
            case 'c':
                if (c->neg || !c->a2.t || !c->range) owrite(out, c->text, c->tlen);
                act = DEL;
                break;
            case 'd': act = DEL; break;
            case 'D':
                p = memchr(ps.s, '\n', ps.len);
                if (!p) { act = DEL; break; }
                i = p + 1 - ps.s;
                memmove(ps.s, p + 1, ps.len - i + 1);
                ps.len -= i;
                act = AGAIN;
                break;
            case 'g': ps.len = 0; badd(&ps, hs.s ? hs.s : "", hs.len); break;
            case 'G': bputc(&ps, '\n'); badd(&ps, hs.s ? hs.s : "", hs.len); break;
            case 'h': hs.len = 0; badd(&hs, ps.s, ps.len); break;
            case 'H': bputc(&hs, '\n'); badd(&hs, ps.s, ps.len); break;
            case 'x': t = ps; ps = hs; hs = t; bputc(&ps, 0); ps.len--; break;
            case 'l': list(c->n); break;
            case 'n':
                if (!havelook) { act = QUIT; break; }
                if (!nflag) oline(out, ps.s, ps.len, nonl);
                flushapps();
                nextline(0);
                break;
            case 'N':
                if (!havelook) { act = QUIT; break; }
                flushapps();
                nextline(1);
                break;
            case 'p': oline(out, ps.s, ps.len, nonl); break;
            case 'P':
                p = memchr(ps.s, '\n', ps.len);
                oline(out, ps.s, p ? p - ps.s : ps.len, 0);
                break;
            case 'q': qcode = c->n; quit = 1; act = QUIT; break;
            case 'Q': qcode = c->n; quit = 1; return;
            case 's': subst(c); break;
            case 'w': oline(c->wout, ps.s, ps.len, nonl); break;
            case 'y':
                for (i = 0; i < ps.len; i++) ps.s[i] = c->ymap[(unsigned char)ps.s[i]];
                break;
            case 'z': ps.len = 0; ps.s[0] = 0; break;
            }
        }
        if ((act == END || act == QUIT) && !nflag) oline(out, ps.s, ps.len, nonl);
        flushapps();
        if (quit) return;
        again = act == AGAIN;
    }
}

/* ---- in-place editing ---- */

static char tmpname[600];

static int startinplace(char *name)
{
    struct stat st;
    char *s;
    int n;
    if (stat(name, &st) < 0) {
        fflush(stdout);
        fprintf(stderr, "sed: can't read %s: %s\n", name, strerror(errno));
        status = 2;
        return 0;
    }
    if (!S_ISREG(st.st_mode) || strlen(name) > 500) {
        fflush(stdout);
        fprintf(stderr, "sed: couldn't edit %s: not a regular file\n", name);
        status = 4;
        return 0;
    }
    strcpy(tmpname, name);
    s = strrchr(tmpname, '/');
    n = s ? s + 1 - tmpname : 0;
    sprintf(tmpname + n, "sed%d.tmp", getpid());
    iout.fp = fopen(tmpname, "w");
    if (!iout.fp) die("couldn't open temporary file %s: %s", tmpname, strerror(errno), 4);
    chmod(tmpname, st.st_mode & 07777);
    iout.nonl = 0;
    out = &iout;
    return 1;
}

static void endinplace(char *name)
{
    char bak[600];
    fclose(iout.fp);
    out = &sout;
    if (suffix && strlen(name) + strlen(suffix) < sizeof bak) {
        sprintf(bak, "%s%s", name, suffix);
        unlink(bak);
        if (link(name, bak) < 0) die("couldn't make backup %s: %s", bak, strerror(errno), 4);
    }
    if (rename(tmpname, name) < 0) die("couldn't rename %s: %s", tmpname, strerror(errno), 4);
}

/* ---- the command line ---- */

static void usage(void)
{
    fflush(stdout);
    fprintf(stderr, "usage: sed [-nEs] [-i[suffix]] [-e script] [-f file] [script] [file ...]\n");
    exit(1);
}

static void addpiece(char *s, int n, char *file)
{
    if (npieces == 64) die("too many script pieces", 0, 0, 1);
    pieces[npieces].start = script.len;
    pieces[npieces].file = file;
    pieces[npieces].num = file ? 0 : ++nexpr;
    npieces++;
    badd(&script, s, n);
    if (n == 0 || s[n - 1] != '\n') bputc(&script, '\n');
}

static void addfile(char *name)
{
    FILE *f = strcmp(name, "-") ? fopen(name, "r") : stdin;
    Buf b;
    int c;
    if (!f) die("couldn't open file %s: %s", name, strerror(errno), 1);
    b.s = 0;
    b.len = b.cap = 0;
    while ((c = getc(f)) != EOF) bputc(&b, c);
    if (f != stdin) fclose(f);
    addpiece(b.s ? b.s : "", b.len, name);
    free(b.s);
}

int main(int argc, char **argv)
{
    int i;
    char *p, *arg;
    static char *dash[] = { "-", 0 };
    sout.fp = stdout;
    serr.fp = stderr;
    out = &sout;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'n': nflag = 1; continue;
            case 'E': case 'r': ext = 1; continue;
            case 's': separate = 1; continue;
            case 'i':
                inplace = separate = 1;
                if (p[1]) suffix = p + 1;
                break;
            case 'e': case 'f':
                arg = p[1] ? p + 1 : argv[++i];
                if (!arg) {
                    fflush(stdout);
                    fprintf(stderr, "sed: option requires an argument -- '%c'\n", *p);
                    usage();
                }
                if (*p == 'e') addpiece(arg, strlen(arg), 0);
                else addfile(arg);
                break;
            default:
                fflush(stdout);
                fprintf(stderr, "sed: invalid option -- '%c'\n", *p);
                usage();
            }
            break;
        }
    }
    if (!npieces) {
        if (i >= argc) usage();
        addpiece(argv[i], strlen(argv[i]), 0);
        i++;
    }
    bputc(&script, 0);
    script.len--;
    bputc(&ps, 0);
    ps.len = 0;
    parse();
    files = argv + i;
    nfiles = argc - i;
    if (!nfiles) {
        if (inplace) die("no input files", 0, 0, 1);
        files = dash;
        nfiles = 1;
    }
    if (!separate) {
        openinput(0);
        fill();
        run();
    } else {
        while (fi < nfiles && !quit) {
            p = files[fi];
            if (inplace && !startinplace(p)) { fi++; continue; }
            openinput(1);
            if (in) {
                lineno = 0;
                fill();
                run();
                closeinput();
                if (inplace) endinplace(p);
            } else if (inplace) {
                fclose(iout.fp);
                unlink(tmpname);
                out = &sout;
            }
        }
    }
    fflush(stdout);
    for (i = 0; i < nwouts; i++)
        if (fclose(wouts[i]->fp) == EOF) status = 4;
    if (ferror(stdout)) status = 4;
    return quit && qcode ? qcode : status;
}
