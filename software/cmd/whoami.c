/* whoami -- the effective user's name */
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
int main(void) { struct passwd *p = getpwuid(geteuid()); puts(p ? p->pw_name : "?"); return 0; }
