/* proc.c -- processes, the scheduler, sleep and wakeup, signals (M3, D22;
 * os-design.md 4.2-4.4).
 *
 * The kernel is not pre-empted (V6): a process in the kernel runs until it
 * sleeps or returns to user mode; the clock only sets need_resched, and
 * trap() yields on the way out to user mode. The scheduler runs in its own
 * context on the boot stack and picks runnable processes round-robin.
 */
#include "defs.h"

struct proc proc[NPROC];
struct proc *proctop = proc;            /* past the last slot ever used: the walks that run often stop here */
struct proc *curproc;
static struct context schedctx;
static int nextpid = 1;
uint ticks;
int need_resched;
extern void trapret(void);

void procinit(void)
{
    int i;
    for (i = 0; i < NPROC; i++) {
        proc[i].state = UNUSED;
        proc[i].asid = i + 1;
    }
}

/* a slot, a pid, a kernel stack with an empty trap frame on top, and a
   context that starts in trapret */
struct proc *allocproc(void)
{
    struct proc *p;
    uint ks;
    int s = splhi();
    for (p = proc; p < &proc[NPROC]; p++)
        if (p->state == UNUSED) break;
    if (p == &proc[NPROC]) { splx(s); return NULL; }
    if (p >= proctop) proctop = p + 1;
    p->state = EMBRYO;
    splx(s);
    if (!(ks = frame_alloc())) { p->state = UNUSED; return NULL; }
    p->pid = nextpid++;
    p->kstack = (char *)ks;
    p->tf = (struct trapframe *)(ks + PGSIZE - sizeof(struct trapframe));
    memset(&p->ctx, 0, sizeof p->ctx);
    p->ctx.sp = (uint)p->tf;
    p->ctx.ra = (uint)trapret;
    p->killed = p->sig = 0;
    p->nlazy = 0;
    memset(p->sighandler, 0, sizeof p->sighandler);
    memset(p->ofile, 0, sizeof p->ofile);
    p->cwd = NULL;
    p->pt = NULL;
    p->parent = NULL;
    p->chan = NULL;
    p->tty = -1;
    p->ticks = p->utime = p->stime = p->cutime = p->cstime = 0;
    p->ntlb = p->ncow = p->nstack = p->cntlb = p->cncow = p->cnstack = 0;
    p->traced = p->stopsig = p->reported = p->contsig = 0;
    p->alarm = 0;
    p->umask = 022;
    return p;
}

/* process 1: a tiny program at 0x1000 that execs /etc/init */
static uint initcode[] = {
    /* li r1, SYS_execve; la r2, path; la r3, argv; li r4, 0; syscall; li r1, SYS_exit; syscall */
    0x2010000B,             /* addi r1, r0, 11 */
    0x2F200000,             /* lui  r2, 0 */
    0x23221020,             /* ori  r2, r2, 0x1020 (path) */
    0x2F300000,             /* lui  r3, 0 */
    0x23331040,             /* ori  r3, r3, 0x1040 (argv) */
    0x20400000,             /* addi r4, r0, 0 */
    0x70000000,             /* syscall */
    0x20100001,             /* addi r1, r0, 1 */
    0x70000000,             /* syscall */
};

void userinit(void)
{
    struct proc *p = allocproc();
    uint pa;
    if (!p) panic("userinit");
    p->pt = pt_new();
    if (!p->pt || map_page(p->pt, USERTEXT, PTE_X | PTE_W) < 0 || map_page(p->pt, USERTOP - PGSIZE, PTE_W) < 0)
        panic("userinit: no memory");
    pa = va2pa(p->pt, USERTEXT, 0);
    memcpy((void *)pa, initcode, sizeof initcode);
    memcpy((void *)(pa + 0x20), "/etc/init", 10);
    *(uint *)(pa + 0x40) = USERTEXT + 0x20;          /* argv = { "/etc/init", 0 } */
    *(uint *)(pa + 0x44) = 0;
    p->textend = p->brk = USERTEXT + PGSIZE;
    p->stackbot = USERTOP - PGSIZE;
    memset(p->tf, 0, sizeof *p->tf);
    p->tf->epc = USERTEXT;
    p->tf->usp = USERTOP;
    p->tf->status = ST_S | ST_PIE;                    /* RTE: user mode, interrupts on */
    p->cwd = namei("/", NULL);
    p->uid = p->euid = p->gid = p->egid = 0;
    safestrcpy(p->name, "init", sizeof p->name);
    p->state = RUNNABLE;
}

