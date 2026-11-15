/* tty.c -- the terminals: UART 0 (/dev/tty0, the console) to UART 3
 * (/dev/tty3; the I/O card's four, D50), interrupt-driven both ways, and
 * the video card's screen and keyboard (/dev/tty4, video.c), with the line discipline of
 * os-design.md 4.5: echo, erase (backspace / DEL), line kill (^U), ^C ->
 * SIGINT, ^\ -> SIGQUIT, ^D end of file; CBREAK and RAW modes; \n out as
 * \r\n (CRMOD). Modes are set with ioctl(TIOCSETP) as in Unix V7.
 *
 * Pseudo-terminals (D46): NPTY pairs. The slave, /dev/ttypN (tty minor 5 + N),
 * is a terminal like the others to whoever opens it -- getty, login, the
 * shell, vi: the same line discipline, signals, modes. Its other end is
 * the master, /dev/ptypN (character device 6, minor N), held by a program
 * (telnetd) that plays the terminal: what it writes is the slave's typing
 * (input(), as from a UART), and it reads what the slave's programs write
 * (the out queue, which no UART empties). Closing the master hangs the
 * slave up (SIGHUP; its reads then end); the master reads the end once the
 * slave has been opened and closed again by everyone. */
#include "defs.h"
#include <sgtty.h>

#define QSIZE 4096                     /* (Kermit's window of long packets fits, in raw mode) */
#define ATOMIC 1024                     /* a write of this much or less is not mixed with anything else's (D123) */
#define CEOT  4
#define CINTR 3
#define CQUIT 28

struct queue { uchar c[QSIZE]; uint head, tail; };     /* head: next in; tail: next out */
#define QLEN(q)  ((q)->head - (q)->tail)
static int qput(struct queue *q, int c) { if (QLEN(q) >= QSIZE) return -1; q->c[q->head++ % QSIZE] = c; return 0; }
static int qget(struct queue *q) { return QLEN(q) ? q->c[q->tail++ % QSIZE] : -1; }
static int qunput(struct queue *q) { if (!QLEN(q)) return -1; return q->c[--q->head % QSIZE]; }

struct tty {
    uint base;
    struct queue raw;           /* the line being typed */
    struct queue can;           /* finished lines (or characters, in CBREAK / RAW) */
    struct queue out;
    int flags, erase, kill;
    int lines;                  /* complete lines (and ^D marks) in can */
    int lastcr;                 /* the last character in was CR (CRMOD: a LF after it is dropped) */
    int opens;
    int master;                 /* a pty: its master is open (base is 0: no UART) */
    int used;                   /* a pty: the slave has been opened since the master was */
    int vt;                     /* the video card's terminal: output to the screen's driver (video.c) */
    struct winsize ws;          /* its size, if anything has said (D126): the screen's; a window's, from wm */
};
#define NUART 4
#define VT    NUART                     /* the video terminal's minor */
#define NPTY 16                         /* (0 to 3 for logins from the network, the rest for wm's windows: D122) */
#define NTTY (NUART + 1 + NPTY)
#define PTY(n) (VT + 1 + (n))           /* a pty slave's minor */
#define ISPTY(m) ((m) > VT)
static struct tty tty[NTTY];

#define REG(t, r) IOB((t)->base + (r))
#define RBR 0
#define THR 0
#define IER 1
#define IIR 2
#define FCR 2
#define LCR 3
#define MCR 4
#define LSR 5

static void tstart(struct tty *t)
{
    int n = 0;
    if (t->vt) {                        /* the screen: while its driver has room; its chip's interrupt asks for more */
        while (QLEN(&t->out) && vt_room()) vt_putc(qget(&t->out));
        vt_flush();
        if (QLEN(&t->out) < QSIZE / 2) wakeup(&t->out);
        return;
    }
    if (!t->base) {                     /* a pty: the master reads the out queue */
        wakeup(&t->base);
        wakeup(&selchan);
        return;
    }
    while (QLEN(&t->out) && (REG(t, LSR) & 0x20) && n < 16) {
        REG(t, THR) = qget(&t->out);
        n++;
    }
    REG(t, IER) = QLEN(&t->out) ? 3 : 1;        /* transmitter interrupts only while there is more */
}

static void output(struct tty *t, int c)
{
    if (c == '\n' && (t->flags & CRMOD)) qput(&t->out, '\r');
    qput(&t->out, c);
}

static void echo(struct tty *t, int c)
{
    if (!(t->flags & ECHO)) return;
    if (c < 32 && c != '\n' && c != '\t' && c != '\b') { output(t, '^'); output(t, c + 64); }
    else output(t, c);
}

