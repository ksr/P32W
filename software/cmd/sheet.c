/* sheet -- a spreadsheet in a window of the window manager: cells of numbers, of text and of formulas worked
 * out from other cells (sheet(1), D147). It is P8X's sheet brought here. There it is 8 columns by 12 rows of
 * 16-bit whole numbers, the machine having no others; here it is 26 columns by 99 rows of real numbers, and as
 * much of it shows as the window has room for.
 *
 *   sheet [file]
 *   sheet -p file        what the sheet comes to, printed: a row a line (no window)
 *
 * One cell is picked, and the last line shows what was typed into it. The arrows pick another, and so does a
 * press of the mouse. Typing begins a new entry for the cell, which Return puts in (and picks the cell below),
 * Tab puts in (and picks the one to the right) and ESC gives up. An entry is a number, or text, or, beginning
 * with =, a formula: numbers and cells (B7) with + - * / and brackets, and the functions SUM, AVG, MAX, MIN
 * and COUNT of a block of cells (A1:C9), and SQRT, ABS and INT of a formula. Backspace empties the cell, ^E
 * changes what is in it. The words on the top line are menus: File has New, Open, Save, Save as and Close;
 * Edit has Change, Empty, Copy and Paste (a formula pasted elsewhere has its cells moved as far as it was).
 *
 * A sheet's file is text, a line for each cell that is not empty: its name, a space, and what was typed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <gfx.h>

#define NC   26
#define NR   99
#define TOP  16                         /* the line at the top: the menus' words and the file's name */
#define FOOT 18                         /* the line at the foot, and the rule over it */
#define RW   4                          /* characters of a row's number, at the left */
#define CWD  10                         /* characters of a cell */
#define MAXE 78                         /* characters of an entry */

enum { STALE, EMPTY, TEXT, NUM, ERR, DOING };

static char *raw[NR][NC];               /* what was typed into each cell (0: nothing) */
static double val[NR][NC];              /* what it comes to, */
static char st[NR][NC];                 /* ... and how it stands: one of the above */
static int cr, cc, r0, c0;              /* the cell picked; the cell in the window's top-left corner */
static int rows, cols;                  /* the cells that show, down and across */
static char name[128], note[80], entry[MAXE + 2], *clip;
static int entering, clipr, clipc;      /* entering: an entry is being typed; where the copied cell was */
static int changed, resized;
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

/* ---------------------------------------------------------------- working a formula out */
static const char *fp;                  /* where in the formula */
static int ferr;
static double expr(void);
static double value(int r, int c);

static void blanks(void) { while (*fp == ' ') fp++; }

static int cellref(int *r, int *c)      /* a cell's name at fp: 1, and fp past it */
{
    const char *p = fp;
    int n = 0;
    if (!isalpha((unsigned char)*p)) return 0;
    *c = toupper((unsigned char)*p++) - 'A';
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) n = n * 10 + *p++ - '0';
    if (n < 1 || n > NR || *c >= NC) return 0;
    *r = n - 1;
    fp = p;
    return 1;
}

static double block(int f)              /* SUM, AVG, MAX, MIN or COUNT of the block of cells at fp: its numbers */
{
    int r1, c1, r2, c2, r, c, n = 0, t;
    double s = 0, m = 0, v;
    blanks();
    if (!cellref(&r1, &c1)) { ferr = 1; return 0; }
    r2 = r1; c2 = c1;
    blanks();
    if (*fp == ':') { fp++; blanks(); if (!cellref(&r2, &c2)) { ferr = 1; return 0; } }
    if (r2 < r1) { t = r1; r1 = r2; r2 = t; }
    if (c2 < c1) { t = c1; c1 = c2; c2 = t; }
    for (r = r1; r <= r2; r++)
        for (c = c1; c <= c2; c++) {
            v = value(r, c);
            if (st[r][c] == ERR) ferr = 1;
            if (st[r][c] != NUM) continue;
            if (!n || (f == 2 && v > m) || (f == 3 && v < m)) m = v;
            s += v; n++;
        }
    return f == 0 ? s : f == 1 ? (n ? s / n : 0) : f == 4 ? n : m;
}

