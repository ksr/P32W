/* date -- print or set the date and time. The clock keeps UTC (D23); date
 * shows and takes local time by TZ (or /etc/TZ), and names the zone when
 * there is one.
 *   date [-u] [-r seconds] [+format]   print it (-u: in UTC; -r: that time, not now)
 *   date [-u] [cc]yymmddhhmm[.ss]      set it (root), as Unix V7: e.g. date 2610141230
 *   date [-u] -s seconds               set it to seconds since 1970 (root)
 * The format is copied with % fields replaced, as strftime(3): see date(1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const months[] = { "January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December" };

static int usage(void)
{
    fprintf(stderr, "usage: date [-u] [-r seconds] [+format]\n"
                    "       date [-u] [cc]yymmddhhmm[.ss] | date [-u] -s seconds\n");
    return 2;
}

static int two(const char *s) { return (s[0] - '0') * 10 + s[1] - '0'; }

static int seconds(const char *s, time_t *t)    /* digits, 0 to 2^31-1; else -1 */
{
    unsigned long v = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > (2147483647UL - (*s - '0')) / 10) return -1;
        v = v * 10 + (*s - '0');
    }
    *t = (time_t)v;
    return 0;
}

static int mdays(int mon, int year)     /* mon 0-11 */
{
    static const int len[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return len[mon] + (mon == 1 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static void put2(int v, int pad) { if (v < 10 && pad) putchar(pad); printf("%d", v); }

static void format(const char *f, struct tm *tm, time_t t, const char *zone)
{
    int h12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
    for (; *f; f++) {
        if (*f != '%' || !f[1]) { putchar(*f); continue; }
        switch (*++f) {
        case 'a': printf("%.3s", days[tm->tm_wday]); break;
        case 'A': fputs(days[tm->tm_wday], stdout); break;
        case 'b': case 'h': printf("%.3s", months[tm->tm_mon]); break;
        case 'B': fputs(months[tm->tm_mon], stdout); break;
        case 'c': format("%a %b %e %H:%M:%S %Y", tm, t, zone); break;
        case 'C': put2((tm->tm_year + 1900) / 100, '0'); break;
        case 'd': put2(tm->tm_mday, '0'); break;
        case 'D': format("%m/%d/%y", tm, t, zone); break;
        case 'e': put2(tm->tm_mday, ' '); break;
        case 'F': format("%Y-%m-%d", tm, t, zone); break;
        case 'H': put2(tm->tm_hour, '0'); break;
        case 'I': put2(h12, '0'); break;
        case 'j': printf("%03d", tm->tm_yday + 1); break;
        case 'k': put2(tm->tm_hour, ' '); break;
        case 'l': put2(h12, ' '); break;
        case 'm': put2(tm->tm_mon + 1, '0'); break;
        case 'M': put2(tm->tm_min, '0'); break;
        case 'n': putchar('\n'); break;
        case 'p': fputs(tm->tm_hour < 12 ? "AM" : "PM", stdout); break;
        case 'r': format("%I:%M:%S %p", tm, t, zone); break;
        case 'R': format("%H:%M", tm, t, zone); break;
        case 's': printf("%ld", (long)t); break;
        case 'S': put2(tm->tm_sec, '0'); break;
        case 't': putchar('\t'); break;
        case 'T': format("%H:%M:%S", tm, t, zone); break;
        case 'u': printf("%d", tm->tm_wday ? tm->tm_wday : 7); break;
        case 'w': printf("%d", tm->tm_wday); break;
        case 'x': format("%m/%d/%y", tm, t, zone); break;
        case 'X': format("%H:%M:%S", tm, t, zone); break;
        case 'y': put2(tm->tm_year % 100, '0'); break;
        case 'Y': printf("%d", tm->tm_year + 1900); break;
        case 'Z': fputs(zone, stdout); break;
        case '%': putchar('%'); break;
        default: putchar('%'); putchar(*f); break;  /* (unknown: as typed) */
        }
    }
}

int main(int argc, char **argv)
{
    time_t t;
    int utc = 0, set = 0, at = 0, i;
    char *fmt = 0, *setarg = 0;
    struct tm *tm;
    const char *zone;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "-u")) utc = 1;
        else if (!strcmp(a, "-s") || !strcmp(a, "-r")) {
            if (i + 1 >= argc) return usage();
            if (seconds(argv[++i], &t) < 0) { fprintf(stderr, "date: %s: bad number of seconds\n", argv[i]); return 2; }
            if (a[1] == 's') set = 1; else at = 1;
        } else if (a[0] == '+' && !fmt) fmt = a + 1;
        else if (a[0] >= '0' && a[0] <= '9' && !setarg) setarg = a;
        else return usage();
    }
    if ((set != 0) + (at != 0) + (setarg != 0) > 1 || (fmt && (set || setarg))) return usage();
    if (setarg) {
        char *a = setarg, *dot = strchr(a, '.');
        int n = dot ? (int)(dot - a) : (int)strlen(a), k, year;
        struct tm st;
        for (k = 0; k < n; k++) if (a[k] < '0' || a[k] > '9') break;
        if (k != n || (n != 10 && n != 12) || (dot && (strlen(dot) != 3 || dot[1] < '0' || dot[1] > '9' ||
                                                       dot[2] < '0' || dot[2] > '9')))
            return usage();
        memset(&st, 0, sizeof st);
        if (n == 12) { year = two(a) * 100 + two(a + 2); a += 4; }
        else { year = 2000 + two(a); a += 2; }                 /* yy: 2000-2099 */
        st.tm_year = year - 1900;
        st.tm_mon = two(a) - 1; st.tm_mday = two(a + 2); st.tm_hour = two(a + 4); st.tm_min = two(a + 6);
        st.tm_sec = dot ? two(dot + 1) : 0;
        if (year < 1970 || year > 2037 || st.tm_mon < 0 || st.tm_mon > 11 || st.tm_mday < 1 ||
            st.tm_mday > mdays(st.tm_mon, year) || st.tm_hour > 23 || st.tm_min > 59 || st.tm_sec > 59) {
            fprintf(stderr, "date: %s: bad date\n", setarg);
            return 2;
        }
        t = utc ? timegm(&st) : mktime(&st);
        if (t < 0) { fprintf(stderr, "date: %s: bad date\n", setarg); return 2; }
        set = 1;
    }
    if (set && stime(&t) < 0) { perror("date"); return 1; }
    if (!at) t = time(0);
    tm = utc ? gmtime(&t) : localtime(&t);
    tzset();
    zone = utc ? "UTC" : tzname[tm->tm_isdst != 0];
    if (fmt) {
        format(fmt, tm, t, zone);
        putchar('\n');
    } else {
        char *s = asctime(tm);
        if (!utc && strcmp(zone, "UTC"))               /* Wed Oct 14 12:30:45 PDT 2026 */
            printf("%.20s%s %s", s, zone, s + 20);
        else
            fputs(s, stdout);
    }
    return 0;
}
