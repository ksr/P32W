/* sys.c -- P32W C library (M2, D22): the system-call wrappers. */
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <stdarg.h>
#include <errno.h>

ssize_t read(int fd, void *buf, size_t n) { return __syscall(SYS_read, fd, (int)buf, n, 0); }
ssize_t write(int fd, const void *buf, size_t n) { return __syscall(SYS_write, fd, (int)buf, n, 0); }
int open(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    return __syscall(SYS_open, (int)path, flags, mode, 0);
}
int creat(const char *path, mode_t mode) { return __syscall(SYS_creat, (int)path, mode, 0, 0); }
int close(int fd) { return __syscall(SYS_close, fd, 0, 0, 0); }
off_t lseek(int fd, off_t off, int whence) { return __syscall(SYS_lseek, fd, off, whence, 0); }
pid_t fork(void) { return __syscall(SYS_fork, 0, 0, 0, 0); }
/* execve: a program; or a script -- a file the kernel will not run (ENOEXEC)
   that starts with #!interpreter [arg] runs as  interpreter [arg] path args,
   and one without #! runs with /bin/sh (as execvp does elsewhere) */
int execve(const char *path, char *const argv[], char *const envp[])
{
    char head[128], *interp, *arg = 0, *e, *nargv[260];
    int fd, n, k = 0, a;
    __syscall(SYS_execve, (int)path, (int)argv, (int)envp, 0);
    if (errno != ENOEXEC) return -1;
    if ((fd = open(path, O_RDONLY)) < 0) { errno = ENOEXEC; return -1; }
    n = read(fd, head, sizeof head - 1);
    close(fd);
    head[n > 0 ? n : 0] = 0;
    if (n >= 2 && head[0] == '#' && head[1] == '!') {
        for (interp = head + 2; *interp == ' ' || *interp == '\t'; interp++) ;
        for (e = interp; *e && *e != ' ' && *e != '\t' && *e != '\n'; e++) ;
        if (*e && *e != '\n') {        /* one argument may follow */
            *e++ = 0;
            while (*e == ' ' || *e == '\t') e++;
            if (*e && *e != '\n') {
                arg = e;
                while (*e && *e != '\n') e++;
                while (e > arg && (e[-1] == ' ' || e[-1] == '\t')) e--;
            }
        }
        *e = 0;
        if (!*interp) { errno = ENOEXEC; return -1; }
    } else {
        for (a = 0; a < n; a++) if (!head[a]) { errno = ENOEXEC; return -1; }   /* (binary: not a script) */
        interp = "/bin/sh";
    }
    nargv[k++] = interp;
    if (arg) nargv[k++] = arg;
    nargv[k++] = (char *)path;
    for (a = 1; argv[a] && k < 259; a++) nargv[k++] = argv[a];
    nargv[k] = 0;
    return __syscall(SYS_execve, (int)interp, (int)nargv, (int)envp, 0);
}
extern char **environ;
int execv(const char *path, char *const argv[]) { return execve(path, argv, environ); }
pid_t getpid(void) { return __syscall(SYS_getpid, 0, 0, 0, 0); }
pid_t getppid(void) { return __syscall(SYS_getppid, 0, 0, 0, 0); }
uid_t getuid(void) { return __syscall(SYS_getuid, 0, 0, 0, 0); }
uid_t geteuid(void) { return __syscall(SYS_geteuid, 0, 0, 0, 0); }
gid_t getgid(void) { return __syscall(SYS_getgid, 0, 0, 0, 0); }
gid_t getegid(void) { return __syscall(SYS_getegid, 0, 0, 0, 0); }
int setuid(uid_t uid) { return __syscall(SYS_setuid, uid, 0, 0, 0); }
int setgid(gid_t gid) { return __syscall(SYS_setgid, gid, 0, 0, 0); }
int link(const char *old, const char *new_) { return __syscall(SYS_link, (int)old, (int)new_, 0, 0); }
int unlink(const char *path) { return __syscall(SYS_unlink, (int)path, 0, 0, 0); }
int chdir(const char *path) { return __syscall(SYS_chdir, (int)path, 0, 0, 0); }
int rmdir(const char *path) { return __syscall(SYS_rmdir, (int)path, 0, 0, 0); }
int dup(int fd) { return __syscall(SYS_dup, fd, 0, 0, 0); }
int dup2(int fd, int fd2) { return __syscall(SYS_dup2, fd, fd2, 0, 0); }
int pipe(int fds[2]) { return __syscall(SYS_pipe, (int)fds, 0, 0, 0); }
int access(const char *path, int mode) { return __syscall(SYS_access, (int)path, mode, 0, 0); }
void *sbrk(int incr) { return (void *)__syscall(SYS_sbrk, incr, 0, 0, 0); }
unsigned alarm(unsigned secs) { return __syscall(SYS_alarm, secs, 0, 0, 0); }
int pause(void) { return __syscall(SYS_pause, 0, 0, 0, 0); }
int nice(int incr) { return __syscall(SYS_nice, incr, 0, 0, 0); }
void sync(void) { __syscall(SYS_sync, 0, 0, 0, 0); }
void _exit(int status) { for (;;) __syscall(SYS_exit, status, 0, 0, 0); }
