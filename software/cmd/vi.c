/* vi -- the screen editor (after M7; backlog). A classic vi for a VT100-style
 * terminal (the Mac's Terminal, nc, a VT100 on the hardware), in raw mode.
 *
 *   vi [file]
 *
 * Normal mode: counts; h j k l (and the arrow keys), w b e, 0 ^ $, G, gg,
 * H M L, ^F ^B ^D ^U, f t F T ; ,, % (brackets); x X r J ~; d c y with a
 * motion (or dd cc yy), D C Y, p P; i a I A o O R; u (undo the last
 * change), . (repeat it); /re ?re n N; ZZ; ^L redraws, ^G shows the file.
 * Ex (:): w [file], q, q!, wq, x, e[!] file, r file, N (a line), $,
 * [range]s/re/text/[g] (ranges: N,M . $ %), [range]d, !cmd.
 * Regular expressions: . * ^ $ and [...] classes; & in a replacement is the
 * match. Lines do not wrap: a long one scrolls sideways. LINES and COLUMNS
 * set the screen size (24 x 80). On a hangup, SIGTERM or end of input with
 * unwritten changes the buffer is saved in $HOME/vi.recover (/tmp/vi.recover
 * without HOME) and vi exits with status 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sgtty.h>
#include <sys/wait.h>

#define KEY_CTRL(c) ((c) & 31)
#define ESC 27
#define MAXLINE 4096

static char **L;                        /* the lines, without newlines */
static int nl, capl;
static int cy, cx;                      /* the cursor: line, byte */
static int top, leftcol;                /* the first line and column shown */
static int rows = 24, cols = 80;        /* the screen; rows - 1 hold text */
static char fname[256];
static int modified;
static char msg[256];
static char **shadow;                   /* what each screen row shows now */
static struct sgttyb saved_tty;
static int want_col = -1;               /* j / k keep this display column */

/* ---------------------------------------------------------------- the terminal */
static char obuf[4096];
static int olen;

static void oflush(void)
{
    int k = 0;
    while (k < olen) {
        int n = write(1, obuf + k, olen - k);
        if (n <= 0) break;
        k += n;
    }
    olen = 0;
}

static void oput(const char *s)
{
    while (*s) {
        if (olen == sizeof obuf) oflush();
        obuf[olen++] = *s++;
    }
}

static void goto_rc(int r, int c)
{
    char b[32];
    sprintf(b, "\033[%d;%dH", r + 1, c + 1);
    oput(b);
}

static void raw_mode(int on)
{
    if (on) {
        struct sgttyb t;
        gtty(0, &saved_tty);
        t = saved_tty;
        t.sg_flags = RAW;
        stty(0, &t);
    } else
        stty(0, &saved_tty);
}

/* input: replayed keys (for .) first, then the terminal */
static unsigned char ibuf[256];
static int ipos, ilen;
static char *replay;
static int rpos, rlen;
static char rec[MAXLINE];               /* the keys of the change being made (for .) */
static int recording, nrec;

static int getkey(void)
{
    int c;
    if (replay && rpos < rlen) c = (unsigned char)replay[rpos++];
    else {
        if (ipos >= ilen) {
            oflush();
            ilen = read(0, ibuf, sizeof ibuf);
            ipos = 0;
            if (ilen <= 0) { ilen = 0; return -1; }
        }
        c = ibuf[ipos++];
        if (c == ESC && ipos + 1 < ilen && ibuf[ipos] == '[') {
            int a = ibuf[ipos + 1];             /* an arrow key: ESC [ A..D in one read */
            if (a >= 'A' && a <= 'D') {
                ipos += 2;
                c = 0x100 + a;
            }
        }
    }
    if (recording && nrec < MAXLINE - 1 && c < 0x100) rec[nrec++] = c;
    return c;
}

/* ---------------------------------------------------------------- the buffer */
static void *xmalloc(int n)
{
    void *p = malloc(n);
    if (!p) { raw_mode(0); fprintf(stderr, "vi: out of memory\n"); exit(1); }
    return p;
}

static char *dupn(const char *s, int n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void insert_line(int at, char *s)
{
    if (nl == capl) {
        char **n;
        capl = capl ? capl * 2 : 64;
        n = xmalloc(sizeof *n * capl);
        if (nl) memcpy(n, L, sizeof *n * nl);
        free(L);
        L = n;
    }
    memmove(L + at + 1, L + at, sizeof *L * (nl - at));
    L[at] = s;
    nl++;
}

static void delete_line(int at)
{
    free(L[at]);
    memmove(L + at, L + at + 1, sizeof *L * (nl - at - 1));
    nl--;
}

static int load(const char *path)       /* lines read, or -1 */
{
    int fd = open(path, O_RDONLY), n, k = 0;
    char buf[1024], line[MAXLINE];
    int len = 0;
    if (fd < 0) return -1;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') { insert_line(nl, dupn(line, len)); len = 0; k++; continue; }
            if (len < MAXLINE - 1) line[len++] = buf[i];
        }
    }
    if (len) { insert_line(nl, dupn(line, len)); k++; }
    close(fd);
    return k;
}

static int save(const char *path, int a, int b)    /* lines a..b-1; bytes written, or -1 */
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666), i, total = 0;
    if (fd < 0) return -1;
    for (i = a; i < b; i++) {
        int n = strlen(L[i]);
        if (write(fd, L[i], n) != n || write(fd, "\n", 1) != 1) { close(fd); return -1; }
        total += n + 1;
    }
    close(fd);
    return total;
}

/* ---------------------------------------------------------------- undo: the last change */
static char **u_old;                    /* the lines the change replaced */
static int u_start, u_nold, u_nnew, u_cy, u_cx, u_valid;
static int c_start, c_nold;             /* the change being made */

