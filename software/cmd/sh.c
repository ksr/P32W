/* sh -- the P32W Unix shell (M5; os-design.md 7): commands with arguments,
 * quotes ('...' and "..." and \), redirection < > >>, pipelines |, lists ;,
 * background &, variables ($NAME ${NAME} $? $$ $! $0-$9 $# $*; NAME=value
 * sets one, NAME=value cmd for one command, and every variable is in the
 * environment; unquoted substitutions are split into words), wildcards
 * * ? [...] (in the last part of a path), here-documents <<WORD, and
 * sh -c 'commands' runs them and finishes. Control flow:
 *     if list; then list; [elif list; then list;] [else list;] fi
 *     while list; do list; done        for NAME in words; do list; done
 *     case word in pat|pat) list ;; ... esac
 *     name() { list; }   ( list )   { list; }
 * (keywords start a command; a construct may span lines -- the shell
 * prompts "> " for the rest). Built-ins: cd, exit, export, unset, break,
 * continue, :, return, set (-e -x, --), shift, read, eval, ., wait, exec,
 * umask, history. Commands are found on $PATH; scripts run by name (libc's
 * execve: #! or sh). A login shell (-sh) reads /etc/profile and
 * $HOME/.profile. Conditions are commands' exit statuses (see test).
 *
 * On a terminal, lines are edited and recalled: the arrow keys (or ^B ^F
 * ^P ^N) move along the line and through the history, ^A / ^E go to its
 * start / end, ^K deletes to the end, ^U the whole line, ^W a word, ^L
 * redraws, ^C abandons it, Tab completes a command or file name (twice:
 * the choices). `history` lists the last 100 commands (kept in
 * $HOME/.sh_history between logins); !! runs the last again, !n number n,
 * !-n the n-th last, !text the last starting with text. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <sgtty.h>
#include <sys/stat.h>

#define MAXW 256                       /* words in a command (more is an error, not dropped) */
#define MAXR 8                         /* redirections in a command */
#define MAXC 16

struct redir {
    int fd;                             /* the descriptor redirected: 0 for <, 1 for >, or N in N> */
    int kind;                           /* '<' read, '>' write, 'a' append, '&' a copy of dupfd */
    int dupfd;
    char *file;
};
struct cmd {
    char *argv[MAXW];
    int argc, toomany;
    struct redir r[MAXR];
    int nr;
    char *assign[16];                   /* NAME=value before the command: for it alone */
    int na;
    int sub;                            /* ( list ): argv[0] holds the list */
};

static char line[1024];
static char *words, *words_end;         /* the current command's words (lex), per command */
static int status, isglob[2 * MAXW + 1];
static char **posv;                     /* $0 $1 ... */
static int posc, lastbg, breaking, continuing, loopdepth;
static int returning, funcdepth;        /* return; inside functions */
static int infds[8], ninfds;            /* the scripts being read: not passed on to commands */
static int opt_e, opt_x, conddepth;     /* set -e, set -x; (in a condition: -e does not apply) */
static char **setargs;                  /* set -- args: the new $1 ... (with $0) */
struct node;
struct fn { char name[32]; struct node *body; };
static struct fn fns[32];
static int nfns;
static void run_text(char *text);
static int run_input(FILE *in, int tty);
static int editing;                     /* on a terminal: the line editor and history */
static void sherror(const char *what, const char *msg);

/* ---------------------------------------------------------------- history */
#define NHIST 100
static char *hist[NHIST];
static int nhist, histbase = 1;         /* hist[0] is command number histbase */
static char histfile[256];              /* $HOME/.sh_history ("": none) */

