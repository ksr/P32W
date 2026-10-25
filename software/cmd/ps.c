/* ps -- the processes: ps [-u user] [-t tty] [-f]
 *   -u user   only that user's (a name or a uid)
 *   -t N      only those on terminal N (0-4; or tty0-tty4)
 *   -f        the user's name instead of the uid */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/pinfo.h>
static int number(const char *s)        /* digits; else -1 */
{
    int v = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 9999999) return -1;
        v = v * 10 + *s - '0';
    }
    return v;
}
int main(int argc, char **argv)
{
    static const char *states = "?ESRRZT";
    struct pinfo p;
    int k, i, only_uid = -1, only_tty = -2, full = 0;
    char tty[8];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") && i + 1 < argc) {
            struct passwd *pw = getpwnam(argv[++i]);
            only_uid = pw ? (int)pw->pw_uid : number(argv[i]);
            if (only_uid < 0) { fprintf(stderr, "ps: no user %s\n", argv[i]); return 1; }
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            const char *t = argv[++i];
            if (!strncmp(t, "tty", 3)) t += 3;
            if ((only_tty = number(t)) < 0) { fprintf(stderr, "ps: %s: not a terminal number\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "-f")) full = 1;
        else { fprintf(stderr, "usage: ps [-u user] [-t tty] [-f]\n"); return 2; }
    }
    printf(full ? "  PID  PPID S USER     TTY   SIZE   TIME CMD\n" : "  PID  PPID S  UID TTY   SIZE   TIME CMD\n");
    for (k = 0; pinfo(k, &p) == 0; k++) {
        if (!p.pid) continue;
        if (only_uid >= 0 && p.uid != only_uid) continue;
        if (only_tty != -2 && p.tty != only_tty) continue;
        printf("%5d %5d %c ", p.pid, p.ppid, p.state >= 0 && p.state <= 6 ? states[p.state] : '?');
        if (full) {
            struct passwd *pw = getpwuid(p.uid);
            if (pw) printf("%-8.8s", pw->pw_name); else printf("%-8d", p.uid);
        } else
            printf("%4d", p.uid);
        if (p.tty < 0) strcpy(tty, "?");
        else if (p.tty < 5) sprintf(tty, "%d", p.tty);         /* tty0-tty4 */
        else sprintf(tty, "p%d", p.tty - 5);                    /* ttyp0-ttyp3 (D46, D50) */
        printf(" %3s %6d %3u:%02u %s\n", tty, p.size,
               p.time / 50 / 60, p.time / 50 % 60, p.name);
    }
    return 0;
}
