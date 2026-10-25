/* chown -- change the owner and group: chown owner[:group] file ..., chown :group file ...,
   chown owner: file ... (the owner's login group)
   (names from /etc/passwd and /etc/group, or numbers) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

/* a string of digits only: its value; otherwise -1 */
static int number(const char *s)
{
    int n = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > 6553) return -1;
        n = n * 10 + *s - '0';
    }
    return n > 65535 ? -1 : n;
}

int main(int argc, char **argv)
{
    int i, st = 0, uid = -1, gid = -1;
    char *owner, *group = NULL, *colon;
    struct stat s;
    struct passwd *pw = NULL;
    struct group *gr;
    i = 1;
    if (i < argc && !strcmp(argv[i], "--")) i++;
    if (argc - i < 2) { fprintf(stderr, "usage: chown owner[:group] file ...\n"); return 2; }
    owner = argv[i++];
    if ((colon = strchr(owner, ':')) != NULL) { *colon = 0; group = colon + 1; }
    if (*owner) {
        if ((pw = getpwnam(owner)) != NULL) uid = pw->pw_uid;
        else if ((uid = number(owner)) < 0) { fprintf(stderr, "chown: unknown user %s\n", owner); return 1; }
    } else if (!group || !*group) { fprintf(stderr, "usage: chown owner[:group] file ...\n"); return 2; }
    if (group && !*group) {                     /* owner: -- the owner's login group */
        if (!pw && !(pw = getpwuid(uid))) { fprintf(stderr, "chown: %s has no login group\n", owner); return 1; }
        gid = pw->pw_gid;
    } else if (group) {
        if ((gr = getgrnam(group)) != NULL) gid = gr->gr_gid;
        else if ((gid = number(group)) < 0) { fprintf(stderr, "chown: unknown group %s\n", group); return 1; }
    }
    for (; i < argc; i++) {
        int u, g;
        if (stat(argv[i], &s) < 0) { fprintf(stderr, "chown: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        u = uid < 0 ? s.st_uid : uid;
        g = gid < 0 ? s.st_gid : gid;
        if (u == s.st_uid && g == s.st_gid) continue;   /* nothing to change: no error, even for a non-root user */
        if (chown(argv[i], u, g) < 0) { fprintf(stderr, "chown: %s: %s\n", argv[i], strerror(errno)); st = 1; }
    }
    return st;
}
