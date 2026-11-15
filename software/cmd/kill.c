/* kill -- send a signal:  kill [-SIG | -N | -s SIG] pid ...  (default SIGTERM);
 * kill -l lists the signal names, kill -l N names signal N (or the signal
 * that killed a command whose exit status is N, 129-143). Signal 0 only
 * checks that the process exists and may be signalled. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>

static const char *names[NSIG] = { "0", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "EMT", "FPE",
                                   "KILL", "BUS", "SEGV", "SYS", "PIPE", "ALRM", "TERM" };     /* (and WINCH, 28: main) */

static int number(const char *s)        /* digits, as a number; else -1 */
{
    long v = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        if ((v = v * 10 + (*s - '0')) > 99999) return -1;
    }
    return (int)v;
}

static int signo(const char *s)        /* a name (with or without SIG, any case) or its number; -1 if neither */
{
    char up[16];
    int k;
    if (*s >= '0' && *s <= '9') return (k = number(s)) >= 0 && k < NSIG && names[k] ? k : -1;
    for (k = 0; s[k] && k < 15; k++) up[k] = toupper(s[k]);
    up[k] = 0;
    if (s[k]) return -1;
    k = !strncmp(up, "SIG", 3) ? 3 : 0;
    {
        int n;
        for (n = 1; n < NSIG; n++) if (names[n] && !strcmp(up + k, names[n])) return n;
    }
    return -1;
}

static int usage(void)
{
    fprintf(stderr, "usage: kill [-SIG | -N | -s SIG] pid ...   kill -l [N]\n");
    return 2;
}

int main(int argc, char **argv)
{
    int i = 1, sig = SIGTERM, st = 0;
    names[SIGWINCH] = "WINCH";          /* (the numbers from 16 to 27 and above 28 have no signal) */
    if (argc > 1 && !strcmp(argv[1], "-l")) {
        int k;
        if (argc == 2) {
            int shown = 0;
            for (k = 1; k < NSIG; k++) if (names[k]) printf("%s%2d %s", shown++ ? shown % 8 == 1 ? "\n" : "\t" : "", k, names[k]);
            putchar('\n');
            return 0;
        }
        for (i = 2; i < argc; i++) {    /* kill -l N: the name */
            k = number(argv[i]);
            if (k > 128 && k < 128 + NSIG) k -= 128;
            if (k < 1 || k >= NSIG || !names[k]) { fprintf(stderr, "kill: %s: bad signal\n", argv[i]); st = 2; continue; }
            puts(names[k]);
        }
        return st;
    }
    if (argc > 1 && !strcmp(argv[1], "-s")) {
        if (argc == 2) return usage();
        if ((sig = signo(argv[2])) < 0) { fprintf(stderr, "kill: %s: bad signal (kill -l lists them)\n", argv[2]); return 2; }
        i = 3;
    } else if (argc > 1 && argv[1][0] == '-' && strcmp(argv[1], "--")) {
        if (!argv[1][1]) return usage();
        if ((sig = signo(argv[1] + 1)) < 0) {
            fprintf(stderr, "kill: %s: bad signal (kill -l lists them)\n", argv[1] + 1);
            return 2;
        }
        i = 2;
    }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    if (i == argc) return usage();
    for (; i < argc; i++) {
        int pid = number(argv[i]);
        if (pid < 0) { fprintf(stderr, "kill: %s: not a process id\n", argv[i]); st = 1; continue; }
        if (kill(pid, sig) < 0) {
            if (errno == EINVAL) { fprintf(stderr, "kill: %d: bad signal\n", sig); return 2; }
            fprintf(stderr, "kill: %s: %s\n", argv[i], strerror(errno));
            st = 1;
        }
    }
    return st;
}