static void hist_add(const char *s, int save)
{
    int n = strlen(s);
    char *p;
    while (n && (s[n - 1] == '\n' || s[n - 1] == ' ')) n--;
    if (!n) return;
    if (nhist && (int)strlen(hist[nhist - 1]) == n && !strncmp(hist[nhist - 1], s, n)) return;   /* (a repeat) */
    if (!(p = malloc(n + 1))) return;
    memcpy(p, s, n);
    p[n] = 0;
    if (nhist == NHIST) {
        free(hist[0]);
        memmove(hist, hist + 1, (NHIST - 1) * sizeof hist[0]);
        nhist--;
        histbase++;
    }
    hist[nhist++] = p;
    if (save && histfile[0]) {
        int f = open(histfile, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (f >= 0) { write(f, p, n); write(f, "\n", 1); close(f); }
    }
}

/* the history file: read at the start; kept to the last NHIST lines */
static void hist_load(void)
{
    char *home = getenv("HOME"), b[1024];
    FILE *f;
    int lines = 0, k;
    if (!home || strlen(home) > sizeof histfile - 16) return;
    strcpy(histfile, home);
    strcat(histfile, strcmp(home, "/") ? "/.sh_history" : ".sh_history");
    if (!(f = fopen(histfile, "r"))) return;
    while (fgets(b, sizeof b, f)) { hist_add(b, 0); lines++; }
    fclose(f);
    if (lines > 2 * NHIST && (f = fopen(histfile, "w")) != 0) {
        for (k = 0; k < nhist; k++) fprintf(f, "%s\n", hist[k]);
        fclose(f);
    }
    histbase = 1;
}

static void hist_list(void)
{
    int k;
    for (k = 0; k < nhist; k++) printf("%5d  %s\n", histbase + k, hist[k]);
}

/* !! !n !-n !text in an interactive line (not inside '...'): 0 if nothing
   changed, 1 if the line was rewritten, -1 (with a message) if no command
   matches */
static int hist_expand(char *line, int size)
{
    static char out[1024];
    char *s = line, *o = out, *end = out + sizeof out - 2;
    int changed = 0, quote = 0;
    while (*s) {
        const char *h = 0;
        char *t = s + 1;
        if (*s == '\'' ) quote = !quote;
        if (*s == '\\' && s[1]) { if (o < end - 1) { *o++ = *s++; *o++ = *s++; } continue; }
        if (*s == '!' && !quote) {
            if (*t == '!') {
                if (nhist) h = hist[nhist - 1];
                t++;
            } else if ((*t >= '0' && *t <= '9') || (*t == '-' && t[1] >= '0' && t[1] <= '9')) {
                int n = atoi(t), k = n < 0 ? nhist + n : n - histbase;
                if (*t == '-') t++;
                while (*t >= '0' && *t <= '9') t++;
                if (k >= 0 && k < nhist) h = hist[k];
            } else if ((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z') || *t == '.' || *t == '/' || *t == '_') {
                int n = 0, k;
                while (t[n] && t[n] != ' ' && t[n] != '\t' && t[n] != '\n' && t[n] != ';' && t[n] != '|' && t[n] != '&') n++;
                for (k = nhist - 1; k >= 0 && !h; k--)
                    if (!strncmp(hist[k], t, n)) h = hist[k];
                t += n;
            } else {
                if (o < end) *o++ = *s;
                s++;
                continue;
            }
            if (!h) {
                int n = t - s;
                fprintf(stderr, "sh: %.*s: event not found\n", n > 40 ? 40 : n, s);
                return -1;
            }
            while (*h && o < end) *o++ = *h++;
            s = t;
            changed = 1;
            continue;
        }
        if (o < end) *o++ = *s;
        s++;
    }
    *o = 0;
    if (!changed) return 0;
    if ((int)strlen(out) >= size) { sherror("history", "line too long"); return -1; }
    strcpy(line, out);
    return 1;
}

/* ---------------------------------------------------------------- the line editor */
static struct sgttyb tsaved;
static volatile int interrupted;

static void on_int(int sig) { (void)sig; signal(SIGINT, on_int); interrupted = 1; }

static void out(const char *s, int n) { if (n > 0) write(1, s, n); }
static void rep(int c, int n)
{
    char b[64];
    memset(b, c, sizeof b);
    while (n > 0) { out(b, n < 64 ? n : 64); n -= 64; }
}

/* ---------------------------------------------------------------- completion (Tab) */
#define NCAND 256
static char *cand[NCAND];
static int ncand;

static void add_cand(const char *name, int dir)         /* (dir: a / after it) */
{
    int k, n = strlen(name);
    for (k = 0; k < ncand; k++) if (!strncmp(cand[k], name, n) && (cand[k][n] == 0 || (cand[k][n] == '/' && !cand[k][n + 1]))) return;
    if (ncand == NCAND || !(cand[ncand] = malloc(n + 2))) return;
    strcpy(cand[ncand], name);
    if (dir) strcat(cand[ncand], "/");
    ncand++;
}

static int cmpcand(const void *a, const void *b) { return strcmp(*(char **)a, *(char **)b); }

/* the names in dir starting with prefix (programs only, for a command) */
static void scan_dir(const char *dir, const char *prefix, int programs)
{
    DIR *d = opendir(*dir ? dir : ".");
    struct dirent *e;
    int n = strlen(prefix);
    if (!d) return;
    while ((e = readdir(d)) != 0) {
        char path[300];
        struct stat st;
        int isdir;
        if (strncmp(e->d_name, prefix, n)) continue;
        if (e->d_name[0] == '.' && prefix[0] != '.') continue;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (strlen(dir) + strlen(e->d_name) > 290) continue;
        sprintf(path, "%s%s", *dir ? dir : "", e->d_name);
        isdir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        if (programs && (isdir || !(st.st_mode & 0111))) continue;
        add_cand(e->d_name, isdir);
    }
    closedir(d);
}

/* candidates for the word buf[start..pos): commands in the first word,
   else files; the result is what the word's last part may become */
static void complete_candidates(const char *buf, int start, int pos, char *dirpart)
{
    char word[256], prefix[256];
    int k, first = 1, wl = pos - start;
    static const char *const builtins[] = { "cd", "exit", "export", "history", "unset", "break", "continue", 0 };
    for (k = 0; k < ncand; k++) free(cand[k]);
    ncand = 0;
    if (wl > 250) wl = 250;
    memcpy(word, buf + start, wl);
    word[wl] = 0;
    for (k = start - 1; k >= 0; k--) {         /* a command word: nothing but spaces back to the start, | ; or & */
        if (buf[k] == ' ' || buf[k] == '\t') continue;
        if (buf[k] != '|' && buf[k] != ';' && buf[k] != '&') first = 0;
        break;
    }
    {
        char *slash = strrchr(word, '/');
        if (slash) { memcpy(dirpart, word, slash - word + 1); dirpart[slash - word + 1] = 0; strcpy(prefix, slash + 1); }
        else { dirpart[0] = 0; strcpy(prefix, word); }
    }
    if (first && !dirpart[0]) {         /* a command: built-ins and $PATH */
        char path[256], *p, *q;
        for (k = 0; builtins[k]; k++) if (!strncmp(builtins[k], prefix, strlen(prefix))) add_cand(builtins[k], 0);
        strncpy(path, getenv("PATH") ? getenv("PATH") : "/bin:/usr/bin", sizeof path - 1);
        path[sizeof path - 1] = 0;
        for (p = path; p; p = q) {
            char dir[260];
            q = strchr(p, ':');
            if (q) *q++ = 0;
            if (!*p || strlen(p) > 250) continue;
            sprintf(dir, "%s/", p);
            scan_dir(dir, prefix, 1);
        }
    } else
        scan_dir(dirpart, prefix, 0);
    qsort(cand, ncand, sizeof cand[0], cmpcand);
}

/* a line from the terminal, edited; its length (with the newline), or 0 at
   the end of input (^D on an empty line) */
static int edit_line(const char *prompt, char *buf, int size)
{
    static char saved[1024];
    struct sgttyb t = tsaved;
    int len = 0, pos = 0, h = nhist, c, k, tabbed = 0;
    t.sg_flags = (t.sg_flags & ~(ECHO | RAW)) | CBREAK;
    stty(0, &t);
    signal(SIGINT, on_int);
    out(prompt, strlen(prompt));
    for (;;) {
        unsigned char ch;
        const char *nl = 0;
        interrupted = 0;
        k = read(0, &ch, 1);
        if (interrupted || (k < 0 && errno == EINTR)) {      /* ^C: the line is abandoned */
            len = pos = 0;
            h = nhist;
            out(prompt, strlen(prompt));
            continue;
        }
        if (k != 1) { if (len) break; len = -1; break; }
        c = ch;
        if (c != 9) tabbed = 0;
        if (c == 27) {                  /* ESC [ x or ESC O x: the arrow keys, Home, End, Delete */
            if (read(0, &ch, 1) != 1) continue;
            if (ch != '[' && ch != 'O') continue;
            if (read(0, &ch, 1) != 1) continue;
            c = ch;
            if (c >= '0' && c <= '9') {
                unsigned char t2;
                if (read(0, &t2, 1) != 1 || t2 != '~') continue;
                c = c == '1' || c == '7' ? 1 : c == '4' || c == '8' ? 5 : c == '3' ? 4 : 0;
            } else
                c = c == 'A' ? 16 : c == 'B' ? 14 : c == 'C' ? 6 : c == 'D' ? 2 : c == 'H' ? 1 : c == 'F' ? 5 : 0;
            if (c == 4 && pos == len) continue;     /* Delete at the end: nothing (not ^D's end of input) */
        }
        switch (c) {
        case '\n': case '\r':
            break;
        case 1: rep('\b', pos); pos = 0; continue;                          /* ^A */
        case 5: out(buf + pos, len - pos); pos = len; continue;            /* ^E */
        case 2: if (pos) { out("\b", 1); pos--; } continue;                /* ^B, left */
        case 6: if (pos < len) { out(buf + pos, 1); pos++; } continue;     /* ^F, right */
        case 4:                                                            /* ^D: delete here; on an empty line, the end */
            if (!len) { len = -1; break; }
            if (pos == len) continue;
            memmove(buf + pos, buf + pos + 1, len - pos - 1);
            len--;
            out(buf + pos, len - pos); out(" ", 1); rep('\b', len - pos + 1);
            continue;
        case 8: case 127:                                                  /* backspace */
            if (!pos) continue;
            memmove(buf + pos - 1, buf + pos, len - pos);
            pos--; len--;
            out("\b", 1); out(buf + pos, len - pos); out(" ", 1); rep('\b', len - pos + 1);
            continue;
        case 11:                                                           /* ^K */
            rep(' ', len - pos); rep('\b', len - pos);
            len = pos;
            continue;
        case 21:                                                           /* ^U */
            rep('\b', pos); rep(' ', len); rep('\b', len);
            len = pos = 0;
            continue;
        case 23: {                                                         /* ^W: the word before the cursor */
            int from = pos;
            while (from && buf[from - 1] == ' ') from--;
            while (from && buf[from - 1] != ' ') from--;
            k = pos - from;
            memmove(buf + from, buf + pos, len - pos);
            rep('\b', k);
            pos = from; len -= k;
            out(buf + pos, len - pos); rep(' ', k); rep('\b', len - pos + k);
            continue;
        }
        case 9: {                                                          /* Tab: complete the word */
            char dirpart[256], add[256];
            int start = pos, plen, n, j;
            while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t' && !strchr("|;&<>", buf[start - 1])) start--;
            complete_candidates(buf, start, pos, dirpart);
            if (!ncand) { out("\007", 1); tabbed = 0; continue; }
            {   /* what the candidates share past what is typed */
                const char *w = buf + start + strlen(dirpart);
                plen = pos - start - strlen(dirpart);
                n = strlen(cand[0]);
                for (j = 1; j < ncand; j++) {
                    int m = 0;
                    while (m < n && cand[j][m] == cand[0][m]) m++;
                    n = m;
                }
                (void)w;
            }
            j = 0;
            if (n > plen) { memcpy(add, cand[0] + plen, n - plen); j = n - plen; }
            if (ncand == 1 && cand[0][strlen(cand[0]) - 1] != '/') add[j++] = ' ';
            add[j] = 0;
            if (j) {                    /* insert it at the cursor */
                if (len + j >= size - 2) continue;
                memmove(buf + pos + j, buf + pos, len - pos);
                memcpy(buf + pos, add, j);
                len += j;
                out(buf + pos, len - pos);
                pos += j;
                rep('\b', len - pos);
                tabbed = ncand > 1;     /* (still several: the next Tab lists them) */
                continue;
            }
            if (ncand > 1 && tabbed) {  /* a second Tab: the list, then the line again */
                int w = 1, cols, rows, r, k2;
                for (k2 = 0; k2 < ncand; k2++) if ((int)strlen(cand[k2]) + 2 > w) w = strlen(cand[k2]) + 2;
                cols = 78 / w;
                if (cols < 1) cols = 1;
                rows = (ncand + cols - 1) / cols;
                out("\n", 1);
                for (r = 0; r < rows; r++) {
                    for (k2 = r; k2 < ncand; k2 += rows) {
                        int l = strlen(cand[k2]);
                        out(cand[k2], l);
                        if (k2 + rows < ncand) rep(' ', w - l);
                    }
                    out("\n", 1);
                }
                out(prompt, strlen(prompt));
                out(buf, len);
                rep('\b', len - pos);
                tabbed = 0;
                continue;
            }
            out("\007", 1);
            tabbed = 1;
            continue;
        }
        case 12:                                                           /* ^L */
            out("\n", 1); out(prompt, strlen(prompt)); out(buf, len); rep('\b', len - pos);
            continue;
        case 16: case 14:                                                  /* ^P up, ^N down: the history */
            if (c == 16 ? h == 0 : h >= nhist) continue;
            if (h == nhist) { memcpy(saved, buf, len); saved[len] = 0; }
            h += c == 16 ? -1 : 1;
            nl = h == nhist ? saved : hist[h];
            k = strlen(nl);
            if (k > size - 2) k = size - 2;
            rep('\b', pos);
            memcpy(buf, nl, k);
            out(buf, k);
            if (len > k) { rep(' ', len - k); rep('\b', len - k); }
            len = pos = k;
            continue;
        default:
            if (c < ' ' || c > '~' || len >= size - 2) continue;
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = c;
            len++;
            out(buf + pos, len - pos); rep('\b', len - pos - 1);
            pos++;
            continue;
        }
        break;
    }
    signal(SIGINT, SIG_IGN);
    stty(0, &tsaved);
    out("\n", 1);
    if (len < 0) return 0;
    buf[len++] = '\n';
    buf[len] = 0;
    return len;
}

/* $NAME ${NAME} $? $$ at s: copy the value to *w; return what follows */
static char *dollar(char *s, char **w)
{
    char name[64], num[16], *v;
    int k = 0;
    s++;
    if (*s == '?' || *s == '$' || *s == '!' || *s == '#') {
        sprintf(num, "%d", *s == '?' ? status : *s == '$' ? getpid() : *s == '!' ? lastbg : posc > 0 ? posc - 1 : 0);
        v = num;
        s++;
    } else if (*s >= '0' && *s <= '9') {
        int k2 = *s++ - '0';
        v = k2 < posc ? posv[k2] : "";
    } else if (*s == '*') {
        int k2;
        s++;
        for (k2 = 1; k2 < posc; k2++) {
            char *a = posv[k2];
            if (k2 > 1) *(*w)++ = ' ';
            while (*a) *(*w)++ = *a++;
        }
        return s;
    } else {
        int brace = *s == '{';
        if (brace) s++;
        while ((*s == '_' || (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (k && *s >= '0' && *s <= '9')) && k < 63)
            name[k++] = *s++;
        name[k] = 0;
        if (brace && *s == '}') s++;
        if (!k) { *(*w)++ = '$'; return s; }
        v = getenv(name);
    }
    if (v) while (*v) *(*w)++ = *v++;
    return s;
}

static void run_text(char *text);

/* `command` at s: run it, copy its output (newlines -> spaces, the last
   dropped) to *w; return what follows the closing backquote */
static char *backquote(char *s, char **w, char *limit)
{
    char cmd[512], buf[256];
    int k = 0, fd[2], pid, child, n, st;
    s++;
    while (*s && *s != '`' && k < (int)sizeof cmd - 1) cmd[k++] = *s++;
    cmd[k] = 0;
    if (*s == '`') s++;
    if (pipe(fd) < 0) return s;
    fflush(NULL);
    if ((child = fork()) == 0) {
        dup2(fd[1], 1);
        close(fd[0]); close(fd[1]);
        run_text(cmd);
        fflush(stdout);
        _exit(status);
    }
    close(fd[1]);
    {
        char *start = *w;
        while ((n = read(fd[0], buf, sizeof buf)) > 0) {
            int i;
            for (i = 0; i < n && *w < limit; i++) *(*w)++ = buf[i] == '\n' ? ' ' : buf[i];
        }
        while (*w > start && (*w)[-1] == ' ') (*w)--;
    }
    close(fd[0]);
    while ((pid = wait(&st)) >= 0 && pid != child) ;
    return s;
}

/* * ? [...] against one name */
static int gmatch(const char *p, const char *s)
{
    for (;; p++, s++) {
        if (*p == '*') {
            p++;
            do if (gmatch(p, s)) return 1; while (*s++);
            return 0;
        }
        if (!*p) return !*s;
        if (!*s) return 0;
        if (*p == '?') continue;
        if (*p == '[') {
            int hit = 0, neg = p[1] == '!' || p[1] == '^';
            if (neg) p++;
            while (*++p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') { if (*s >= *p && *s <= p[2]) hit = 1; p += 2; }
                else if (*s == *p) hit = 1;
            }
            if (hit == neg) return 0;
            continue;
        }
        if (*p != *s) return 0;
    }
}

static int cmpstr(const void *a, const void *b) { return strcmp(*(char **)a, *(char **)b); }

/* a wildcard word -> the sorted names it matches (in its directory), or itself */
/* the names matching pat, sorted, into out (at most room of them, their text
   at *space, below limit); none: pat itself. -1 if they do not all fit: the
   callers say so, rather than run with some of them */
static int glob(char *pat, char **out, int room, char **space, char *limit)
{
    char dir[256], *slash = strrchr(pat, '/'), *base;
    DIR *d;
    struct dirent *e;
    int n = 0;
    if (slash) {
        int k = slash - pat;
        if (k >= (int)sizeof dir) { out[0] = pat; return 1; }      /* (a directory name too long: no match) */
        memcpy(dir, pat, k); dir[k] = 0; if (!k) strcpy(dir, "/"); base = slash + 1;
    }
    else { strcpy(dir, "."); base = pat; }
    if ((d = opendir(dir)) != NULL) {
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' && base[0] != '.') continue;
            if (!gmatch(base, e->d_name)) continue;
            if (n >= room || *space + strlen(dir) + strlen(e->d_name) + 3 > limit) { closedir(d); return -1; }
            out[n] = *space;
            if (slash) *space += sprintf(*space, "%s/%s", strcmp(dir, "/") ? dir : "", e->d_name) + 1;
            else *space += sprintf(*space, "%s", e->d_name) + 1;
            n++;
        }
        closedir(d);
    }
    if (!n) { out[0] = pat; return 1; }
    qsort(out, n, sizeof *out, cmpstr);
    return n;
}

static void sherror(const char *what, const char *msg) { fprintf(stderr, "sh: %s: %s\n", what, msg); }

/* split the line into tokens: words (quotes removed) and the operators | < > >> ; & && || */
static int lex(char *s, char **tok, int *isop)
{
    int n = 0, bqsplit;
    char *w = words;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (!*s || *s == '#') break;
        if (n >= 2 * MAXW) { sherror("line", "too many words"); return -1; }
        if (strchr("|<>;&", *s) || (*s >= '0' && *s <= '9' && (s[1] == '<' || s[1] == '>'))) {
            tok[n] = w;
            if (*s >= '0' && *s <= '9') *w++ = *s++;            /* 2> 2>> 2>&1 0< */
            *w++ = *s;
            if ((*s == '>' || *s == '&' || *s == '|') && s[1] == *s) *w++ = *s++;   /* >> && || */
            else if (*s == '>' && s[1] == '&' && s[2] >= '0' && s[2] <= '9') {      /* >&N */
                *w++ = *++s;
                *w++ = *++s;
            }
            *w++ = 0;
            s++;
            isop[n++] = 1;
            continue;
        }
        tok[n] = w;
        isop[n] = 0;
        isglob[n] = 0;
        bqsplit = 0;
        if (*s == '(') {                        /* ( list ): one word, \001 and the list as written */
            int d = 0;
            char q = 0;
            *w++ = 1;
            for (s++; *s; s++) {
                if (q) { if (*s == q) q = 0; }
                else if (*s == '\'' || *s == '"' || *s == '`') q = *s;
                else if (*s == '(') d++;
                else if (*s == ')' && d-- == 0) break;
                if (w > words_end - 300) { sherror("line", "too long"); return -1; }
                *w++ = *s;
            }
            if (*s != ')') { sherror("syntax", "( without )"); return -1; }
            s++;
            *w++ = 0;
            n++;
            continue;
        }
        while (*s && !strchr(" \t\n|<>;&", *s)) {
            if (*s == '\'' || *s == '"') {
                char q = *s++;
                while (*s && *s != q) {
                    if (q == '"' && *s == '$') { s = dollar(s, &w); continue; }
                    if (q == '"' && *s == '`') { s = backquote(s, &w, words_end - 300); continue; }
                    if (q == '"' && *s == '\\' && s[1]) s++;
                    *w++ = *s++;
                }
                if (!*s) { sherror("line", "unmatched quote"); return -1; }
                s++;
            } else if (*s == '\\' && s[1]) {
                s++;
                *w++ = *s++;
            } else if (*s == '$') {
                s = dollar(s, &w);
                bqsplit = 1;                    /* unquoted: the value splits into words ($* too) */
            } else if (*s == '`') {
                s = backquote(s, &w, words_end - 300);
                bqsplit = 1;                    /* unquoted: its output splits into words */
            } else {
                if (*s == '*' || *s == '?' || *s == '[') isglob[n] = 1;
                *w++ = *s++;
            }
            if (w > words_end - 300) { sherror("line", "too long"); return -1; }
        }
        *w++ = 0;
        n++;
        if (bqsplit) {                          /* (an assignment's value is not split: x=`cmd` is one word) */
            char *p = tok[n - 1];
            if (*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                while (*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) p++;
                if (*p == '=') bqsplit = 0;
            }
        }
        if (bqsplit) {                          /* split the word at its spaces */
            char *p = tok[n - 1], *sp;
            while ((sp = strchr(p, ' ')) != NULL && n < 2 * MAXW) {
                *sp++ = 0;
                while (*sp == ' ') sp++;
                if (!*sp) break;
                tok[n] = sp;
                isop[n] = isglob[n] = 0;
                n++;
                p = sp;
            }
            if (!*tok[n - 1]) n--;                  /* an empty substitution: no word */
        }
    }
    tok[n] = 0;
    return n;
}

static struct fn *find_fn(const char *name)
{
    int k;
    for (k = 0; k < nfns; k++) if (!strcmp(fns[k].name, name)) return &fns[k];
    return 0;
}

static void run(struct node *nd);
static struct node *dup_tree(struct node *nd);

/* a function: $1 ... are its arguments for the time of the call */
static void call_fn(struct fn *f, int argc, char **argv)
{
    char **saved_v = posv, **args;
    int saved_c = posc, k;
    if (funcdepth > 50) { sherror(f->name, "functions nested too deeply"); status = 2; return; }
    args = malloc((argc + 1) * sizeof *args);
    if (!args) return;
    for (k = 0; k < argc; k++) args[k] = strdup(argv[k]);
    args[argc] = 0;
    posv = args;
    posc = argc;
    funcdepth++;
    status = 0;
    run(f->body);
    funcdepth--;
    returning = 0;
    posv = saved_v;
    posc = saved_c;
    for (k = 0; k < argc; k++) free(args[k]);
    free(args);
}

/* read NAME ...: a line from the standard input, a byte at a time (so no
   more is taken than the line); words to the names, the rest to the last */
static void do_read(int argc, char **argv)
{
    static char buf[1024];
    int n = 0, got, k;
    char ch, *p = buf;
    while ((got = read(0, &ch, 1)) == 1 && ch != '\n') if (n < (int)sizeof buf - 1) buf[n++] = ch;
    buf[n] = 0;
    status = got == 1 || n ? 0 : 1;
    for (k = 1; k < argc; k++) {
        char *start, *end;
        while (*p == ' ' || *p == '\t') p++;
        start = p;
        if (k == argc - 1) { end = p + strlen(p); while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--; }
        else { while (*p && *p != ' ' && *p != '\t') p++; end = p; }
        {
            char save = *end;
            *end = 0;
            setenv(argv[k], start, 1);
            *end = save;
        }
        if (*p && k < argc - 1) p++;
    }
}

static void run_pipeline(struct cmd *c, int nc, int bg)
{
    int i, pids[MAXC], prev = -1, fd[2];
    struct fn *f;
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "export")) {
        int k;                          /* variables are in the environment already: NAME=value sets one */
        for (k = 1; k < c[0].argc; k++) {
            char *eq = strchr(c[0].argv[k], '=');
            if (eq) { *eq = 0; setenv(c[0].argv[k], eq + 1, 1); }
        }
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "cd")) {
        char *dir = c[0].argc > 1 ? c[0].argv[1] : getenv("HOME");
        if (!dir) dir = "/";
        if (chdir(dir) < 0) { sherror(dir, strerror(errno)); status = 1; }
        else status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], ":") && !c[0].nr) { status = 0; return; }
    if (nc == 1 && c[0].argc && (!strcmp(c[0].argv[0], "break") || !strcmp(c[0].argv[0], "continue"))) {
        if (!loopdepth) { sherror(c[0].argv[0], "not in a loop"); status = 1; return; }
        if (c[0].argv[0][0] == 'b') breaking = 1; else continuing = 1;
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "unset")) {
        int k;
        for (k = 1; k < c[0].argc; k++) unsetenv(c[0].argv[k]);
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "exit"))
        exit(c[0].argc > 1 ? atoi(c[0].argv[1]) : status);
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "return")) {
        if (!funcdepth) { sherror("return", "not in a function"); status = 1; return; }
        if (c[0].argc > 1) status = atoi(c[0].argv[1]);
        returning = 1;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "shift")) {
        int k = c[0].argc > 1 ? atoi(c[0].argv[1]) : 1;
        if (k < 0 || k > posc - 1) { sherror("shift", "nothing to shift"); status = 1; return; }
        posv[k] = posv[0];              /* ($0 stays $0) */
        posv += k;
        posc -= k;
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "set")) {
        int k = 1;
        extern char **environ;
        if (c[0].argc == 1) {           /* set: the variables */
            char **e;
            for (e = environ; *e; e++) printf("%s\n", *e);
            status = 0;
            return;
        }
        for (; k < c[0].argc && (c[0].argv[k][0] == '-' || c[0].argv[k][0] == '+') && c[0].argv[k][1]; k++) {
            char *o = c[0].argv[k] + 1;
            if (!strcmp(c[0].argv[k], "--")) { k++; break; }
            for (; *o; o++)
                if (*o == 'e') opt_e = c[0].argv[k][0] == '-';
                else if (*o == 'x') opt_x = c[0].argv[k][0] == '-';
                else { sherror("set", "options: -e -x (+e +x to turn off), or -- args"); status = 2; return; }
        }
        if (k < c[0].argc || (k > 1 && !strcmp(c[0].argv[k - 1], "--"))) {    /* set [--] args: $1 ... */
            int j, n = c[0].argc - k;
            char **nv = malloc((n + 2) * sizeof *nv);
            if (!nv) return;
            nv[0] = strdup(posv[0]);
            for (j = 0; j < n; j++) nv[j + 1] = strdup(c[0].argv[k + j]);
            nv[n + 1] = 0;
            posv = setargs = nv;        /* (the old list is kept: a function may still use it) */
            posc = n + 1;
        }
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "read")) {     /* (read < file: the shell's input for a moment) */
        int k, saved = -1, ok = 1;
        for (k = 0; k < c[0].nr; k++)
            if (c[0].r[k].kind == '<' && c[0].r[k].fd == 0) {
                int f = open(c[0].r[k].file, O_RDONLY);
                if (f < 0) { sherror(c[0].r[k].file, strerror(errno)); status = 1; ok = 0; break; }
                if (saved < 0) saved = dup(0);
                dup2(f, 0);
                close(f);
            }
        if (ok) do_read(c[0].argc, c[0].argv);
        if (saved >= 0) { dup2(saved, 0); close(saved); }
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "umask")) {
        int m = umask(0);
        if (c[0].argc > 1) {
            char *p = c[0].argv[1];
            int v = 0;
            for (; *p >= '0' && *p <= '7'; p++) v = v * 8 + (*p - '0');
            if (*p || v > 0777) { umask(m); sherror("umask", "an octal mask, e.g. 022"); status = 2; return; }
            m = v;
        } else
            printf("%03o\n", m);
        umask(m);
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "eval")) {
        int k, len = 2;
        char *t;
        for (k = 1; k < c[0].argc; k++) len += strlen(c[0].argv[k]) + 1;
        if (!(t = malloc(len))) return;
        t[0] = 0;
        for (k = 1; k < c[0].argc; k++) { if (k > 1) strcat(t, " "); strcat(t, c[0].argv[k]); }
        strcat(t, "\n");
        status = 0;
        run_text(t);
        free(t);
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], ".")) {          /* . file: its commands, here */
        FILE *in;
        if (c[0].argc < 2) { sherror(".", "a file is needed"); status = 2; return; }
        if (!(in = fopen(c[0].argv[1], "r"))) { sherror(c[0].argv[1], strerror(errno)); status = 1; return; }
        run_input(in, 0);
        fclose(in);
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "wait")) {
        int st;
        while (wait(&st) >= 0) ;
        status = 0;
        return;
    }
    if (nc == 1 && c[0].argc && !strcmp(c[0].argv[0], "exec") && !bg) {
        if (c[0].argc < 2) { status = 0; return; }
        fflush(NULL);
        execvp(c[0].argv[1], c[0].argv + 1);
        sherror(c[0].argv[1], errno == ENOENT ? "not found" : strerror(errno));
        status = 127;
        return;
    }
    /* a function, alone and without redirections: in this shell (it may set variables) */
    if (nc == 1 && !bg && c[0].argc && !c[0].nr && !c[0].sub && (f = find_fn(c[0].argv[0])) != 0) {
        int k;
        for (k = 0; k < c[0].na; k++) {
            char *eq = strchr(c[0].assign[k], '=');
            *eq = 0;
            setenv(c[0].assign[k], eq + 1, 1);
            *eq = '=';
        }
        call_fn(f, c[0].argc, c[0].argv);
        return;
    }
    for (i = 0; i < nc; i++) {
        if (i < nc - 1 && pipe(fd) < 0) { sherror("pipe", strerror(errno)); return; }
        fflush(NULL);                   /* (or the child would write the buffer again) */
        pids[i] = fork();
        if (pids[i] < 0) { sherror("fork", strerror(errno)); return; }
        if (pids[i] == 0) {
            if (bg) { signal(SIGINT, SIG_IGN); signal(SIGQUIT, SIG_IGN); }
            else { signal(SIGINT, SIG_DFL); signal(SIGQUIT, SIG_DFL); }
            {
                int k;
                for (k = 0; k < ninfds; k++) if (infds[k] > 2) close(infds[k]);
            }
            if (prev >= 0) { dup2(prev, 0); close(prev); }
            if (i < nc - 1) { dup2(fd[1], 1); close(fd[1]); close(fd[0]); }
            {                           /* the redirections, left to right (2>&1 copies what 1 is now) */
                int k;
                for (k = 0; k < c[i].nr; k++) {
                    struct redir *rd = &c[i].r[k];
                    if (rd->kind == '&') {
                        if (dup2(rd->dupfd, rd->fd) < 0) { fprintf(stderr, "sh: %d: bad file descriptor\n", rd->dupfd); _exit(1); }
                        continue;
                    }
                    {
                        int f = rd->kind == '<' ? open(rd->file, O_RDONLY)
                              : open(rd->file, O_WRONLY | O_CREAT | (rd->kind == 'a' ? O_APPEND : O_TRUNC), 0666);
                        if (f < 0) { sherror(rd->file, strerror(errno)); _exit(1); }
                        if (f != rd->fd) { dup2(f, rd->fd); close(f); }
                    }
                }
            }
            {                           /* NAME=value cmd: for this command only */
                int k;
                for (k = 0; k < c[i].na; k++) {
                    char *eq = strchr(c[i].assign[k], '=');
                    *eq = 0;
                    setenv(c[i].assign[k], eq + 1, 1);
                }
            }
            if (!c[i].argc || !strcmp(c[i].argv[0], ":")) _exit(0);
            if (c[i].sub) {             /* ( list ): in this child */
                char *t = malloc(strlen(c[i].argv[0]) + 2);
                strcpy(t, c[i].argv[0] + 1);
                strcat(t, "\n");
                status = 0;
                run_text(t);
                fflush(NULL);
                _exit(status);
            }
            if (!strcmp(c[i].argv[0], "history")) { hist_list(); fflush(stdout); _exit(0); }
            if ((f = find_fn(c[i].argv[0])) != 0) {     /* a function in a pipeline: in this child */
                call_fn(f, c[i].argc, c[i].argv);
                fflush(NULL);
                _exit(status);
            }
            execvp(c[i].argv[0], c[i].argv);
            sherror(c[i].argv[0], errno == ENOENT ? "not found" : strerror(errno));
            _exit(127);
        }
        if (prev >= 0) close(prev);
        if (i < nc - 1) { close(fd[1]); prev = fd[0]; }
    }
    if (bg) { lastbg = pids[nc - 1]; printf("%d\n", pids[nc - 1]); return; }
    /* in whatever order they end: the last command may well end first (D87) */
    for (i = nc; i > 0; ) {
        int st, pid = wait(&st), k;
        if (pid < 0) break;
        for (k = 0; k < nc && pids[k] != pid; k++) ;
        if (k == nc) continue;          /* (a background job) */
        i--;
        if (k == nc - 1) {
            status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
            if (WIFSIGNALED(st) && WTERMSIG(st) != SIGINT && WTERMSIG(st) != SIGPIPE)
                fprintf(stderr, "%s\n", WTERMSIG(st) == SIGSEGV ? "memory fault" :
                        WTERMSIG(st) == SIGKILL ? "killed" : "terminated by a signal");
        }
    }
}

