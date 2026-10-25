/* write -- write to another user's terminal
 *
 *   write user [ttyN]
 *
 * Finds user in /etc/utmp (record k is ttyk) and copies the standard input
 * to their terminal a line at a time, after a header
 *
 *     Message from SENDER on ttyN at HH:MM ...
 *
 * and ends with EOF (at the end of the input, or on an interrupt). With
 * the user on more than one terminal and none named, the first is used
 * (and write says so).
 *
 * write is set-group-id tty (/bin/write, mode 2755, group tty): terminals
 * are owned by their user, group tty, mode 0620 (login), so the group
 * lets write open one. A terminal without group write (mesg n) is refused
 * -- checked again before every line -- unless the real user is root. The
 * terminal is opened first; then write gives up the group (setgid to the
 * real group), so nothing else is done with it. Control characters other
 * than tab, newline and bell are shown as ^X (and bytes over 127 as M-x),
 * so a message cannot send escape sequences to the other terminal. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pwd.h>
#include <utmp.h>
#include <sys/stat.h>

#define S_IWGRP 020

static int tty = -1;                    /* the other terminal */
static char *who;                       /* its user */
static char out[600];                   /* a line for it, made safe */
static int nout;

/* the /dev/ttyN that fd is open on: N; -1 if none */
static int ttyof(int fd)
{
    struct stat f, s;
    char path[16];
    int k;
    if (fstat(fd, &f) < 0 || !S_ISCHR(f.st_mode)) return -1;
    for (k = 0; k < 10; k++) {
        sprintf(path, "/dev/tty%d", k);
        if (stat(path, &s) == 0 && S_ISCHR(s.st_mode) && s.st_rdev == f.st_rdev) return k;
    }
    return -1;
}

/* may we write to the terminal: group write on, or we are really root */
static int allowed(int fd)
{
    struct stat s;
    return fstat(fd, &s) == 0 && ((s.st_mode & S_IWGRP) || getuid() == 0);
}

static void flush(void)
{
    if (nout && write(tty, out, nout) != nout) {
        fprintf(stderr, "write: %s's terminal: %s\n", who, strerror(errno));
        _exit(1);
    }
    nout = 0;
}

/* one byte of the message, as it is safe to show it */
static void put(int c)
{
    if (nout > (int)sizeof out - 8) flush();
    if (c >= 128) { out[nout++] = 'M'; out[nout++] = '-'; c &= 127; }
    if (c == '\t' || c == '\007' || (c >= ' ' && c < 127)) out[nout++] = c;
    else { out[nout++] = '^'; out[nout++] = c ^ 0x40; }
}

static void finish(int sig)
{
    nout = 0;
    write(tty, "EOF\r\n", 5);
    _exit(0);
}

int main(int argc, char **argv)
{
    struct utmp u;
    struct passwd *p;
    struct stat s;
    char me[16], path[16], head[96];
    int fd, k, want = -1, found = -1, count = 0, c, mine;
    time_t t;
    struct tm *tm;
    if (argc < 2 || argc > 3 || argv[1][0] == '-') { fprintf(stderr, "usage: write user [ttyN]\n"); return 2; }
    who = argv[1];
    if (argc == 3) {
        if (strncmp(argv[2], "tty", 3) || argv[2][3] < '0' || argv[2][3] > '9' || argv[2][4]) {
            fprintf(stderr, "write: %s: not a terminal (tty0, tty1 ...)\n", argv[2]);
            return 2;
        }
        want = argv[2][3] - '0';
    }
    if (!getpwnam(who)) { fprintf(stderr, "write: no user %s\n", who); return 1; }
    p = getpwuid(getuid());
    strncpy(me, p ? p->pw_name : "???", sizeof me - 1);
    me[sizeof me - 1] = 0;
    if ((fd = open(UTMP, O_RDONLY)) < 0) { perror(UTMP); return 1; }
    for (k = 0; read(fd, &u, sizeof u) == sizeof u; k++) {
        if (!u.ut_name[0] || strncmp(u.ut_name, who, sizeof u.ut_name) || strlen(who) > sizeof u.ut_name) continue;
        if (want >= 0 && k != want) continue;
        if (found < 0) found = k;
        count++;
    }
    close(fd);
    if (found < 0) {
        if (want >= 0) fprintf(stderr, "write: %s is not logged in on tty%d\n", who, want);
        else fprintf(stderr, "write: %s is not logged in\n", who);
        return 1;
    }
    if (count > 1) fprintf(stderr, "write: %s is logged in more than once; writing to tty%d\n", who, found);
    sprintf(path, "/dev/tty%d", found);
    if (stat(path, &s) == 0 && !(s.st_mode & S_IWGRP) && getuid() != 0) {
        fprintf(stderr, "write: %s has messages disabled\n", who);
        return 1;
    }
    if ((tty = open(path, O_WRONLY)) < 0) { fprintf(stderr, "write: %s: %s\n", path, strerror(errno)); return 1; }
    setgid(getgid());                   /* the group tty: only to open the terminal */
    if (fstat(tty, &s) < 0 || !S_ISCHR(s.st_mode)) { fprintf(stderr, "write: %s: not a terminal\n", path); return 1; }
    if (!allowed(tty)) { fprintf(stderr, "write: %s has messages disabled\n", who); return 1; }
    mine = -1;
    for (fd = 0; fd < 3 && mine < 0; fd++) mine = ttyof(fd);
    t = time(0);
    tm = localtime(&t);
    if (mine >= 0) sprintf(head, "\r\n\007Message from %s on tty%d at %02d:%02d ...\r\n", me, mine, tm->tm_hour, tm->tm_min);
    else sprintf(head, "\r\n\007Message from %s at %02d:%02d ...\r\n", me, tm->tm_hour, tm->tm_min);
    if (write(tty, head, strlen(head)) < 0) { fprintf(stderr, "write: %s: %s\n", path, strerror(errno)); return 1; }
    signal(SIGINT, finish);
    signal(SIGHUP, finish);
    signal(SIGPIPE, SIG_IGN);
    while ((c = getchar()) != EOF) {
        if (c != '\n') { put(c); continue; }
        out[nout++] = '\r';
        out[nout++] = '\n';
        if (!allowed(tty)) {            /* (mesg n while we were typing) */
            fprintf(stderr, "write: %s has messages disabled\n", who);
            return 1;
        }
        flush();
    }
    if (nout) { out[nout++] = '\r'; out[nout++] = '\n'; flush(); }
    write(tty, "EOF\r\n", 5);
    return 0;
}
