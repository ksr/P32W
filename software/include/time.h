/* time.h -- P32W Unix (D22) */
#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
time_t time(time_t *t);
int stime(time_t *t);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);  /* by TZ, or /etc/TZ (else UTC) */
void tzset(void);
extern char *tzname[2];
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);
time_t timegm(struct tm *tm);       /* the seconds since 1970 of a UTC time */
time_t mktime(struct tm *tm);       /* a local time (TZ) as seconds since 1970 */
typedef long clock_t;
#define CLOCKS_PER_SEC 50           /* (the clock's ticks: CLK_TCK) */
clock_t clock(void);                /* the CPU time used, user and system */
double difftime(time_t t1, time_t t0);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
#endif