static void begin_change(int a, int n)  /* lines a..a+n-1 are about to change */
{
    int i;
    if (u_old) {
        for (i = 0; i < u_nold; i++) free(u_old[i]);
        free(u_old);
    }
    u_old = xmalloc(sizeof(char *) * (n ? n : 1));
    for (i = 0; i < n; i++) u_old[i] = dupn(L[a + i], strlen(L[a + i]));
    u_nold = n;
    c_start = a;
    c_nold = n;
    u_cy = cy;
    u_cx = cx;
    modified = 1;
}

static void end_change(int nnew)        /* ... and became nnew lines */
{
    u_start = c_start;
    u_nnew = nnew;
    u_valid = 1;
}

static void undo(void)
{
    char **now;
    int i, n;
    if (!u_valid) { strcpy(msg, "Nothing to undo"); return; }
    n = u_nnew;
    now = xmalloc(sizeof(char *) * (n ? n : 1));
    for (i = 0; i < n; i++) now[i] = L[u_start + i];
    memmove(L + u_start, L + u_start + n, sizeof *L * (nl - u_start - n));
    nl -= n;
    for (i = 0; i < u_nold; i++) insert_line(u_start + i, u_old[i]);
    free(u_old);
    u_old = now;                        /* (so u undoes the undo) */
    u_nnew = u_nold;
    u_nold = n;
    {
        int y = cy, x = cx;
        cy = u_cy; cx = u_cx;
        u_cy = y; u_cx = x;
    }
    if (!nl) insert_line(0, dupn("", 0));
    modified = 1;
}

/* ---------------------------------------------------------------- the screen */
static int dcol(const char *s, int x)   /* display column of byte x (tabs: 8) */
{
    int c = 0, i;
    for (i = 0; i < x && s[i]; i++) c = s[i] == '\t' ? (c / 8 + 1) * 8 : c + 1;
    return c;
}

static int byte_at(const char *s, int col)  /* the byte shown at display column col */
{
    int c = 0, i;
    for (i = 0; s[i]; i++) {
        int w = s[i] == '\t' ? (c / 8 + 1) * 8 - c : 1;
        if (c + w > col) return i;
        c += w;
    }
    return i;
}

static void row_text(int line, char *out)   /* what row shows for a line (cols chars at most) */
{
    const char *s = L[line];
    int c = 0, k = 0, i;
    for (i = 0; s[i] && c < leftcol + cols; i++) {
        int w = s[i] == '\t' ? (c / 8 + 1) * 8 - c : 1, j;
        for (j = 0; j < w; j++, c++)
            if (c >= leftcol && c < leftcol + cols) {
                int ch = (unsigned char)s[i];
                out[k++] = s[i] == '\t' ? ' ' : (ch < 32 || ch == 127) ? '?' : ch;
            }
    }
    out[k] = 0;
}

static void clamp(void)
{
    int len;
    if (cy >= nl) cy = nl - 1;
    if (cy < 0) cy = 0;
    len = strlen(L[cy]);
    if (cx > len - 1) cx = len - 1;
    if (cx < 0) cx = 0;
}

static void redraw_all(void)
{
    int r;
    oput("\033[H\033[2J");
    for (r = 0; r < rows; r++) { free(shadow[r]); shadow[r] = dupn("", 0); }
}

static void refresh(int insert_mode)
{
    int r, c;
    char line[MAXLINE];
    if (cy < top) top = cy;
    if (cy >= top + rows - 1) top = cy - (rows - 2);
    c = dcol(L[cy], cx);
    if (c < leftcol) leftcol = c;
    if (c >= leftcol + cols) leftcol = c - cols + 1;
    for (r = 0; r < rows - 1; r++) {
        if (top + r < nl) row_text(top + r, line);
        else strcpy(line, "~");
        if (strcmp(line, shadow[r])) {
            goto_rc(r, 0);
            oput(line);
            oput("\033[K");
            free(shadow[r]);
            shadow[r] = dupn(line, strlen(line));
        }
    }
    {   /* the last row: a message */
        char status[300];
        sprintf(status, "%.*s", cols - 1, msg[0] ? msg : insert_mode ? "" : "");
        if (strcmp(status, shadow[rows - 1])) {
            goto_rc(rows - 1, 0);
            oput(status);
            oput("\033[K");
            free(shadow[rows - 1]);
            shadow[rows - 1] = dupn(status, strlen(status));
        }
    }
    goto_rc(cy - top, c - leftcol);
    oflush();
}

/* ---------------------------------------------------------------- regular expressions */
/* . * ^ $ [...]; the Kernighan-Pike matcher */
static int match_here(const char *re, const char *s, const char **end);

static int match_class(const char **rep, int c)
{
    const char *re = *rep + 1;
    int neg = 0, ok = 0;
    if (*re == '^') { neg = 1; re++; }
    if (*re == ']') { ok |= c == ']'; re++; }
    while (*re && *re != ']') {
        if (re[1] == '-' && re[2] && re[2] != ']') { ok |= c >= re[0] && c <= re[2]; re += 3; }
        else { ok |= c == *re; re++; }
    }
    *rep = *re ? re + 1 : re;
    return c && (ok ^ neg);
}

static int match1(const char *re, int c, const char **next)   /* one atom against c */
{
    if (*re == '[') { const char *r = re; int m = match_class(&r, c); *next = r; return m; }
    if (*re == '\\' && re[1]) { *next = re + 2; return c == re[1]; }
    *next = re + 1;
    return c && (*re == '.' || *re == c);
}

