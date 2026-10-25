/* sys/utsname.h -- P32W Unix: the system's name (uname(2), uname(1)) */
#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H
struct utsname {
    char sysname[32];           /* "P32W Unix" */
    char nodename[32];          /* "p32w" */
    char release[32];
    char version[32];
    char machine[32];           /* "p32w" */
};
int uname(struct utsname *u);
#endif
