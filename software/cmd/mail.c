/* mail -- send and read mail (in the manner of V7's)
 *
 *   mail user ...      send the standard input (up to its end, or a line
 *                      holding just ".") to each user
 *   mail               read your mail
 *   mail -f [file]     read the mail in file (default $HOME/mbox)
 *   mail -e            exit status 0 if you have mail, 1 if not (silent)
 *
 * Mailboxes are /usr/spool/mail/USER in the Unix mbox format: each message
 * starts with a line "From SENDER Wed Oct 14 12:34:56 2026" and ends with a
 * blank line; a line of the message that starts with "From " is stored as
 * ">From ". Reading shows "N messages", then the newest message, then the
 * prompt "? " for a command:
 *
 *   newline, +, n   the next (older) message; after the last: q
 *   -               the previous (newer) one
 *   p               print the message again
 *   d               delete it, go on to the next
 *   s [file ...]    save it (with its From line) at the end of each file
 *                   (default $HOME/mbox), delete it, go on
 *   w [file ...]    the same without the From line
 *   q (or the end of the input)  quit, writing back the mailbox without
 *                   the deleted messages
 *   x               exit, changing nothing
 *   !command        run a shell command
 *   ?, h            this list
 *
 * Locking: USER.lock in /usr/spool/mail, made with O_CREAT|O_EXCL, around
 * every append and rewrite (tried for 10 seconds; a lock older than 60
 * seconds is broken).
 *
 * Privilege (how it is safe): mail is set-group-id mail (/bin/mail, mode
 * 2755, group mail) and nothing more. /usr/spool/mail is root:mail 775,
 * each mailbox USER:mail 660. The group lets mail append to another
 * user's mailbox and make lock files; reading and rewriting your own
 * mailbox needs only that you own it. Everything in your own space -- the
 * s and w files, $HOME/mbox, dead.letter, -f's file, ! commands -- is done
 * after giving the group up (setgid to the real group: for s, w and ! in
 * a child process), so with exactly your own rights; a file in the spool
 * cannot be reached that way. mail never creates another user's mailbox
 * (it could not give it to them), except when the real user is root:
 * then it is made, given to the user, group mail, mode 660. Your own
 * missing mailbox is made when you send yourself mail. Delivery checks
 * that the mailbox is a plain file of the recipient's with one link. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SPOOL "/usr/spool/mail"
#define MAILGID 6                       /* the group mail, if /etc/group has none */
#define NAMEMAX 20                      /* (NAME.lock must fit a directory entry) */
#define PATHMAX 256

static char myname[NAMEMAX + 1];        /* the real user */
static char myhome[PATHMAX];

/* ---------------------------------------------------------------- locking */

static void lockpath(char *path, const char *name) { sprintf(path, "%s/%s.lock", SPOOL, name); }

/* lock NAME's mailbox: 0, or -1 (said why) */
static int lock(const char *name)
{
    char path[PATHMAX];
    struct stat s;
    int i, fd;
    lockpath(path, name);
    for (i = 0; i < 10; i++) {
        if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0640)) >= 0) { close(fd); return 0; }
        if (errno != EEXIST) { fprintf(stderr, "mail: %s: %s\n", path, strerror(errno)); return -1; }
        if (stat(path, &s) == 0 && time(0) - s.st_mtime > 60) {
            unlink(path);               /* (left by a mail that died) */
            continue;
        }
        sleep(1);
    }
    fprintf(stderr, "mail: %s's mailbox is locked\n", name);
    return -1;
}

static void unlock(const char *name)
{
    char path[PATHMAX];
    lockpath(path, name);
    unlink(path);
}

/* no interrupts while a lock is held */
static void hold(int on)
{
    static sighandler_t i, q, h;
    if (on) { i = signal(SIGINT, SIG_IGN); q = signal(SIGQUIT, SIG_IGN); h = signal(SIGHUP, SIG_IGN); }
    else { signal(SIGINT, i); signal(SIGQUIT, q); signal(SIGHUP, h); }
}

/* give up the group mail for good: the real user's own rights from now on */
static void plain(void)
{
    setgid(getgid());
}

/* ---------------------------------------------------------------- sending */

static char *body;                      /* the message, ">From " escaped */
static int blen, bcap;

static void add(const char *s, int n)
{
    if (blen + n + 1 > bcap) {
        int cap = bcap ? bcap : 1024;
        while (blen + n + 1 > cap) cap *= 2;
        if (!(body = realloc(body, cap))) { fprintf(stderr, "mail: the message is too long\n"); exit(1); }
        bcap = cap;
    }
    memcpy(body + blen, s, n);
    blen += n;
}

