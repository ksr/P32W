#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H
/* CPU time in clock ticks: HZ (CLK_TCK) a second */
#define CLK_TCK 50
struct tms { unsigned tms_utime, tms_stime, tms_cutime, tms_cstime; };
long times(struct tms *buf);        /* returns the ticks since boot */
#endif
