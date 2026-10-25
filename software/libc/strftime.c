/* strftime.c -- P32W C library: clock, difftime and strftime (<time.h>).
 * strftime knows C99's conversions and POSIX's (%e %G %g %V %u %z ...), in
 * the "C" locale; %E and %O are accepted and ignored. */
#include <time.h>
#include <string.h>
#include <sys/times.h>

clock_t clock(void)                         /* CPU time in ticks (CLOCKS_PER_SEC a second) */
{
    struct tms t;
    times(&t);
    return (clock_t)(t.tms_utime + t.tms_stime);
}

double difftime(time_t t1, time_t t0) { return (double)t1 - (double)t0; }

static const char *const wdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const months[] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };

struct out { char *p; size_t n, max; int over; };

static void put(struct out *o, const char *s, int n)        /* n < 0: all of s */
{
    if (n < 0) n = strlen(s);
    while (n-- > 0) {
        if (o->n + 1 >= o->max) { o->over = 1; return; }
        o->p[o->n++] = *s++;
    }
}

static void num(struct out *o, int v, int width, int pad)
{
    char t[16];
    int k = 0, neg = v < 0;
    unsigned u = neg ? 0u - (unsigned)v : (unsigned)v;
    do { t[k++] = '0' + u % 10; u /= 10; } while (u);
    while (k < width) t[k++] = pad;
    if (neg) t[k++] = '-';
    while (k > 0) put(o, &t[--k], 1);
}

static int isleap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* ISO 8601: the week (1-53) and its year; weeks start on Monday, and
   week 1 holds the year's first Thursday */
static int isoweek(const struct tm *tm, int *year)
{
    int y = tm->tm_year + 1900, wday = (tm->tm_wday + 6) % 7, wk = (tm->tm_yday - wday + 10) / 7;
    if (wk < 1) {                           /* the last week of the year before */
        *year = y - 1;
        return (tm->tm_yday + 365 + isleap(y - 1) - wday + 10) / 7;
    }
    if (wk == 53 && tm->tm_yday - wday + 3 >= 365 + isleap(y)) { *year = y + 1; return 1; }
    *year = y;
    return wk;
}

static void format(struct out *o, const char *f, const struct tm *tm);

static void conv(struct out *o, int c, const struct tm *tm)
{
    int h12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, y;
    switch (c) {
    case 'a': put(o, wdays[tm->tm_wday % 7], 3); break;
    case 'A': put(o, wdays[tm->tm_wday % 7], -1); break;
    case 'b': case 'h': put(o, months[tm->tm_mon % 12], 3); break;
    case 'B': put(o, months[tm->tm_mon % 12], -1); break;
    case 'c': format(o, "%a %b %e %H:%M:%S %Y", tm); break;
    case 'C': num(o, (tm->tm_year + 1900) / 100, 2, '0'); break;
    case 'd': num(o, tm->tm_mday, 2, '0'); break;
    case 'D': case 'x': format(o, "%m/%d/%y", tm); break;
    case 'e': num(o, tm->tm_mday, 2, ' '); break;
    case 'F': format(o, "%Y-%m-%d", tm); break;
    case 'g': isoweek(tm, &y); num(o, y % 100, 2, '0'); break;
    case 'G': isoweek(tm, &y); num(o, y, 1, '0'); break;
    case 'H': num(o, tm->tm_hour, 2, '0'); break;
    case 'I': num(o, h12, 2, '0'); break;
    case 'j': num(o, tm->tm_yday + 1, 3, '0'); break;
    case 'm': num(o, tm->tm_mon + 1, 2, '0'); break;
    case 'M': num(o, tm->tm_min, 2, '0'); break;
    case 'n': put(o, "\n", 1); break;
    case 'p': put(o, tm->tm_hour < 12 ? "AM" : "PM", 2); break;
    case 'r': format(o, "%I:%M:%S %p", tm); break;
    case 'R': format(o, "%H:%M", tm); break;
    case 'S': num(o, tm->tm_sec, 2, '0'); break;
    case 't': put(o, "\t", 1); break;
    case 'T': case 'X': format(o, "%H:%M:%S", tm); break;
    case 'u': num(o, tm->tm_wday ? tm->tm_wday : 7, 1, '0'); break;
    case 'U': num(o, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0'); break;
    case 'V': num(o, isoweek(tm, &y), 2, '0'); break;
    case 'w': num(o, tm->tm_wday, 1, '0'); break;
    case 'W': num(o, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, '0'); break;
    case 'y': num(o, (tm->tm_year + 1900) % 100, 2, '0'); break;
    case 'Y': num(o, tm->tm_year + 1900, 1, '0'); break;
    case 'z': {                             /* the zone's offset from UTC: +hhmm */
        struct tm t = *tm;
        time_t loc = mktime(&t), utc;
        int off;
        t = *tm;
        utc = timegm(&t);
        off = (int)(utc - loc) / 60;
        put(o, off < 0 ? "-" : "+", 1);
        if (off < 0) off = -off;
        num(o, off / 60 * 100 + off % 60, 4, '0');
        break;
    }
    case 'Z': tzset(); put(o, tzname[tm->tm_isdst > 0], -1); break;
    case '%': put(o, "%", 1); break;
    default: { char ch = c; put(o, "%", 1); put(o, &ch, 1); break; }
    }
}

static void format(struct out *o, const char *f, const struct tm *tm)
{
    for (; *f; f++) {
        if (*f != '%') { put(o, f, 1); continue; }
        if (*++f == 'E' || *f == 'O') f++;  /* (the C locale has no alternatives) */
        if (!*f) break;
        conv(o, *f, tm);
    }
}

/* 0 if the result (with its '\0') does not fit in max */
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    struct out o;
    if (max == 0) return 0;
    o.p = s; o.n = 0; o.max = max; o.over = 0;
    format(&o, fmt, tm);
    s[o.n] = '\0';
    return o.over ? 0 : o.n;
}