uint tlbmark;                           /* ntlbrefill when curproc was switched in */

void scheduler(void)
{
    struct proc *p = proc;
    for (;;) {
        int found = 0, k;
        spl0();
        splhi();
        for (k = 0; k < NPROC; k++) {
            p++;
            if (p >= proctop) p = proc;
            if (p->state != RUNNABLE) continue;
            found = 1;
            curproc = p;
            p->state = RUNNING;
            p->ticks = 0;
            need_resched = 0;
            switchuvm(p);
            tlbmark = ntlbrefill;
            swtch(&schedctx, &p->ctx);
            p->ntlb += ntlbrefill - tlbmark;    /* (start.s counts refills for all; this is p's share) */
            curproc = NULL;
            break;
        }
        if (!found) {
            spl0();
            waitirq();                  /* nothing to run: idle until an interrupt */
        }
    }
}

/* back to the scheduler (interrupts off, curproc's state already changed) */
void sched(void)
{
    int s = splhi();
    if (curproc->state == RUNNING) panic("sched: running");
    swtch(&curproc->ctx, &schedctx);
    splx(s);
}

void yield(void)
{
    int s = splhi();
    curproc->state = RUNNABLE;
    sched();
    splx(s);
}

/* sleep on chan until wakeup(chan); interrupts are off from the caller's
   check of its condition until the process is asleep */
void sleep(void *chan)
{
    int s = splhi();
    curproc->chan = chan;
    curproc->state = SLEEPING;
    sched();
    curproc->chan = NULL;
    splx(s);
}

void wakeup(void *chan)
{
    struct proc *p;
    int s = splhi();
    for (p = proc; p < proctop; p++)
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
    splx(s);
}

int fork(void)
{
    struct proc *np, *p = curproc;
    int i;
    if (!(np = allocproc())) return -EAGAIN;
    if (heap_reserve(p->nlazy) < 0) {   /* (the child's untouched heap is promised too) */
        frame_free((uint)np->kstack);
        np->state = UNUSED;
        return -ENOMEM;
    }
    if (!(np->pt = pt_copy(p->pt))) {
        heap_unreserve(p->nlazy);
        frame_free((uint)np->kstack);
        np->state = UNUSED;
        return -ENOMEM;
    }
    np->nlazy = p->nlazy;
    *np->tf = *p->tf;
    np->tf->r[1] = 0;                   /* the child's fork() returns 0 */
    np->brk = p->brk;
    np->stackbot = p->stackbot;
    np->textend = p->textend;
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i]) np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);
    np->uid = p->uid; np->euid = p->euid; np->gid = p->gid; np->egid = p->egid;
    np->umask = p->umask;
    np->tty = p->tty;
    memcpy(np->sighandler, p->sighandler, sizeof p->sighandler);
    memcpy(np->name, p->name, sizeof p->name);
    np->parent = p;
    np->state = RUNNABLE;
    return np->pid;
}

void exit(int status)
{
    struct proc *p = curproc, *q;
    int i;
    if (p == &proc[0]) panic("init exited");
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i]) { fileclose(p->ofile[i]); p->ofile[i] = NULL; }
    if (p->cwd) { iput(p->cwd); p->cwd = NULL; }
    splhi();
    for (q = proc; q < proctop; q++)
        if (q->parent == p) {
            q->parent = &proc[0];       /* orphans go to init */
            if (q->state == ZOMBIE) wakeup(&proc[0]);
        }
    p->exitstatus = status;
    p->state = ZOMBIE;
    wakeup(p->parent);
    sched();
    panic("zombie ran");
}