void ttyinit(void)
{
    int u;
    static const uint base[NUART] = { UART0, UART1, UART2, UART3 };
    for (u = 0; u < NUART; u++) {
        struct tty *t = &tty[u];
        t->base = base[u];
        t->flags = ECHO | CRMOD;
        t->erase = 8;
        t->kill = 21;
        REG(t, LCR) = 0x80;             /* 9600 baud (divisor 12, as the monitor), 8N1, FIFOs */
        REG(t, 0) = 12;
        REG(t, 1) = 0;
        REG(t, LCR) = 0x03;
        REG(t, FCR) = 0x07;
        REG(t, MCR) = 0x08;             /* OUT2: interrupts out */
        REG(t, IER) = 1;                /* received data */
    }
    mtcr_imask(mfcr_imask() | 1 << IRQ_UART0 | 1 << IRQ_UART1);  /* (UART 2 shares level 2, UART 3 level 3) */
    tty[VT].vt = 1;
    tty[VT].ws.ws_row = 25; tty[VT].ws.ws_col = 80;     /* (the screen: video.c's ROWS and COLS) */
    tty[VT].ws.ws_xpixel = 640; tty[VT].ws.ws_ypixel = 400;
    tty[VT].flags = ECHO | CRMOD;
    tty[VT].erase = 8;
    tty[VT].kill = 21;
}


static void signal_tty(int unit, int sig)
{
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++)
        if (p->tty == unit && p->state != UNUSED && p->state != ZOMBIE)
            psignal(p, sig);
}

static void input(int unit, int c)
{
    struct tty *t = &tty[unit];
    if (t->flags & RAW) {
        if (qput(&t->can, c) == 0) t->lines++;
        if (t->flags & ECHO) echo(t, c);    /* (RAW echoes too when asked) */
        wakeup(&t->can);
        return;
    }
    c &= 0x7F;
    if (t->flags & CRMOD) {             /* Return: CR, LF, or (some terminals) CR LF -- one end of line */
        if (c == '\n' && t->lastcr) { t->lastcr = 0; return; }
        t->lastcr = c == '\r';
        if (c == '\r') c = '\n';
    }
    if (c == CINTR || c == CQUIT) {
        t->raw.head = t->raw.tail = 0;
        t->can.head = t->can.tail = 0;
        t->lines = 0;
        echo(t, c);
        output(t, '\n');
        signal_tty(unit, c == CINTR ? SIGINT : SIGQUIT);
        return;
    }
    if (t->flags & CBREAK) {
        if (qput(&t->can, c) == 0) t->lines++;
        echo(t, c);
        wakeup(&t->can);
        return;
    }
    if (c == t->erase || c == 127) {
        if (qunput(&t->raw) >= 0 && (t->flags & ECHO)) { output(t, '\b'); output(t, ' '); output(t, '\b'); }
        return;
    }
    if (c == t->kill) {
        t->raw.head = t->raw.tail = 0;
        echo(t, c);
        output(t, '\n');
        return;
    }
    if (c == CEOT) {                    /* end of file: the line so far, then a mark */
        int k;
        while ((k = qget(&t->raw)) >= 0) qput(&t->can, k);
        qput(&t->can, CEOT);
        t->lines++;
        wakeup(&t->can);
        return;
    }
    if (QLEN(&t->raw) >= QSIZE - 1) return;      /* line full: ignore */
    qput(&t->raw, c);
    echo(t, c);
    if (c == '\n') {
        int k;
        while ((k = qget(&t->raw)) >= 0) qput(&t->can, k);
        t->lines++;
        wakeup(&t->can);
    }
}

/* the screen's driver has room again (video.c, at interrupt level) */
void vtstart(void) { tstart(&tty[VT]); }

/* a character typed on the video card's keyboard (video.c, at interrupt level) */
void vtinput(int c)
{
    input(VT, c);
    tstart(&tty[VT]);
    wakeup(&selchan);
}

void ttyintr(int unit)
{
    struct tty *t = &tty[unit];
    (void)REG(t, IIR);                  /* (reading IIR clears a transmitter interrupt) */
    if (REG(t, LSR) & 1) wakeup(&selchan);     /* (select may be waiting for it) */
    while (REG(t, LSR) & 1)
        input(unit, REG(t, RBR));
    tstart(t);
    if (QLEN(&t->out) < QSIZE / 2) wakeup(&t->out);
}

int tty_ready(int minor)                /* (select) */
{
    return tty[minor].lines > 0 || (ISPTY(minor) && !tty[minor].master);
}

