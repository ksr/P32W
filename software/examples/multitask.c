/* multitask.c -- several processes at once on one CPU. Build and run it on P32W:
 *     cc -o multitask multitask.c
 *     ./multitask          (4 workers; ./multitask 6 for six, up to 8)
 *
 * Each worker is a process of its own (fork) that counts the primes in its
 * own range of numbers and never gives up the CPU by itself: the timer
 * interrupt takes it away every 2 ticks (40 ms) and the kernel runs the
 * next one, so their progress lines come out interleaved -- preemptive
 * multitasking. Each worker also changes its own copy of a global variable
 * (separate address spaces: the parent's copy stays as it was), sends its
 * result back through a pipe, and ends with an exit status the parent
 * collects with wait(). Meanwhile the parent shows the process table, as ps
 * would: the workers running, each with the CPU time it has had so far.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/pinfo.h>

#define MAXW   8
#define SPAN   6000                 /* numbers each worker checks */
#define STEPS  4                    /* progress lines each worker prints */

int shared = 100;                   /* every process gets its own copy at fork */

static int isprime(unsigned n)
{
    unsigned d;
    if (n < 2)
        return 0;
    for (d = 2; d * d <= n; d++)
        if (n % d == 0)
            return 0;
    return 1;
}

static long now(void)
{
    struct tms t;
    return times(&t);               /* clock ticks since boot (50 a second) */
}

static void worker(int k, int out)
{
    unsigned lo = 1000000 + k * SPAN, n;
    int primes = 0, step;
    char line[64];

    shared = k;                     /* only this process's copy changes */
    for (step = 0; step < STEPS; step++) {
        unsigned from = lo + step * (SPAN / STEPS), to = from + SPAN / STEPS;
        for (n = from; n < to; n++)
            primes += isprime(n);
        printf("    worker %d (pid %d): %u..%u checked, %d primes so far (tick %ld)\n",
               k, getpid(), lo, to - 1, primes, now());
        fflush(stdout);
    }
    sprintf(line, "%d %d %d\n", k, primes, shared);
    write(out, line, strlen(line));  /* the result, to the parent */
    exit(k + 1);                    /* an exit status the parent will see */
}

static const char *state_name(int s)
{
    static const char *names[] = { "free", "starting", "sleeping", "runnable", "running", "zombie", "stopped" };
    return s >= 0 && s <= 6 ? names[s] : "?";
}

static void show_table(int parent)
{
    struct pinfo p;
    int slot;
    printf("  the process table now (as ps shows it):\n");
    printf("      PID  PPID  STATE      CPU  NAME\n");
    for (slot = 0; pinfo(slot, &p) == 0; slot++)
        if (p.pid && (p.pid == parent || p.ppid == parent))
            printf("    %5d %5d  %-9s %4u  %s\n", p.pid, p.ppid, state_name(p.state), p.time, p.name);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 4, fds[2], k, status, total = 0;
    int pids[MAXW];
    long start;
    char buf[256];
    int got = 0, r;

    if (n < 1 || n > MAXW) {
        fprintf(stderr, "usage: multitask [workers, 1 to %d]\n", MAXW);
        return 1;
    }
    if (pipe(fds) < 0) {
        perror("pipe");
        return 1;
    }
    printf("parent (pid %d) starts %d workers; each counts the primes among %d numbers\n",
           getpid(), n, SPAN);
    fflush(stdout);
    start = now();
    for (k = 0; k < n; k++) {
        pids[k] = fork();
        if (pids[k] < 0) {
            perror("fork");
            return 1;
        }
        if (pids[k] == 0) {         /* the child */
            close(fds[0]);
            worker(k, fds[1]);
        }
    }
    close(fds[1]);                  /* the parent only reads */

    sleep(1);                       /* let them run a while ... */
    show_table(getpid());           /* ... and look at them */

    /* the results, through the pipe (read returns 0 when every worker has closed it) */
    while (got < (int)sizeof buf - 1 && (r = read(fds[0], buf + got, sizeof buf - 1 - got)) > 0)
        got += r;
    buf[got] = 0;
    printf("  results through the pipe:\n");
    {
        char *line = buf;
        while (*line) {
            int w, primes, theirs;
            if (sscanf(line, "%d %d %d", &w, &primes, &theirs) == 3) {
                printf("    worker %d found %d primes (its copy of shared was %d)\n", w, primes, theirs);
                total += primes;
            }
            while (*line && *line != '\n')
                line++;
            if (*line)
                line++;
        }
    }
    for (k = 0; k < n; k++) {       /* collect every worker's exit status */
        int pid = wait(&status);
        printf("  wait: pid %d ended with exit status %d\n", pid, (status >> 8) & 0xFF);
    }
    printf("parent: %d primes in all, %ld ticks (%ld.%02ld s); the parent's shared is still %d\n",
           total, now() - start, (now() - start) / 50, (now() - start) % 50 * 2, shared);
    return 0;
}
