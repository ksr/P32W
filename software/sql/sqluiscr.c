/* sqluiscr.c -- sqlui's terminal and screen (see sqlui.c and sqlui(1)).
 *
 * The screen is a VT100's: cursor addressing, erase to the end of the line,
 * reverse and bold; there is no curses. What each line shows is kept, and
 * only lines that change are sent again (the terminal may be on a slow
 * serial line); output is collected and written when a key is read. The
 * terminal is put in CBREAK mode without echo, so ^C still interrupts, and
 * it is put back on every way out: Q, ^C, SIGTERM, SIGHUP, an error.
 * Also here: the line editor, and making SQL text.
 */
#include <signal.h>
#include <errno.h>
#include "sqlui.h"
#ifdef __P32__
#include <sgtty.h>
static struct sgttyb tsave;
#else
#include <termios.h>                    /* (the host, for testing) */
static struct termios tsave;
#endif

sqldb *db;
char *dbname;
int R = 24, C = 80;
char msg[256];
static int ttyset;
static char **shadow;                   /* what each line of the screen shows */
static int helpkey;                     /* edit returns ? on an empty line */

/* ---------------------------------------------------------------- the terminal */

static char ob[2048];
static int on;

static void oflush(void)
{
    if (on) write(1, ob, on);
    on = 0;
}

static void oputc(int c)
{
    if (on >= (int)sizeof ob) oflush();
    ob[on++] = c;
}

static void oputs(const char *s)
{
    while (*s) oputc(*s++);
}

void gorc(int r, int c)
{
    char b[24];
    sprintf(b, "\033[%d;%dH", r + 1, c + 1);
    oputs(b);
}

static void ttyraw(void)
{
#ifdef __P32__
    struct sgttyb t;
    gtty(0, &tsave);
    memcpy(&t, &tsave, sizeof t);
    t.sg_flags = CBREAK;                /* no ECHO, no CRMOD */
    stty(0, &t);
#else
    struct termios t;
    tcgetattr(0, &tsave);
    memcpy(&t, &tsave, sizeof t);
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
#endif
    ttyset = 1;
}

static void ttyrestore(void)
{
    if (!ttyset) return;
#ifdef __P32__
    stty(0, &tsave);
#else
    tcsetattr(0, TCSANOW, &tsave);
#endif
    ttyset = 0;
}

static void onsig(int sig)
{
    msg[0] = 0;
    done(1);
}

void *xmalloc(int n)
{
    void *p = malloc(n);
    if (!p) {
        strcpy(msg, "out of memory");
        done(2);
    }
    return p;
}

char *xstrdup(const char *s)
{
    return strcpy(xmalloc(strlen(s) + 1), s);
}

/* keys: an escape sequence (ESC [ x, ESC [ n ~, ESC O x) is one key; an
   ESC with nothing after it in the same read is the Esc key */
static unsigned char ib[64];
static int ip, il;

static int rawget(void)
{
    if (ip >= il) {
        oflush();
        do il = read(0, ib, sizeof ib); while (il < 0 && errno == EINTR);
        if (il <= 0) done(0);
        ip = 0;
    }
    return ib[ip++];
}

int getkey(void)
{
    int c = rawget(), n;
    if (c != ESC || ip >= il || (ib[ip] != '[' && ib[ip] != 'O')) return c;
    ip++;
    c = rawget();
    if (c >= '0' && c <= '9') {
        for (n = 0; c >= '0' && c <= '9'; c = rawget()) n = n * 10 + c - '0';
        if (c != '~') return 0;
        return n == 1 || n == 7 ? K_HOME : n == 4 || n == 8 ? K_END : n == 5 ? K_PGUP :
               n == 6 ? K_PGDN : n == 3 ? K_DEL : 0;
    }
    return c == 'A' ? K_UP : c == 'B' ? K_DOWN : c == 'C' ? K_RIGHT : c == 'D' ? K_LEFT :
           c == 'H' ? K_HOME : c == 'F' ? K_END : 0;
}

/* ---------------------------------------------------------------- the screen */

/* show line r as s (with A_ bytes), if it does not already */
static void putline(int r, const char *s)
{
    int attr = 0;
    const char *p;
    if (shadow[r] && !strcmp(shadow[r], s)) return;
    gorc(r, 0);
    for (p = s; *p; p++)
        if (*p == A_REV) oputs("\033[7m"), attr = 1;
        else if (*p == A_BOLD) oputs("\033[1m"), attr = 1;
        else if (*p == A_OFF) oputs("\033[0m");
        else oputc(*p);
    if (attr) oputs("\033[0m");
    oputs("\033[K");
    free(shadow[r]);
    shadow[r] = xstrdup(s);
}

void forget(int r)               /* line r was written to directly */
{
    free(shadow[r]);
    shadow[r] = 0;
}