static double primary(void)
{
    static const char *const fn[] = { "SUM", "AVG", "MAX", "MIN", "COUNT", "SQRT", "ABS", "INT" };
    double v;
    char *end, w[8];
    int r, c, k, n;
    blanks();
    if (*fp == '(') { fp++; v = expr(); blanks(); if (*fp == ')') fp++; else ferr = 1; return v; }
    if (*fp == '-') { fp++; return -primary(); }
    if (isdigit((unsigned char)*fp) || *fp == '.') {
        v = strtod(fp, &end);
        if (end == fp) { ferr = 1; return 0; }
        fp = end;
        return v;
    }
    if (cellref(&r, &c)) {
        v = value(r, c);
        if (st[r][c] == ERR) ferr = 1;
        return st[r][c] == NUM ? v : 0;         /* (an empty cell, or text, counts for nothing) */
    }
    for (n = 0; n < 7 && isalpha((unsigned char)fp[n]); n++) w[n] = toupper((unsigned char)fp[n]);
    w[n] = 0;
    for (k = 0; k < 8; k++) if (!strcmp(w, fn[k])) break;
    fp += n;
    blanks();
    if (k == 8 || *fp != '(') { ferr = 1; return 0; }
    fp++;
    v = k < 5 ? block(k) : expr();
    blanks();
    if (*fp == ')') fp++; else ferr = 1;
    if (k == 5) { if (v < 0) ferr = 1; else v = sqrt(v); }
    if (k == 6) v = fabs(v);
    if (k == 7) v = floor(v);
    return v;
}

static double term(void)
{
    double v = primary(), d;
    for (;;) {
        blanks();
        if (*fp == '*') { fp++; v *= primary(); }
        else if (*fp == '/') { fp++; d = primary(); if (d == 0) ferr = 1; else v /= d; }
        else return v;
    }
}

static double expr(void)
{
    double v = term();
    for (;;) {
        blanks();
        if (*fp == '+') { fp++; v += term(); }
        else if (*fp == '-') { fp++; v -= term(); }
        else return v;
    }
}

static double value(int r, int c)       /* what a cell comes to, worked out if it has not been since the last change */
{
    const char *s = raw[r][c], *was = fp;
    char *end;
    int e = ferr;
    if (st[r][c] == DOING) { st[r][c] = ERR; return 0; }        /* (it is worked out from itself) */
    if (st[r][c] != STALE) return val[r][c];
    val[r][c] = 0;
    if (!s || !*s) { st[r][c] = EMPTY; return 0; }
    if (*s == '=') {
        st[r][c] = DOING;
        fp = s + 1; ferr = 0;
        val[r][c] = expr();
        blanks();
        if (*fp || ferr || st[r][c] == ERR) { st[r][c] = ERR; val[r][c] = 0; } else st[r][c] = NUM;
        fp = was; ferr = e;
        return val[r][c];
    }
    val[r][c] = strtod(s, &end);
    while (*end == ' ') end++;
    if (end != s && !*end) st[r][c] = NUM; else { st[r][c] = TEXT; val[r][c] = 0; }
    return val[r][c];
}

static void stale(void) { memset(st, STALE, sizeof st); }

static void set(int r, int c, const char *s)   /* a cell's entry (empty: none) */
{
    char *t = 0;
    if (s && *s && !(t = malloc(strlen(s) + 1))) { strcpy(note, "no memory for more"); return; }
    if (t) strcpy(t, s);
    free(raw[r][c]);
    raw[r][c] = t;
    stale();
    changed = 1;
}

/* ---------------------------------------------------------------- the picture */
static int ry(int r) { return gheight - TOP - 16 * (r + 2); }   /* the bottom dot of the window's row r of cells */

static void head(void)
{
    char s[100];
    int room = (gwidth - 104) / 8 - 1, n = strlen(name);
    gink(G_WHITE); gfbox(0, gheight - TOP, gwidth - 1, gheight - 1);
    gink(G_BLACK); gtext(8, gheight - TOP, "File"); gtext(56, gheight - TOP, "Edit");
    if (room > 2) {
        if (room > 90) room = 90;
        sprintf(s, "%.*s%s", room - 2, !n ? "(no name)" : n > room - 2 ? name + n - (room - 2) : name, changed ? " *" : "");
        gtext(104, gheight - TOP, s);
    }
    gink(G_WHITE);
}

