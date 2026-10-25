/* passwd [user] -- change a password (set-user-id root; os-design.md 6).
 * A user changes their own after giving the old one; root changes anyone's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sgtty.h>
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>

static void ask(const char *prompt, char *buf, int n)
{
    struct sgttyb b;
    int len, saved = 0;
    if (gtty(0, &b) == 0) { saved = b.sg_flags; b.sg_flags &= ~ECHO; stty(0, &b); }
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, n, stdin)) buf[0] = 0;
    if (gtty(0, &b) == 0) { b.sg_flags = saved; stty(0, &b); }
    printf("\n");
    len = strlen(buf);
    if (len && buf[len - 1] == '\n') buf[len - 1] = 0;
}

int main(int argc, char **argv)
{
    struct passwd *p;
    char old[64], new1[64], new2[64], salt[8], hash[32], line[256], name[64];
    FILE *in, *out;
    int uid = getuid(), n;
    if (argc > 1 && strlen(argv[1]) >= sizeof name) { fprintf(stderr, "passwd: no user %.20s...\n", argv[1]); return 1; }
    if (argc > 1) strcpy(name, argv[1]);         /* (passwd runs as root: the length is checked first) */
    else if ((p = getpwuid(uid)) != 0) { strncpy(name, p->pw_name, sizeof name - 1); name[sizeof name - 1] = 0; }
    else { fprintf(stderr, "passwd: who are you?\n"); return 1; }
    if (!(p = getpwnam(name))) { fprintf(stderr, "passwd: no user %s\n", name); return 1; }
    if (uid != 0 && uid != p->pw_uid) { fprintf(stderr, "passwd: permission denied\n"); return 1; }
    if (uid != 0 && p->pw_passwd[0]) {
        ask("Old password: ", old, sizeof old);
        if (!pwcheck(old, p->pw_passwd)) { printf("Sorry\n"); return 1; }
    }
    ask("New password: ", new1, sizeof new1);
    ask("Again: ", new2, sizeof new2);
    if (strcmp(new1, new2)) { printf("They don't match; the password is unchanged.\n"); return 1; }
    sprintf(salt, "%04x", (unsigned)(time(0) ^ getpid() * 7919) & 0xFFFF);
    strcpy(hash, new1[0] ? pwhash(new1, salt) : "");
    if (!(in = fopen("/etc/passwd", "r")) || !(out = fopen("/etc/ptmp", "w"))) { perror("passwd"); return 1; }
    n = strlen(name);
    while (fgets(line, sizeof line, in)) {
        if (!strncmp(line, name, n) && line[n] == ':') {
            char *rest = strchr(line + n + 1, ':');
            fprintf(out, "%s:%s%s", name, hash, rest ? rest : ":\n");
        } else
            fputs(line, out);
    }
    fclose(in);
    fclose(out);
    chmod("/etc/ptmp", 0644);
    if (rename("/etc/ptmp", "/etc/passwd") < 0) { perror("passwd"); return 1; }
    printf("Password changed.\n");
    return 0;
}