void clearscr(void)
{
    int r;
    oputs("\033[0m\033[H\033[2J");
    for (r = 0; r < R; r++) {
        free(shadow[r]);
        shadow[r] = xstrdup("");
    }
}

/* a line is made in lb: lv is its width so far (at most C - 1, so that the
   last column is never written and nothing scrolls) */
static char lb[1024];
static int ll;
int lv;

void lbeg(void)
{
    ll = lv = 0;
    lb[0] = 0;
}

void lattr(int a)
{
    lb[ll++] = a;
    lb[ll] = 0;
}

static int printable(int c)
{
    c &= 255;
    return c < 32 || c > 126 ? '.' : c;
}

void lstr(const char *s, int n)  /* at most n characters (all if n < 0) */
{
    for (; *s && n-- != 0 && lv < C - 1; lv++) lb[ll++] = printable(*s++);
    lb[ll] = 0;
}

void lpad(int x)
{
    for (; lv < x && lv < C - 1; lv++) lb[ll++] = ' ';
    lb[ll] = 0;
}

void lend(int r)
{
    putline(r, lb);
}

/* the status line: database | screen | message or information */
void status(const char *scr, const char *info)
{
    lbeg();
    lattr(A_REV);
    lstr(" ", 1);
    lstr(dbname, -1);
    lstr(" | ", -1);
    lstr(scr, -1);
    if (msg[0] || (info && info[0])) {
        lstr(" | ", -1);
        lstr(msg[0] ? msg : info, -1);
    }
    lpad(C - 1);
    lend(R - 2);
}

void keys(const char *s)
{
    lbeg();
    lstr(s, -1);
    lend(R - 1);
}

void error(const char *s)
{
    snprintf(msg, sizeof msg, "%s", s);
    oputc(7);
}

void done(int status)
{
    if (shadow) {
        oputs("\033[0m\033[H\033[2J");
        oflush();
    }
    ttyrestore();
    if (status && msg[0]) fprintf(stderr, "sqlui: %s\n", msg);
    if (db && status == 0) sql_close(db);
    exit(status);
}

/* ---------------------------------------------------------------- editing a line */

/* edit s (at most max bytes) in a field of the screen at row r, column x,
   w wide; *pos is the cursor. *nul (if nul) says the value is NULL: it shows
   NULL and the first character typed replaces it. The keys of the editor
   are used here; any other key ends editing and is returned. */
int edit(char *s, int max, int r, int x, int w, int *pos, int *nul)
{
    int p = *pos, off = 0, n, k, i, redraw = 1;
    forget(r);
    for (;;) {
        n = strlen(s);
        if (p > n) p = n;
        if (p < 0) p = 0;
        if (p < off) off = p, redraw = 1;
        if (p >= off + w) off = p - w + 1, redraw = 1;
        if (redraw) {
            gorc(r, x);
            i = 0;
            if (nul && *nul) oputs("\033[1mNULL\033[0m"), i = 4;
            else
                for (; i < w && off + i < n; i++) oputc(printable(s[off + i]));
            for (; i < w; i++) oputc(' ');
            redraw = 0;
        }
        gorc(r, x + p - off);
        k = getkey();
        if (k == '?' && !n && helpkey) {
            *pos = p;
            return k;
        }
        if (k >= 32 && k < 127) {
            if (nul && *nul) {
                *nul = 0;
                s[0] = 0;
                n = p = 0;
                redraw = 1;
            }
            if (n < max) {
                memmove(s + p + 1, s + p, n - p + 1);
                s[p] = k;
                if (p == n && p - off < w - 1 && !redraw) oputc(k);
                else redraw = 1;
                p++;
            }
        } else if (k == 8 || k == 127) {
            if (p > 0 && !(nul && *nul)) {
                memmove(s + p - 1, s + p, n - p + 1);
                p--;
                redraw = 1;
            }
        } else if (k == K_DEL || k == CTL('D')) {
            if (p < n) memmove(s + p, s + p + 1, n - p);
            redraw = 1;
        } else if (k == K_LEFT) p--;
        else if (k == K_RIGHT) p++;
        else if (k == K_HOME || k == CTL('A')) p = 0;
        else if (k == K_END || k == CTL('E')) p = n;
        else if (k == CTL('U')) {
            s[0] = 0;
            if (nul) *nul = 0;
            redraw = 1;
        } else if (k == CTL('K')) {
            s[p] = 0;
            redraw = 1;
        } else {
            *pos = p;
            return k;
        }
    }
}

/* a line typed on the bottom line after label; returns the key that ended
   it (Enter, Esc, or with keep, also Up and Down) */
