/* gwin.c -- libgfx in a window. Under wm (wm(1)) the screen is the window manager's; a program that is run
 * in one of its windows (WM is set, and it has a terminal) asks wm to do its drawing: each call of the library
 * goes to wm as a request, written to the program's own terminal, and wm draws it in a window that is opened
 * for the program's picture. The calls are the same, so a program need not know.
 *
 * A request is  ESC _  a letter  numbers with ; between  [: text]  ESC \  -- a string a terminal that is not
 * wm's passes over. What a call returns comes back the same way, as numbers typed at the terminal: so the
 * terminal is set to give characters at once, unechoed, while one is waited for. What is typed by hand comes
 * the same way, between the answers: ginput tells the two apart, and hands the program both (D125).
 */
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#ifdef __P32__
#include <sgtty.h>
#endif
#include <gfx.h>
#include "gfxint.h"

int _gwm;                               /* drawing is asked of wm */
static int tfd = -1;                    /* the program's terminal */
static int ww = 400, wh = 300;          /* the picture to ask for (gsize) */
static unsigned char kq[64];            /* keys typed while an answer of wm's was waited for: ginput has them next */
static int kh, kt;
static int back = -1;                   /* a character read after an ESC that was a key */
static int held, hx, hy, hb;            /* what the mouse did, heard while a key was handed over: ginput has it next */
static char out[1024];                  /* requests on their way: written whole ones at a time, 1024 bytes at most, */
static int nout, req;                   /* which the terminal takes unmixed with anything else (kernel/tty.c). req: */
                                        /* where the request being put together begins */

static void put(int n)                  /* the first n bytes written, and what is after them moved to the front */
{
    char *p = out;
    int k, left = n;
    while (left > 0) {
        k = write(tfd, p, left);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; break; }
        p += k; left -= k;
    }
    for (k = n; k < nout; k++) out[k - n] = out[k];
    nout -= n;
}

void _gwsend(void) { put(nout); req = 0; }

static void ch(int c)
{
    if (nout == (int)sizeof out) {      /* no room: the whole requests go (or, of one too long to be whole, what there is) */
        if (req) { put(req); req = 0; } else put(nout);
    }
    out[nout++] = (char)c;
}

static void num(int v)
{
    char d[12];
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    int n = 0;
    if (v < 0) ch('-');
    do d[n++] = (char)('0' + u % 10); while ((u /= 10) != 0);
    while (n) ch(d[--n]);
}

/* a request: its letter, n numbers, and text or 0 */
void _gwv(int op, int n, const int *v, const char *s)
{
    int k;
    req = nout;
    ch(27); ch('_'); ch(op);
    for (k = 0; k < n; k++) { if (k) ch(';'); num(v[k]); }
    if (s) {
        ch(':');
        for (; *s; s++) ch(*s == '\n' ? 0x1F : *s < 0x20 || *s > 0x7E ? '?' : *s);     /* (a new line goes as a mark) */
    }
    ch(27); ch('\\');
    req = nout;
}

void _gwn(int op, int n, int a, int b, int c, int d, int e, int f)
{
    int v[6];
    v[0] = a; v[1] = b; v[2] = c; v[3] = d; v[4] = e; v[5] = f;
    _gwv(op, n, v, 0);
}

/* The terminal gives its characters as they come, unechoed, while an answer is waited for, and from the first
   ginput until gclose. */
#ifdef __P32__
static struct sgttyb was;
#endif
static int israw;

static int raw(void)                    /* 1 if this call made it so (and is to undo it) */
{
#ifdef __P32__
    struct sgttyb t;
    if (israw || gtty(tfd, &was) < 0) return 0;
    t = was; t.sg_flags = (t.sg_flags | CBREAK) & ~ECHO;
    stty(tfd, &t);
#else
    if (israw) return 0;
#endif
    israw = 1;
    return 1;
}

