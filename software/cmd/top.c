/* top -- the processes, busiest first, the screen redrawn every few seconds
 * (a VT100-style terminal; its height is the terminal's, 24 where that is not known).
 *
 *   top [-d seconds] [-n iterations] [-b]
 *
 * Keys: q quits; k kills (asks for the pid and the signal); n sets how many
 * processes are shown; s the delay; space redraws now. -b (batch) prints the
 * screens one after another, without escape sequences or keys (for scripts
 * and logs; with -n). %CPU is the share of the clock ticks since the last
 * screen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <pwd.h>
#include <sgtty.h>
#include <utmp.h>
#include <sys/times.h>
#include <sys/sysinfo.h>
#include <sys/pinfo.h>


#define NP 64
struct row { int pid, uid, state, size; unsigned time, delta; char name[16]; };
static struct row rows_[NP];
static unsigned last_time[NP];
static int last_pid[NP];
static long last_ticks;
static struct sysinfo last_si;          /* (the paging rates: since the last screen) */
static int batch, delay = 3, maxshow = 0, lines = 24;
static struct sgttyb saved;
static int have_tty;
static volatile int woke;

static void on_alarm(int sig) { (void)sig; signal(SIGALRM, on_alarm); woke = 1; }

static void restore(void) { if (have_tty) stty(0, &saved); }

static void quit(int sig) { (void)sig; restore(); printf("\n"); exit(0); }

static int bydelta(const void *a, const void *b)
{
    const struct row *x = a, *y = b;
    if (x->delta != y->delta) return x->delta < y->delta ? 1 : -1;
    if (x->time != y->time) return x->time < y->time ? 1 : -1;
    return x->pid - y->pid;
}

static const char *user(int uid)
{
    static char b[16];
    struct passwd *pw = getpwuid(uid);
    if (pw) { strncpy(b, pw->pw_name, 8); b[8] = 0; return b; }
    sprintf(b, "%d", uid);
    return b;
}

static void screen(void)
{
    struct pinfo p;
    struct sysinfo si;
    long now = times(0), elapsed;
    int k, n = 0, running = 0, sleeping = 0, zombie = 0, users = 0, show;
    time_t t = time(0);
    struct tm *tm = localtime(&t);
    struct utmp u;
    int fd;
    elapsed = last_ticks ? now - last_ticks : now;
    if (elapsed <= 0) elapsed = 1;
    for (k = 0; pinfo(k, &p) == 0 && k < NP; k++) {
        struct row *r;
        int j;
        if (!p.pid) continue;
        r = &rows_[n++];
        r->pid = p.pid; r->uid = p.uid; r->state = p.state; r->size = p.size; r->time = p.time;
        memcpy(r->name, p.name, 16);
        r->name[15] = 0;
        r->delta = p.time;
        for (j = 0; j < NP; j++)
            if (last_pid[j] == p.pid) { r->delta = p.time - last_time[j]; break; }
        if (p.state == 3 || p.state == 4) running++;
        else if (p.state == 5) zombie++;
        else sleeping++;
    }
    for (k = 0; k < NP; k++) last_pid[k] = 0;
    for (k = 0; k < n; k++) { last_pid[k] = rows_[k].pid; last_time[k] = rows_[k].time; }
    last_ticks = now;
    qsort(rows_, n, sizeof rows_[0], bydelta);
    if ((fd = open(UTMP, O_RDONLY)) >= 0) {
        while (read(fd, &u, sizeof u) == sizeof u) if (u.ut_name[0]) users++;
        close(fd);
    }
    if (sysinfo(&si) < 0) memset(&si, 0, sizeof si);
    if (!batch) {                       /* (the terminal's size each time: a window may have been made another) */
        int r;
        ttysize(1, 0, &r);
        if (r > 8 && r != lines) { lines = r; printf("\033[2J"); }
        printf("\033[H");
    }
    printf("top - %02d:%02d:%02d up %2u:%02u, %d user%s,  load average: %u.%02u, %u.%02u, %u.%02u%s\n",
           tm->tm_hour, tm->tm_min, tm->tm_sec, si.uptime / 3600, si.uptime / 60 % 60, users, users == 1 ? "" : "s",
           si.loads[0] >> 11, (si.loads[0] & 2047) * 100 >> 11, si.loads[1] >> 11, (si.loads[1] & 2047) * 100 >> 11,
           si.loads[2] >> 11, (si.loads[2] & 2047) * 100 >> 11, batch ? "" : "\033[K");
    printf("Tasks: %d total, %d running, %d sleeping, %d zombie%s\n", n, running, sleeping, zombie, batch ? "" : "\033[K");
    printf("Mem: %u KiB total, %u KiB used, %u KiB free%s\n", si.totalram / 1024, (si.totalram - si.freeram) / 1024,
           si.freeram / 1024, batch ? "" : "\033[K");
    if (last_si.uptime || last_si.tlbrefills) {     /* (rates since the last screen) */
        long hz = elapsed;
        printf("Paging: %lu TLB refills/s, %lu copy-on-write copies/s, %lu stack pages/s%s\n",
               (unsigned long)(si.tlbrefills - last_si.tlbrefills) * 50 / hz,
               (unsigned long)(si.cowcopies - last_si.cowcopies) * 50 / hz,
               (unsigned long)(si.stackpages - last_si.stackpages) * 50 / hz, batch ? "" : "\033[K");
    } else
        printf("Paging: %u TLB refills, %u copy-on-write copies, %u stack pages since boot%s\n",
               si.tlbrefills, si.cowcopies, si.stackpages, batch ? "" : "\033[K");
    last_si = si;
    printf("  PID USER     S   SIZE  %%CPU    TIME COMMAND%s\n", batch ? "" : "\033[K");
    show = batch ? n : lines - 7;
    if (maxshow && maxshow < show) show = maxshow;
    for (k = 0; k < n && k < show; k++) {
        struct row *r = &rows_[k];
        unsigned pct = r->delta * 1000 / elapsed;
        printf("%5d %-8s %c %6d %3u.%u %4u:%02u %s%s\n", r->pid, user(r->uid), "?ESRRZT"[r->state < 7 ? r->state : 0],
               r->size, pct / 10, pct % 10, r->time / 50 / 60, r->time / 50 % 60, r->name, batch ? "" : "\033[K");
    }
    if (!batch) {
        for (; k < show; k++) printf("\033[K\n");
        printf("\033[J");
    }
    fflush(stdout);
}