static int match_here(const char *re, const char *s, const char **end)
{
    const char *next;
    if (!*re) { *end = s; return 1; }
    if (*re == '$' && !re[1]) { if (!*s) { *end = s; return 1; } return 0; }
    {
        int m = match1(re, (unsigned char)*s, &next);
        if (*next == '*') {
            const char *t = s;          /* as many as possible, then back off */
            while (*t && match1(re, (unsigned char)*t, &next)) t++;
            for (; t >= s; t--) if (match_here(next + 1, t, end)) return 1;
            return 0;
        }
        if (m) return match_here(next, s + 1, end);
    }
    return 0;
}

/* the first match in s at or after from: its start, *len; -1 if none */
static int re_find(const char *re, const char *s, int from, int *len)
{
    const char *end;
    int i, n = strlen(s);
    if (re[0] == '^') {
        if (from == 0 && match_here(re + 1, s, &end)) { *len = end - s; return 0; }
        return -1;
    }
    for (i = from; i <= n; i++)
        if (match_here(re, s + i, &end)) { *len = end - (s + i); return i; }
    return -1;
}

static int re_find_last(const char *re, const char *s, int before, int *len)
{
    int i, best = -1, l;
    for (i = 0; i < before; i++) {
        int k = re_find(re, s, i, &l);
        if (k < 0 || k >= before) break;
        best = k;
        *len = l;
        i = k;
    }
    return best;
}

