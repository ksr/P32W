/* process.c -- P32W C library: system (<stdlib.h>), popen and pclose
 * (<stdio.h>): a command run by /bin/sh -c. Their result is the status
 * wait gives (WIFEXITED, WEXITSTATUS ...); 127 as the exit status means
 * the shell could not be run. */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

/* the status of child pid. There is no waitpid, so another child that
   ends first (another popen's command) is kept here for its own turn */
#define NKEPT 16
static int kept_pid[NKEPT], kept_status[NKEPT];

static int reap(int pid)
{
    int w, status, k;
    for (k = 0; k < NKEPT; k++)
        if (kept_pid[k] == pid) {
            kept_pid[k] = 0;
            return kept_status[k];
        }
    while ((w = wait(&status)) != pid && w != -1)
        for (k = 0; k < NKEPT; k++)
            if (!kept_pid[k]) {
                kept_pid[k] = w;
                kept_status[k] = status;
                break;
            }
    return w == -1 ? -1 : status;
}

/* system(0) asks whether there is a shell: there is. The caller ignores
   interrupt and quit while the command runs (as the shell does) */
int system(const char *cmd)
{
    int pid, status;
    sighandler_t oint, oquit;
    if (!cmd) return 1;
    fflush(NULL);
    if ((pid = fork()) < 0) return -1;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    oint = signal(SIGINT, SIG_IGN);
    oquit = signal(SIGQUIT, SIG_IGN);
    status = reap(pid);
    signal(SIGINT, oint);
    signal(SIGQUIT, oquit);
    return status;
}

#define MAXFD 64
static int child[MAXFD];                    /* the command at the other end of each popen'd descriptor */

FILE *popen(const char *cmd, const char *mode)
{
    int fds[2], pid, rd = mode[0] == 'r', mine, theirs, i;
    FILE *f;
    if ((mode[0] != 'r' && mode[0] != 'w') || pipe(fds) < 0) return NULL;
    mine = rd ? fds[0] : fds[1];
    theirs = rd ? fds[1] : fds[0];
    fflush(NULL);
    if (mine >= MAXFD || (pid = fork()) < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    if (pid == 0) {
        dup2(theirs, rd ? 1 : 0);
        close(fds[0]);
        close(fds[1]);
        for (i = 3; i < MAXFD; i++)         /* (not the other popen pipes: */
            if (child[i]) close(i);         /*  their readers would never see the end) */
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    close(theirs);
    if (!(f = fdopen(mine, rd ? "r" : "w"))) {
        close(mine);
        return NULL;
    }
    child[mine] = pid;
    return f;
}

int pclose(FILE *f)
{
    int fd = fileno(f), pid;
    if (fd < 0 || fd >= MAXFD || !(pid = child[fd])) return -1;
    fclose(f);
    child[fd] = 0;
    return reap(pid);
}
