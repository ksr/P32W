/* unistd.h -- P32W C library (M2, D22): the system calls (sys/syscall.h). */
#ifndef _UNISTD_H
#define _UNISTD_H
#include <sys/types.h>
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
int __syscall(int n, int a, int b, int c, int d);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execl(const char *path, const char *arg0, ...);       /* (the list ends with (char *)0) */
int execlp(const char *file, const char *arg0, ...);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int link(const char *old, const char *new_);
int unlink(const char *path);
int reboot(int howto);                  /* root: halt, or with RB_REBOOT restart */
#define RB_REBOOT 0x100
#define RB_NOSYNC 0x200                 /* (without writing the cache out: after fsck -y on /) */
int chdir(const char *path);
int rmdir(const char *path);
int dup(int fd);
int dup2(int fd, int fd2);
int pipe(int fds[2]);
int access(const char *path, int mode);
void *sbrk(int incr);
unsigned alarm(unsigned secs);
int pause(void);
int nice(int incr);
void sync(void);
unsigned sleep(unsigned secs);
int isatty(int fd);
char *getcwd(char *buf, size_t size);
void _exit(int status);
pid_t setsid(void);
#endif