int lineedit(const char *label, char *s, int max, int keep)
{
    int k, pos = strlen(s), x = strlen(label);
    for (;;) {
        gorc(R - 1, 0);
        oputs("\033[1m");
        oputs(label);
        oputs("\033[0m");
        helpkey = keep;
        k = edit(s, max, R - 1, x, C - 1 - x, &pos, 0);
        helpkey = 0;
        if (k == '\r' || k == '\n') return '\r';
        if (k == ESC || k == CTL('C')) return ESC;
        if (keep && (k == K_UP || k == K_DOWN || k == '?')) return k;
    }
}

int ask(const char *q)           /* 1 if the answer is y */
{
    int k;
    keys(q);
    gorc(R - 1, strlen(q) < (unsigned)C - 1 ? strlen(q) : C - 2);
    k = getkey();
    return k == 'y' || k == 'Y';
}

/* ---------------------------------------------------------------- SQL text */

void add(struct buf *b, const char *s, int n)
{
    if (n < 0) n = strlen(s);
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        if (!(b->s = realloc(b->s, b->cap))) done(2);
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

void addname(struct buf *b, const char *s)       /* "name" */
{
    add(b, "\"", 1);
    add(b, s, -1);
    add(b, "\"", 1);
}

static int isnum(const char *s)
{
    if (*s == '-' || *s == '+') s++;
    if (!*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

/* a value: NULL, a number (for an INTEGER column) or 'text' with '' */
void addval(struct buf *b, const char *s, int nul, int isint)
{
    if (nul) add(b, "NULL", 4);
    else if (isint && isnum(s)) add(b, s, -1);
    else {
        add(b, "'", 1);
        for (; *s; s++) {
            if (*s == '\'') add(b, "'", 1);
            add(b, s, 1);
        }
        add(b, "'", 1);
    }
}

void firstword(const char *s, char *w)
{
    int n = 0;
    while (*s == ' ' || *s == '\t' || *s == '(') s++;
    for (; n < 10 && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')); s++)
        w[n++] = *s >= 'a' ? *s - 32 : *s;
    w[n] = 0;
}

int isquery(const char *s)
{
    char w[12];
    firstword(s, w);
    return !strcmp(w, "SELECT") || !strcmp(w, "SHOW") || !strcmp(w, "DESCRIBE") ||
           !strcmp(w, "DESC") || !strcmp(w, "EXPLAIN");
}

/* the integer a query gives (its first value); -1 (and msg) on an error */
int count(const char *q)
{
    sqlstmt *st = sql_query(db, q);
    int n = -1;
    if (st && sql_next(st) > 0) n = sql_int(st, 0);
    if (n < 0) error(sql_errmsg(db));
    if (st) sql_finish(st);
    return n;
}

/* ---------------------------------------------------------------- views of text */

/* lines to look at (help, a table's description) */
void view(const char *title, char **ln, int n)
{
    int top = 0, k, i, h = R - 2;
    char info[40];
    for (;;) {
        for (i = 0; i < h; i++) {
            lbeg();
            if (top + i < n) lstr(ln[top + i], -1);
            lend(i);
        }
        if (n > h) sprintf(info, "lines %d-%d of %d", top + 1, top + h < n ? top + h : n, n);
        else info[0] = 0;
        status(title, info);
        keys("Up Down PgUp PgDn scroll  q back");
        gorc(R - 1, 0);
        k = getkey();
        msg[0] = 0;
        if (k == K_DOWN || k == 'j') top++;
        else if (k == K_UP || k == 'k') top--;
        else if (k == K_PGDN || k == ' ' || k == CTL('F')) top += h;
        else if (k == K_PGUP || k == CTL('B')) top -= h;
        else if (k == K_HOME || k == 'g') top = 0;
        else if (k == K_END || k == 'G') top = n - h;
        else if (k == 'q' || k == 'Q' || k == ESC || k == '\r' || k == CTL('G')) return;
        if (top > n - h) top = n - h;
        if (top < 0) top = 0;
    }
}

/* the terminal's size (ttysize: the kernel's, or LINES and COLUMNS); its modes; the signals */
void scrinit(void)
{
    int i, r = 0, c = 0;
#ifdef __P32__
    ttysize(1, &c, &r);
#else
    if (getenv("LINES")) r = atoi(getenv("LINES"));    /* (the host, for testing) */
    if (getenv("COLUMNS")) c = atoi(getenv("COLUMNS"));
#endif
    if (r >= 8) R = r > 200 ? 200 : r;
    if (c >= 40) C = c > 250 ? 250 : c;
    shadow = xmalloc(R * sizeof *shadow);
    for (i = 0; i < R; i++) shadow[i] = 0;
    signal(SIGINT, onsig);
    signal(SIGQUIT, onsig);
    signal(SIGTERM, onsig);
    signal(SIGHUP, onsig);
    ttyraw();
}