static void line(const char *t)
{
    char s[120];
    int k, room = (gwidth - 4) / 8;
    if (room > (int)sizeof s - 1) room = sizeof s - 1;
    for (k = 0; k < room && t[k]; k++) s[k] = t[k];
    for (; k < room; k++) s[k] = ' ';
    s[room] = 0;
    glook(G_OPAQUE); gtext(4, 0, s); glook(0);
    gline(0, FOOT - 1, gwidth - 1, FOOT - 1);
}

static void foot(void)                  /* the cell picked and what was typed into it, or the entry being typed */
{
    char s[200];
    if (entering) sprintf(s, "%c%d: %s_", 'A' + cc, cr + 1, entry);
    else sprintf(s, "%c%d: %.60s%s%s", 'A' + cc, cr + 1, raw[cr][cc] ? raw[cr][cc] : "", note[0] ? "   " : "", note);
    line(s);
    note[0] = 0;
}

static void shown(int r, int c, char *s)        /* how a cell shows: CWD characters, the last a space */
{
    char t[40];
    int n;
    memset(s, ' ', CWD); s[CWD] = 0;
    value(r, c);
    if (st[r][c] == EMPTY) return;
    if (st[r][c] == TEXT) { n = strlen(raw[r][c]); memcpy(s, raw[r][c], n < CWD - 1 ? n : CWD - 1); return; }
    if (st[r][c] == ERR) strcpy(t, "#ERR");
    else {
        sprintf(t, "%.8g", val[r][c]);
        if ((int)strlen(t) > CWD - 1) sprintf(t, "%.3g", val[r][c]);
        if ((int)strlen(t) > CWD - 1) strcpy(t, "########");
    }
    n = strlen(t);
    memcpy(s + CWD - 1 - n, t, n);      /* (numbers to the right) */
}

static void mark(void)                  /* the picked cell turned over, if it shows */
{
    int x, y;
    if (cr < r0 || cr >= r0 + rows || cc < c0 || cc >= c0 + cols) return;
    x = 8 * RW + 8 * CWD * (cc - c0); y = ry(cr - r0);
    gink(G_XOR); gfbox(x - 2, y, x + 8 * CWD - 4, y + 15); gink(G_WHITE);
}

static void grid(void)                  /* the columns' letters, and every row that shows */
{
    char s[130];
    int r, c;
    memset(s, ' ', RW + CWD * cols); s[RW + CWD * cols] = 0;
    for (c = 0; c < cols; c++) s[RW + CWD * c + CWD / 2 - 1] = 'A' + c0 + c;
    glook(G_OPAQUE | G_UNDER); gtext(0, gheight - TOP - 16, s);
    glook(G_OPAQUE);
    for (r = 0; r < rows; r++) {
        sprintf(s, "%*d ", RW - 1, r0 + r + 1);
        for (c = 0; c < cols; c++) shown(r0 + r, c0 + c, s + RW + CWD * c);
        gtext(0, ry(r), s);
    }
    glook(0);
    mark();
}

static int fit(void)
{
    cols = (gwidth - 8 * RW) / (8 * CWD); rows = (gheight - TOP - 16 - FOOT - 1) / 16;
    if (cols > NC) cols = NC;
    if (rows > NR) rows = NR;
    if (cols > 12) cols = 12;           /* (a row is one line of text: 124 characters at most) */
    return cols >= 2 && rows >= 2;
}

static void see(void)                   /* the window moved over the sheet so that the picked cell shows */
{
    if (cr < r0) r0 = cr;
    if (cr >= r0 + rows) r0 = cr - rows + 1;
    if (cc < c0) c0 = cc;
    if (cc >= c0 + cols) c0 = cc - cols + 1;
    if (r0 > NR - rows) r0 = NR - rows;
    if (c0 > NC - cols) c0 = NC - cols;
    if (r0 < 0) r0 = 0;
    if (c0 < 0) c0 = 0;
}

static void all(void) { resized = 0; see(); gclear(); head(); grid(); foot(); gflush(); }

