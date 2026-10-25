/* login [name [tty]] -- check a password, become the user, run their shell
 * (M4; os-design.md 6). Records the login in /etc/utmp; gives the user the
 * terminal (owner, group tty, mode 0620: write(1) and wall(1) are setgid
 * tty, and mesg n takes the group's write away); prints /etc/motd, and
 * "You have mail." when the user's mailbox is not empty. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sgtty.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <utmp.h>
#include <sys/stat.h>

static void readline(const char *prompt, char *buf, int n, int echo)
{
    struct sgttyb b;
    int len, saved = 0;
    if (!echo && gtty(0, &b) == 0) { saved = b.sg_flags; b.sg_flags &= ~ECHO; stty(0, &b); }
    printf("%s", prompt);               /* (after echo is off: typing may already be on its way) */
    fflush(stdout);
    if (!fgets(buf, n, stdin)) buf[0] = 0;
    if (!echo && gtty(0, &b) == 0) { b.sg_flags = saved; stty(0, &b); printf("\n"); }
    len = strlen(buf);
    if (len && buf[len - 1] == '\n') buf[len - 1] = 0;
}

int main(int argc, char **argv)
{
    char name[64], pw[64], line[16], shell[64], *sh, envhome[80], envuser[48], envshell[80];
    char envlog[48], *envp[] = { "PATH=/bin:/usr/bin", envhome, envuser, envlog, envshell, 0 }, *av[2];
    struct passwd *p;
    int fd, slot;
    int tries;
    strncpy(line, argc > 2 ? argv[2] : "tty0", sizeof line - 1);
    line[sizeof line - 1] = 0;
    for (tries = 0; ; tries++) {        /* three tries, then back to getty */
        if (argc > 1 && !tries) strncpy(name, argv[1], sizeof name - 1), name[sizeof name - 1] = 0;
        else readline("login: ", name, sizeof name, 1);
        if (!name[0]) { tries--; continue; }
        p = getpwnam(name);
        if (p && !p->pw_passwd[0]) break;
        readline("Password: ", pw, sizeof pw, 0);
        if (p && pwcheck(pw, p->pw_passwd)) break;
        printf("Login incorrect\n");
        sleep(2 + tries);
        if (tries == 2) return 1;
    }
    {
        char dev[24];
        struct group *g = getgrnam("tty");
        sprintf(dev, "/dev/%s", line);
        chown(dev, p->pw_uid, g ? (int)g->gr_gid : (int)p->pw_gid);
        chmod(dev, 0620);
    }
    if ((slot = utmp_slot(line)) >= 0 && (fd = open(UTMP, O_WRONLY)) >= 0) {
        struct utmp u;
        memset(&u, 0, sizeof u);
        strncpy(u.ut_line, line, sizeof u.ut_line);
        strncpy(u.ut_name, p->pw_name, sizeof u.ut_name);
        u.ut_time = time(0);
        lseek(fd, slot * sizeof u, 0);
        write(fd, &u, sizeof u);
        close(fd);
    }
    if (chdir(p->pw_dir) < 0) { printf("No directory %s: logging in with home /\n", p->pw_dir); chdir("/"); }
    setgid(p->pw_gid);
    if (setuid(p->pw_uid) < 0) { perror("login: setuid"); return 1; }
    if ((fd = open("/etc/motd", O_RDONLY)) >= 0) {
        char buf[256];
        int n;
        while ((n = read(fd, buf, sizeof buf)) > 0) write(1, buf, n);
        close(fd);
    }
    {
        char mbox[48];
        struct stat st;
        sprintf(mbox, "/usr/spool/mail/%.20s", p->pw_name);
        if (stat(mbox, &st) == 0 && st.st_size > 0) printf("You have mail.\n");
        fflush(stdout);                 /* (exec follows) */
    }
    sh = p->pw_shell;
    sprintf(envhome, "HOME=%s", p->pw_dir);
    sprintf(envuser, "USER=%s", p->pw_name);
    sprintf(envlog, "LOGNAME=%s", p->pw_name);
    sprintf(envshell, "SHELL=%s", sh);
    shell[0] = '-';
    strncpy(shell + 1, strrchr(sh, '/') ? strrchr(sh, '/') + 1 : sh, sizeof shell - 2);
    av[0] = shell; av[1] = 0;
    execve(sh, av, envp);
    perror(sh);
    return 1;
}
