/* dirname string -- the directory part of a path name */
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    char *s;
    int n;
    if (argc != 2) { fprintf(stderr, "usage: dirname string\n"); return 2; }
    s = argv[1];
    n = strlen(s);
    while (n > 1 && s[n - 1] == '/') n--;               /* trailing slashes */
    while (n > 0 && s[n - 1] != '/') n--;               /* the last component */
    if (n == 0) { puts("."); return 0; }                /* no / before it */
    while (n > 1 && s[n - 1] == '/') n--;               /* the slashes before it */
    s[n] = 0;
    puts(s);
    return 0;
}