#define GLOBSPACE 16384

static int is_assign(const char *t)     /* NAME=... */
{
    const char *p = t;
    if (!(*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) return 0;
    while (*p == '_' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) p++;
    return *p == '=';
}

static void execute(char **tok, int *isop, int n)
{
    struct cmd *c;
    int nc = 0, i = 0;
    char *globspace = malloc(GLOBSPACE), **t2 = malloc((2 * MAXW + 1) * sizeof *t2);
    int *o2 = malloc((2 * MAXW + 1) * sizeof *o2);
    c = malloc(MAXC * sizeof *c);
    if (!globspace || !t2 || !o2 || !c) { sherror("memory", "no room"); free(globspace); free(t2); free(o2); free(c); return; }
    {   /* wildcards: expand the marked words in place */
        char *sp = globspace;
        int m = 0, k;
        for (k = 0; k < n && m < 2 * MAXW; k++) {
            if (!isop[k] && isglob[k]) {
                int got = glob(tok[k], t2 + m, 2 * MAXW - m, &sp, globspace + GLOBSPACE), j;
                if (got < 0) {
                    sherror(tok[k], "too many names");
                    status = 2;
                    free(globspace); free(t2); free(o2); free(c);
                    return;
                }
                for (j = 0; j < got; j++) o2[m + j] = 0;
                m += got;
            } else {
                t2[m] = tok[k];
                o2[m++] = isop[k];
            }
        }
        tok = t2;
        isop = o2;
        n = m;
    }
    /* NAME=value ... alone: set variables */
    {
        int k, all = n > 0;
        for (k = 0; k < n && all; k++) {
            char *eq = isop[k] ? 0 : strchr(tok[k], '=');
            if (!eq || eq == tok[k]) all = 0;
        }
        if (all) {
            for (k = 0; k < n; k++) {
                char *eq = strchr(tok[k], '=');
                *eq = 0;
                setenv(tok[k], eq + 1, 1);
            }
            status = 0;
            goto done;
        }
    }
    {
    int skip = 0;                       /* && / ||: this pipeline is not run */
    while (i < n) {
        int bg = 0, andor = 0;
        memset(c, 0, MAXC * sizeof *c);
        nc = 1;
        for (; i < n; i++) {
            struct cmd *cur = &c[nc - 1];
            if (!isop[i]) {
                if (!cur->argc && !cur->sub && cur->na < 16 && is_assign(tok[i])) { cur->assign[cur->na++] = tok[i]; continue; }
                if (!cur->argc && tok[i][0] == 1) cur->sub = 1;
                if (cur->argc < MAXW - 1) cur->argv[cur->argc++] = tok[i];
                else cur->toomany = 1;
                continue;
            }
            if (tok[i][1] == tok[i][0] && (tok[i][0] == '&' || tok[i][0] == '|')) {
                andor = tok[i][0];      /* && or ||: ends this pipeline */
                i++;
                break;
            }
            if (tok[i][0] == '|') {
                if (nc == MAXC) { sherror("pipeline", "too long"); goto done; }
                nc++;
            } else if (tok[i][0] == '<' || tok[i][0] == '>' || (tok[i][0] >= '0' && tok[i][0] <= '9')) {
                char *t = tok[i];
                struct redir *rd;
                if (cur->nr == MAXR) { sherror("syntax", "too many redirections"); goto done; }
                rd = &cur->r[cur->nr++];
                rd->fd = *t >= '0' && *t <= '9' ? *t++ - '0' : *t == '<' ? 0 : 1;
                if (t[0] == '>' && t[1] == '&') { rd->kind = '&'; rd->dupfd = t[2] - '0'; continue; }
                rd->kind = t[0] == '<' ? '<' : t[1] == '>' ? 'a' : '>';
                if (i + 1 >= n || isop[i + 1]) { sherror("syntax", "redirection needs a file"); goto done; }
                rd->file = tok[++i];
            } else {                    /* ; or & ends the list item */
                bg = tok[i][0] == '&';
                i++;
                break;
            }
        }
        {
            int k, many = 0;
            for (k = 0; k < nc; k++) many |= c[k].toomany;
            if (many) { fprintf(stderr, "sh: more than %d arguments\n", MAXW - 1); status = 2; skip = 1; }
        }
        if (!skip && (nc > 1 || c[0].argc || c[0].nr || c[0].na))
            run_pipeline(c, nc, bg);
        if (returning || breaking || continuing) break;
        /* a && b runs b if a succeeded, a || b if it failed; a skipped
           pipeline leaves the status as it was (so a && b || c works) */
        if (andor) conddepth++;         /* (set -e: what && / || tests may fail) */
        skip = andor == '&' ? status != 0 : andor == '|' ? status == 0 : 0;
        if (andor) conddepth--;
        if (andor && i >= n) break;
    }
    }
done:
    free(globspace);
    free(t2);
    free(o2);
    free(c);
}

/* ---------------------------------------------------------------- control flow */
enum { CMD, IF, THEN, ELIF, ELSE, FI, WHILE, DO, DONE, FOR, CASE, ESAC, LBRACE, RBRACE, DSEMI, FUNC };
struct item { int kind; char *text; };
#define NBRANCH 32
struct node {
    int kind;                           /* CMD IF WHILE FOR CASE FUNC (a definition) LBRACE (a group) */
    char *text;                         /* CMD: the command; FOR / CASE: the header; FUNC: the name */
    struct node *next;
    struct node *cond[NBRANCH], *body[NBRANCH];   /* IF: the branches; WHILE: cond[0] body[0]; CASE: body[k] */
    char *pats[NBRANCH];                /* CASE: each branch's patterns (a|b) */
    int nbranch;
    struct node *els;
};

static const char *const kw[] = { "", "if", "then", "elif", "else", "fi", "while", "do", "done", "for",
                                  "case", "esac", "{", "}" };

/* split text into commands (on ; and newlines, outside quotes), each with
   its leading keyword split off */
static int split(char *text, struct item *it, int max)
{
    int n = 0, pend_dsemi = 0;
    char *s = text, *start;
    while (*s && n < max - 2) {
        char q = 0;
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == ';') s++;
        if (!*s) break;
        if (*s == '#') { while (*s && *s != '\n') s++; continue; }
        start = s;
        {
            int paren = 0, dsemi = 0;
            for (; *s; s++) {
                if (q) { if (*s == q) q = 0; continue; }
                if (*s == '\\' && s[1]) { s++; continue; }
                if (*s == '\'' || *s == '"' || *s == '`') { q = *s; continue; }
                if (*s == '(') paren++;
                else if (*s == ')' && paren) paren--;
                if (paren) continue;            /* (a ( list ) is one command) */
                if (*s == ';' && s[1] == ';') { dsemi = 1; break; }
                if (*s == ';' || *s == '\n') break;
            }
            if (*s) *s++ = 0;
            if (dsemi) s++;             /* ;; ends a case branch: the command (below), then the mark */
            pend_dsemi = dsemi;
            while (*start == ' ' || *start == '\t') start++;
            if (!*start) {
                if (pend_dsemi && n < max - 2) { it[n].kind = DSEMI; it[n++].text = 0; }
                continue;
            }
        }
        for (;;) {                      /* a keyword, then maybe the rest of the command */
            int k, len = 0, kind = CMD;
            while (start[len] && start[len] != ' ' && start[len] != '\t') len++;
            for (k = 1; k <= RBRACE; k++)
                if ((int)strlen(kw[k]) == len && !strncmp(start, kw[k], len)) kind = k;
            if (kind == CMD) {          /* name() [{ ...]: a function's definition */
                char *p = start;
                while (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (p > start && *p >= '0' && *p <= '9')) p++;
                if (p > start && p - start < 32) {
                    char *r = p;
                    while (*r == ' ' || *r == '\t') r++;
                    if (r[0] == '(' && r[1] == ')') {
                        *p = 0;
                        it[n].kind = FUNC;
                        it[n++].text = start;
                        start = r + 2;
                        while (*start == ' ' || *start == '\t') start++;
                        if (!*start || n >= max - 2) break;
                        continue;
                    }
                }
            }
            if (kind == CASE) {         /* case WORD in: the header, then maybe a branch */
                char *in = start + len, *e;
                for (e = in; *e; e++)
                    if ((e[0] == ' ' || e[0] == '\t') && e[1] == 'i' && e[2] == 'n' && (!e[3] || e[3] == ' ' || e[3] == '\t')) break;
                it[n].kind = CASE;
                it[n++].text = start;
                if (!*e) break;
                e += 3;
                if (*e) *e++ = 0;
                start = e;
                while (*start == ' ' || *start == '\t') start++;
                if (!*start || n >= max - 2) break;
                continue;
            }
            if (kind == CMD || kind == FOR) {
                it[n].kind = kind;
                it[n++].text = start;
                break;
            }
            it[n].kind = kind;
            it[n++].text = 0;
            start += len;
            while (*start == ' ' || *start == '\t') start++;
            if (!*start || n >= max - 2) break;
        }
        if (pend_dsemi && n < max - 2) { it[n].kind = DSEMI; it[n++].text = 0; }
        pend_dsemi = 0;
    }
    return n;
}