/* ---------------------------------------------------------------- menus, and things asked for (as the finder's) */
static int menu(int x, char **item, int n)
{
    int w = 0, k, x0, y0, y1 = gheight - TOP - 1, kept, on = -1, mx, my, b, e;
    for (k = 0; k < n; k++) if ((int)strlen(item[k]) > w) w = strlen(item[k]);
    w = 8 * w + 16;
    x0 = x + w > gwidth ? gwidth - w : x;
    if (x0 < 0) x0 = 0;
    y0 = y1 - 16 * n - 3;
    kept = gkeep(x0, y0, x0 + w - 1, y1);
    gink(G_BLACK); gfbox(x0, y0, x0 + w - 1, y1);
    gink(G_WHITE); gbox(x0, y0, x0 + w - 1, y1);
    for (k = 0; k < n; k++) gtext(x0 + 8, y1 - 2 - 16 * (k + 1), item[k]);
    gflush();
    for (;;) {
        e = ginput(&mx, &my, &b, -1);
        if (e == G_SIZE) { resized = 1; on = -1; break; }
        if (e != G_MOUSE) { on = -1; break; }           /* (a key, or the end: nothing is chosen) */
        k = mx > x0 && mx < x0 + w - 1 && my < y1 - 1 && my >= y1 - 2 - 16 * n ? (y1 - 2 - my) / 16 : -1;
        if (k != on) {
            gink(G_XOR);
            if (on >= 0) gfbox(x0 + 2, y1 - 2 - 16 * (on + 1), x0 + w - 3, y1 - 3 - 16 * on);
            if (k >= 0) gfbox(x0 + 2, y1 - 2 - 16 * (k + 1), x0 + w - 3, y1 - 3 - 16 * k);
            gink(G_WHITE);
            on = k;
            gflush();
        }
        if (!(b & 1)) break;
    }
    if (kept >= 0) { gstamp(kept, x0, y0, G_COPY); gdrop(kept); gflush(); }
    else resized = 1;                   /* (no room to keep it: all drawn again) */
    return on;
}

static int ask(const char *q, char *s, int max)        /* asked for at the foot. Return: 1; ESC or the mouse: 0 */
{
    char t[200];
    int k, n = strlen(s), b;
    for (;;) {
        sprintf(t, "%s: %s_", q, s);
        line(t); gflush();
        k = ginput(0, 0, &b, -1);
        if (k == G_SIZE) { resized = 1; return 0; }
        if (k == G_MOUSE) { if (b & 1) return 0; continue; }
        if (k <= 0 || stop) return 0;
        if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }
        if (k == '\n' || k == '\r') return n > 0;
        if (k == 8 || k == 127) { if (n > 0) s[--n] = 0; continue; }
        if (k == 21) { s[n = 0] = 0; continue; }
        if (k > 32 && k < 127 && n < max) { s[n++] = k; s[n] = 0; }
    }
}

static int key1(const char *q)
{
    int k;
    line(q); gflush();
    do k = ginput(0, 0, 0, -1); while (k == G_MOUSE);
    if (k == G_SIZE) { resized = 1; return 0; }
    if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }
    return k > 0 ? k : 0;
}

/* ---------------------------------------------------------------- the sheet's file */
static void empty(void)
{
    int r, c;
    for (r = 0; r < NR; r++) for (c = 0; c < NC; c++) { free(raw[r][c]); raw[r][c] = 0; }
    stale();
    cr = cc = r0 = c0 = 0;
    changed = 0;
}

static void load(void)
{
    FILE *f = name[0] ? fopen(name, "r") : 0;
    char l[200], *p;
    int r, c, n = 0;
    empty();
    if (!f) { if (name[0]) sprintf(note, errno == ENOENT ? "a new sheet" : "cannot be read: %.40s", strerror(errno)); return; }
    while (fgets(l, sizeof l, f)) {
        l[strcspn(l, "\n")] = 0;
        fp = l;
        if (!cellref(&r, &c) || *fp != ' ') continue;   /* (a line that is no cell is passed over) */
        p = (char *)fp + 1;
        if (strlen(p) > MAXE) p[MAXE] = 0;
        set(r, c, p);
        n++;
    }
    fclose(f);
    changed = 0;
    sprintf(note, "%d cell%s", n, n == 1 ? "" : "s");
}

