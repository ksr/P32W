/* signal.h -- P32W Unix signals (D22): Unix V7's numbers, and 4.3BSD's for what came after (D126). */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#define NSIG     32
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGEMT    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGBUS   10
#define SIGSEGV  11
#define SIGSYS   12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGWINCH 28         /* the terminal's size has changed: nothing happens unless it is caught */
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
typedef int sig_atomic_t;
sighandler_t signal(int sig, sighandler_t handler);
int kill(int pid, int sig);
int raise(int sig);
#endif
