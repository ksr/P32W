/* uname -- the system's name: uname [-snrvma]
 *   -s the system (the default)  -n the node  -r the release  -v the version
 *   -m the hardware  -a all five */
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
int main(int argc, char **argv)
{
    static const char letters[] = "snrvm";
    struct utsname un;
    char *u[5];
    int want = 0, i, k, first = 1;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1]) goto usage;
        for (a++; *a; a++) {
            const char *l = strchr(letters, *a);
            if (*a == 'a') want = 31;
            else if (l && *a) want |= 1 << (l - letters);
            else goto usage;
        }
    }
    if (!want) want = 1;
    uname(&un);
    u[0] = un.sysname; u[1] = un.nodename; u[2] = un.release; u[3] = un.version; u[4] = un.machine;
    for (k = 0; k < 5; k++)
        if (want & 1 << k) { printf("%s%s", first ? "" : " ", u[k]); first = 0; }
    putchar('\n');
    return 0;
usage:
    fprintf(stderr, "usage: uname [-snrvma]\n");
    return 2;
}