static int save(void)
{
    FILE *f = fopen(name, "w");
    int r, c, n = 0;
    if (!f) { sprintf(note, "%.40s: %.30s", name, strerror(errno)); return -1; }
    for (r = 0; r < NR; r++)
        for (c = 0; c < NC; c++)
            if (raw[r][c]) { fprintf(f, "%c%d %s\n", 'A' + c, r + 1, raw[r][c]); n++; }
    if (ferror(f) | fclose(f)) { sprintf(note, "%.40s: not all written", name); return -1; }
    changed = 0;
    sprintf(note, "%d cell%s written", n, n == 1 ? "" : "s");
    return 0;
}

static int writeout(int as)
{
    char s[128];
    if (as || !name[0]) {
        strcpy(s, name);
        if (!ask("Save as", s, 120)) return -1;
        strcpy(name, s);
    }
    return save();
}

/* ---------------------------------------------------------------- what is done to cells */
/* the copied entry put into the picked cell: a formula's cells moved as far as the entry is from where it was */
static void paste(void)
{
    char out[MAXE + 20], *o = out;
    const char *s = clip;
    int r, c, dr = cr - clipr, dc = cc - clipc;
    if (!clip) { strcpy(note, "nothing was copied"); return; }
    if (*s != '=') { set(cr, cc, s); return; }
    while (*s && o - out < MAXE) {
        fp = s;
        if (isalpha((unsigned char)*s) && (s == clip || !isalnum((unsigned char)s[-1])) && cellref(&r, &c) && !isalnum((unsigned char)*fp)) {
            r += dr; c += dc;
            if (r < 0 || r >= NR || c < 0 || c >= NC) { strcpy(note, "a cell of it would be off the sheet"); return; }
            o += sprintf(o, "%c%d", 'A' + c, r + 1);
            s = fp;
        } else *o++ = *s++;
    }
    *o = 0;
    set(cr, cc, out);
}

static void move(int dr, int dc)        /* another cell picked */
{
    cr += dr; cc += dc;
    if (cr < 0) cr = 0;
    if (cr >= NR) cr = NR - 1;
    if (cc < 0) cc = 0;
    if (cc >= NC) cc = NC - 1;
}

static void put(void)                   /* the entry being typed goes into the picked cell */
{
    int n = strlen(entry);
    while (n > 0 && entry[n - 1] == ' ') entry[--n] = 0;
    set(cr, cc, entry);
    entering = 0;
}

static int print(void)                  /* -p: the sheet's cells as they show, down to the last row and column used */
{
    char s[CWD + 1];
    int r, c, lr = -1, lc = -1;
    for (r = 0; r < NR; r++) for (c = 0; c < NC; c++) if (raw[r][c]) { if (r > lr) lr = r; if (c > lc) lc = c; }
    for (r = 0; r <= lr; r++) {
        for (c = 0; c <= lc; c++) { shown(r, c, s); fputs(s, stdout); }
        putchar('\n');
    }
    return 0;
}