int ttyopen(int minor, int flags)
{
    if (minor >= NTTY || (minor == VT && !vt_present)) return -ENXIO;
    if (ISPTY(minor)) {                 /* a pty slave: only while its master is open */
        if (!tty[minor].master) return -EIO;
        tty[minor].used = 1;
    }
    tty[minor].opens++;
    if (curproc->tty < 0) curproc->tty = minor;        /* its controlling terminal */
    return 0;
}

int ttyclose(int minor)
{
    if (--tty[minor].opens == 0 && ISPTY(minor)) wakeup(&tty[minor].base);   /* (the master reads the end) */
    return 0;
}

/* /dev/tty: whichever terminal controls the process (its minor ignored) */
int cttyopen(int minor, int flags) { (void)minor; return curproc->tty < 0 ? -ENXIO : ttyopen(curproc->tty, flags); }
int cttyclose(int minor) { (void)minor; return curproc->tty < 0 ? 0 : ttyclose(curproc->tty); }
int cttyread(int minor, uint dst, int n, uint off) { (void)minor; return curproc->tty < 0 ? -ENXIO : ttyread(curproc->tty, dst, n, off); }
int cttywrite(int minor, uint src, int n, uint off) { (void)minor; return curproc->tty < 0 ? -ENXIO : ttywrite(curproc->tty, src, n, off); }
int cttyioctl(int minor, int cmd, uint arg) { (void)minor; return curproc->tty < 0 ? -ENXIO : ttyioctl(curproc->tty, cmd, arg); }

int ttyread(int minor, uint dst, int n, uint off)
{
    struct tty *t = &tty[minor];
    int k = 0, s;
    s = splhi();
    while (t->lines == 0) {
        if (ISPTY(minor) && !t->master) { splx(s); return 0; }    /* a pty hung up: the end */
        if (curproc->killed) { splx(s); return -EINTR; }
        sleep(&t->can);
    }
    while (k < n) {
        int c = qget(&t->can);
        uchar ch;
        if (c < 0) break;
        if (!(t->flags & (RAW | CBREAK))) {
            if (c == CEOT) { t->lines--; break; }
        } else
            t->lines--;
        ch = c;
        splx(s);
        if (copyout(dst + k, &ch, 1) < 0) return -EFAULT;
        s = splhi();
        k++;
        if (c == '\n' && !(t->flags & (RAW | CBREAK))) { t->lines--; break; }
        if ((t->flags & (RAW | CBREAK)) && t->lines == 0) break;
    }
    splx(s);
    return k;
}

int ttywrite(int minor, uint src, int n, uint off)
{
    struct tty *t = &tty[minor];
    int k;
    uchar chunk[64];
    int have = 0, at = 0;
    if (ISPTY(minor) && !t->master) return -EIO;     /* a pty hung up */
    if (n <= ATOMIC) {                  /* a short write goes in whole: nothing else's characters (another program's, */
        int s = splhi();                /* the echo of what is typed) come between its own. So it waits for room for */
        while (QSIZE - 2 - (int)QLEN(&t->out) < 2 * n) {       /* all of it (a new line may be two characters) */
            tstart(t);
            if (ISPTY(minor) && !t->master) { splx(s); return -EIO; }
            if (curproc->killed) { splx(s); return -EINTR; }
            sleep(&t->out);
        }
        splx(s);
    }
    for (k = 0; k < n; k++) {
        uchar c;
        int s;
        if (at == have) {               /* from the program 64 bytes at a time */
            have = n - k < (int)sizeof chunk ? n - k : (int)sizeof chunk;
            if (copyin(chunk, src + k, have) < 0) return k ? k : -EFAULT;
            at = 0;
        }
        c = chunk[at++];
        s = splhi();
        while (QLEN(&t->out) >= QSIZE - 2) {
            tstart(t);
            if (ISPTY(minor) && !t->master) { splx(s); return k ? k : -EIO; }
            if (curproc->killed) { splx(s); return k ? k : -EINTR; }
            sleep(&t->out);
        }
        output(t, c);
        splx(s);
    }
    {
        int s = splhi();
        tstart(t);
        splx(s);
    }
    return n;
}

/* after a mode change: what was typed ahead counted as the new mode reads
   it -- every character in CBREAK and RAW (and a line being typed is
   finished: it is all readable), a line or a ^D mark in cooked mode */
static void retype(struct tty *t)
{
    int s = splhi(), c;
    uint k;
    if (t->flags & (RAW | CBREAK))
        while ((c = qget(&t->raw)) >= 0) qput(&t->can, c);
    t->lines = 0;
    for (k = t->can.tail; k != t->can.head; k++) {
        c = t->can.c[k % QSIZE];
        if ((t->flags & (RAW | CBREAK)) || c == '\n' || c == CEOT) t->lines++;
    }
    splx(s);
}

