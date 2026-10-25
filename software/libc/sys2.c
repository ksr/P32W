/* sys2.c -- P32W C library (D22): more system calls and their helpers. */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sgtty.h>
#include <time.h>
#include <dirent.h>

extern void __sigtramp(void);

pid_t wait(int *status) { return __syscall(SYS_wait, (int)status, 0, 0, 0); }
int kill(int pid, int sig) { return __syscall(SYS_kill, pid, sig, 0, 0); }
int raise(int sig) { return kill(getpid(), sig); }
sighandler_t signal(int sig, sighandler_t h)
{
    return (sighandler_t)__syscall(SYS_signal, sig, (int)h, (int)__sigtramp, 0);
}
int stat(const char *path, struct stat *st) { return __syscall(SYS_stat, (int)path, (int)st, 0, 0); }
int fstat(int fd, struct stat *st) { return __syscall(SYS_fstat, fd, (int)st, 0, 0); }
int chmod(const char *path, int mode) { return __syscall(SYS_chmod, (int)path, mode, 0, 0); }
int chown(const char *path, int uid, int gid) { return __syscall(SYS_chown, (int)path, uid, gid, 0); }
int mkdir(const char *path, int mode) { return __syscall(SYS_mkdir, (int)path, mode, 0, 0); }
int mknod(const char *path, int mode, int dev) { return __syscall(SYS_mknod, (int)path, mode, dev, 0); }
int umask(int mask) { return __syscall(SYS_umask, mask, 0, 0, 0); }
int rename(const char *a, const char *b) { return __syscall(SYS_rename, (int)a, (int)b, 0, 0); }
time_t time(time_t *t) { return __syscall(SYS_time, (int)t, 0, 0, 0); }
int stime(time_t *t) { return __syscall(SYS_stime, (int)t, 0, 0, 0); }
int ioctl(int fd, int cmd, void *arg) { return __syscall(SYS_ioctl, fd, cmd, (int)arg, 0); }
int gtty(int fd, struct sgttyb *b) { return ioctl(fd, TIOCGETP, b); }
int stty(int fd, struct sgttyb *b) { return ioctl(fd, TIOCSETP, b); }
int isatty(int fd) { struct sgttyb b; return gtty(fd, &b) == 0; }
pid_t setsid(void) { return __syscall(SYS_setsid, 0, 0, 0, 0); }
#include <sys/utsname.h>
int uname(struct utsname *u) { return __syscall(SYS_uname, (int)u, 0, 0, 0); }

static void onalarm(int sig) { (void)sig; }
unsigned sleep(unsigned secs)
{
    time_t end = time(0) + secs;
    sighandler_t old = signal(SIGALRM, onalarm);
    while (time(0) < end) {
        alarm(end - time(0));
        pause();
        signal(SIGALRM, onalarm);       /* (a caught signal reverts to the default) */
    }
    alarm(0);
    signal(SIGALRM, old);
    return 0;
}

int execvp(const char *file, char *const argv[])
{
    char buf[256], *path, *p;
    int denied = 0;                     /* (a file found but not runnable: EACCES, not ENOENT) */
    if (strchr(file, '/')) return execv(file, argv);
    if (strlen(file) > 50) { errno = ENAMETOOLONG; return -1; }    /* (names have at most 27; buf: 200 + / + 50 + NUL) */
    path = getenv("PATH");
    if (!path) path = "/bin:/usr/bin";
    while (*path) {
        int n = 0;
        p = buf;
        while (*path && *path != ':' && n < 200) { *p++ = *path++; n++; }
        if (*path == ':') path++;
        if (n) *p++ = '/';
        strcpy(p, file);
        execv(buf, argv);
        if (errno == EACCES) denied = 1;
        else if (errno != ENOENT) return -1;
    }
    errno = denied ? EACCES : ENOENT;
    return -1;
}

/* ---------------------------------------------------------------- directories */
DIR *opendir(const char *path)
{
    DIR *d;
    struct stat st;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode)) { close(fd); errno = ENOTDIR; return NULL; }
    d = malloc(sizeof *d);
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    return d;
}
struct dirent *readdir(DIR *d)
{
    struct { unsigned ino; char name[28]; } e;
    for (;;) {
        if (read(d->fd, &e, sizeof e) != sizeof e) return NULL;
        if (e.ino) break;
    }
    d->ent.d_ino = e.ino;
    memcpy(d->ent.d_name, e.name, 28);
    d->ent.d_name[28] = 0;
    return &d->ent;
}
int closedir(DIR *d) { int r = close(d->fd); free(d); return r; }