static int readkey(int secs)            /* a key, or -1 when secs pass first */
{
    char c;
    int n;
    woke = 0;
    alarm(secs);
    n = read(0, &c, 1);
    alarm(0);
    return n == 1 ? (unsigned char)c : -1;
}

static int ask(const char *q)          /* a number typed on the bottom line */
{
    char b[16];
    int n = 0, c;
    printf("\033[%d;1H\033[K%s", lines, q);
    fflush(stdout);
    for (;;) {
        c = readkey(60);
        if (c < 0 || c == '\r' || c == '\n') break;
        if ((c == 8 || c == 127) && n) { n--; printf("\b \b"); fflush(stdout); continue; }
        if (n < 15 && c >= ' ') { b[n++] = c; putchar(c); fflush(stdout); }
    }
    b[n] = 0;
    return n ? atoi(b) : -1;
}

int main(int argc, char **argv)
{
    int i, iterations = 0, done = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b")) batch = 1;
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) iterations = atoi(argv[++i]);
        else { fprintf(stderr, "usage: top [-d seconds] [-n iterations] [-b]\n"); return 2; }
    }
    if (delay < 1) delay = 1;
    ttysize(1, 0, &i);
    if (i > 8) lines = i;
    signal(SIGALRM, on_alarm);
    signal(SIGINT, quit);
    signal(SIGTERM, quit);
    if (!batch && gtty(0, &saved) == 0) {
        struct sgttyb t = saved;
        t.sg_flags = (t.sg_flags & ~(ECHO | RAW)) | CBREAK;
        stty(0, &t);
        have_tty = 1;
        printf("\033[H\033[2J");
    }
    for (;;) {
        int c;
        screen();
        if (iterations && ++done >= iterations) break;
        if (batch) { printf("\n"); sleep(delay); continue; }
        c = readkey(delay);
        if (c == 'q') break;
        if (c == 'k') {
            int pid = ask("PID to kill: "), sig;
            if (pid > 0) {
                sig = ask("signal [15]: ");
                kill(pid, sig > 0 ? sig : SIGTERM);
            }
            printf("\033[H\033[2J");
        } else if (c == 'n') {
            maxshow = ask("how many processes to show (0 = all that fit): ");
            if (maxshow < 0) maxshow = 0;
            printf("\033[H\033[2J");
        } else if (c == 's') {
            int d = ask("seconds between screens: ");
            if (d > 0) delay = d;
            printf("\033[H\033[2J");
        }
    }
    restore();
    if (!batch) printf("\033[%d;1H\n", lines);
    return 0;
}
