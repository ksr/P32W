/* cron -- run commands at set times (started by /etc/rc; it puts itself in
 * the background). Each minute it reads /etc/crontab -- lines of
 *     minute hour day-of-month month day-of-week command
 * (each field * or numbers, ranges a-b, lists a,b, and steps: * or a range
 * followed by /n;
 * day of week 0 = Sunday; # comments) -- and runs, as root, the command of
 * every line matching that minute (local time, TZ), with sh -c and output to
 * /dev/null unless the command redirects it, in /, with HOME=/, USER=root,
 * PATH=/bin:/usr/bin and SHELL=/bin/sh. It also runs the jobs `at` has
 * queued in /usr/spool/at whose time has come, as the user who owns the
 * job's file, in the directory and with the environment and umask at saved,
 * appending their output to that user's $HOME/at.out; the job's copy in
 * /tmp is removed when it has run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define SPOOL "/usr/spool/at"

/* does value v match the field (lo..hi its range)? */
static int field_match(const char *f, int v, int lo, int hi)
{
    while (*f) {
        int a = lo, b = hi, step = 1;
        if (*f == '*') f++;
        else {
            a = b = atoi(f);
            while (*f >= '0' && *f <= '9') f++;
            if (*f == '-') { f++; b = atoi(f); while (*f >= '0' && *f <= '9') f++; }
        }
        if (*f == '/') { f++; step = atoi(f); if (step < 1) step = 1; while (*f >= '0' && *f <= '9') f++; }
        if (v >= a && v <= b && (v - a) % step == 0) return 1;
        if (*f == ',') f++;
        else break;
    }
    return 0;
}

/* the environment of a command: a crontab line's (root's), or an at job's
 * (what at saved, the rest from the user's /etc/passwd entry) */
static char envbuf[1200], *envp[12];
static int nenv, envlen;

static void env_set(const char *name, const char *value)
{
    int k, n = strlen(name);
    for (k = 0; k < nenv; k++)
        if (!strncmp(envp[k], name, n) && envp[k][n] == '=') return;   /* (the first one stays) */
    if (nenv >= 11 || envlen + n + strlen(value) + 2 > sizeof envbuf) return;
    envp[nenv] = envbuf + envlen;
    sprintf(envp[nenv++], "%s=%s", name, value);
    envlen += strlen(envbuf + envlen) + 1;
    envp[nenv] = 0;
}

static void env_clear(void) { nenv = envlen = 0; envp[0] = 0; }

/* run cmd with sh -c (or the file script with sh) as uid / gid, in dir,
 * with umask mask, output to out (a path, or 0: /dev/null); a script is
 * removed when it has run */
static void run(const char *cmd, int uid, int gid, const char *dir, int mask, const char *out, const char *script)
{
    int pid = fork(), st;
    if (pid < 0) return;
    if (pid == 0) {
        if (fork() == 0) {              /* (the grandchild: cron never waits for it) */
            int fd, sh;
            close(0); close(1); close(2);
            open("/dev/null", O_RDONLY);
            if (!out || (fd = open(out, O_WRONLY | O_CREAT | O_APPEND, 0600)) < 0) open("/dev/null", O_WRONLY);
            else if (fd != 1) { dup2(fd, 1); close(fd); }
            dup(1);
            if (out) chown(out, uid, gid);
            if ((sh = fork()) == 0) {
                char *av[4];
                setgid(gid);
                setuid(uid);
                umask(mask);
                if (dir && chdir(dir) < 0) {
                    fprintf(stderr, "cron: %s: cannot change to the directory; the job is not run\n", dir);
                    _exit(1);
                }
                av[0] = "sh";
                if (script) { av[1] = (char *)script; av[2] = 0; }
                else { av[1] = "-c"; av[2] = (char *)cmd; av[3] = 0; }
                execve("/bin/sh", av, envp);
                _exit(127);
            }
            if (sh > 0) while (wait(&st) != sh) ;
            if (script) unlink(script);
            _exit(0);
        }
        _exit(0);
    }
    while (wait(&st) != pid) ;
}

/* a job's first line: "# at uid gid dir"; 0 or -1 */
static int sscan_head(const char *h, int *uid, int *gid, char *dir)
{
    int k = 0;
    if (strncmp(h, "# at ", 5)) return -1;
    h += 5;
    *uid = atoi(h);
    while (*h >= '0' && *h <= '9') h++;
    while (*h == ' ') h++;
    *gid = atoi(h);
    while (*h >= '0' && *h <= '9') h++;
    while (*h == ' ') h++;
    while (*h && *h != '\n' && k < 255) dir[k++] = *h++;
    dir[k] = 0;
    return 0;
}

/* the lines after the first: "# env NAME=value" into the environment,
 * "# umask 022" into *mask, up to the first that is neither */
