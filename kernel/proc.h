/* proc.h -- processes (M3, D22; os-design.md 4.2) */

/* What trap entry saves on the kernel stack (start.s builds and restores it). */
struct trapframe {
    uint r[15];             /* r[1]..r[14]; r[0] unused */
    uint usp;               /* the user stack pointer (r15 in user mode) */
    uint epc;
    uint status;
    uint cause;
    uint badaddr;
};

/* What swtch() saves: the callee-saved registers, sp and where to resume. */
struct context {
    uint r8, r9, r10, r11, r12, r13, r14, sp, ra;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE, STOPPED };

struct proc {
    enum procstate state;
    int pid;
    struct proc *parent;
    int exitstatus;
    void *chan;             /* sleeping on */
    int killed;             /* a signal to act on before returning to user mode */
    int sig;                /* the pending signals: bit n for signal n */
    uint sighandler[NSIG];  /* SIG_DFL (0), SIG_IGN (1) or a user address */
    uint asid;
    uint *pt;               /* page table: NPTE PTEs (physical address of PTFRAMES contiguous frames) */
    uint brk;               /* end of the heap (user address) */
    uint nlazy;             /* heap pages promised by sbrk, not yet touched (vm.c: reserved, mapped on use) */
    uint stackbot;          /* lowest mapped stack page */
    uint textend;           /* end of the (read-only) text */
    char *kstack;           /* bottom of the kernel stack (one frame) */
    struct trapframe *tf;
    struct context ctx;
    struct file *ofile[NOFILE];
    struct inode *cwd;
    ushort uid, euid, gid, egid;
    ushort umask;
    int tty;                /* controlling terminal (minor) or -1 */
    int ticks;              /* in this time slice */
    uint alarm;             /* clock tick when SIGALRM is due, 0 = none */
    uint utime, stime;      /* clock ticks charged in user mode, in the kernel */
    int traced;             /* ptrace: a debugger is its parent; it stops at each signal */
    int stopsig, reported;  /* STOPPED: by which signal; has wait() told the parent yet */
    int contsig;            /* the signal its debugger lets through when it continues (0: none) */
    uint cutime, cstime;    /* the same for its children, added when reaped */
    uint ntlb, ncow, nstack;        /* memory events: TLB refills, copy-on-write page copies,
                                       stack pages added (the backlog's memory statistics) */
    uint cntlb, cncow, cnstack;     /* the same for its children, added when reaped */
    char name[16];
};

extern struct proc proc[NPROC], *proctop;
extern uint ntlbrefill, ncowcopy, nstackpage;     /* the same, for the whole system */
extern uint tlbmark;                               /* ntlbrefill when curproc was switched in */
extern struct proc *curproc;