/* the current directory, by walking up through .. (a path longer than 255,
   or a size too small for it: ERANGE; buf NULL: malloc'd, size at least) */
char *getcwd(char *buf, size_t size)
{
    char path[256], tmp[256], dots[256];
    struct stat cur, parent, root;
    DIR *d;
    struct dirent *e;
    path[0] = 0;
    strcpy(dots, ".");
    if (stat("/", &root) < 0 || stat(".", &cur) < 0) return NULL;
    while (cur.st_ino != root.st_ino || cur.st_dev != root.st_dev) {
        if (strlen(dots) + 4 > sizeof dots) { errno = ERANGE; return NULL; }
        strcat(dots, "/..");
        if (stat(dots, &parent) < 0 || !(d = opendir(dots))) return NULL;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (parent.st_dev == cur.st_dev) { if (e->d_ino == cur.st_ino) break; }
            else {                      /* a mount point: its entry names the directory below it */
                struct stat st;
                if (strlen(dots) + strlen(e->d_name) + 2 > sizeof tmp) continue;
                strcpy(tmp, dots); strcat(tmp, "/"); strcat(tmp, e->d_name);
                if (stat(tmp, &st) == 0 && st.st_dev == cur.st_dev && st.st_ino == cur.st_ino) break;
            }
        }
        if (!e) { closedir(d); return NULL; }
        if (strlen(e->d_name) + strlen(path) + 2 > sizeof tmp) { closedir(d); errno = ERANGE; return NULL; }
        strcpy(tmp, "/");
        strcat(tmp, e->d_name);
        strcat(tmp, path);
        strcpy(path, tmp);
        closedir(d);
        cur = parent;
    }
    if (!path[0]) strcpy(path, "/");
    if (!buf) {
        if (size < strlen(path) + 1) size = strlen(path) + 1;
        if (!(buf = malloc(size))) return NULL;
    } else if (strlen(path) + 1 > size) { errno = ERANGE; return NULL; }
    strcpy(buf, path);
    return buf;
}

/* ---------------------------------------------------------------- time */
static struct tm tmbuf;
struct tm *gmtime(const time_t *t)
{
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    long s = *t, days = s / 86400, y = 1970;
    s %= 86400;
    tmbuf.tm_hour = s / 3600;
    tmbuf.tm_min = s / 60 % 60;
    tmbuf.tm_sec = s % 60;
    tmbuf.tm_wday = (days + 4) % 7;
    for (;;) {
        int len = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < len) break;
        days -= len;
        y++;
    }
    tmbuf.tm_year = y - 1900;
    tmbuf.tm_yday = days;
    for (tmbuf.tm_mon = 0; ; tmbuf.tm_mon++) {
        int len = mdays[tmbuf.tm_mon] + (tmbuf.tm_mon == 1 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        if (days < len) break;
        days -= len;
    }
    tmbuf.tm_mday = days + 1;
    tmbuf.tm_isdst = 0;
    return &tmbuf;
}
char *asctime(const struct tm *tm)
{
    static char buf[32];
    static const char *const day[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char *p = buf;
    const char *s;
    for (s = day[tm->tm_wday]; *s; ) *p++ = *s++;
    *p++ = ' ';
    for (s = mon[tm->tm_mon]; *s; ) *p++ = *s++;
    p[0] = ' '; p[1] = '0' + tm->tm_mday / 10; p[2] = '0' + tm->tm_mday % 10; p[3] = ' ';
    p[4] = '0' + tm->tm_hour / 10; p[5] = '0' + tm->tm_hour % 10; p[6] = ':';
    p[7] = '0' + tm->tm_min / 10; p[8] = '0' + tm->tm_min % 10; p[9] = ':';
    p[10] = '0' + tm->tm_sec / 10; p[11] = '0' + tm->tm_sec % 10; p[12] = ' ';
    {
        int y = tm->tm_year + 1900;
        p[13] = '0' + y / 1000; p[14] = '0' + y / 100 % 10; p[15] = '0' + y / 10 % 10; p[16] = '0' + y % 10;
    }
    p[17] = '\n'; p[18] = 0;
    if (buf[8] == '0') buf[8] = ' ';
    return buf;
}
char *ctime(const time_t *t) { return asctime(localtime(t)); }

time_t timegm(struct tm *tm)
{
    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday, era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (time_t)(era * 146097 + doe - 719468) * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}
/* ---------------------------------------------------------------- time zones
   The kernel keeps UTC. Local time follows TZ (the environment, else the
   first line of /etc/TZ; neither: UTC), a POSIX rule: std offset [dst
   [offset] [,start[/time],end[/time]]], e.g. "PST8PDT,M3.2.0,M11.1.0" or
   "CET-1CEST,M3.5.0,M10.5.0/3"; offsets are west of Greenwich (hours[:mm]);
   a rule Mm.w.d is the w-th (5: last) weekday d of month m; times 02:00.
   With a dst name and no rule, the US rule. */
char *tzname[2] = { "UTC", "UTC" };
static int tz_std, tz_dst, tz_hasdst;   /* seconds west of UTC */
static int tz_rule[2][4];               /* start, end: month, week, day, seconds into the day */
static int tz_done;

static const char *tz_name(const char *s, char *out)
{
    int n = 0;
    if (*s == '<') {
        s++;
        while (*s && *s != '>' && n < 15) out[n++] = *s++;
        if (*s == '>') s++;
    } else
        while (*s && ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) && n < 15) out[n++] = *s++;
    out[n] = 0;
    return s;
}