/* the message from the standard input: to its end or a line "." */
static void readmsg(void)
{
    char line[512];
    int bol = 1, n;
    while (fgets(line, sizeof line, stdin)) {
        n = strlen(line);
        if (bol && (!strcmp(line, ".\n") || !strcmp(line, "."))) break;
        if (bol && !strncmp(line, "From ", 5)) add(">", 1);
        add(line, n);
        bol = line[n - 1] == '\n';
    }
    if (blen && body[blen - 1] != '\n') add("\n", 1);
}

/* append the message to NAME's mailbox: 0, or -1 (said why) */
static int deliver(const char *name, const char *head)
{
    struct passwd *pw;
    struct group *gr;
    struct stat s;
    char path[PATHMAX];
    int uid, fd, r = 0;
    if (strlen(name) > NAMEMAX || strchr(name, '/') || !(pw = getpwnam(name))) {
        fprintf(stderr, "mail: no user %s\n", name);
        return -1;
    }
    uid = pw->pw_uid;
    sprintf(path, "%s/%s", SPOOL, name);
    if (lock(name) < 0) return -1;
    fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0 && errno == ENOENT && (getuid() == 0 || uid == getuid())) {
        /* root may make anyone's mailbox; you, your own */
        if ((fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_EXCL, 0660)) >= 0) {
            gr = getgrnam("mail");
            if (getuid() == 0) chown(path, uid, gr ? gr->gr_gid : MAILGID);
            chmod(path, 0660);
        }
    }
    if (fd < 0) {
        if (errno == ENOENT) fprintf(stderr, "mail: %s has no mailbox\n", name);
        else fprintf(stderr, "mail: %s: %s\n", path, strerror(errno));
        unlock(name);
        return -1;
    }
    if (fstat(fd, &s) < 0 || !S_ISREG(s.st_mode) || s.st_uid != uid || s.st_nlink != 1) {
        fprintf(stderr, "mail: %s is not %s's own file: not delivered\n", path, name);
        r = -1;
    } else if (write(fd, head, strlen(head)) < 0 || (blen && write(fd, body, blen) != blen) || write(fd, "\n", 1) != 1) {
        fprintf(stderr, "mail: %s: %s\n", path, strerror(errno));
        r = -1;
    }
    close(fd);
    unlock(name);
    return r;
}

static int sendmail(int n, char **names)
{
    char head[80], path[PATHMAX];
    time_t t;
    int i, failed = 0, fd;
    readmsg();
    t = time(0);
    sprintf(head, "From %s %s", myname, ctime(&t));     /* (ctime ends with a newline) */
    hold(1);
    for (i = 0; i < n; i++)
        if (deliver(names[i], head) < 0) failed = 1;
    hold(0);
    if (failed) {
        plain();                        /* dead.letter: as the user */
        sprintf(path, "%.240s/dead.letter", myhome);
        if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0 || (blen && write(fd, body, blen) != blen))
            fprintf(stderr, "mail: %s: %s\n", path, strerror(errno));
        else fprintf(stderr, "mail: the message is saved in %s\n", path);
        if (fd >= 0) close(fd);
    }
    return failed;
}

/* ---------------------------------------------------------------- reading */

static int mfd = -1;                    /* the mailbox */
static int msize;                       /* its size when it was read */
static int *start, nmsg;                /* message k: start[k] .. start[k + 1] (start[nmsg] = msize) */
static char *gone;                      /* deleted */
static int spool;                       /* the mailbox is mine in the spool (else -f) */

static void addstart(int off)
{
    static int cap;
    if (nmsg + 2 > cap) {
        cap = cap ? cap * 2 : 32;
        if (!(start = realloc(start, cap * sizeof *start))) { fprintf(stderr, "mail: too many messages\n"); exit(1); }
    }
    start[nmsg++] = off;
}

/* find the messages: a "From " at the start of a line */
static void scan(void)
{
    static const char pat[] = "\nFrom ";
    char buf[512];
    int off = 0, n, i, state = 1;
    lseek(mfd, 0, 0);
    while (off < msize && (n = read(mfd, buf, msize - off < (int)sizeof buf ? msize - off : (int)sizeof buf)) > 0) {
        for (i = 0; i < n; i++) {
            if (buf[i] == pat[state]) {
                if (++state == 6) { addstart(off + i - 4); state = 0; }
            } else state = buf[i] == '\n';
        }
        off += n;
    }
    if (nmsg == 0 || start[0] != 0) {   /* (text before the first From line: a message too) */
        addstart(0);
        memmove(start + 1, start, (nmsg - 1) * sizeof *start);
        start[0] = 0;
    }
    start[nmsg] = msize;
    if (!(gone = calloc(nmsg, 1))) { fprintf(stderr, "mail: too many messages\n"); exit(1); }
}

/* copy from .. to of fd in to fd out (out < 0: to the standard output,
   shown safely if it is a terminal); 0 or -1 */
