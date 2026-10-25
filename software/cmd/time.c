/* time -- run a command and report its real, user and system time (to stderr)
 *
 *   time [-l] command [arg ...]
 *
 * -l adds the command's memory events (as BSD's time -l): TLB refills,
 * copy-on-write page copies and stack pages added, from pinfo's counts for
 * this process's children. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/pinfo.h>

static void show(const char *what, unsigned ticks)
{
    fprintf(stderr, "%-6s%5u.%02u\n", what, ticks / CLK_TCK, ticks % CLK_TCK * (100 / CLK_TCK));
}

/* this process's slot: its children's memory counts */
static void mine(struct pinfo *p)
{
    int k, me = getpid();
    for (k = 0; pinfo(k, p) == 0; k++)
        if (p->pid == me) return;
    memset(p, 0, sizeof *p);
}

int main(int argc, char **argv)
{
    struct tms t0, t1;
    struct pinfo m0, m1;
    long r0, r1;
    int pid, st, mem = 0;
    if (argc > 1 && !strcmp(argv[1], "-l")) { mem = 1; argv++; argc--; }
    if (argc < 2) { fprintf(stderr, "usage: time [-l] command [arg ...]\n"); return 2; }
    if (mem) mine(&m0);
    r0 = times(&t0);
    if ((pid = fork()) < 0) { perror("time: fork"); return 2; }
    if (pid == 0) {
        execvp(argv[1], argv + 1);
        perror(argv[1]);
        _exit(127);
    }
    signal(SIGINT, SIG_IGN);            /* (^C stops the command, not the timing) */
    signal(SIGQUIT, SIG_IGN);
    while (wait(&st) != pid)
        ;
    r1 = times(&t1);
    fputc('\n', stderr);
    show("real", r1 - r0);
    show("user", t1.tms_cutime - t0.tms_cutime);
    show("sys", t1.tms_cstime - t0.tms_cstime);
    if (mem) {
        mine(&m1);
        fprintf(stderr, "%10u  TLB refills\n", m1.cntlb - m0.cntlb);
        fprintf(stderr, "%10u  copy-on-write page copies\n", m1.cncow - m0.cncow);
        fprintf(stderr, "%10u  stack pages added\n", m1.cnstack - m0.cnstack);
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
