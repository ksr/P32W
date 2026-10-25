/* grp.h -- P32W Unix groups: /etc/group lines name:password:gid:member,member */
#ifndef _GRP_H
#define _GRP_H
struct group { char *gr_name, *gr_passwd; int gr_gid; char **gr_mem; };
struct group *getgrnam(const char *name);
struct group *getgrgid(int gid);
#endif