/* how far the items leave a construct open (0: complete) */
static int depth(struct item *it, int n)
{
    int d = 0, k;
    for (k = 0; k < n; k++) {
        if (it[k].kind == IF || it[k].kind == WHILE || it[k].kind == FOR || it[k].kind == CASE || it[k].kind == LBRACE) d++;
        if (it[k].kind == FI || it[k].kind == DONE || it[k].kind == ESAC || it[k].kind == RBRACE) d--;
    }
    return d;
}

static struct item *items;
static int nitems, pos, synerr;

static struct node *parse(int stop1, int stop2, int stop3)
{
    struct node *head = 0, **tail = &head;
    while (pos < nitems) {
        struct item *t = &items[pos];
        struct node *nd;
        if (t->kind == stop1 || t->kind == stop2 || t->kind == stop3) return head;
        nd = calloc(1, sizeof *nd);
        nd->kind = t->kind;
        nd->text = t->text;
        pos++;
        if (t->kind == IF) {
            int b = 0;
            for (;;) {
                nd->cond[b] = parse(THEN, -1, -1);
                if (pos >= nitems || items[pos].kind != THEN) { synerr = 1; return head; }
                pos++;
                nd->body[b++] = parse(ELIF, ELSE, FI);
                if (pos >= nitems) { synerr = 1; return head; }
                if (items[pos].kind == ELIF && b < NBRANCH) { pos++; continue; }
                if (items[pos].kind == ELSE) { pos++; nd->els = parse(FI, -1, -1); }
                if (pos >= nitems || items[pos].kind != FI) { synerr = 1; return head; }
                pos++;
                break;
            }
            nd->nbranch = b;
        } else if (t->kind == WHILE || t->kind == FOR) {
            if (t->kind == WHILE) nd->cond[0] = parse(DO, -1, -1);
            if (pos >= nitems || items[pos].kind != DO) { synerr = 1; return head; }
            pos++;
            nd->body[0] = parse(DONE, -1, -1);
            if (pos >= nitems || items[pos].kind != DONE) { synerr = 1; return head; }
            pos++;
        } else if (t->kind == LBRACE) {         /* { list }: a group, in this shell */
            nd->body[0] = parse(RBRACE, -1, -1);
            if (pos >= nitems || items[pos].kind != RBRACE) { synerr = 1; return head; }
            pos++;
        } else if (t->kind == FUNC) {           /* name() { list } */
            if (pos >= nitems || items[pos].kind != LBRACE) { synerr = 1; return head; }
            pos++;
            nd->body[0] = parse(RBRACE, -1, -1);
            if (pos >= nitems || items[pos].kind != RBRACE) { synerr = 1; return head; }
            pos++;
        } else if (t->kind == CASE) {           /* case WORD in pat) list ;; ... esac */
            int b = 0;
            while (pos < nitems && items[pos].kind != ESAC) {
                struct item *pi = &items[pos];
                char *pt, *e, q = 0;
                struct node *first = 0;
                if (pi->kind == DSEMI) { pos++; continue; }
                if (pi->kind != CMD || b == NBRANCH) { synerr = 1; return head; }
                pt = pi->text;
                while (*pt == ' ' || *pt == '\t' || *pt == '(') pt++;
                for (e = pt; *e; e++) {
                    if (q) { if (*e == q) q = 0; continue; }
                    if (*e == '\'' || *e == '"') { q = *e; continue; }
                    if (*e == ')') break;
                }
                if (*e != ')') { synerr = 1; return head; }
                *e++ = 0;
                nd->pats[b] = pt;
                while (*e == ' ' || *e == '\t') e++;
                pos++;
                if (*e) {               /* a command after pat) on the same line */
                    first = calloc(1, sizeof *first);
                    first->kind = CMD;
                    first->text = e;
                }
                {
                    struct node *rest = parse(DSEMI, ESAC, -1);
                    if (first) { first->next = rest; nd->body[b] = first; }
                    else nd->body[b] = rest;
                }
                b++;
                if (pos < nitems && items[pos].kind == DSEMI) pos++;
            }
            if (pos >= nitems) { synerr = 1; return head; }
            pos++;
            nd->nbranch = b;
        } else if (t->kind != CMD) {
            synerr = 1;
            return head;
        }
        *tail = nd;
        tail = &nd->next;
    }
    return head;
}

