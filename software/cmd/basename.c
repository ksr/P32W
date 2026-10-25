/* basename string [suffix] -- the last component of a path name */
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    char *b, *s;
    int n, k;
    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: basename string [suffix]\n"); return 2; }
    s = argv[1];
    n = strlen(s);
    while (n > 1 && s[n - 1] == '/') s[--n] = 0;        /* trailing slashes go first */
    if (n == 1 && s[0] == '/') { puts("/"); return 0; }  /* (all slashes) */
    b = strrchr(s, '/');
    b = b ? b + 1 : s;
    n = strlen(b);
    if (argc > 2 && (k = strlen(argv[2])) < n && !strcmp(b + n - k, argv[2])) b[n - k] = 0;
    puts(b);
    return 0;
}
