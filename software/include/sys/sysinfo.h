/* sys/sysinfo.h -- P32W Unix: the system's state (uptime, top) */
#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H
struct sysinfo {
    unsigned uptime;            /* seconds since boot */
    unsigned totalram, freeram; /* bytes */
    unsigned procs;             /* processes */
    unsigned loads[3];          /* the load average over 1, 5, 15 minutes, x 2048 */
    unsigned tlbrefills;        /* since boot: TLB refills, */
    unsigned cowcopies;         /* copy-on-write page copies, */
    unsigned stackpages;        /* stack pages added */
};
int sysinfo(struct sysinfo *info);
#endif
