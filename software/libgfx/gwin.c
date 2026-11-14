/* gwin.c -- libgfx in a window. Under wm (wm(1)) the screen is the window manager's; a program that is run
 * in one of its windows (WM is set, and it has a terminal) asks wm to do its drawing: each call of the library
 * goes to wm as a request, written to the program's own terminal, and wm draws it in a window that is opened
 * for the program's picture. The calls are the same, so a program need not know.
 *
 * A request is  ESC _  a letter  numbers with ; between  [: text]  ESC \  -- a string a terminal that is not
 * wm's passes over. What a call returns comes back the same way, as numbers typed at the terminal: so the
 * terminal is set to give characters at once, unechoed, while one is waited for.
 */
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef __P32__
#include <sgtty.h>
#endif
#include <gfx.h>
#include "gfxint.h"

int _gwm;                               /* drawing is asked of wm */
static int tfd = -1;                    /* the program's terminal */
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

/* what wm answers to the request just made: up to max numbers; how many came, or -1 */
int _gwa(int *v, int max)
{
    unsigned char c;
    int n = 0, st = 0, neg = 0, any = 0, val = 0;
#ifdef __P32__
    struct sgttyb was, t;
    int mode = gtty(tfd, &was) == 0;
    if (mode) { t = was; t.sg_flags = (t.sg_flags | CBREAK) & ~ECHO; stty(tfd, &t); }
#endif
    _gwsend();
    for (;;) {
        if (read(tfd, &c, 1) != 1) { if (errno == EINTR) continue; n = -1; break; }
        if (st == 0) { if (c == 27) st = 1; continue; }
        if (st == 1) { st = c == '_' ? 2 : 0; continue; }
        if (c == 27) {                  /* the end: ESC \ */
            if (any && n < max) v[n++] = neg ? -val : val;
            read(tfd, &c, 1);
            break;
        }
        if (c == '-') neg = 1;
        else if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); any = 1; }
        else if (c == ';') { if (n < max) v[n++] = neg ? -val : val; val = neg = any = 0; }
    }
#ifdef __P32__
    if (mode) stty(tfd, &was);
#endif
    return n;
}

/* gopen in a window: 1 and a window is asked for; 0 if the program is not in one of wm's. The picture is 400 by
   300 dots, or what WMSIZE says (640x440 at most). Nothing is waited for: an answer comes as typing at the
   terminal, and a program started with & has not the terminal's ear (its shell has), so a program that only
   draws must not need one. */
static void bye(void) { if (_gwm) _gwclose(); }  /* (a program that ends without gclose: its picture goes all the same, */
                                                /* as the screen goes back to the console when it has the chip itself) */
int _gwopen(void)
{
    static int armed;
    const char *size = getenv("WMSIZE");
    int w = 400, h = 300;
    if (!getenv("WM")) return 0;
    tfd = open("/dev/tty", O_RDWR);
    if (tfd < 0) return 0;
    if (size) { w = atoi(size); while (*size && *size != 'x') size++; h = *size ? atoi(size + 1) : 0; }
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w > 640) w = 640;
    if (h > 440) h = 440;
    nout = req = 0;
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
    close(tfd);
    tfd = -1;
    _gwm = 0;
}
