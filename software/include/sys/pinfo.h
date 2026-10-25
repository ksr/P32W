/* sys/pinfo.h -- P32W Unix: one slot of the process table (ps, top, shutdown) */
#ifndef _SYS_PINFO_H
#define _SYS_PINFO_H
struct pinfo {
    int pid;                    /* 0: a free slot */
    int ppid;
    int state;                  /* P_FREE ... P_STOPPED */
    int uid;                    /* the real user id */
    int tty;                    /* the controlling terminal's minor, -1 none */
    int size;                   /* bytes: the break plus the stack */
    unsigned time;              /* user + system CPU, in ticks (50 a second) */
    char name[16];              /* the program's name */
    unsigned ntlb;              /* TLB refills (memory touched beyond the TLB's 64 + 64 pages) */
    unsigned ncow;              /* copy-on-write page copies (after fork) */
    unsigned nstack;            /* stack pages added */
    unsigned cntlb, cncow, cnstack;     /* the same for its children, added when reaped */
};
#define P_FREE     0
#define P_EMBRYO   1            /* being created */
#define P_SLEEPING 2
#define P_RUNNABLE 3
#define P_RUNNING  4
#define P_ZOMBIE   5
#define P_STOPPED  6            /* traced */
#define NPROC_MAX  64           /* slots 0 to 63 */
int pinfo(int slot, struct pinfo *p);   /* -1 (EINVAL) past the last slot */
#endif
