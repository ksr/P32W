/* cycles -- measure a command in the emulator: its times, as time(1), with the
 * measuring gate up around it (D85)
 *
 *   cycles command [arg ...]
 *   cycles start
 *   cycles stop
 *
 * The gate is bit 7 of the I/O card's output port (/dev/port): in p32uemu -G
 * it starts and stops a count of the machine's clocks, which the emulator
 * reports when the gate falls. It is for the emulator only: on the real machine
 * use time(1). start and stop leave it to you what runs between them. The
 * port's other bits are left at 0. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/times.h>

#define GATE 0x80

static int port = -1;

static void gate(int up)
{
    char c = up ? GATE : 0;
    if (port >= 0) write(port, &c, 1);
}

static void show(const char *what, unsigned ticks)
{
    fprintf(stderr, "%-6s%5u.%02u\n", what, ticks / CLK_TCK, ticks % CLK_TCK * (100 / CLK_TCK));
}

int main(int argc, char **argv)
{
    struct tms t0, t1;
    long r0, r1;
    int pid, st;
    if (argc < 2) {
        fprintf(stderr, "usage: cycles command [arg ...]\n       cycles start | stop\n");
        return 2;
    }
    port = open("/dev/port", O_WRONLY);
    if (argc == 2 && !strcmp(argv[1], "start")) { gate(1); return port < 0; }
    if (argc == 2 && !strcmp(argv[1], "stop")) { gate(0); return port < 0; }
    r0 = times(&t0);
    gate(1);
    if ((pid = fork()) < 0) { gate(0); perror("cycles: fork"); return 2; }
    if (pid == 0) {
        close(port);
        execvp(argv[1], argv + 1);
        perror(argv[1]);
        _exit(127);
    }
    signal(SIGINT, SIG_IGN);            /* (^C stops the command, not the measuring) */
    signal(SIGQUIT, SIG_IGN);
    while (wait(&st) != pid)
        ;
    gate(0);
    r1 = times(&t1);
    fputc('\n', stderr);
    show("real", r1 - r0);
    show("user", t1.tms_cutime - t0.tms_cutime);
    show("sys", t1.tms_cstime - t0.tms_cstime);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