int main(int argc, char **argv)
{
    static char *filem[] = { "New", "Open", "Save        ^O", "Save as", "Close       ^X" };
    static char *editm[] = { "Change      ^E", "Empty       Backspace", "Copy        ^Y", "Paste       ^P" };
    char s[128];
    int x, y, b, was = 0, k, c, n, or0, oc0, wc = -1, full;
    if (argc == 3 && !strcmp(argv[1], "-p")) {
        strncpy(name, argv[2], sizeof name - 1);
        if (access(name, 4) < 0) { perror(name); return 1; }
        load();
        return print();
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) { fprintf(stderr, "usage: sheet [file]\n       sheet -p file\n"); return 2; }
    if (!getenv("WM")) { fprintf(stderr, "sheet: not in a window of wm\n"); return 1; }
    if (argc > 1) strncpy(name, argv[1], sizeof name - 1);
    load();
    gsize(8 * (RW + CWD * 6), 16 * 14 + TOP + 16 + FOOT + 1);   /* (six columns, fourteen rows) */
    if (gopen() < 0 || gmopen() < 0) { perror("sheet"); return 1; }
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    if (!fit()) { gclose(); fprintf(stderr, "sheet: the window is too small\n"); return 1; }
    gmaysize(8 * (RW + CWD * 2), 16 * 2 + TOP + 16 + FOOT + 1, 8 * CWD, 16);
    all();
    while (!stop) {
        if (resized) { if (fit()) all(); else resized = 0; }
        k = ginput(&x, &y, &b, -1);
        if (k < 0) break;
        if (k == G_SIZE) { resized = 1; continue; }
        c = 0; full = 0; or0 = r0; oc0 = c0;
        if (k == G_MOUSE) {
            if (!(b & 1) || was) { was = b & 1; continue; }
            was = 1;
            if (y >= gheight - TOP) {
                if (entering) { put(); full = 1; }
                if (x >= 4 && x < 44) { k = menu(4, filem, 5); c = k < 0 ? 0 : 2000 + k; was = 0; }
                else if (x >= 52 && x < 92) { k = menu(52, editm, 4); c = k == 0 ? 5 : k == 1 ? 8 : k == 2 ? 25 : k == 3 ? 16 : 0; was = 0; }
            } else if (y >= FOOT && y < gheight - TOP - 16 && x >= 8 * RW) {
                int r = (gheight - TOP - 16 - 1 - y) / 16, cl = (x - 8 * RW) / (8 * CWD);
                if (r < rows && cl < cols) { if (entering) { put(); full = 1; } mark(); cr = r0 + r; cc = c0 + cl; mark(); }
            }
        } else if (k == 27) {           /* an arrow: ESC [ and a letter. ESC alone gives an entry up */
            if ((k = ginput(0, 0, 0, 5)) != '[') { if (entering) { entering = 0; } k = 0; }
            else k = ginput(0, 0, 0, 5);
            c = k == 'A' ? 1001 : k == 'B' ? 1002 : k == 'C' ? 1003 : k == 'D' ? 1004 : 0;
        } else if (entering) {
            n = strlen(entry);
            if (k == '\n' || k == '\r') c = 1002;
            else if (k == '\t') c = 1003;
            else if (k == 8 || k == 127) { if (n > 0) entry[n - 1] = 0; else entering = 0; }
            else if (k == 21) entry[0] = 0;
            else if (k >= 32 && k < 127 && n < MAXE) { entry[n] = k; entry[n + 1] = 0; }
        } else if (k == '\n' || k == '\r') c = 1002;
        else if (k == '\t') c = 1003;
        else if (k >= 32 && k < 127) { entry[0] = k; entry[1] = 0; entering = 1; }
        else c = k;
        if (c >= 1001 && c <= 1004) {   /* another cell: an entry being typed goes in first */
            if (entering) { put(); full = 1; }
            mark();
            move(c == 1001 ? -1 : c == 1002 ? 1 : 0, c == 1003 ? 1 : c == 1004 ? -1 : 0);
            mark();
        }
        switch (c) {
        case 5: strncpy(entry, raw[cr][cc] ? raw[cr][cc] : "", MAXE); entry[MAXE] = 0; entering = 1; break;     /* ^E */
        case 8: case 127: set(cr, cc, 0); full = 1; break;
        case 25:                                        /* ^Y: copy */
            free(clip);
            clip = 0;
            if (raw[cr][cc] && (clip = malloc(strlen(raw[cr][cc]) + 1)) != 0) { strcpy(clip, raw[cr][cc]); clipr = cr; clipc = cc; strcpy(note, "copied"); }
            break;
        case 16: paste(); full = 1; break;              /* ^P */
        case 15: case 2002: writeout(0); break;         /* ^O */
        case 2003: writeout(1); break;
        case 2000:
            if (changed && (k = key1("Lose what is on the sheet? y to do it")) != 'y' && k != 'Y') break;
            empty(); name[0] = 0; full = 1;
            break;
        case 2001:
            if (changed && (k = key1("Lose what is on the sheet? y to do it")) != 'y' && k != 'Y') break;
            s[0] = 0;
            if (ask("Open", s, 120)) { strcpy(name, s); load(); full = 1; }
            break;
        case 24: case 2004:                             /* ^X */
            if (changed) {
                k = key1("Save it before ending? y, n (ESC: go on)");
                if (k == 'y' || k == 'Y') { if (writeout(0) == 0) stop = 1; }
                else if (k == 'n' || k == 'N') stop = 1;
            } else stop = 1;
            break;
        }
        if (stop) break;
        if (resized) continue;
        see();
        if (full || r0 != or0 || c0 != oc0) grid();
        if (changed != wc) { head(); wc = changed; }
        foot();
        gflush();
    }
    gmclose();
    gclose();
    return 0;
}
