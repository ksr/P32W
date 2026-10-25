/* adduser -- a new user (root): adduser [-p] [-u uid] [-g group] [-c comment] name
 *
 * Adds the line to /etc/passwd (no password yet: set one with passwd name),
 * gives the user the next free uid from 100 (or -u), group staff (or -g, a
 * name or number), a home directory /usr/name of their own: mode 755, or
 * 700 with -p (private), and an empty mailbox /usr/spool/mail/name (the
 * user's, group mail, 660: mail(1) is setgid mail). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    int i, uid = -1, gid = 10, priv = 0, fd;
    char *name = 0, *comment = "", home[64], line[256];
    struct passwd *pw;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p")) priv = 1;
        else if (!strcmp(argv[i], "-u") && i + 1 < argc) uid = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) comment = argv[++i];
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) {
            char *g = argv[++i];
            struct group *gr = getgrnam(g);
            if (gr) gid = gr->gr_gid;
            else if (*g >= '0' && *g <= '9') gid = atoi(g);
            else { fprintf(stderr, "adduser: no group %s\n", g); return 1; }
        } else if (argv[i][0] == '-' || name) {
            fprintf(stderr, "usage: adduser [-p] [-u uid] [-g group] [-c comment] name\n");
            return 2;
        } else
            name = argv[i];
    }
    if (!name) { fprintf(stderr, "usage: adduser [-p] [-u uid] [-g group] [-c comment] name\n"); return 2; }
    if (geteuid()) { fprintf(stderr, "adduser: only root may add users\n"); return 1; }
    if (strlen(name) > 8 || !name[0]) { fprintf(stderr, "adduser: a name is 1 to 8 characters\n"); return 1; }
    for (i = 0; name[i]; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9' && i) || name[i] == '_')) {
            fprintf(stderr, "adduser: %s: use lower-case letters, digits and _\n", name);
            return 1;
        }
    if (strchr(comment, ':')) { fprintf(stderr, "adduser: the comment may not hold ':'\n"); return 1; }
    if (getpwnam(name)) { fprintf(stderr, "adduser: %s already exists\n", name); return 1; }
    if (uid < 0) {                      /* the next free uid from 100 */
        uid = 100;
        while (getpwuid(uid)) uid++;
    } else if (getpwuid(uid)) { fprintf(stderr, "adduser: uid %d is taken\n", uid); return 1; }
    sprintf(home, "/usr/%s", name);
    if (mkdir(home, priv ? 0700 : 0755) < 0) { perror(home); return 1; }
    chown(home, uid, gid);
    chmod(home, priv ? 0700 : 0755);
    sprintf(line, "%s::%d:%d:%s:%s:/bin/sh\n", name, uid, gid, comment, home);
    if ((fd = open("/etc/passwd", O_WRONLY)) < 0) { perror("/etc/passwd"); rmdir(home); return 1; }
    lseek(fd, 0, 2);
    if (write(fd, line, strlen(line)) != (int)strlen(line)) { perror("/etc/passwd"); close(fd); return 1; }
    close(fd);
    {
        char mbox[64];
        struct group *g = getgrnam("mail");
        sprintf(mbox, "/usr/spool/mail/%.40s", name);
        if ((fd = open(mbox, O_WRONLY | O_CREAT, 0660)) >= 0) {
            close(fd);
            chown(mbox, uid, g ? (int)g->gr_gid : 6);
            chmod(mbox, 0660);
        } else
            perror(mbox);
    }
    printf("%s: uid %d, gid %d, home %s%s; set a password with: passwd %s\n", name, uid, gid, home,
           priv ? " (private)" : "", name);
    return 0;
}