/* ---------------------------------------------------------------- motions */
static int cls(int c) { return c == ' ' || c == '\t' ? 0 : (c == '_' || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z')) ? 1 : 2; }

static void word_fwd(int *y, int *x)
{
    const char *s = L[*y];
    int c = s[*x] ? cls(s[*x]) : 0;
    if (s[*x]) while (s[*x] && cls(s[*x]) == c && c) (*x)++;
    for (;;) {
        s = L[*y];
        while (s[*x] == ' ' || s[*x] == '\t') (*x)++;
        if (s[*x] || *y == nl - 1) return;
        (*y)++;
        *x = 0;
        if (!L[*y][0]) return;
    }
}

static void word_back(int *y, int *x)
{
    for (;;) {
        const char *s = L[*y];
        if (*x == 0) { if (*y == 0) return; (*y)--; *x = strlen(L[*y]); if (!*x) return; continue; }
        (*x)--;
        while (*x > 0 && (s[*x] == ' ' || s[*x] == '\t')) (*x)--;
        if (s[*x] == ' ' || s[*x] == '\t') continue;
        {
            int c = cls(s[*x]);
            while (*x > 0 && cls(s[*x - 1]) == c) (*x)--;
        }
        return;
    }
}

static void word_end(int *y, int *x)
{
    const char *s;
    (*x)++;
    for (;;) {
        s = L[*y];
        while (s[*x] == ' ' || s[*x] == '\t') (*x)++;
        if (s[*x]) break;
        if (*y == nl - 1) { *x = strlen(s) ? strlen(s) - 1 : 0; return; }
        (*y)++;
        *x = 0;
    }
    {
        int c = cls(s[*x]);
        while (s[*x + 1] && cls(s[*x + 1]) == c) (*x)++;
    }
}

static int first_nonblank(int y)
{
    int x = 0;
    while (L[y][x] == ' ' || L[y][x] == '\t') x++;
    return x;
}

static char last_find, last_findch;

static int find_char(int how, int ch, int count, int *x)   /* f t F T */
{
    const char *s = L[cy];
    int n = strlen(s), pos = cx;
    if (how == 't') pos++;              /* (so t; does not stick before the same char) */
    if (how == 'T') pos--;
    while (count-- > 0) {
        if (how == 'f' || how == 't') {
            for (pos++; pos < n && s[pos] != ch; pos++) ;
            if (pos >= n) return 0;
        } else {
            for (pos--; pos >= 0 && s[pos] != ch; pos--) ;
            if (pos < 0) return 0;
        }
    }
    *x = how == 't' ? pos - 1 : how == 'T' ? pos + 1 : pos;
    return 1;
}

static int match_bracket(int *y, int *x)
{
    const char *pairs = "()[]{}";
    const char *s = L[*y], *p = 0;
    int i = *x, depth = 0, dir, yy = *y, xx;
    while (s[i] && !(p = strchr(pairs, s[i]))) i++;
    if (!s[i] || !p) return 0;
    dir = (p - pairs) % 2 ? -1 : 1;
    {
        char open = dir > 0 ? *p : p[-1], close = dir > 0 ? p[1] : *p;
        xx = i;
        for (;;) {
            char c = L[yy][xx];
            if (c == open) depth++;
            if (c == close) depth--;
            if (depth == 0) { *y = yy; *x = xx; return 1; }
            xx += dir;
            while (xx < 0 || !L[yy][xx] && dir > 0) {
                if (dir > 0) { if (++yy >= nl) return 0; xx = 0; if (L[yy][0]) break; }
                else { if (--yy < 0) return 0; xx = strlen(L[yy]) - 1; if (xx >= 0) break; }
            }
        }
    }
}

static char lastre[256];
static int lastdir = 1;

static int search(int dir, int *y, int *x)
{
    int i, len, k;
    if (!lastre[0]) { strcpy(msg, "No previous regular expression"); return 0; }
    for (i = 0; i <= nl; i++) {         /* (i == nl: back on the cursor's line, wrapped) */
        int line = dir > 0 ? (cy + i) % nl : (cy - i + nl) % nl, n = strlen(L[line]);
        if (dir > 0) {
            int from = i == 0 ? cx + 1 : 0;
            if (from > n) continue;
            k = re_find(lastre, L[line], from, &len);
            if (k >= 0 && (i < nl || k <= cx)) { *y = line; *x = k; return 1; }
        } else {
            k = re_find_last(lastre, L[line], i == 0 ? cx : n + 1, &len);
            if (k >= 0 && (i < nl || k >= cx)) { *y = line; *x = k; return 1; }
        }
    }
    sprintf(msg, "Pattern not found: %.200s", lastre);
    return 0;
}

/* ---------------------------------------------------------------- the yank register */
static char **yank;
static int nyank, yank_lines;           /* linewise, or characters (one line or more) */

static void set_yank(char **lines, int n, int linewise)
{
    int i;
    for (i = 0; i < nyank; i++) free(yank[i]);
    free(yank);
    yank = lines;
    nyank = n;
    yank_lines = linewise;
}

/* lines y1..y2 (linewise) or the text from (y1,x1) up to (y2,x2) into the register */
static void copy_range(int y1, int x1, int y2, int x2, int linewise)
{
    char **v;
    int n = y2 - y1 + 1, i;
    v = xmalloc(sizeof(char *) * n);
    if (linewise)
        for (i = 0; i < n; i++) v[i] = dupn(L[y1 + i], strlen(L[y1 + i]));
    else if (n == 1)
        v[0] = dupn(L[y1] + x1, x2 - x1);
    else {
        v[0] = dupn(L[y1] + x1, strlen(L[y1]) - x1);
        for (i = 1; i < n - 1; i++) v[i] = dupn(L[y1 + i], strlen(L[y1 + i]));
        v[n - 1] = dupn(L[y2], x2);
    }
    set_yank(v, n, linewise);
}

/* delete what copy_range copied (the change already begun) */
static void delete_range(int y1, int x1, int y2, int x2, int linewise)
{
    int i;
    if (linewise) {
        for (i = y1; i <= y2; i++) delete_line(y1);
        if (!nl) insert_line(0, dupn("", 0));
        cy = y1 < nl ? y1 : nl - 1;
        cx = first_nonblank(cy);
        return;
    }
    {
        char *a = L[y1], *b = L[y2], *n;
        int la = x1, lb = strlen(b) - x2;
        n = xmalloc(la + lb + 1);
        memcpy(n, a, la);
        memcpy(n + la, b + x2, lb);
        n[la + lb] = 0;
        for (i = y1 + 1; i <= y2; i++) delete_line(y1 + 1);
        free(L[y1]);
        L[y1] = n;
        cy = y1;
        cx = x1;
    }
}

static void put(int after, int count)
{
    int i, k;
    if (!nyank) { strcpy(msg, "Nothing to put"); return; }
    if (yank_lines) {
        int at = after ? cy + 1 : cy;
        begin_change(at, 0);
        for (k = 0; k < count; k++)
            for (i = 0; i < nyank; i++) insert_line(at + k * nyank + i, dupn(yank[i], strlen(yank[i])));
        end_change(nyank * count);
        cy = at;
        cx = first_nonblank(cy);
        return;
    }
    begin_change(cy, 1);
    {
        int x = after && L[cy][0] ? cx + 1 : cx, added = 0;
        for (k = 0; k < count; k++) {
            char *s = L[cy], *rest = dupn(s + x, strlen(s) - x), *n;
            if (nyank == 1) {
                n = xmalloc(strlen(s) + strlen(yank[0]) + 1);
                memcpy(n, s, x);
                strcpy(n + x, yank[0]);
                strcat(n, rest);
                free(L[cy]);
                L[cy] = n;
                x += strlen(yank[0]);
                free(rest);
            } else {
                n = xmalloc(x + strlen(yank[0]) + 1);
                memcpy(n, s, x);
                strcpy(n + x, yank[0]);
                free(L[cy]);
                L[cy] = n;
                for (i = 1; i < nyank - 1; i++) insert_line(cy + i, dupn(yank[i], strlen(yank[i])));
                n = xmalloc(strlen(yank[nyank - 1]) + strlen(rest) + 1);
                strcpy(n, yank[nyank - 1]);
                strcat(n, rest);
                insert_line(cy + nyank - 1, n);
                added += nyank - 1;
                free(rest);
                break;
            }
        }
        end_change(1 + added);
        cx = x - 1 >= 0 ? x - 1 : 0;
    }
}

/* ---------------------------------------------------------------- insert mode */
static void insert_mode(int replace)    /* (the change already begun on line cy) */
{
    int start = cy, c;
    for (;;) {
        refresh(1);
        c = getkey();
        if (c < 0 || c == ESC) break;
        if (c >= 0x100) continue;           /* (arrows: ignored while inserting) */
        if (c == '\r' || c == '\n') {
            char *s = L[cy];
            int ind = 0;
            insert_line(cy + 1, dupn(s + cx, strlen(s) - cx));
            s[cx] = 0;
            cy++;
            cx = 0;
            (void)ind;
            continue;
        }
        if (c == 8 || c == 127) {
            if (cx > 0) {
                char *s = L[cy];
                memmove(s + cx - 1, s + cx, strlen(s + cx) + 1);
                cx--;
            }
            continue;
        }
        if (c == KEY_CTRL('W')) {
            char *s = L[cy];
            int x = cx;
            while (x > 0 && (s[x - 1] == ' ' || s[x - 1] == '\t')) x--;
            while (x > 0 && s[x - 1] != ' ' && s[x - 1] != '\t') x--;
            memmove(s + x, s + cx, strlen(s + cx) + 1);
            cx = x;
            continue;
        }
        if (c == KEY_CTRL('U')) {
            char *s = L[cy];
            memmove(s, s + cx, strlen(s + cx) + 1);
            cx = 0;
            continue;
        }
        if (c == KEY_CTRL('V')) c = getkey();
        {
            char *s = L[cy], *n;
            int len = strlen(s);
            if (len >= MAXLINE - 2) continue;
            if (replace && cx < len) { s[cx++] = c; continue; }
            n = xmalloc(len + 2);
            memcpy(n, s, cx);
            n[cx] = c;
            strcpy(n + cx + 1, s + cx);
            free(L[cy]);
            L[cy] = n;
            cx++;
        }
    }
    end_change(cy - start + 1);
    if (cx > 0) cx--;
}

/* ---------------------------------------------------------------- ex commands */
static int quitting, lost;              /* lost: end of input, not a quit */

static int addr(const char **p, int *v)  /* a line address: 1 and *v (0-based), or 0 */
{
    const char *s = *p;
    if (*s == '.') { *v = cy; s++; }
    else if (*s == '$') { *v = nl - 1; s++; }
    else if (*s >= '0' && *s <= '9') { *v = atoi(s) - 1; while (*s >= '0' && *s <= '9') s++; }
    else return 0;
    while (*s == '+' || *s == '-') {
        int sign = *s++ == '+' ? 1 : -1, n = 1;
        if (*s >= '0' && *s <= '9') { n = atoi(s); while (*s >= '0' && *s <= '9') s++; }
        *v += sign * n;
    }
    *p = s;
    return 1;
}

static void substitute(int a, int b, const char *arg)
{
    char delim = *arg, pat[256], rep[256];
    const char *p = arg + 1;
    int k = 0, g = 0, changed = 0, y, last = -1, len;
    if (!delim) { strcpy(msg, "Bad substitute"); return; }
    while (*p && *p != delim && k < 255) { if (*p == '\\' && p[1] == delim) p++; pat[k++] = *p++; }
    pat[k] = 0;
    if (*p) p++;
    k = 0;
    while (*p && *p != delim && k < 255) { if (*p == '\\' && p[1] == delim) p++; rep[k++] = *p++; }
    rep[k] = 0;
    if (*p) p++;
    if (*p == 'g') g = 1;
    if (!pat[0]) strcpy(pat, lastre);
    else strcpy(lastre, pat);
    for (y = a; y <= b; y++)                    /* no match: no change (and not modified) */
        if (re_find(pat, L[y], 0, &len) >= 0) break;
    if (y > b) { strcpy(msg, "Substitute pattern match failed"); return; }
    begin_change(a, b - a + 1);
    for (y = a; y <= b; y++) {
        char out[MAXLINE];
        const char *s = L[y];
        int from = 0, o = 0, at, did = 0;
        while ((at = re_find(pat, s, from, &len)) >= 0) {
            const char *r;
            if (o + at - from + 1 >= MAXLINE) break;
            memcpy(out + o, s + from, at - from);
            o += at - from;
            for (r = rep; *r && o < MAXLINE - 1; r++) {
                if (*r == '&') { if (o + len < MAXLINE) { memcpy(out + o, s + at, len); o += len; } }
                else if (*r == '\\' && r[1]) out[o++] = *++r;
                else out[o++] = *r;
            }
            did = 1;
            from = at + len;
            if (len == 0) { if (s[from] && o < MAXLINE - 1) out[o++] = s[from]; from++; }
            if (!g || from > (int)strlen(s)) break;
        }
        if (did) {
            int rest = strlen(s) - from;
            if (rest > 0 && o + rest < MAXLINE) { memcpy(out + o, s + from, rest); o += rest; }
            out[o] = 0;
            free(L[y]);
            L[y] = dupn(out, o);
            changed++;
            last = y;
        }
    }
    end_change(b - a + 1);
    if (!changed) { u_valid = 0; strcpy(msg, "Substitute pattern match failed"); return; }
    cy = last;
    cx = first_nonblank(cy);
}

static void shell_escape(const char *cmd)
{
    int pid, st;
    sighandler_t old;
    goto_rc(rows - 1, 0);
    oput("\r\n");
    oflush();
    raw_mode(0);
    if ((pid = fork()) == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    old = signal(SIGINT, SIG_IGN);          /* (the command's ^C is not ours) */
    if (pid > 0) while (wait(&st) != pid) ;
    signal(SIGINT, old);
    printf("[Hit return to continue] ");
    fflush(stdout);
    raw_mode(1);
    getkey();
    redraw_all();
}

static void ex(const char *line)
{
    const char *p = line;
    int a = cy, b = cy, range = 0, len;
    char cmd[16], arg[256];
    while (*p == ' ' || *p == ':') p++;
    if (*p == '%') { a = 0; b = nl - 1; range = 2; p++; }
    else if (addr(&p, &a)) {
        b = a;
        range = 1;
        if (*p == ',') { p++; if (!addr(&p, &b)) b = a; range = 2; }
    }
    if (!*p) {                          /* :N -- go to a line (clamped) */
        if (b >= nl) b = nl - 1;
        if (b < 0) b = 0;
        if (range) { cy = b; cx = first_nonblank(cy); }
        return;
    }
    /* a range must lie in the buffer, in order (:0r reads at the top) */
    if (a > b) { strcpy(msg, "Backwards range"); return; }
    if (b >= nl || a < (*p == 'r' && a == b ? -1 : 0)) { strcpy(msg, "Not that many lines in buffer"); return; }
    if (*p == '!') { shell_escape(p + 1); return; }
    len = 0;
    while (*p && ((*p >= 'a' && *p <= 'z') || (*p == '!' && len)) && len < 15) cmd[len++] = *p++;
    cmd[len] = 0;
    if (!len) { cmd[0] = *p ? *p++ : 0; cmd[1] = 0; }
    while (*p == ' ') p++;
    strncpy(arg, p, 255);
    arg[255] = 0;
    if (!strcmp(cmd, "s")) { substitute(a, b, arg); return; }
    if (!strcmp(cmd, "d")) {
        begin_change(a, b - a + 1);
        copy_range(a, 0, b, 0, 1);
        delete_range(a, 0, b, 0, 1);
        end_change(0);
        return;
    }
    if (!strcmp(cmd, "w") || !strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
        const char *f = arg[0] ? arg : fname;
        int n;
        if (!strcmp(cmd, "x") && !modified) { quitting = 1; return; }
        if (!f[0]) { strcpy(msg, "No file name"); return; }
        if (!fname[0]) strncpy(fname, f, 255);
        if (!range) { a = 0; b = nl - 1; }
        if ((n = save(f, a, b + 1)) < 0) { sprintf(msg, "\"%.200s\" cannot write", f); return; }
        sprintf(msg, "\"%.200s\" %d lines, %d characters", f, b - a + 1, n);
        if (!strcmp(f, fname) && a == 0 && b == nl - 1) modified = 0;   /* (all of it) */
        if (cmd[1] == 'q' || cmd[0] == 'x') quitting = 1;
        return;
    }
    if (!strcmp(cmd, "q")) {
        if (modified) { strcpy(msg, "No write since last change (:q! overrides)"); return; }
        quitting = 1;
        return;
    }
    if (!strcmp(cmd, "q!")) { quitting = 1; return; }
    if (!strcmp(cmd, "e") || !strcmp(cmd, "e!")) {
        int n;
        if (modified && cmd[1] != '!') { strcpy(msg, "No write since last change (:e! overrides)"); return; }
        if (!arg[0]) strcpy(arg, fname);
        while (nl) delete_line(nl - 1);
        strncpy(fname, arg, 255);
        n = load(fname);
        if (!nl) insert_line(0, dupn("", 0));
        cy = cx = top = 0;
        modified = 0;
        u_valid = 0;
        if (n < 0) sprintf(msg, "\"%.200s\" [New file]", fname);
        else sprintf(msg, "\"%.200s\" %d lines", fname, n);
        return;
    }
    if (!strcmp(cmd, "r")) {
        int before = nl, at = (range ? b : cy) + 1, n, i;
        char **keep;
        if (!arg[0]) { strcpy(msg, "No file name"); return; }
        /* read at the end, then move the lines into place */
        n = load(arg);
        if (n < 0) { sprintf(msg, "\"%.200s\" cannot open", arg); return; }
        keep = xmalloc(sizeof(char *) * (n ? n : 1));
        for (i = 0; i < n; i++) keep[i] = L[before + i];
        nl = before;
        begin_change(at, 0);
        for (i = 0; i < n; i++) insert_line(at + i, keep[i]);
        end_change(n);
        free(keep);
        sprintf(msg, "\"%.200s\" %d lines", arg, n);
        cy = at < nl ? at : nl - 1;
        return;
    }
    sprintf(msg, "%.40s: not an editor command", cmd);
}

static void read_ex(char first)          /* a command line on the last row; run it */
{
    char line[256];
    int n = 0, c;
    goto_rc(rows - 1, 0);
    oput("\033[K");
    line[0] = first;
    {
        char b[2]; b[0] = first; b[1] = 0;
        oput(b);
    }
    for (;;) {
        oflush();
        c = getkey();
        if (c < 0) return;
        if (c == '\r' || c == '\n') break;
        if (c == ESC) { msg[0] = 0; return; }
        if (c == 8 || c == 127) {
            if (!n) { msg[0] = 0; return; }
            n--;
            oput("\b \b");
            continue;
        }
        if (c < 0x100 && n < 250) {
            char b[2];
            line[++n] = c;
            b[0] = c; b[1] = 0;
            oput(b);
        }
    }
    line[n + 1] = 0;
    msg[0] = 0;
    if (first == ':') ex(line + 1);
    else {
        if (line[1]) strcpy(lastre, line + 1);
        lastdir = first == '/' ? 1 : -1;
        {
            int y, x;
            if (search(lastdir, &y, &x)) { cy = y; cx = x; }
        }
    }
}

/* ---------------------------------------------------------------- normal mode */
/* a motion for an operator or alone: the target in *y *x; linewise motions
   set *lw; returns 0 if there is none */
static int motion(int c, int count, int *y, int *x, int *lw, int *incl)
{
    int i;
    *y = cy; *x = cx; *lw = 0; *incl = 0;
    switch (c) {
    case 'h': case 0x144: case 8: *x = cx - count < 0 ? 0 : cx - count; return 1;
    case 'l': case 0x143: case ' ': { int len = strlen(L[cy]); *x = cx + count > len ? len : cx + count; return 1; }
    case 'j': case 0x142: case KEY_CTRL('N'): *y = cy + count >= nl ? nl - 1 : cy + count; *lw = 1; return 1;
    case 'k': case 0x141: case KEY_CTRL('P'): *y = cy - count < 0 ? 0 : cy - count; *lw = 1; return 1;
    case '0': *x = 0; return 1;
    case '^': *x = first_nonblank(cy); return 1;
    case '$': *y = cy + count - 1 >= nl ? nl - 1 : cy + count - 1; *x = strlen(L[*y]); *incl = 0; return 1;
    case 'w': for (i = 0; i < count; i++) word_fwd(y, x); return 1;
    case 'b': for (i = 0; i < count; i++) word_back(y, x); return 1;
    case 'e': for (i = 0; i < count; i++) word_end(y, x); *incl = 1; return 1;
    case 'G': *y = count > 0 ? (count - 1 < nl ? count - 1 : nl - 1) : nl - 1; *x = first_nonblank(*y); *lw = 1; return 1;
    case 'H': *y = top; *x = first_nonblank(*y); *lw = 1; return 1;
    case 'L': *y = top + rows - 2 < nl ? top + rows - 2 : nl - 1; *x = first_nonblank(*y); *lw = 1; return 1;
    case 'M': { int last = top + rows - 2 < nl ? top + rows - 2 : nl - 1; *y = (top + last) / 2; *x = first_nonblank(*y); *lw = 1; return 1; }
    case '%': *incl = 1; return match_bracket(y, x);
    case 'f': case 't': case 'F': case 'T': {
        int ch = getkey();
        last_find = c; last_findch = ch;
        *incl = c == 'f' || c == 't';
        return find_char(c, ch, count, x);
    }
    case ';': case ',': {
        int how = last_find;
        if (!how) return 0;
        if (c == ',') how = how == 'f' ? 'F' : how == 'F' ? 'f' : how == 't' ? 'T' : 't';
        *incl = how == 'f' || how == 't';
        return find_char(how, last_findch, count, x);
    }
    case 'n': case 'N': return search(c == 'n' ? lastdir : -lastdir, y, x);
    }
    return 0;
}

static void operate(int op, int count, int had, int forced)   /* d c y with a motion (forced: given) */
{
    int c = forced ? forced : getkey(), y, x, lw, incl, y1, x1, y2, x2, n2 = 1;
    if (c >= '1' && c <= '9') {
        n2 = 0;
        had = 1;
        while (c >= '0' && c <= '9') { n2 = n2 * 10 + c - '0'; c = getkey(); }
    }
    count *= n2;
    if (c == 'G' && !had) count = 0;
    if (c == op) {                      /* dd cc yy: whole lines */
        y = cy + count - 1 >= nl ? nl - 1 : cy + count - 1;
        lw = 1; y1 = cy; y2 = y; x1 = 0; x2 = 0;
    } else {
        if (op == 'c' && c == 'w') c = 'e', incl = 1;   /* cw changes to the end of the word */
        if (!motion(c, count, &y, &x, &lw, &incl)) return;
        if (c == 'e' || (op == 'c' && c == 'e')) incl = 1;
        if (y < cy || (y == cy && x < cx)) { y1 = y; x1 = x; y2 = cy; x2 = cx; }
        else { y1 = cy; x1 = cx; y2 = y; x2 = x; }
        if (!lw && c == 'w' && y2 > y1 && x2 == 0) {     /* dw at a line's end stays on the line */
            y2--;
            x2 = strlen(L[y2]);
        }
        if (incl && !lw && L[y2][x2]) x2++;
    }
    if (lw) {
        if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
        copy_range(y1, 0, y2, 0, 1);
        if (op == 'y') { strcpy(msg, ""); cy = y1; return; }
        if (op == 'c') {
            begin_change(y1, y2 - y1 + 1);
            for (x = y1; x < y2; x++) delete_line(y1 + 1);
            free(L[y1]);
            L[y1] = dupn("", 0);
            cy = y1;
            cx = 0;
            insert_mode(0);
            return;
        }
        begin_change(y1, y2 - y1 + 1);
        delete_range(y1, 0, y2, 0, 1);
        end_change(0);
        return;
    }
    copy_range(y1, x1, y2, x2, 0);
    if (op == 'y') { cy = y1; cx = x1; return; }
    begin_change(y1, y2 - y1 + 1);
    delete_range(y1, x1, y2, x2, 0);
    if (op == 'c') { insert_mode(0); return; }
    end_change(1);
    clamp();
}

static void join(int count)
{
    int k;
    if (cy + 1 >= nl) return;
    if (count < 2) count = 2;
    if (cy + count > nl) count = nl - cy;
    begin_change(cy, count);
    for (k = 1; k < count; k++) {
        char *a = L[cy], *b = L[cy + 1], *n;
        int la = strlen(a);
        while (*b == ' ' || *b == '\t') b++;
        while (la > 0 && (a[la - 1] == ' ' || a[la - 1] == '\t')) la--;
        n = xmalloc(la + strlen(b) + 2);
        memcpy(n, a, la);
        n[la] = 0;
        if (*b && la) strcat(n, b[0] == ')' ? "" : " ");
        cx = strlen(n);
        strcat(n, b);
        free(L[cy]);
        L[cy] = n;
        delete_line(cy + 1);
    }
    end_change(1);
}

static void command(void)
{
    int c = getkey(), count = 0, had = 0, y, x, lw, incl;
    if (c < 0) { lost = 1; quitting = 1; return; }      /* end of input */
    if (c >= '1' && c <= '9') {
        while (c >= '0' && c <= '9') { count = count * 10 + c - '0'; c = getkey(); }
        had = 1;
    }
    if (!had) count = 1;
    msg[0] = 0;
    switch (c) {
    case ESC: return;
    case KEY_CTRL('L'): redraw_all(); return;
    case KEY_CTRL('G'): sprintf(msg, "\"%.200s\"%s line %d of %d", fname[0] ? fname : "[No file]", modified ? " [Modified]" : "", cy + 1, nl); return;
    case KEY_CTRL('F'): top += (rows - 3) * count; if (top > nl - 1) top = nl - 1; cy = top; cx = first_nonblank(cy); return;
    case KEY_CTRL('B'): top -= (rows - 3) * count; if (top < 0) top = 0; cy = top + rows - 2 < nl ? top + rows - 2 : nl - 1; cx = first_nonblank(cy); return;
    case KEY_CTRL('D'): cy += (rows - 1) / 2; top += (rows - 1) / 2; if (cy >= nl) cy = nl - 1; if (top > nl - 1) top = nl - 1; return;
    case KEY_CTRL('U'): cy -= (rows - 1) / 2; top -= (rows - 1) / 2; if (cy < 0) cy = 0; if (top < 0) top = 0; return;
    case 'g': if (getkey() == 'g') { cy = had ? (count - 1 < nl ? count - 1 : nl - 1) : 0; cx = first_nonblank(cy); } return;
    case 'G':
        motion('G', had ? count : 0, &y, &x, &lw, &incl);
        cy = y; cx = x;
        return;
    case ':': case '/': case '?': read_ex(c); return;
    case 'u': undo(); return;
    case '.': {
        static char again[MAXLINE];
        if (!nrec) return;
        memcpy(again, rec, nrec);
        replay = again; rpos = 0; rlen = nrec;
        while (rpos < rlen) command();
        replay = 0;
        return;
    }
    case 'Z': if (getkey() == 'Z') ex("x"); return;
    case 'i': case 'a': case 'I': case 'A': case 'o': case 'O': case 'R':
        recording = 1; nrec = 1; rec[0] = c;
        if (c == 'a' && L[cy][0]) cx++;
        if (c == 'I') cx = first_nonblank(cy);
        if (c == 'A') cx = strlen(L[cy]);
        if (c == 'o' || c == 'O') {
            int at = c == 'o' ? cy + 1 : cy;
            begin_change(at, 0);
            insert_line(at, dupn("", 0));
            cy = at;
            cx = 0;
            c_nold = 0;
        } else
            begin_change(cy, 1);
        insert_mode(c == 'R');
        recording = 0;
        return;
    case 'x': case 'X': {
        int len = strlen(L[cy]), a, b;
        if (!len) return;
        recording = 1; nrec = 1; rec[0] = c;
        if (c == 'x') { a = cx; b = cx + count > len ? len : cx + count; }
        else { if (!cx) { recording = 0; return; } a = cx - count < 0 ? 0 : cx - count; b = cx; }
        copy_range(cy, a, cy, b, 0);
        begin_change(cy, 1);
        delete_range(cy, a, cy, b, 0);
        end_change(1);
        clamp();
        recording = 0;
        return;
    }
    case 'r': {
        int ch = getkey(), k;
        if (ch < 0 || ch == ESC || cx + count > (int)strlen(L[cy])) return;
        recording = 1; nrec = 2; rec[0] = 'r'; rec[1] = ch;
        begin_change(cy, 1);
        for (k = 0; k < count; k++) L[cy][cx + k] = ch;
        cx += count - 1;
        end_change(1);
        recording = 0;
        return;
    }
    case '~': {
        int k, len = strlen(L[cy]);
        if (!len) return;
        begin_change(cy, 1);
        for (k = 0; k < count && cx < len; k++, cx++) {
            char ch = L[cy][cx];
            if (ch >= 'a' && ch <= 'z') L[cy][cx] = ch - 32;
            else if (ch >= 'A' && ch <= 'Z') L[cy][cx] = ch + 32;
        }
        end_change(1);
        clamp();
        return;
    }
    case 'J': recording = 1; nrec = 1; rec[0] = 'J'; join(had ? count : 2); recording = 0; return;
    case 'y':                           /* (not a change: . still repeats the last one) */
        operate(c, count, had, 0);
        return;
    case 'd': case 'c':
        recording = 1; nrec = 1; rec[0] = c;
        operate(c, count, had, 0);
        recording = 0;
        return;
    case 'D': case 'C':
        recording = 1; nrec = 1; rec[0] = c;
        operate(c == 'D' ? 'd' : 'c', 1, 0, '$');
        recording = 0;
        return;
    case 'Y':
        copy_range(cy, 0, cy + count - 1 < nl ? cy + count - 1 : nl - 1, 0, 1);
        return;
    case 'p': case 'P':
        recording = 1; nrec = 1; rec[0] = c;
        put(c == 'p', count);
        recording = 0;
        return;
    }
    if (motion(c, count, &y, &x, &lw, &incl)) {
        if (lw && (c == 'j' || c == 'k' || c >= 0x141 || c == KEY_CTRL('N') || c == KEY_CTRL('P'))) {
            if (want_col < 0) want_col = dcol(L[cy], cx);
            cy = y;
            cx = byte_at(L[cy], want_col);
            clamp();
            return;
        }
        want_col = -1;
        cy = y;
        cx = x;
        clamp();
        return;
    }
    oput("\007");
}

/* ---------------------------------------------------------------- main */
/* vi is going away without :w (a hangup, SIGTERM, end of input): save a
 * changed buffer in $HOME/vi.recover and say so */
static void recover(const char *why)
{
    char path[300];
    const char *h = getenv("HOME");
    if (!modified) return;
    if (h && h[0] && strlen(h) < 280) sprintf(path, "%s/vi.recover", h);
    else strcpy(path, "/tmp/vi.recover");
    if (save(path, 0, nl) < 0) fprintf(stderr, "vi: %s: changes lost (cannot write %s)\n", why, path);
    else fprintf(stderr, "vi: %s: changes saved in %s\n", why, path);
}

static void on_signal(int sig)
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    raw_mode(0);
    recover(sig == SIGHUP ? "hangup" : "terminated");
    exit(1);
}

int main(int argc, char **argv)
{
    int r, n;
    char *e;
    if ((e = getenv("LINES")) != 0 && atoi(e) > 2) rows = atoi(e);
    if ((e = getenv("COLUMNS")) != 0 && atoi(e) > 10) cols = atoi(e);
    shadow = xmalloc(sizeof(char *) * rows);
    for (r = 0; r < rows; r++) shadow[r] = dupn("", 0);
    if (argc > 1) {
        strncpy(fname, argv[1], 255);
        n = load(fname);
        if (n < 0) sprintf(msg, "\"%.200s\" [New file]", fname);
        else sprintf(msg, "\"%.200s\" %d lines", fname, n);
    }
    if (!nl) insert_line(0, dupn("", 0));
    signal(SIGHUP, on_signal);
    signal(SIGTERM, on_signal);
    raw_mode(1);
    redraw_all();
    while (!quitting) {
        clamp();
        refresh(0);
        command();
    }
    goto_rc(rows - 1, 0);
    oput("\033[K");
    oflush();
    raw_mode(0);
    if (lost && modified) { recover("end of input"); return 1; }
    return 0;
}