static int copy(int in, int from, int to, int out)
{
    char buf[512];
    int n, i, safe = out < 0 && isatty(1);
    lseek(in, from, 0);
    while (from < to) {
        if ((n = read(in, buf, to - from < (int)sizeof buf ? to - from : (int)sizeof buf)) <= 0) return -1;
        from += n;
        if (out >= 0) { if (write(out, buf, n) != n) return -1; continue; }
        for (i = 0; i < n; i++) {
            int c = buf[i] & 0xFF;
            if (!safe || c == '\n' || c == '\t' || (c >= ' ' && c < 127)) putchar(c);
            else if (c >= 128) printf("M-%c", c - 128 >= ' ' && c - 128 < 127 ? c - 128 : '?');
            else printf("^%c", c ^ 0x40);
        }
    }
    return 0;
}

/* where message k's text starts, after its From line */
static int textof(int k)
{
    char buf[128];
    int off = start[k], end = start[k + 1], n, i;
    lseek(mfd, off, 0);
    while (off < end && (n = read(mfd, buf, sizeof buf)) > 0)
        for (i = 0; i < n && off < end; i++)
            if (buf[i] == '\n') return off + 1;
            else off++;
    return off;
}

/* run in a child with the user's own rights: save message k in file
   (nohead: without its From line), or run a shell command */
static int child(int k, const char *file, int nohead, const char *cmd)
{
    int pid, st, fd, end;
    sighandler_t i, q;
    fflush(stdout);
    if ((pid = fork()) < 0) { perror("mail: fork"); return -1; }
    if (pid == 0) {
        plain();
        setuid(getuid());
        if (cmd) {
            execl("/bin/sh", "sh", "-c", cmd, (char *)0);
            perror("/bin/sh");
            _exit(127);
        }
        if ((fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0600)) < 0) {
            fprintf(stderr, "mail: %s: %s\n", file, strerror(errno));
            _exit(1);
        }
        end = start[k + 1];
        if (nohead && end - start[k] >= 2) {     /* (w: not the blank line that ends it either) */
            char two[2];
            lseek(mfd, end - 2, 0);
            if (read(mfd, two, 2) == 2 && two[0] == '\n' && two[1] == '\n') end--;
        }
        if (copy(mfd, nohead ? textof(k) : start[k], end, fd) < 0) {
            fprintf(stderr, "mail: %s: %s\n", file, strerror(errno));
            _exit(1);
        }
        _exit(0);
    }
    i = signal(SIGINT, SIG_IGN);
    q = signal(SIGQUIT, SIG_IGN);
    while (wait(&st) != pid)
        ;
    signal(SIGINT, i);
    signal(SIGQUIT, q);
    return st == 0 ? 0 : -1;
}

/* write the mailbox back without the deleted messages (and with any mail
   that came meanwhile); 0 or 1 */
