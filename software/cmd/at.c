/* at, atq, atrm -- run commands once, later (cron runs them)
 *   at TIME [DATE]         the commands on the input run at TIME
 *   at now [+ N minutes|hours|days]
 *   atq                    the jobs waiting, in time order (root: everyone's)
 *   atrm job ...           remove jobs (your own; root: any)
 * TIME is HH:MM (24-hour; today, or tomorrow if it has passed), DATE is
 * MM/DD or YYYY/MM/DD, all local time (TZ); a date and time that have
 * passed are refused. A job runs as you, in this directory, with your
 * HOME, USER, LOGNAME, PATH, SHELL, TZ and umask, its output appended to
 * $HOME/at.out. The queue is /usr/spool/at (a job is a file named by its
 * time, owned by its user):
 *     # at uid gid directory
 *     # env NAME=value          (one line each)
 *     # umask 022
 *     the commands */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>

#define SPOOL "/usr/spool/at"
#define MAXJOBS 200

static int isjob(const char *name)      /* a job's name: digits, maybe .digits */
{
    const char *p = name;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') { p++; if (!*p) return 0; while (*p >= '0' && *p <= '9') p++; }
    return !*p && strlen(name) < 32;
}

static int owner(const char *path)      /* the uid that owns a job file, or -1 */
{
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) return -1;
    return st.st_uid;
}

static int later(const char *a, const char *b)  /* does job a come after job b? */
{
    unsigned long x = strtoul(a, 0, 10), y = strtoul(b, 0, 10);
    const char *da = strchr(a, '.'), *db = strchr(b, '.');
    if (x != y) return x > y;
    return (da ? atoi(da + 1) : 0) > (db ? atoi(db + 1) : 0);
}

static int atq(int argc)
{
    static char names[MAXJOBS][32];
    DIR *d;
    struct dirent *e;
    int me = getuid(), n = 0, k, j;
    if (argc > 1) { fprintf(stderr, "usage: atq\n"); return 2; }
    if (!(d = opendir(SPOOL))) return 0;
    while ((e = readdir(d)) != 0 && n < MAXJOBS)
        if (isjob(e->d_name)) strcpy(names[n++], e->d_name);
    closedir(d);
    for (k = 1; k < n; k++)             /* (in time order) */
        for (j = k; j > 0 && later(names[j - 1], names[j]); j--) {
            char t[32];
            strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
        }
    for (k = 0; k < n; k++) {
        char path[80];
        int uid;
        time_t t;
        struct passwd *pw;
        sprintf(path, "%s/%s", SPOOL, names[k]);
        if ((uid = owner(path)) < 0 || (me && uid != me)) continue;
        t = strtoul(names[k], 0, 10);
        pw = getpwuid(uid);
        if (pw) printf("%-14s %.24s  %s\n", names[k], ctime(&t), pw->pw_name);
        else printf("%-14s %.24s  %d\n", names[k], ctime(&t), uid);
    }
    return 0;
}

static int atrm(int argc, char **argv)
{
    int i, st = 0;
    if (argc < 2) { fprintf(stderr, "usage: atrm job ...\n"); return 2; }
    for (i = 1; i < argc; i++) {
        char path[80];
        int uid;
        if (!isjob(argv[i])) { fprintf(stderr, "atrm: %s: no such job\n", argv[i]); st = 1; continue; }
        sprintf(path, "%s/%s", SPOOL, argv[i]);
        if ((uid = owner(path)) < 0) { fprintf(stderr, "atrm: %s: no such job\n", argv[i]); st = 1; continue; }
        if (getuid() && uid != (int)getuid()) { fprintf(stderr, "atrm: %s: not yours\n", argv[i]); st = 1; continue; }
        if (unlink(path) < 0) { perror(argv[i]); st = 1; }
    }
    return st;
}

static int usage(void)
{
    fprintf(stderr, "usage: at HH:MM [MM/DD | YYYY/MM/DD]  |  at now [+ N minutes|hours|days]\n");
    return 2;
}

static int digits(const char *s, int min, int max, int *v)   /* 1-4 digits, min..max; 0 or -1 */
{
    int n = 0, k;
    for (k = 0; s[k]; k++) {
        if (s[k] < '0' || s[k] > '9' || k >= 4) return -1;
        n = n * 10 + s[k] - '0';
    }
    if (!k || n < min || n > max) return -1;
    *v = n;
    return 0;
}

