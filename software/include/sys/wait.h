/* sys/wait.h -- P32W Unix (D22). status = exit code << 8, or the signal that killed. */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#include <sys/types.h>
#define WIFEXITED(s)   (((s) & 0x7F) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s) (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#define WIFSTOPPED(s)  (((s) & 0xFF) == 0x7F)        /* a traced child stopped (ptrace) */
#define WSTOPSIG(s)    (((s) >> 8) & 0xFF)
#define WTERMSIG(s)    ((s) & 0x7F)
pid_t wait(int *status);
#endif