static const char *tz_time(const char *s, int *v)    /* [+-]hh[:mm[:ss]] in seconds */
{
    int sign = 1, h = 0, m = 0, sec = 0;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') h = h * 10 + *s++ - '0';
    if (*s == ':') { s++; while (*s >= '0' && *s <= '9') m = m * 10 + *s++ - '0'; }
    if (*s == ':') { s++; while (*s >= '0' && *s <= '9') sec = sec * 10 + *s++ - '0'; }
    *v = sign * (h * 3600 + m * 60 + sec);
    return s;
}

static const char *tz_ruleparse(const char *s, int *r)
{
    r[3] = 7200;
    if (*s != 'M') return s;
    s++;
    r[0] = atoi(s); while (*s >= '0' && *s <= '9') s++;
    if (*s == '.') s++;
    r[1] = atoi(s); while (*s >= '0' && *s <= '9') s++;
    if (*s == '.') s++;
    r[2] = atoi(s); while (*s >= '0' && *s <= '9') s++;
    if (*s == '/') s = tz_time(s + 1, &r[3]);
    return s;
}

void tzset(void)
{
    static char names[2][16], line[64];
    const char *s = getenv("TZ");
    tz_done = 1;
    tz_std = tz_dst = tz_hasdst = 0;
    tzname[0] = tzname[1] = "UTC";
    if (!s || !*s) {
        int fd = open("/etc/TZ", 0), n;
        if (fd < 0) return;
        n = read(fd, line, sizeof line - 1);
        close(fd);
        if (n <= 0) return;
        line[n] = 0;
        for (n = 0; line[n] && line[n] != '\n'; n++) ;
        line[n] = 0;
        s = line;
    }
    if (*s == ':') s++;
    s = tz_name(s, names[0]);
    if (!names[0][0]) return;
    s = tz_time(s, &tz_std);
    tzname[0] = tzname[1] = names[0];
    tz_dst = tz_std;
    if (!*s || *s == ',') return;
    s = tz_name(s, names[1]);
    if (!names[1][0]) return;
    tzname[1] = names[1];
    tz_hasdst = 1;
    tz_dst = tz_std - 3600;
    if (*s && *s != ',') s = tz_time(s, &tz_dst);
    tz_rule[0][0] = 3; tz_rule[0][1] = 2; tz_rule[0][2] = 0; tz_rule[0][3] = 7200;   /* (the US rule) */
    tz_rule[1][0] = 11; tz_rule[1][1] = 1; tz_rule[1][2] = 0; tz_rule[1][3] = 7200;
    if (*s == ',') s = tz_ruleparse(s + 1, tz_rule[0]);
    if (*s == ',') s = tz_ruleparse(s + 1, tz_rule[1]);
}