static int mdays(int mon, int year)     /* mon 1-12 */
{
    static const int len[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return len[mon - 1] + (mon == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static void putline(int fd, const char *a, const char *b, const char *c)
{
    write(fd, a, strlen(a));
    write(fd, b, strlen(b));
    write(fd, c, strlen(c));
}

int main(int argc, char **argv)
{
    static const char *const keep[] = { "HOME", "USER", "LOGNAME", "PATH", "SHELL", "TZ", 0 };
    const char *prog = strrchr(argv[0], '/');
    time_t now = time(0), when;
    struct tm tm;
    char path[80], dir[256], line[512], *s;
    int fd, seq, k, mask;
    prog = prog ? prog + 1 : argv[0];
    if (!strcmp(prog, "atq")) return atq(argc);
    if (!strcmp(prog, "atrm")) return atrm(argc, argv);
    if (argc < 2) return usage();
    tm = *localtime(&now);
    if (!strcmp(argv[1], "now")) {
        int n;
        if (argc == 2) when = now;
        else {
            if (argc != 5 || strcmp(argv[2], "+") || digits(argv[3], 0, 9999, &n) < 0) return usage();
            s = argv[4];
            if (!strcmp(s, "min") || !strcmp(s, "mins") || !strcmp(s, "minute") || !strcmp(s, "minutes")) when = now + 60L * n;
            else if (!strcmp(s, "hour") || !strcmp(s, "hours")) when = now + 3600L * n;
            else if (!strcmp(s, "day") || !strcmp(s, "days")) when = now + 86400L * n;
            else return usage();
        }
    } else {
        int h, m, y = tm.tm_year + 1900, mon, day;
        char hh[8], *colon = strchr(argv[1], ':');
        if (argc > 3 || !colon || colon - argv[1] > 2) return usage();
        memcpy(hh, argv[1], colon - argv[1]);
        hh[colon - argv[1]] = 0;
        if (digits(hh, 0, 23, &h) < 0 || strlen(colon + 1) != 2 || digits(colon + 1, 0, 59, &m) < 0) return usage();
        tm.tm_hour = h;
        tm.tm_min = m;
        tm.tm_sec = 0;
        if (argc == 3) {
            char f[3][8];
            int nf = 0, len;
            s = argv[2];
            for (;;) {                  /* the fields between the slashes */
                char *slash = strchr(s, '/');
                len = slash ? (int)(slash - s) : (int)strlen(s);
                if (nf == 3 || len > 4) return usage();
                memcpy(f[nf], s, len);
                f[nf++][len] = 0;
                if (!slash) break;
                s = slash + 1;
            }
            if (nf == 1) return usage();
            k = 0;
            if (nf == 3 && (strlen(f[0]) != 4 || digits(f[k++], 1970, 2037, &y) < 0)) return usage();
            if (digits(f[k], 1, 12, &mon) < 0 || digits(f[k + 1], 1, mdays(mon, y), &day) < 0) {
                fprintf(stderr, "at: %s: bad date\n", argv[2]);
                return 2;
            }
            tm.tm_year = y - 1900;
            tm.tm_mon = mon - 1;
            tm.tm_mday = day;
        }
        when = mktime(&tm);
        if (when < now - now % 60) {
            if (argc == 3) { fprintf(stderr, "at: %s %s: that time has passed\n", argv[1], argv[2]); return 2; }
            when += 86400;              /* (a time already gone today: tomorrow) */
        }
    }
    when -= when % 60;                  /* (minutes: cron looks at the queue each minute) */
    if (!getcwd(dir, sizeof dir)) strcpy(dir, "/");
    for (seq = 0; seq < 100; seq++) {   /* the job's file: its time (and a number, if taken) */
        sprintf(path, seq ? "%s/%lu.%d" : "%s/%lu", SPOOL, (unsigned long)when, seq);
        if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600)) >= 0) break;
    }
    if (fd < 0) { perror(SPOOL); return 1; }
    sprintf(line, "# at %d %d ", (int)getuid(), (int)getgid());
    putline(fd, line, dir, "\n");
    for (k = 0; keep[k]; k++)
        if ((s = getenv(keep[k])) != 0 && !strchr(s, '\n') && strlen(s) < 400) {
            sprintf(line, "# env %s=", keep[k]);
            putline(fd, line, s, "\n");
        }
    mask = umask(022);
    umask(mask);
    sprintf(line, "# umask %03o\n", mask & 0777);
    write(fd, line, strlen(line));
    while (fgets(line, sizeof line, stdin)) write(fd, line, strlen(line));
    close(fd);
    {
        const char *job = strrchr(path, '/') + 1;
        printf("job %s at %.24s\n", job, ctime(&when));
    }
    return 0;
}
