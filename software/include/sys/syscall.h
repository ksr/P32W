/* sys/syscall.h -- P32W Unix system-call numbers (D22, os-design.md 4.4).
 * Unix V7's numbers where V7 has the call. The calling convention:
 * SYSCALL with the number in r1 and arguments in r2-r5; the result comes
 * back in r1, a failure as -errno; every other register is preserved.
 * The C library's __syscall(n, a, b, c, d) is exactly this. */
#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#define SYS_exit      1
#define SYS_fork      2
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_wait      7
#define SYS_creat     8
#define SYS_link      9
#define SYS_unlink   10
#define SYS_execve   11
#define SYS_chdir    12
#define SYS_time     13
#define SYS_mknod    14
#define SYS_chmod    15
#define SYS_chown    16
#define SYS_sbrk     17
#define SYS_stat     18
#define SYS_lseek    19
#define SYS_mount    21
#define SYS_umount   22
#define SYS_getpid   20
#define SYS_setuid   23
#define SYS_getuid   24
#define SYS_stime    25
#define SYS_ptrace   26
#define SYS_alarm    27
#define SYS_fstat    28
#define SYS_pause    29
#define SYS_utime    30
#define SYS_access   33
#define SYS_nice     34
#define SYS_sync     36
#define SYS_kill     37
#define SYS_getppid  39
#define SYS_dup      41
#define SYS_pipe     42
#define SYS_times    43
#define SYS_setgid   46
#define SYS_getgid   47
#define SYS_signal   48
#define SYS_geteuid  49
#define SYS_getegid  50
#define SYS_ioctl    54
#define SYS_umask    60
#define SYS_mkdir    61
#define SYS_rmdir    62
#define SYS_rename   63
#define SYS_dup2     64
#define SYS_uname    65
#define SYS_sigreturn 66
#define SYS_reboot   70
#define SYS_pinfo    71        /* P32W: one process-table slot, for ps */
#define SYS_sysinfo  73
#define SYS_ustat    74
#define SYS_setsid   72        /* leave the controlling terminal */

/* networking (D45): 4.2BSD's numbers; sendto and recvfrom take an argument
   block (struct __sendto, __recvfrom), setsockopt and getsockopt level << 16 | name */
#define SYS_fcntl       92
#define SYS_select      93
#define SYS_socket      97
#define SYS_connect     98
#define SYS_accept      99
#define SYS_bind        104
#define SYS_setsockopt  105
#define SYS_listen      106
#define SYS_getsockopt  118
#define SYS_recvfrom    125
#define SYS_sendto      133
#define SYS_shutdown    134
#define SYS_getpeername 141
#define SYS_getsockname 150
#define SYS_netctl      160       /* P32W: the interface, and host names */

#endif