static long tz_when(int year, int *r)   /* a rule's moment in year, as seconds of local time since 1970 */
{
    struct tm tm;
    int first, day, len;
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    memset(&tm, 0, sizeof tm);
    tm.tm_year = year - 1900;
    tm.tm_mon = r[0] - 1;
    tm.tm_mday = 1;
    first = (int)((timegm(&tm) / 86400 + 4) % 7);      /* the weekday of the 1st */
    day = 1 + (r[2] - first + 7) % 7 + (r[1] - 1) * 7;
    len = mdays[r[0] - 1] + (r[0] == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    while (day > len) day -= 7;
    tm.tm_mday = day;
    return (long)timegm(&tm) + r[3];
}

static int tz_isdst(time_t t)           /* is UTC moment t in summer time? */
{
    long start, end;
    int year;
    if (!tz_hasdst) return 0;
    year = gmtime(&t)->tm_year + 1900;
    start = tz_when(year, tz_rule[0]) + tz_std;        /* (the start is given in standard time, */
    end = tz_when(year, tz_rule[1]) + tz_dst;          /*  the end in summer time) */
    if (start < end) return (long)t >= start && (long)t < end;
    return !((long)t >= end && (long)t < start);       /* (the southern hemisphere) */
}

struct tm *localtime(const time_t *t)
{
    time_t l;
    int dst;
    struct tm *tm;
    if (!tz_done) tzset();
    dst = tz_isdst(*t);
    l = *t - (dst ? tz_dst : tz_std);
    tm = gmtime(&l);
    tm->tm_isdst = dst;
    return tm;
}

time_t mktime(struct tm *tm)            /* local time -> seconds since 1970 (UTC) */
{
    time_t l = timegm(tm), t;
    if (!tz_done) tzset();
    t = l + tz_std;
    if (tz_hasdst && tz_isdst(l + tz_dst)) t = l + tz_dst;    /* (read as summer time, it is summer time) */
    *tm = *localtime(&t);
    return t;
}

#include <sys/times.h>
#include <utime.h>
long times(struct tms *buf) { return __syscall(SYS_times, (int)buf, 0, 0, 0); }
int utime(const char *path, const struct utimbuf *t) { return __syscall(SYS_utime, (int)path, (int)t, 0, 0); }

#include <stdarg.h>
#define EXECL_MAX 32
static int execl_(const char *path, int search, const char *arg0, va_list ap)
{
    char *argv[EXECL_MAX + 1];
    int n = 0;
    argv[n++] = (char *)arg0;
    while (arg0 && n < EXECL_MAX && (argv[n] = va_arg(ap, char *)) != 0) n++;
    argv[n] = 0;
    return search ? execvp(path, argv) : execv(path, argv);
}
int execl(const char *path, const char *arg0, ...)
{
    va_list ap;
    int r;
    va_start(ap, arg0);
    r = execl_(path, 0, arg0, ap);
    va_end(ap);
    return r;
}
int execlp(const char *file, const char *arg0, ...)
{
    va_list ap;
    int r;
    va_start(ap, arg0);
    r = execl_(file, 1, arg0, ap);
    va_end(ap);
    return r;
}

#include <sys/sysinfo.h>
int sysinfo(struct sysinfo *info) { return __syscall(SYS_sysinfo, (int)info, sizeof *info, 0, 0); }

#include <sys/ptrace.h>
int ptrace(int req, int pid, unsigned addr, unsigned data) { return __syscall(SYS_ptrace, req, pid, (int)addr, (int)data); }

#include <sys/mount.h>
int mount(const char *special, const char *dir, int flags) { return __syscall(SYS_mount, (int)special, (int)dir, flags, 0); }
int umount(const char *special) { return __syscall(SYS_umount, (int)special, 0, 0, 0); }

#include <sys/ustat.h>
int ustat(int dev, struct ustat *u) { return __syscall(SYS_ustat, dev, (int)u, 0, 0); }
int reboot(int howto) { return __syscall(SYS_reboot, howto, 0, 0, 0); }

#include <sys/pinfo.h>
int pinfo(int slot, struct pinfo *p) { return __syscall(SYS_pinfo, slot, (int)p, sizeof *p, 0); }
