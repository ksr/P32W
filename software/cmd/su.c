/* su -- become another user (default root), with their password
 *
 *   su [-] [user] [-c command]
 *
 * With -: a login environment (HOME, USER, LOGNAME, SHELL, PATH; the home
 * directory; the shell reads /etc/profile and ~/.profile). -c: run the
 * command (with that user's shell's -c) instead of an interactive shell. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sgtty.h>
#include <pwd.h>

int main(int argc, char **argv)
{
    char pw[64], *name = "root", *cmd = 0, *av[4], shname[40], *base;
    struct passwd *p;
    struct sgttyb b;
    int len, saved = 0, login = 0, k;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-") || !strcmp(argv[k], "-l")) login = 1;
        else if (!strcmp(argv[k], "-c") && k + 1 < argc) cmd = argv[++k];
        else if (argv[k][0] == '-') { fprintf(stderr, "usage: su [-] [user] [-c command]\n"); return 2; }
        else name = argv[k];
    }
    if (!(p = getpwnam(name))) { fprintf(stderr, "su: no user %s\n", name); return 1; }
    if (getuid() != 0 && p->pw_passwd[0]) {
        if (gtty(0, &b) == 0) { saved = b.sg_flags; b.sg_flags &= ~ECHO; stty(0, &b); }
        printf("Password: ");
        fflush(stdout);
        if (!fgets(pw, sizeof pw, stdin)) pw[0] = 0;
        if (gtty(0, &b) == 0) { b.sg_flags = saved; stty(0, &b); }
        printf("\n");
        len = strlen(pw);
        if (len && pw[len - 1] == '\n') pw[len - 1] = 0;
        if (!pwcheck(pw, p->pw_passwd)) { printf("Sorry\n"); return 1; }
    }
    setgid(p->pw_gid);
    if (setuid(p->pw_uid) < 0) { perror("su"); return 1; }
    base = strrchr(p->pw_shell, '/') ? strrchr(p->pw_shell, '/') + 1 : p->pw_shell;
    if (login) {
        setenv("HOME", p->pw_dir, 1);
        setenv("USER", p->pw_name, 1);
        setenv("LOGNAME", p->pw_name, 1);
        setenv("SHELL", p->pw_shell, 1);
        setenv("PATH", "/bin:/usr/bin", 1);
        if (chdir(p->pw_dir) < 0) chdir("/");
        shname[0] = '-';                /* (a login shell: the profiles) */
        strncpy(shname + 1, base, sizeof shname - 2);
        shname[sizeof shname - 1] = 0;
    } else {
        strncpy(shname, base, sizeof shname - 1);
        shname[sizeof shname - 1] = 0;
    }
    av[0] = shname;
    if (cmd) { av[1] = "-c"; av[2] = cmd; av[3] = 0; }
    else av[1] = 0;
    execv(p->pw_shell, av);
    perror(p->pw_shell);
    return 1;
}
