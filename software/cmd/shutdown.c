/* shutdown -- bring the system down: shutdown [-r] [now | +minutes] [message]
 *
 * Warns every terminal (each minute, then at the end), stops every process
 * (SIGTERM, then SIGKILL two seconds later), writes everything out, marks
 * the filesystem clean and halts -- or, with -r, restarts the machine into
 * the monitor. Root only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/pinfo.h>


static char msg[200];

static void wall(const char *text)
{
    static const char *ttys[] = { "/dev/tty0", "/dev/tty1", "/dev/tty2", "/dev/tty3", "/dev/tty4" };
    char buf[300];
    int t, fd;
    sprintf(buf, "\r\n\007*** %s ***\r\n%s%s", text, msg, msg[0] ? "\r\n" : "");
    for (t = 0; t < 5; t++)
        if ((fd = open(ttys[t], O_WRONLY)) >= 0) {
            write(fd, buf, strlen(buf));
            close(fd);
        }
}

static void killall(int sig)
{
    struct pinfo p;
    int k, me = getpid();
    for (k = 0; pinfo(k, &p) == 0; k++)
        if (p.pid > 1 && p.pid != me) kill(p.pid, sig);
}

int main(int argc, char **argv)
{
    int restart = 0, mins = 0, i = 1;
    char line[80];
    if (argc > i && !strcmp(argv[i], "-r")) { restart = 1; i++; }
    else if (argc > i && !strcmp(argv[i], "-h")) i++;
    if (argc > i && !strcmp(argv[i], "now")) i++;
    else if (argc > i && argv[i][0] == '+') {
        const char *d = argv[i] + 1;
        if (!*d || strlen(d) > 4) d = "x";
        for (; *d; d++) if (*d < '0' || *d > '9') break;
        if (*d) { fprintf(stderr, "shutdown: %s: bad time (now or +minutes)\n", argv[i]); return 2; }
        mins = atoi(argv[i++] + 1);
    }
    for (; i < argc; i++) {
        if (strlen(msg) + strlen(argv[i]) + 2 >= sizeof msg) break;
        if (msg[0]) strcat(msg, " ");
        strcat(msg, argv[i]);
    }
    if (geteuid()) { fprintf(stderr, "shutdown: only root may do that\n"); return 1; }
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    for (; mins > 0; mins--) {
        sprintf(line, "The system is going down%s in %d minute%s", restart ? " for a reboot" : "",
                mins, mins == 1 ? "" : "s");
        wall(line);
        sleep(60);
    }
    wall(restart ? "The system is going down for a reboot NOW" : "The system is going down NOW");
    sync();
    kill(1, SIGTERM);                   /* init: no new logins */
    killall(SIGTERM);
    sleep(2);
    killall(SIGKILL);
    sleep(1);
    sync();
    if (reboot(restart ? RB_REBOOT : 0) < 0) { perror("shutdown"); return 1; }
    return 0;
}