static int rewrite(const char *box)
{
    char tmp[PATHMAX];
    struct stat s;
    int k, tfd, wfd, size, r = 1;
    if (access(box, W_OK) < 0) { fprintf(stderr, "mail: %s: %s: nothing changed\n", box, strerror(errno)); return 1; }
    hold(1);
    if (spool && lock(myname) < 0) { fprintf(stderr, "mail: nothing changed\n"); hold(0); return 1; }
    if (fstat(mfd, &s) < 0 || s.st_size < msize) {
        fprintf(stderr, "mail: %s was changed meanwhile: nothing changed\n", box);
        goto out;
    }
    size = s.st_size;
    if (spool) sprintf(tmp, "%s/%s.tmp", SPOOL, myname);
    else sprintf(tmp, "/tmp/mail%d", getpid());
    unlink(tmp);
    if ((tfd = open(tmp, O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) { fprintf(stderr, "mail: %s: %s: nothing changed\n", tmp, strerror(errno)); goto out; }
    for (k = 0; k < nmsg; k++)
        if (!gone[k] && copy(mfd, start[k], start[k + 1], tfd) < 0) break;
    if (k < nmsg || copy(mfd, msize, size, tfd) < 0) {
        fprintf(stderr, "mail: %s: %s: nothing changed\n", tmp, strerror(errno));
        close(tfd);
        unlink(tmp);
        goto out;
    }
    size = lseek(tfd, 0, 2);
    if ((wfd = open(box, O_WRONLY | O_TRUNC)) < 0 || copy(tfd, 0, size, wfd) < 0) {
        fprintf(stderr, "mail: %s: %s: the mail is kept in %s\n", box, strerror(errno), tmp);
        if (wfd >= 0) close(wfd);
        close(tfd);
        goto out;
    }
    close(wfd);
    close(tfd);
    unlink(tmp);
    r = 0;
out:
    if (spool) unlock(myname);
    hold(0);
    return r;
}

static void help(void)
{
    printf("newline, +, n  next message      -  previous message\n"
           "p              print it again    d  delete it\n"
           "s [file ...]   save it ($HOME/mbox) and delete it\n"
           "w [file ...]   the same without the From line\n"
           "q              quit, keeping the changes\n"
           "x              exit, changing nothing\n"
           "!command       run a shell command\n");
}

/* the next message not deleted, older (dir -1) or newer (+1): its number, or -1 */
static int step(int k, int dir)
{
    for (k += dir; k >= 0 && k < nmsg; k += dir)
        if (!gone[k]) return k;
    return -1;
}

static int readmail(const char *box)
{
    char line[PATHMAX], *p, *f, mbox[PATHMAX];
    struct stat s;
    int cur, show = 1, changed = 0, k, ok;
    if (spool) hold(1);
    if (spool && lock(myname) < 0) return 1;
    mfd = open(box, O_RDONLY);
    if (mfd >= 0 && fstat(mfd, &s) == 0) msize = s.st_size;
    if (spool) { unlock(myname); hold(0); }
    if (mfd < 0 && (errno != ENOENT || !spool)) { fprintf(stderr, "mail: %s: %s\n", box, strerror(errno)); return 1; }
    if (mfd < 0 || msize == 0) { printf("No mail.\n"); return 0; }
    scan();
    printf("%d message%s\n", nmsg, nmsg == 1 ? "" : "s");
    sprintf(mbox, "%.240s/mbox", myhome);
    cur = nmsg - 1;
    for (;;) {
        if (show) { copy(mfd, start[cur], start[cur + 1], -1); show = 0; }
        printf("? ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) { printf("\n"); break; }
        if ((p = strchr(line, '\n')) != NULL) *p = 0;
        for (p = line; *p == ' ' || *p == '\t'; p++)
            ;
        if (!*p || ((*p == '+' || *p == 'n') && !p[1])) {
            if ((cur = step(cur, -1)) < 0) break;
            show = 1;
        } else if (*p == '-' && !p[1]) {
            if ((k = step(cur, 1)) < 0) printf("no newer message\n");
            else { cur = k; show = 1; }
        } else if (*p == 'p' && !p[1]) show = 1;
        else if (*p == 'd' && !p[1]) {
            gone[cur] = changed = 1;
            if ((cur = step(cur, -1)) < 0) break;
            show = 1;
        } else if ((*p == 's' || *p == 'w') && (!p[1] || p[1] == ' ' || p[1] == '\t')) {
            int nohead = *p == 'w', any = 0;
            ok = 1;
            for (f = strtok(p + 1, " \t"); f; f = strtok(NULL, " \t")) {
                any = 1;
                if (child(cur, f, nohead, NULL) < 0) { ok = 0; break; }
            }
            if (!any && child(cur, mbox, nohead, NULL) < 0) ok = 0;
            if (ok) {
                gone[cur] = changed = 1;
                if ((cur = step(cur, -1)) < 0) break;
                show = 1;
            }
        } else if (*p == 'q' && !p[1]) break;
        else if (*p == 'x' && !p[1]) return 0;
        else if (*p == '!') { child(0, NULL, 0, p + 1); printf("!\n"); }
        else if ((*p == '?' || *p == 'h') && !p[1]) help();
        else printf("unknown command (? lists them)\n");
    }
    return changed ? rewrite(box) : 0;
}

int main(int argc, char **argv)
{
    struct passwd *pw = getpwuid(getuid());
    struct stat s;
    char box[PATHMAX], *home = getenv("HOME");
    if (!pw) { fprintf(stderr, "mail: who are you? (uid %d is not in /etc/passwd)\n", getuid()); return 1; }
    if (strlen(pw->pw_name) > NAMEMAX) { fprintf(stderr, "mail: %s: the name is too long\n", pw->pw_name); return 1; }
    strcpy(myname, pw->pw_name);
    strncpy(myhome, home && *home ? home : pw->pw_dir, sizeof myhome - 1);
    sprintf(box, "%s/%s", SPOOL, myname);
    if (argc == 2 && !strcmp(argv[1], "-e")) return stat(box, &s) == 0 && s.st_size > 0 ? 0 : 1;
    if (argc >= 2 && !strcmp(argv[1], "-f") && argc <= 3) {
        plain();                        /* someone's own file: their own rights */
        if (argc == 3) strncpy(box, argv[2], sizeof box - 1);
        else sprintf(box, "%.240s/mbox", myhome);
        return readmail(box);
    }
    if (argc >= 2 && argv[1][0] == '-') {
        fprintf(stderr, "usage: mail user ...    mail [-f [file]]    mail -e\n");
        return 2;
    }
    if (argc >= 2) return sendmail(argc - 1, argv + 1);
    spool = 1;
    return readmail(box);
}