static void cooked(void)
{
#ifdef __P32__
    if (israw) stty(tfd, &was);
#endif
    israw = 0;
}

/* a character from the terminal: wait 50ths of a second for it at most (below 0: for ever). -1: none came in
   the time; -2: the terminal is gone; -3: a signal the program catches came first (only if intr: otherwise
   the wait goes on after it) */
static int rdc(int wait, int intr)
{
    struct timeval tv;
    fd_set r;
    unsigned char c;
    int k;
    if (back >= 0) { k = back; back = -1; return k; }
    for (;;) {
        if (wait >= 0) {
            FD_ZERO(&r); FD_SET(tfd, &r);
            tv.tv_sec = wait / 50; tv.tv_usec = wait % 50 * 20000;
            k = select(tfd + 1, &r, 0, 0, &tv);
            if (k < 0 && errno == EINTR) { if (intr) return -3; continue; }
            if (k <= 0) return -1;
        }
        k = read(tfd, &c, 1);
        if (k == 1) return c;
        if (k < 0 && errno == EINTR) { if (intr) return -3; continue; }
        return -2;
    }
}

/* The next thing the terminal gives: a key that was typed (its character, 1 to 255), or an answer of wm's
   (U_ANS: its numbers in v, max at most, how many in *n). 0: nothing in the time; -1: the terminal is gone;
   -2: a signal the program catches came before anything else did (only if intr).
   An answer is written whole by wm, so what follows its ESC is there at once: an ESC with nothing after it
   in a 25th of a second, or with anything but _, is the key. */
#define U_ANS 256
static int unit(int wait, int *v, int max, int *n, int intr)
{
    int c, neg = 0, any = 0, val = 0;
    for (;;) {
        c = rdc(wait, intr);
        if (c == -1) return 0;
        if (c == -3) return -2;
        if (c < 0) return -1;
        if (c != 27) { if (c) return c; continue; }
        c = rdc(2, 0);
        if (c == -2) return -1;
        if (c != '_') { if (c >= 0) back = c; return 27; }
        *n = 0;
        for (;;) {
            c = rdc(-1, 0);
            if (c < 0) return -1;
            if (c == 27) {              /* the end: ESC \ */
                if (any && *n < max) v[(*n)++] = neg ? -val : val;
                if (rdc(-1, 0) < 0) return -1;
                return U_ANS;
            }
            if (c == '-') neg = 1;
            else if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); any = 1; }
            else if (c == ';') { if (*n < max) v[(*n)++] = neg ? -val : val; val = neg = any = 0; }
        }
    }
}

static void keep(int c) { if (kh - kt < (int)sizeof kq) kq[kh++ % sizeof kq] = (unsigned char)c; }

/* what wm answers to the request just made: up to max numbers; how many came, or -1. Keys typed meanwhile
   are kept for ginput. */
int _gwa(int *v, int max)
{
    int n = 0, u, mine = raw();
    _gwsend();
    while ((u = unit(-1, v, max, &n, 0)) != U_ANS) {
        if (u <= 0) { n = -1; break; }
        keep(u);
    }
    if (mine) cooked();
    return n;
}

/* ginput in a window. The keys come as they are typed; the mouse is asked of wm (request M: answered when the
   mouse does something in the window, or when the time is up). If a key comes while that is waited for, wm is
   told to wait no longer (request N), and what it sends up to its answer to that is read here: so nothing is
   on its way when ginput comes back, and what the program reads from its terminal next is what was typed. */
static void nomore(void)
{
    int v[4], n, u;
    _gwn('N', 0, 0, 0, 0, 0, 0, 0);
    _gwsend();
    for (;;) {
        u = unit(-1, v, 4, &n, 0);
        if (u <= 0) return;
        if (u != U_ANS) { keep(u); continue; }
        if (n == 4) { if (v[3]) { held = 1; hx = v[0]; hy = v[1]; hb = v[2]; } continue; }  /* (it had answered) */
        return;                         /* the answer to N: no numbers */
    }
}

