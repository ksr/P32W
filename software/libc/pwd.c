/* pwd.c -- P32W C library (D22): /etc/passwd, /etc/group, the password hash.
 *
 * pwhash: a deliberately slow keyed hash (os-design.md 6: fine for a hobby
 * machine on two serial lines, not a defence against a stolen disk): two
 * 32-bit lanes mixed 2000 times over salt and password. tools/mkpasswd.py
 * computes the same function on the host, to build /etc/passwd. */
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rotl(unsigned x, int k) { return x << k | x >> (32 - k); }

char *pwhash(const char *pw, const char *salt)
{
    static char out[24];
    unsigned a = 0x811C9DC5, b = 0x9E3779B9, s;
    int r;
    const char *p;
    s = (unsigned)strtoul(salt, 0, 16) & 0xFFFF;
    a ^= s;
    b ^= s * 0x10001;
    for (r = 0; r < 2000; r++) {
        for (p = pw; *p; p++) {
            a = (a ^ (unsigned char)*p) * 0x01000193;
            b = rotl(b ^ a, 5) * 0x9E3779B1;
        }
        a ^= rotl(b, 13) + r;
        b += a ^ 0x5BD1E995;
    }
    sprintf(out, "%04x$%08x%08x", s, a, b);
    return out;
}

int pwcheck(const char *pw, const char *stored)
{
    if (!stored[0]) return 1;                   /* no password */
    if (stored[0] == '*' || strlen(stored) != 21) return 0;
    return strcmp(pwhash(pw, stored), stored) == 0;
}

/* ---------------------------------------------------------------- /etc/passwd */
static FILE *pwf;
static char pwline[256];
static struct passwd pwent;

static char *field(char **s)
{
    char *f = *s, *c = strchr(f, ':');
    if (c) { *c = 0; *s = c + 1; }
    else {
        c = strchr(f, '\n');
        if (c) *c = 0;
        *s = f + strlen(f);
    }
    return f;
}

void setpwent(void) { if (pwf) rewind(pwf); }
void endpwent(void) { if (pwf) { fclose(pwf); pwf = 0; } }

struct passwd *getpwent(void)
{
    char *s;
    if (!pwf && !(pwf = fopen("/etc/passwd", "r"))) return 0;
    for (;;) {
        if (!fgets(pwline, sizeof pwline, pwf)) return 0;
        if (pwline[0] == '#' || pwline[0] == '\n') continue;
        s = pwline;
        pwent.pw_name = field(&s);
        pwent.pw_passwd = field(&s);
        pwent.pw_uid = atoi(field(&s));
        pwent.pw_gid = atoi(field(&s));
        pwent.pw_gecos = field(&s);
        pwent.pw_dir = field(&s);
        pwent.pw_shell = field(&s);
        if (!*pwent.pw_shell) pwent.pw_shell = "/bin/sh";
        return &pwent;
    }
}

struct passwd *getpwnam(const char *name)
{
    struct passwd *p;
    setpwent();
    while ((p = getpwent()) != 0)
        if (!strcmp(p->pw_name, name)) break;
    endpwent();
    return p;
}

struct passwd *getpwuid(int uid)
{
    struct passwd *p;
    setpwent();
    while ((p = getpwent()) != 0)
        if (p->pw_uid == uid) break;
    endpwent();
    return p;
}

/* ---------------------------------------------------------------- /etc/group */
static char grline[256], *grmem[32];
static struct group grent;

static struct group *grfind(const char *name, int gid)
{
    FILE *f = fopen("/etc/group", "r");
    char *s, *m;
    int n;
    if (!f) return 0;
    while (fgets(grline, sizeof grline, f)) {
        s = grline;
        grent.gr_name = field(&s);
        grent.gr_passwd = field(&s);
        grent.gr_gid = atoi(field(&s));
        m = field(&s);
        if (name ? strcmp(grent.gr_name, name) : grent.gr_gid != gid) continue;
        for (n = 0; *m && n < 31; n++) {
            grmem[n] = m;
            m = strchr(m, ',');
            if (!m) { n++; break; }
            *m++ = 0;
        }
        grmem[n] = 0;
        grent.gr_mem = grmem;
        fclose(f);
        return &grent;
    }
    fclose(f);
    return 0;
}
struct group *getgrnam(const char *name) { return grfind(name, 0); }
struct group *getgrgid(int gid) { return grfind(0, gid); }