static void scan_env(char *h, int *mask)
{
    char *nl;
    if (!(h = strchr(h, '\n'))) return;
    for (h++; (nl = strchr(h, '\n')) != 0; h = nl + 1) {
        *nl = 0;
        if (!strncmp(h, "# env ", 6)) {
            char *eq = strchr(h + 6, '=');
            if (eq && eq > h + 6) { *eq = 0; env_set(h + 6, eq + 1); *eq = '='; }
        } else if (!strncmp(h, "# umask ", 8)) *mask = (int)strtoul(h + 8, 0, 8) & 0777;
        else { *nl = '\n'; break; }
        *nl = '\n';
    }
}

static void crontab(struct tm *tm)
{
    FILE *f = fopen("/etc/crontab", "r");
    char line[256];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *fld[5], *p = line;
        int k;
        if (line[0] == '#' || line[0] == '\n') continue;
        for (k = 0; k < 5; k++) {       /* five fields, then the command */
            while (*p == ' ' || *p == '\t') p++;
            fld[k] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (!*p) break;
            *p++ = 0;
        }
        if (k < 5) continue;
        while (*p == ' ' || *p == '\t') p++;
        {
            int n = strlen(p);
            if (n && p[n - 1] == '\n') p[n - 1] = 0;
        }
        if (!*p) continue;
        int dom = field_match(fld[2], tm->tm_mday, 1, 31), dow = field_match(fld[4], tm->tm_wday, 0, 6);
        /* (POSIX: with both day fields restricted, either may match; else both must) */
        int day = strcmp(fld[2], "*") && strcmp(fld[4], "*") ? dom || dow : dom && dow;
        if (field_match(fld[0], tm->tm_min, 0, 59) && field_match(fld[1], tm->tm_hour, 0, 23) &&
            day && field_match(fld[3], tm->tm_mon + 1, 1, 12)) {
            env_clear();
            env_set("HOME", "/");
            env_set("USER", "root");
            env_set("LOGNAME", "root");
            env_set("PATH", "/bin:/usr/bin");
            env_set("SHELL", "/bin/sh");
            if (getenv("TZ")) env_set("TZ", getenv("TZ"));
            run(p, 0, 0, "/", 022, 0, 0);
        }
    }
    fclose(f);
}

static void at_jobs(time_t now)
{
    DIR *d = opendir(SPOOL);
    struct dirent *e;
    char names[32][32];
    int n = 0, k;
    if (!d) return;
    while ((e = readdir(d)) != 0 && n < 32)
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9' && (time_t)strtoul(e->d_name, 0, 10) <= now)
            strcpy(names[n++], e->d_name);
    closedir(d);
    for (k = 0; k < n; k++) {
        char path[80], head[1024], dir[256], out[128], copy[80];
        int uid = -1, gid = 0, fd, mask = 022;
        struct passwd *pw;
        const char *tz;
        sprintf(path, "%s/%s", SPOOL, names[k]);
        if ((fd = open(path, O_RDONLY)) < 0) continue;
        memset(head, 0, sizeof head);
        read(fd, head, sizeof head - 1);
        close(fd);
        dir[0] = 0;
        if (sscan_head(head, &uid, &gid, dir) < 0) { unlink(path); continue; }
        {   /* the user is the file's owner, never what its first line says */
            struct stat st;
            if (stat(path, &st) < 0) continue;
            uid = st.st_uid;
        }
        pw = getpwuid(uid);
        if (!pw) { unlink(path); continue; }
        gid = pw->pw_gid;
        env_clear();
        scan_env(head, &mask);
        env_set("HOME", pw->pw_dir);    /* (what the job did not save) */
        env_set("USER", pw->pw_name);
        env_set("LOGNAME", pw->pw_name);
        env_set("PATH", "/bin:/usr/bin");
        env_set("SHELL", pw->pw_shell && *pw->pw_shell ? pw->pw_shell : "/bin/sh");
        if ((tz = getenv("TZ")) != 0) env_set("TZ", tz);
        sprintf(out, "%s/at.out", pw->pw_dir);
        /* the job runs from a copy (the spool entry is gone at once; the copy when it has run) */
        sprintf(copy, "/tmp/at%s", names[k]);
        if (rename(path, copy) < 0) { unlink(path); continue; }
        chown(copy, uid, gid);
        run(0, uid, gid, dir[0] ? dir : "/", mask, out, copy);
    }
}

int main(int argc, char **argv)
{
    int pid;
    (void)argc; (void)argv;
    if (getuid() != 0) { fprintf(stderr, "cron: only root runs cron\n"); return 1; }
    if ((pid = fork()) < 0) { perror("cron"); return 1; }
    if (pid > 0) return 0;              /* (the background: /etc/rc goes on) */
    setsid();
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    close(0); close(1); close(2);
    open("/dev/null", O_RDONLY); open("/dev/null", O_WRONLY); dup(1);
    for (;;) {
        time_t now = time(0);
        struct tm tm;
        sleep(60 - now % 60);
        now = time(0);
        tm = *localtime(&now);
        crontab(&tm);
        at_jobs(now);
    }
}
