/* getty tty0 -- open a terminal, ask for a name, run login (M4) */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sgtty.h>
int main(int argc, char **argv)
{
    char dev[16], name[64], *av[4];
    struct sgttyb b;
    int n;
    if (argc < 2) return 1;
    sprintf(dev, "/dev/%s", argv[1]);
    close(0); close(1); close(2);
    if (open(dev, O_RDWR) < 0) return 1;
    dup(0); dup(0);
    if (gtty(0, &b) == 0) {             /* a sane terminal for the login */
        b.sg_flags = ECHO | CRMOD;
        b.sg_erase = 8;
        b.sg_kill = 21;
        stty(0, &b);
    }
    for (;;) {
        printf("\nP32W Unix (%s)\n\nlogin: ", argv[1]);
        fflush(stdout);
        if (!fgets(name, sizeof name, stdin)) return 0;
        n = strlen(name);
        if (n && name[n - 1] == '\n') name[--n] = 0;
        if (n) break;
    }
    av[0] = "login"; av[1] = name; av[2] = argv[1]; av[3] = 0;
    execv("/bin/login", av);
    perror("getty: /bin/login");
    return 1;
}