#define WORDS 16384

/* one command line, with buffers of its own (a function it calls runs
   commands of its own before this one is done) */
static void run_command(char *text)
{
    char *copy, **tok, *saved = words, *saved_end = words_end;
    int *isop, n;
    if (opt_x) fprintf(stderr, "+ %s\n", text);
    copy = strdup(text);
    tok = malloc((2 * MAXW + 1) * sizeof *tok);
    isop = malloc((2 * MAXW + 1) * sizeof *isop);
    words = malloc(WORDS);
    if (!copy || !tok || !isop || !words) { sherror("memory", "no room"); status = 2; }
    else {
        words_end = words + WORDS;
        if ((n = lex(copy, tok, isop)) > 0) execute(tok, isop, n);
    }
    free(copy);
    free(tok);
    free(isop);
    free(words);
    words = saved;
    words_end = saved_end;
}

/* set -e: a failed command outside a condition ends the shell */
static void check_e(void) { if (opt_e && status && !conddepth) exit(status); }

static void run(struct node *nd)
{
    for (; nd && !breaking && !continuing && !returning; nd = nd->next) {
        if (nd->kind == CMD) { run_command(nd->text); check_e(); }
        else if (nd->kind == FUNC) {    /* a definition: kept (the tree is not freed with the text) */
            struct fn *f = find_fn(nd->text);
            if (!f && nfns < 32) f = &fns[nfns++];
            if (!f) { sherror(nd->text, "too many functions"); status = 2; continue; }
            strcpy(f->name, nd->text);
            f->body = dup_tree(nd->body[0]);    /* (a copy: the text it came from is reused; an old body stays, a call may be running it) */
            status = 0;
        } else if (nd->kind == LBRACE) run(nd->body[0]);
        else if (nd->kind == CASE) {
            char hdr[1024], *tok[2 * MAXW + 1], *val, *saved = words, *saved_end = words_end;
            int isop[2 * MAXW + 1], n, b, done = 0;
            strncpy(hdr, nd->text, sizeof hdr - 1);
            hdr[sizeof hdr - 1] = 0;
            words = malloc(WORDS);
            if (!words) { words = saved; continue; }
            words_end = words + WORDS;
            {                           /* the word between case and in: expanded, not split */
                char *wd = hdr + 4, *e2;
                while (*wd == ' ' || *wd == '\t') wd++;
                e2 = wd + strlen(wd);
                if (e2 - wd >= 2 && e2[-1] == 'n' && e2[-2] == 'i') e2 -= 2;
                while (e2 > wd && (e2[-1] == ' ' || e2[-1] == '\t')) e2--;
                *e2 = 0;
                n = lex(wd, tok, isop);
                {
                    int k2, len2 = 1;
                    for (k2 = 0; k2 < n; k2++) len2 += strlen(tok[k2]) + 1;
                    val = malloc(len2);
                    val[0] = 0;
                    for (k2 = 0; k2 < n; k2++) { if (k2) strcat(val, " "); strcat(val, tok[k2]); }
                }
            }
            status = 0;
            for (b = 0; b < nd->nbranch && !done; b++) {
                char pat[512], *p, *e;
                strncpy(pat, nd->pats[b], sizeof pat - 1);
                pat[sizeof pat - 1] = 0;
                for (p = pat; p && !done; p = e) {      /* a|b|c */
                    char *ptok[8];
                    int pisop[8], pn;
                    e = strchr(p, '|');
                    if (e) *e++ = 0;
                    pn = lex(p, ptok, pisop);   /* (quotes and $ in the pattern) */
                    if (pn >= 1 && gmatch(ptok[0], val)) { done = 1; run(nd->body[b]); }
                    else if (pn == 0 && !*val) { done = 1; run(nd->body[b]); }
                }
            }
            free(val);
            free(words);
            words = saved;
            words_end = saved_end;
        } else if (nd->kind == IF) {
            int b, done = 0;
            for (b = 0; b < nd->nbranch && !done; b++) {
                conddepth++;
                run(nd->cond[b]);
                conddepth--;
                if (status == 0) { run(nd->body[b]); done = 1; }
            }
            if (!done) { if (nd->els) run(nd->els); else status = 0; }
        } else if (nd->kind == WHILE) {
            loopdepth++;
            for (;;) {
                conddepth++;
                run(nd->cond[0]);
                conddepth--;
                if (status) { status = 0; break; }
                run(nd->body[0]);
                continuing = 0;
                if (breaking) { breaking = 0; break; }
            }
            loopdepth--;
        } else if (nd->kind == FOR) {
            /* for NAME in words: the words expanded once, now */
            static char hdr[1024];
            char *tok[2 * MAXW + 1], *vals[2 * MAXW + 1], name[64];
            int isop[2 * MAXW + 1], n, k, nv = 0, g;
            strncpy(hdr, nd->text, sizeof hdr - 1);
            {
                char *saved = words, *saved_end = words_end;
                words = malloc(WORDS);
                if (!words) { words = saved; continue; }
                words_end = words + WORDS;
                n = lex(hdr, tok, isop);
                if (n < 3 || strcmp(tok[2], "in")) {
                    sherror("for", "for NAME in words");
                    status = 2;
                    free(words); words = saved; words_end = saved_end;
                    continue;
                }
                strncpy(name, tok[1], sizeof name - 1);
                name[sizeof name - 1] = 0;
                {   /* (wildcards in the list) */
                    char *space = malloc(GLOBSPACE), *sp = space;
                    if (!space) { free(words); words = saved; words_end = saved_end; continue; }
                    for (k = 3; k < n && nv >= 0 && nv < 2 * MAXW; k++)
                        if (!isglob[k]) vals[nv++] = tok[k];
                        else if ((g = glob(tok[k], vals + nv, 2 * MAXW - nv, &sp, space + GLOBSPACE)) >= 0) nv += g;
                        else { sherror(tok[k], "too many names"); status = 2; nv = -1; }
                    for (k = 0; k < nv; k++) vals[k] = strdup(vals[k]);
                    free(space);
                }
                free(words);
                words = saved;
                words_end = saved_end;
            }
            if (0) {
            }
            loopdepth++;
            for (k = 0; k < nv; k++) {
                setenv(name, vals[k], 1);
                run(nd->body[0]);
                continuing = 0;
                if (breaking) { breaking = 0; break; }
            }
            loopdepth--;
            for (k = 0; k < nv; k++) free(vals[k]);
        }
    }
}

