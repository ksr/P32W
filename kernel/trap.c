/* trap.c -- every trap and interrupt that start.s does not finish itself
 * (M3, D22). */
#include "defs.h"

static const char *const causes[] = {
    "?", "illegal instruction", "privileged instruction", "misaligned access", "bus error",
    "syscall", "break", "?", "TLB miss (fetch)", "TLB miss (load)", "TLB miss (store)", "protection",
};

static int sync_due;

/* the load average: runnable processes, averaged over 1, 5 and 15 minutes
   (every 5 s, in 11-bit fixed point, as classic Unix keeps it) */
uint loadavg[3];

static void calc_load(void)
{
    static const uint e[3] = { 1884, 2014, 2037 };      /* 2048 * exp(-5 s / 1, 5, 15 min) */
    struct proc *p;
    uint n = 0;
    int k;
    for (p = proc; p < proctop; p++)
        if (p->state == RUNNABLE || p->state == RUNNING) n++;
    for (k = 0; k < 3; k++)
        loadavg[k] = (loadavg[k] * e[k] + n * 2048 * (2048 - e[k])) >> 11;
}

void clockintr(int user)
{
    struct proc *p;
    IOW(TCTRL) = 3;                     /* acknowledge, keep counting */
    ticks++;
    if (curproc) {                      /* CPU time: the tick goes to whoever it interrupted */
        if (user) curproc->utime++;
        else curproc->stime++;
    }
    if (curproc && ++curproc->ticks >= QUANTUM)
        need_resched = 1;
    for (p = proc; p < proctop; p++)            /* alarms */
        if (p->alarm && p->alarm <= ticks && p->state != UNUSED && p->state != ZOMBIE) {
            p->alarm = 0;
            psignal(p, SIGALRM);
        }
    net_tick();                         /* (sockets and select waiting: D45) */
    if ((ticks % (5 * HZ)) == 0)
        calc_load();
    if ((ticks % (30 * HZ)) == 0)
        sync_due = 1;                   /* write delayed blocks every 30 s (in process context) */
}

/* By the vector the card put in CAUSE (D57); by the level when no card
   answered (a card without vectors: its handlers read its status). */
static void interrupt(uint cause, int user)
{
    uint v = (cause >> 8) & 0xFF;
    switch (v) {
    case VECTOR(WIN_IO, 0): clockintr(user); return;
    case VECTOR(WIN_IO, 1): case VECTOR(WIN_IO, 2):
    case VECTOR(WIN_IO, 3): case VECTOR(WIN_IO, 4):
        ttyintr(v - VECTOR(WIN_IO, 1)); return;         /* UARTs 0-3 */
    case VECTOR(WIN_IO, 8): netintr(); return;
    case VECTOR(WIN_VIDEO, 0): kbdintr(); return;       /* the video card's PS/2 section */
    case VECTOR(WIN_VIDEO, 1): vidintr(); return;       /* ... its chip: the write FIFO is empty (D104) */
    case VEC_NONE: break;
    default: kprintf("stray interrupt vector %x\n", v); return;
    }
    switch (cause & 7) {
    case IRQ_TIMER: clockintr(user); break;
    case IRQ_UART0: ttyintr(0); ttyintr(2); break;
    case IRQ_UART1: ttyintr(1); ttyintr(3); break;
    case IRQ_NET: netintr(); break;
    case IRQ_PS2: kbdintr(); vidintr(); break;
    default: kprintf("stray interrupt %d\n", cause & 7);
    }
}

void trap(struct trapframe *tf)
{
    uint cause = tf->cause;
    int user = (tf->status & ST_PS) == 0;
    struct proc *p = curproc;

    if (cause & CAUSE_IRQ) {
        if (user) p->tf = tf;
        interrupt(cause, user);
        if (!user) return;              /* back into the kernel code it interrupted */
    } else if (!user && cause == EXC_BUSERROR && probing) {
        probe_fault = 1;                /* vm.c's probes: nothing there; skip the access */
        tf->epc += 4;
        return;
    } else if (!user) {
        kprintf("kernel trap: %s at %p, address %p\n", causes[cause < 12 ? cause : 0], tf->epc, tf->badaddr);
        panic("kernel trap");
    } else {
        p->tf = tf;
        spl0();                         /* system calls and faults run with interrupts on */
        if (cause == EXC_SYSCALL) {
            syscall(tf);
        } else if ((cause == EXC_TLBMISS_R || cause == EXC_TLBMISS_W) &&
                   (grow_heap(p, tf->badaddr) == 0 || grow_stack(p, tf->badaddr) == 0)) {
            /* a heap page made, or the stack grew: RTE re-executes the access */
        } else if (cause == EXC_PROTECT && cow_break(p->pt, tf->badaddr) == 0) {
            /* copy-on-write: the page is the writer's now; replace the TLB's read-only entry */
            tlbwrite(mfcr_tlbhi(), p->pt[tf->badaddr >> 12]);
        } else {
            int sig = cause == EXC_ILLEGAL || cause == EXC_PRIVILEGE ? SIGILL :
                      cause == EXC_BREAK ? SIGTRAP : cause == EXC_MISALIGNED || cause == EXC_BUSERROR ? SIGBUS : SIGSEGV;
            if ((p->sighandler[sig] < 2 || !p->sighandler[0]) && !p->traced)   /* (a debugger reports it) */
                kprintf("pid %d (%s): %s at %p, address %p\n", p->pid, p->name,
                        causes[cause < 12 ? cause : 0], tf->epc, tf->badaddr);
            if (p->sighandler[sig] == (uint)SIG_IGN && !p->traced)
                p->sighandler[sig] = (uint)SIG_DFL;    /* (ignored, the fault would only come again) */
            psignal(p, sig);
        }
    }
    /* on the way back to user mode */
    if (sync_due) { sync_due = 0; bflush(); }
    if (p->killed) issig();
    if (need_resched) yield();
    if (p->killed) issig();
    splhi();
}
