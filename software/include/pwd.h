/* pwd.h -- P32W Unix users (D22; os-design.md 6). /etc/passwd lines:
 *   name:password:uid:gid:full name:home:shell
 * password: empty (none), "*" (no login), or SSSS$HHHHHHHHHHHHHHHH from pwhash(). */
#ifndef _PWD_H
#define _PWD_H
struct passwd {
    char *pw_name, *pw_passwd;
    int pw_uid, pw_gid;
    char *pw_gecos, *pw_dir, *pw_shell;
};
struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(int uid);
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);
char *pwhash(const char *password, const char *salt);   /* salt: 4 hex digits */
int pwcheck(const char *password, const char *stored);   /* 1 if it matches */
#endif