static struct node *dup_tree(struct node *nd)       /* (with copies of the strings) */
{
    struct node *head = 0, **tail = &head;
    for (; nd; nd = nd->next) {
        struct node *c = malloc(sizeof *c);
        int b;
        if (!c) break;
        *c = *nd;
        c->next = 0;
        if (nd->text) c->text = strdup(nd->text);
        for (b = 0; b < NBRANCH; b++) {
            c->cond[b] = dup_tree(nd->cond[b]);
            c->body[b] = dup_tree(nd->body[b]);
            c->pats[b] = nd->pats[b] ? strdup(nd->pats[b]) : 0;
        }
        c->els = dup_tree(nd->els);
        *tail = c;
        tail = &c->next;
    }
    return head;
}

static void freenodes(struct node *nd)
{
    while (nd) {
        struct node *next = nd->next;
        int b;
        for (b = 0; b < NBRANCH; b++) { freenodes(nd->cond[b]); freenodes(nd->body[b]); }
        freenodes(nd->els);
        free(nd);
        nd = next;
    }
}

/* run a complete piece of text (one line, or a construct over several) */
static void run_text(char *text)
{
    struct item it[256];
    struct node *tree;
    items = it;
    nitems = split(text, it, 256);
    pos = 0;
    synerr = 0;
    tree = parse(-1, -1, -1);
    if (synerr || pos < nitems) { sherror("syntax", "unexpected keyword"); status = 2; }
    else run(tree);
    freenodes(tree);
}