/* reap a child: its pid; its status to statusp (a user address, or 0) */
int wait(uint statusp)
{
    struct proc *p, *me = curproc;
    for (;;) {
        int kids = 0, s = splhi();
        for (p = proc; p < proctop; p++) {
            if (p->parent != me) continue;
            kids = 1;
            if (p->state == STOPPED && !p->reported) {     /* a traced child stopped */
                int pid = p->pid, st = p->stopsig << 8 | 0x7F;
                p->reported = 1;
                splx(s);
                if (statusp && copyout(statusp, &st, 4) < 0) return -EFAULT;
                return pid;
            }
            if (p->state == ZOMBIE) {
                int pid = p->pid, st = p->exitstatus;
                if (statusp && copyout(statusp, &st, 4) < 0) { splx(s); return -EFAULT; }   /* (first: a bad pointer loses nothing) */
                me->cutime += p->utime + p->cutime;
                me->cstime += p->stime + p->cstime;
                me->cntlb += p->ntlb + p->cntlb;
                me->cncow += p->ncow + p->cncow;
                me->cnstack += p->nstack + p->cnstack;
                pt_free(p->pt);
                heap_unreserve(p->nlazy);   /* (its untouched heap pages) */
                p->nlazy = 0;
                tlbflush();                 /* the slot's ASID will be reused */
                frame_free((uint)p->kstack);
                p->pt = NULL;
                p->kstack = NULL;
                p->parent = NULL;
                p->pid = 0;
                p->state = UNUSED;
                splx(s);
                return pid;
            }
        }
        splx(s);
        if (!kids) return -ECHILD;
        if (me->killed) return -EINTR;
        sleep(me);
    }
}

/* ---------------------------------------------------------------- signals */
void psignal(struct proc *p, int sig)
{
    if (sig <= 0 || sig >= NSIG) return;
    if (p->sighandler[sig] == (uint)SIG_IGN && sig != SIGKILL && !p->traced) return;
    if (p->state == STOPPED) {          /* a stopped (traced) process: only SIGKILL moves it */
        if (sig == SIGKILL) { p->contsig = SIGKILL; p->state = RUNNABLE; }
        return;
    }
    p->sig |= 1u << sig;                /* (pending: a set, one bit per signal) */
    p->killed = 1;
    if (p->state == SLEEPING) p->state = RUNNABLE;     /* interrupt its sleep */
}

int kill(int pid, int sig)
{
    struct proc *p;
    if (sig < 0 || sig >= NSIG) return -EINVAL;     /* (0: only whether pid exists) */
    for (p = proc; p < &proc[NPROC]; p++)
        if (p->pid == pid && p->state != UNUSED && p->state != ZOMBIE) {
            if (curproc->euid && curproc->euid != p->uid && curproc->uid != p->uid)
                return -EPERM;
            psignal(p, sig);
            return 0;
        }
    return -ESRCH;
}

/* on the way back to user mode: act on a pending signal. A handler runs in
   user mode through the library's trampoline (sys_signal records it): the
   trap frame is saved on the user stack, the handler is called with the
   signal number, and sigreturn restores the frame. */
void issig(void)
{
    struct proc *p = curproc;
    int sig;
    uint h, sp;
    p->killed = 0;
    if (!p->sig) return;
    if (p->sig & (1u << SIGKILL)) sig = SIGKILL;    /* (SIGKILL first, then the lowest) */
    else for (sig = 1; sig < NSIG && !(p->sig & (1u << sig)); sig++) ;
    p->sig &= ~(1u << sig);
    if (p->sig) p->killed = 1;          /* (more pending: the next return to user mode) */
    if (p->traced && sig != SIGKILL) {  /* traced: stop, and let the debugger decide */
        int s = splhi();
        p->stopsig = sig;
        p->reported = 0;
        p->state = STOPPED;
        wakeup(p->parent);
        sched();
        splx(s);
        sig = p->contsig;
        p->contsig = 0;
        if (!sig) return;
    }
    h = p->sighandler[sig];
    if (h == (uint)SIG_IGN && sig != SIGKILL) return;
    if (h == (uint)SIG_DFL || sig == SIGKILL || !p->sighandler[0]) {
        exit(sig);                      /* status: the signal in the low bits */
        return;
    }
    /* deliver: [user stack] = the trap frame; r1 = sig, r2 = handler, pc = trampoline */
    sp = (p->tf->usp - sizeof(struct trapframe)) & ~3;
    if (copyout(sp, p->tf, sizeof(struct trapframe)) < 0) { exit(SIGSEGV); return; }
    p->sighandler[sig] = (uint)SIG_DFL;  /* V7: reset to default when caught */
    p->tf->usp = sp;
    p->tf->r[1] = sig;
    p->tf->r[2] = h;
    p->tf->epc = p->sighandler[0];       /* slot 0 holds the trampoline */
}
