/* init -- process 1 (M4; os-design.md 4.6): runs /etc/rc, then keeps a
 * getty on each terminal (tty0, tty1, and tty4 -- the video card's screen
 * and keyboard, D50 -- when the card is fitted), starting a new one whenever a
 * session ends, and clears that terminal's /etc/utmp record. Orphans are
 * reaped here. SIGTERM (shutdown) stops the respawning. If there is no getty, it falls back to a shell on tty0. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <utmp.h>

static char *envp[] = { "PATH=/bin:/usr/bin", "HOME=/", 0 };
#define NT 3
static const int term[NT] = { 0, 1, 4 };    /* the terminals with a getty */
static int nterm = 2;                   /* (tty4 too if it opens) */
static int pids[NT];
static volatile int down;           /* SIGTERM (from shutdown): no new logins */

static void ondown(int sig) { (void)sig; down = 1; }
static void onhup(int sig) { (void)sig; signal(SIGHUP, onhup); }

static int spawn(int t)
{
    char line[8], *argv[3];
    int pid;
    sprintf(line, "tty%d", t);
    argv[0] = "getty"; argv[1] = line; argv[2] = 0;
    pid = fork();
    if (pid == 0) {
        setsid();
        execve("/etc/getty", argv, envp);
        _exit(1);
    }
    return pid;
}

static void clear_utmp(int t)
{
    struct utmp u;
    int fd = open(UTMP, O_WRONLY);
    if (fd < 0) return;
    memset(&u, 0, sizeof u);
    lseek(fd, t * sizeof u, 0);
    write(fd, &u, sizeof u);
    close(fd);
}

static long started[NT];                 /* when each terminal's getty began */
static int quick[NT];                    /* how many ended within 5 seconds in a row */

int main(void)
{
    int st, pid, t;
    if (open("/dev/tty0", O_RDWR) < 0) return 1;
    dup(0);
    dup(0);
    if ((t = open("/dev/tty4", O_RDWR)) >= 0) { close(t); nterm = 3; }    /* a video card */
    if (access("/etc/rc", 4) == 0) {
        char *argv[] = { "sh", "/etc/rc", 0 };
        if ((pid = fork()) == 0) { execve("/bin/sh", argv, envp); _exit(1); }
        while (wait(&st) != pid) ;
    }
    if (access("/etc/getty", 1) < 0) {             /* single user: a shell on the console */
        char *argv[] = { "-sh", 0 };
        for (;;) {
            if ((pid = fork()) == 0) { execve("/bin/sh", argv, envp); _exit(1); }
            while (wait(&st) != pid) ;
        }
    }
    close(0); close(1); close(2);
    setsid();
    signal(SIGTERM, ondown);
    signal(SIGHUP, onhup);              /* (caught, not ignored: an ignored signal stays ignored
                                           across exec, and every login would inherit that) */
    for (t = 0; t < nterm; t++) { pids[t] = spawn(term[t]); started[t] = time(0); }
    for (;;) {
        pid = wait(&st);
        if (pid < 0) {
            if (down) pause();          /* every child gone: wait for the halt */
            continue;
        }
        for (t = 0; t < nterm; t++)
            if (pid == pids[t]) {
                long now = time(0);
                clear_utmp(term[t]);
                /* (a getty that ends at once, again and again: wait a minute, not a tight loop) */
                if (now - started[t] < 5) { if (++quick[t] >= 5) { sleep(60); quick[t] = 0; } }
                else quick[t] = 0;
                pids[t] = down ? 0 : spawn(term[t]);
                started[t] = time(0);
            }
    }
}