int _gwinput(int *x, int *y, int *b, int wait, int mouse)
{
    int v[4], n, u;
    raw();
    if (kh != kt) return kq[kt++ % sizeof kq];
    if (!mouse) {                       /* keys alone: nothing is asked of wm */
        _gwsend();
        u = unit(wait, v, 4, &n, 1);
        return u > 0 && u < U_ANS ? u : u == -1 ? -1 : 0;
    }
    if (held) {
        held = 0;
        if (x) *x = hx;
        if (y) *y = hy;
        if (b) *b = hb;
        return G_MOUSE;
    }
    _gwn('M', 1, wait, 0, 0, 0, 0, 0);
    _gwsend();
    for (;;) {
        u = unit(-1, v, 4, &n, 1);
        if (u == -1) return -1;
        if (u == U_ANS) {
            if (n != 4) continue;
            if (x) *x = v[0];
            if (y) *y = v[1];
            if (b) *b = v[2];
            return v[3] ? G_MOUSE : 0;
        }
        nomore();                       /* a key first, or a signal: the mouse is not waited for any longer */
        return u > 0 ? u : 0;
    }
}

/* gwrite in a window: the words go as hexadecimal digits, as many whole lines to a request as it has room for */
void _gwbits(int x0, int y0, int x1, int y1, const unsigned short *bits, int how)
{
    static const char hex[] = "0123456789abcdef";
    char text[4 * 224 + 1];
    int v[5], w = (x1 - x0 + 16) >> 4, per, y, k, r, n;
    if (w < 1 || w > 64) return;
    per = 224 / w;
    for (y = y1; y >= y0; y -= per) {   /* (the top line is the first) */
        r = y - y0 + 1 < per ? y - y0 + 1 : per;
        for (n = k = 0; k < r * w; k++) {
            unsigned d = bits[(y1 - y) * w + k];
            text[n++] = hex[d >> 12 & 15]; text[n++] = hex[d >> 8 & 15]; text[n++] = hex[d >> 4 & 15]; text[n++] = hex[d & 15];
        }
        text[n] = 0;
        v[0] = x0; v[1] = y - r + 1; v[2] = x1; v[3] = y; v[4] = how;
        _gwv('w', 5, v, text);
    }
}

void gsize(int w, int h) { ww = w; wh = h; }

/* gopen in a window: 1 and a window is asked for; 0 if the program is not in one of wm's. The picture is 400 by
   300 dots, or what the program asked for (gsize), or what WMSIZE says: 640 by 440 at most. Nothing is waited
   for: an answer comes as typing at the terminal, and a program started with & has not the terminal's ear (its
   shell has), so a program that only draws must not need one. */
static void bye(void) { if (_gwm) _gwclose(); }  /* (a program that ends without gclose: its picture goes all the same, */
                                                /* as the screen goes back to the console when it has the chip itself) */
int _gwopen(void)
{
    static int armed;
    const char *size = getenv("WMSIZE");
    int w = ww, h = wh;
    if (!getenv("WM")) return 0;
    tfd = open("/dev/tty", O_RDWR);
    if (tfd < 0) return 0;
    if (size) { w = atoi(size); while (*size && *size != 'x') size++; h = *size ? atoi(size + 1) : 0; }
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w > 640) w = 640;
    if (h > 440) h = 440;
    nout = req = 0;
    kh = kt = held = 0; back = -1;
    if (!armed) { armed = 1; atexit(bye); }
    _gwn('O', 2, w, h, 0, 0, 0, 0);
    _gwm = 1;
    gwidth = w; gheight = h;
    gcellw = 8; gcellh = 16;
    return 1;
}

void _gwclose(void)
{
    _gwn('Q', 0, 0, 0, 0, 0, 0, 0);
    _gwsend();
    cooked();
    close(tfd);
    tfd = -1;
    _gwm = 0;
}
