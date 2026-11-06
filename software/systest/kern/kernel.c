/* kernel.c -- the kernel's fixed behaviours, as a user sees them */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>

static int seen;
static void on_hup(int s) { (void)s; seen |= 1; signal(SIGHUP, on_hup); }
static void on_term(int s) { (void)s; seen |= 2; signal(SIGTERM, on_term); }

int main(void)
{
    int fd, k, st, pid, fds[24], ino1, ino2;
    struct stat sb;
    struct sysinfo si0, si1;
    /* 1. a new file made read-only can still be written by its maker, once */
    fd = creat("ro", 0444);
    printf("1 creat 0444: %s", fd >= 0 ? "ok" : strerror(errno));
    if (fd >= 0) { printf(", write %d", (int)write(fd, "abc", 3)); close(fd); }
    fd = open("ro", O_WRONLY);
    printf(", reopen for writing: %s\n", fd >= 0 ? "allowed (root?)" : strerror(errno));
    if (fd >= 0) close(fd);
    /* 2. kill with a bad signal number */
    k = kill(getpid(), 99);
    printf("2 kill 99: %d %s\n", k, k < 0 ? strerror(errno) : "");
    printf("  kill 0 (exists): %d\n", kill(getpid(), 0));
    /* 3. a fault whose signal is ignored ends the process */
    fflush(stdout);
    if ((pid = fork()) == 0) {
        signal(SIGSEGV, SIG_IGN);
        *(volatile int *)0x10 = 1;      /* (no page there) */
        _exit(0);
    }
    while (wait(&st) != pid) ;
    printf("3 ignored SIGSEGV: %s %d\n", WIFSIGNALED(st) ? "killed by signal" : "exited", WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
    /* 4. two signals pending at once both arrive */
    fflush(stdout);
    if ((pid = fork()) == 0) {
        signal(SIGHUP, on_hup);
        signal(SIGTERM, on_term);
        while (seen != 3) pause();
        _exit(seen);
    }
    sleep(1);
    kill(pid, SIGHUP);
    kill(pid, SIGTERM);
    while (wait(&st) != pid) ;
    printf("4 HUP and TERM while paused: exit %d (3: both seen)\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    /* 5. st_rdev: a device's number, 0 for a file */
    stat("/etc/passwd", &sb);
    printf("5 st_rdev: file %d", sb.st_rdev);
    stat("/dev/tty", &sb);
    printf(", /dev/tty %s\n", sb.st_rdev ? "nonzero" : "0");
    /* 6. wait with a bad pointer keeps the child's status */
    fflush(stdout);
    if ((pid = fork()) == 0) _exit(7);
    sleep(1);
    k = wait((int *)0xFFFFFFF0);
    printf("6 wait(bad pointer): %d %s", k, k < 0 ? strerror(errno) : "");
    k = wait(&st);
    printf(", then wait: %s, status %d\n", k == pid ? "the child" : "?", WEXITSTATUS(st));
    /* 7. out of descriptors: open(O_CREAT) fails and creates nothing */
    for (k = 0; k < 24; k++) if ((fds[k] = open("/etc/passwd", O_RDONLY)) < 0) break;
    fd = open("newfile", O_WRONLY | O_CREAT, 0644);
    printf("7 open with no descriptor left: %s, file made: %s\n", fd < 0 ? strerror(errno) : "opened?", access("newfile", 0) == 0 ? "yes" : "no");
    while (--k >= 0) close(fds[k]);
    /* 8. a new file takes the lowest free inode: a removed one is used again,
       and the one above it after that */
    close(creat("i1", 0644));
    close(creat("i2", 0644));
    close(creat("i3", 0644));
    stat("i1", &sb); ino1 = sb.st_ino;
    stat("i2", &sb); ino2 = sb.st_ino;
    unlink("i2");
    unlink("i1");
    close(creat("i4", 0644));
    close(creat("i5", 0644));
    stat("i4", &sb); k = sb.st_ino == ino1;
    stat("i5", &sb);
    printf("8 removed inodes used again, lowest first: %s\n", k && sb.st_ino == ino2 ? "yes" : "no");
    /* 9. processes leave no memory behind, and spare page tables count as free */
    sysinfo(&si0);
    fflush(stdout);
    for (k = 0; k < 8; k++) {
        if ((pid = fork()) == 0) _exit(0);
        while (wait(&st) != pid) ;
    }
    sysinfo(&si1);
    printf("9 free memory after 8 forks: %s\n", si0.freeram == si1.freeram ? "the same" : "different");
    return 0;
}
