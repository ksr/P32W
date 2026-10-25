/* id -- the user and group ids, with names:  id [-u | -g] [-n] [-r]
 *   (none)  uid=100(ken) gid=10(staff), and euid= / egid= when they differ
 *   -u, -g  only the effective user (group) id;  -n  its name instead;
 *   -r      the real id instead of the effective one */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

static const char *uname_(int uid) { struct passwd *p = getpwuid(uid); return p ? p->pw_name : 0; }
static const char *gname(int gid) { struct group *g = getgrgid(gid); return g ? g->gr_name : 0; }

int main(int argc, char **argv)
{
    int uid = getuid(), euid = geteuid(), gid = getgid(), egid = getegid();
    int u = 0, g = 0, n = 0, r = 0, i;
    const char *s;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1]) goto usage;
        for (a++; *a; a++)
            if (*a == 'u') u = 1;
            else if (*a == 'g') g = 1;
            else if (*a == 'n') n = 1;
            else if (*a == 'r') r = 1;
            else goto usage;
    }
    if (u + g > 1 || ((n || r) && !u && !g)) goto usage;
    if (u || g) {
        int id = u ? (r ? uid : euid) : (r ? gid : egid);
        s = u ? uname_(id) : gname(id);
        if (n && s) puts(s);
        else printf("%d\n", id);
        return 0;
    }
    s = uname_(uid);
    printf("uid=%d(%s)", uid, s ? s : "?");
    s = gname(gid);
    printf(" gid=%d(%s)", gid, s ? s : "?");
    if (euid != uid) { s = uname_(euid); printf(" euid=%d(%s)", euid, s ? s : "?"); }
    if (egid != gid) { s = gname(egid); printf(" egid=%d(%s)", egid, s ? s : "?"); }
    printf("\n");
    return 0;
usage:
    fprintf(stderr, "usage: id [-u | -g] [-n] [-r]\n");
    return 2;
}