/* <<WORD in a line: the lines after it, up to WORD alone, go to a
   temporary file and the line reads < file instead (<<-WORD: leading
   tabs dropped; a quoted 'WORD': no $ or ` substitutions, else done now) */
static char *heredocs[16];
static int nheredocs, heredoc_seq;

static int get_line(FILE *in, int tty, const char *prompt, char *buf, int size)
{
    if (editing && in == stdin && tty) {
        fflush(stdout);
        return edit_line(prompt, buf, size) > 0;
    }
    if (tty) { fputs(prompt, stdout); fflush(stdout); }
    return fgets(buf, size, in) != 0;
}

static int take_heredocs(char *ln, int size, FILE *in, int tty)
{
    char *p = ln, q = 0;
    while (*p) {
        if (q) { if (*p == q) q = 0; p++; continue; }
        if (*p == '\'' || *p == '"' || *p == '`') { q = *p++; continue; }
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (p[0] == '<' && p[1] == '<') {
            char word[64], name[40], rest[1024], doc[1024];
            int strip = 0, quoted = 0, k = 0, fd;
            char *e = p + 2;
            if (*e == '-') { strip = 1; e++; }
            while (*e == ' ' || *e == '\t') e++;
            while (*e && *e != ' ' && *e != '\t' && *e != '\n' && *e != ';' && *e != '|' && *e != '&' && *e != '<' && *e != '>' && k < 63) {
                if (*e == '\'' || *e == '"' || *e == '\\') { quoted = 1; e++; continue; }
                word[k++] = *e++;
            }
            word[k] = 0;
            if (!k) { sherror("syntax", "<< needs a word"); return -1; }
            sprintf(name, "/tmp/sh%d.%d", (int)getpid(), heredoc_seq++);
            if ((fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) { sherror(name, strerror(errno)); return -1; }
            if (nheredocs < 16) heredocs[nheredocs++] = strdup(name);
            for (;;) {                  /* the document's lines */
                char *d = doc, *saved = words, *saved_end = words_end;
                int n;
                if (!get_line(in, tty, "> ", doc, sizeof doc)) break;
                if (strip) while (*d == '\t') d++;
                n = strlen(d);
                if (n && d[n - 1] == '\n') n--;
                if (n == k && !strncmp(d, word, k)) break;
                if (quoted) { write(fd, d, strlen(d)); continue; }
                {                       /* $ and ` substitutions, now */
                    char *buf = malloc(WORDS), *w = buf, *s2 = d;
                    if (!buf) break;
                    words = buf;
                    words_end = buf + WORDS;
                    while (*s2 && w < words_end - 300) {
                        if (*s2 == '\\' && (s2[1] == '$' || s2[1] == '`' || s2[1] == '\\')) { s2++; *w++ = *s2++; }
                        else if (*s2 == '$') s2 = dollar(s2, &w);
                        else if (*s2 == '`') s2 = backquote(s2, &w, words_end - 300);
                        else *w++ = *s2++;
                    }
                    write(fd, buf, w - buf);
                    free(buf);
                    words = saved;
                    words_end = saved_end;
                }
            }
            close(fd);
            /* the line: << WORD becomes < file */
            strncpy(rest, e, sizeof rest - 1);
            rest[sizeof rest - 1] = 0;
            if ((p - ln) + strlen(name) + 3 + strlen(rest) >= (size_t)size) { sherror("line", "too long"); return -1; }
            sprintf(p, "< %s%s", name, rest);
            p += 2 + strlen(name);
            continue;
        }
        p++;
    }
    return 0;
}

static void drop_heredocs(void)
{
    while (nheredocs > 0) { unlink(heredocs[--nheredocs]); free(heredocs[nheredocs]); }
}

/* commands from in until its end (a terminal, a script, a . file, a profile) */
static int run_input(FILE *in, int tty)
{
    char *text = malloc(4096), *scratch = malloc(4096), *ln = malloc(1024);
    struct item *it = malloc(256 * sizeof *it);
    int len = 0, k;
    if (!text || !scratch || !ln || !it) { sherror("memory", "no room"); return 2; }
    if (in != stdin && ninfds < 8) infds[ninfds++] = fileno(in);
    for (;;) {
        const char *prompt = len ? "> " : geteuid() ? "$ " : "# ";
        if (editing && in == stdin && tty) {
            fflush(stdout);
            if (!edit_line(prompt, ln, 1024)) break;
            k = hist_expand(ln, 1024);
            if (k < 0) { len = 0; continue; }
            if (k > 0) fputs(ln, stdout);           /* (what !! became) */
            hist_add(ln, 1);
        } else if (!get_line(in, tty, prompt, ln, 1024))
            break;
        if (strstr(ln, "<<") && take_heredocs(ln, 1024, in, tty) < 0) { len = 0; drop_heredocs(); continue; }
        if (len + (int)strlen(ln) >= 4096 - 1) { sherror("input", "too long"); len = 0; continue; }
        strcpy(text + len, ln);
        len += strlen(ln);
        strcpy(scratch, text);          /* (split works in place) */
        if (depth(it, split(scratch, it, 256)) > 0) continue;   /* a construct is still open */
        run_text(text);
        drop_heredocs();
        len = 0;
    }
    if (len) sherror("syntax", "end of input inside a construct");
    if (in != stdin && ninfds > 0) ninfds--;
    free(text); free(scratch); free(ln); free(it);
    return status;
}

static void profile(const char *path)   /* a login shell: /etc/profile, $HOME/.profile */
{
    FILE *f = fopen(path, "r");
    if (f) { run_input(f, 0); fclose(f); }
}

int main(int argc, char **argv)
{
    int tty = isatty(0);
    FILE *in = stdin;
    posv = argv;
    posc = argc;
    if (argc > 2 && !strcmp(argv[1], "-c")) {     /* sh -c 'commands' [args]: run them and finish */
        static char cmd[4096];
        if (strlen(argv[2]) >= sizeof cmd - 2) { sherror("-c", "too long"); return 2; }
        strcpy(cmd, argv[2]);
        strcat(cmd, "\n");
        posv = argv + 2;
        posc = argc - 2;
        run_text(cmd);
        return status;
    }
    if (argc > 1) {
        if (!(in = fopen(argv[1], "r"))) { sherror(argv[1], strerror(errno)); return 1; }
        posv = argv + 1;
        posc = argc - 1;
        tty = 0;
    }
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    if (argv[0][0] == '-') {            /* a login shell (login runs -sh) */
        char path[300], *home = getenv("HOME");
        profile("/etc/profile");
        if (home && strlen(home) < 280) { sprintf(path, "%s/.profile", home); profile(path); }
    }
    editing = tty && in == stdin && gtty(0, &tsaved) == 0;
    if (editing) hist_load();
    run_input(in, tty);
    if (tty) putchar('\n');
    return status;
}