int ttyioctl(int minor, int cmd, uint arg)
{
    struct tty *t = &tty[minor];
    struct sgttyb b;
    if (cmd == TIOCGETP) {
        memset(&b, 0, sizeof b);
        b.sg_erase = t->erase;
        b.sg_kill = t->kill;
        b.sg_flags = t->flags;
        return copyout(arg, &b, sizeof b);
    }
    if (cmd == TIOCSETP) {
        if (copyin(&b, arg, sizeof b) < 0) return -EFAULT;
        t->erase = b.sg_erase;
        t->kill = b.sg_kill;
        t->flags = b.sg_flags;
        retype(t);
        return 0;
    }
    if (cmd == TIOCGWINSZ) return copyout(arg, &t->ws, sizeof t->ws) < 0 ? -EFAULT : 0;
    if (cmd == TIOCSWINSZ) {            /* a new size: the terminal's programs are told, if it is another */
        struct winsize w;
        if (copyin(&w, arg, sizeof w) < 0) return -EFAULT;
        if (memcmp(&w, &t->ws, sizeof w)) {
            t->ws = w;
            signal_tty(minor, SIGWINCH);
        }
        return 0;
    }
    return -EINVAL;
}

/* ---------------------------------------------------------------- pty masters (D46) */
int ptyopen(int minor, int flags)
{
    struct tty *t;
    (void)flags;
    if (minor >= NPTY) return -ENXIO;
    t = &tty[PTY(minor)];
    if (t->master || t->opens) return -EBUSY;      /* one master; and no slave left from before */
    t->raw.head = t->raw.tail = t->can.head = t->can.tail = t->out.head = t->out.tail = 0;
    t->lines = t->lastcr = t->used = 0;
    t->base = 0;
    t->flags = ECHO | CRMOD;
    t->erase = 8;
    t->kill = 21;
    t->master = 1;
    memset(&t->ws, 0, sizeof t->ws);    /* (its size: for the master to say) */
    return 0;
}

/* the master says, and may ask, the size of its terminal (wm: a window's size) */
int ptyioctl(int minor, int cmd, uint arg)
{
    if (minor >= NPTY || !tty[PTY(minor)].master) return -ENXIO;
    if (cmd != TIOCGWINSZ && cmd != TIOCSWINSZ) return -EINVAL;         /* (the modes are the slave's own to set) */
    return ttyioctl(PTY(minor), cmd, arg);
}

int ptyclose(int minor)
{
    struct tty *t = &tty[PTY(minor)];
    t->master = 0;
    signal_tty(PTY(minor), SIGHUP);      /* the line is gone */
    wakeup(&t->can);
    wakeup(&t->out);
    return 0;
}

/* what the slave's programs wrote: up to n bytes, waiting for some; 0 once
   the slave has been opened and everyone has closed it again */
int ptyread(int minor, uint dst, int n, uint off)
{
    struct tty *t = &tty[PTY(minor)];
    uchar buf[64];
    int k = 0, m, s;
    (void)off;
    s = splhi();
    while (QLEN(&t->out) == 0) {
        if (t->used && t->opens == 0) { splx(s); return 0; }
        if (curproc->killed) { splx(s); return -EINTR; }
        sleep(&t->base);
    }
    while (k < n && QLEN(&t->out)) {
        for (m = 0; m < (int)sizeof buf && k + m < n && QLEN(&t->out); m++) buf[m] = qget(&t->out);
        splx(s);
        if (copyout(dst + k, buf, m) < 0) return k ? k : -EFAULT;
        k += m;
        s = splhi();
    }
    wakeup(&t->out);                    /* (writers waiting for room) */
    splx(s);
    return k;
}

/* the terminal's typing, as if from its keyboard */
int ptywrite(int minor, uint src, int n, uint off)
{
    struct tty *t = &tty[PTY(minor)];
    uchar buf[64];
    int k, m, i, s;
    (void)off;
    for (k = 0; k < n; k += m) {
        m = n - k < (int)sizeof buf ? n - k : (int)sizeof buf;
        if (copyin(buf, src + k, m) < 0) return k ? k : -EFAULT;
        s = splhi();
        for (i = 0; i < m; i++) input(PTY(minor), buf[i]);
        tstart(t);                      /* (the echo, to the master) */
        splx(s);
    }
    return n;
}

int pty_ready(int minor)                /* (select: the master has something to read, or the end) */
{
    struct tty *t = &tty[PTY(minor)];
    return QLEN(&t->out) > 0 || (t->used && t->opens == 0);
}

