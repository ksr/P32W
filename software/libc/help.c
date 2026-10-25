/* help.c -- --help for every program: crt0 calls __help before main, and a
 * program run with --help as its only argument shows its manual page
 * instead (ls --help is man ls). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void __help(int argc, char **argv)
{
    const char *name;
    if (argc != 2 || strcmp(argv[1], "--help")) return;
    name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];
    if (*name == '-') name++;           /* (a login shell is -sh) */
    execl("/bin/man", "man", name, (char *)0);
    fprintf(stderr, "%s: no help here (man %s)\n", name, name);
    exit(2);
}
