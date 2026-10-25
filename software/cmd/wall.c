/* wall -- write a message to everyone logged in
 *
 *   wall [file]
 *
 * Reads the message (up to 2000 bytes) from file or the standard input and
 * writes it, after a header
 *
 *     Broadcast message from USER (ttyN) at HH:MM ...
 *
 * to every terminal with a user in /etc/utmp, the sender's own included.
 *
 * wall is set-group-id tty (/bin/wall, mode 2755, group tty), like write:
 * terminals are owned by their user, group tty, mode 0620. A terminal
 * without group write (mesg n) is skipped, unless the real user is root,
 * who reaches every terminal. The terminals are opened first; then wall
 * gives up the group (setgid to the real group) before it reads file, so
 * the file is read with the user's own rights. Control characters other
 * than tab, newline and bell are shown as ^X (bytes over 127 as M-x). */
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
#define MAXMSG 2000
#define NTTY 10

static char msg[MAXMSG + 1];
static char out[100 + 4 * MAXMSG + 2 * MAXMSG];   /* the header, the message made safe */

/* the /dev/ttyN that fd is open on: N; -1 if none */
static int ttyof(int fd)
{
    struct stat f, s;
    char path[16];
    int k;
    if (fstat(fd, &f) < 0 || !S_ISCHR(f.st_mode)) return -1;
    for (k = 0; k < NTTY; k++) {
        sprintf(path, "/dev/tty%d", k);
        if (stat(path, &s) == 0 && S_ISCHR(s.st_mode) && s.st_rdev == f.st_rdev) return k;
    }
    return -1;
}

int main(int argc, char **argv)
{
    struct utmp u;
    struct passwd *p;
    struct stat s;
    char path[16], *o, extra;
    int fds[NTTY], nfd = 0, fd, k, n, len, mine, root = getuid() == 0;
    time_t t;
    struct tm *tm;
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) { fprintf(stderr, "usage: wall [file]\n"); return 2; }
    if ((fd = open(UTMP, O_RDONLY)) < 0) { perror(UTMP); return 1; }
    for (k = 0; k < NTTY && read(fd, &u, sizeof u) == sizeof u; k++) {
        int tf;
        if (!u.ut_name[0]) continue;
        sprintf(path, "/dev/tty%d", k);
        if (stat(path, &s) < 0 || (!(s.st_mode & S_IWGRP) && !root)) continue;     /* (mesg n) */
        if ((tf = open(path, O_WRONLY)) < 0) continue;
        if (fstat(tf, &s) < 0 || !S_ISCHR(s.st_mode) || (!(s.st_mode & S_IWGRP) && !root)) { close(tf); continue; }
        fds[nfd++] = tf;
    }
    close(fd);
    setgid(getgid());                   /* the group tty: only to open the terminals */
    if (argc == 2) {
        if ((fd = open(argv[1], O_RDONLY)) < 0) { fprintf(stderr, "wall: %s: %s\n", argv[1], strerror(errno)); return 1; }
    } else fd = 0;
    for (len = 0; len < MAXMSG && (n = read(fd, msg + len, MAXMSG - len)) > 0; len += n)
        ;
    if (len == MAXMSG && read(fd, &extra, 1) == 1) fprintf(stderr, "wall: only the first %d bytes are sent\n", MAXMSG);
    p = getpwuid(getuid());
    mine = -1;
    for (k = 0; k < 3 && mine < 0; k++) mine = ttyof(k);
    t = time(0);
    tm = localtime(&t);
    if (mine >= 0) sprintf(out, "\r\n\007Broadcast message from %s (tty%d) at %02d:%02d ...\r\n", p ? p->pw_name : "???", mine, tm->tm_hour, tm->tm_min);
    else sprintf(out, "\r\n\007Broadcast message from %s at %02d:%02d ...\r\n", p ? p->pw_name : "???", tm->tm_hour, tm->tm_min);
    o = out + strlen(out);
    for (k = 0; k < len; k++) {
        int c = msg[k] & 0xFF;
        if (c == '\n') { *o++ = '\r'; *o++ = '\n'; continue; }
        if (c >= 128) { *o++ = 'M'; *o++ = '-'; c &= 127; }
        if (c == '\t' || c == '\007' || (c >= ' ' && c < 127)) *o++ = c;
        else { *o++ = '^'; *o++ = c ^ 0x40; }
    }
    if (len && msg[len - 1] != '\n') { *o++ = '\r'; *o++ = '\n'; }
    signal(SIGPIPE, SIG_IGN);
    for (k = 0; k < nfd; k++) {
        write(fds[k], out, o - out);
        close(fds[k]);
    }
    return 0;
}
