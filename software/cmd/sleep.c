/* sleep seconds -- wait that many whole seconds */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    unsigned long n = 0;
    const char *s = argc == 2 ? argv[1] : "";
    if (!*s) { fprintf(stderr, "usage: sleep seconds\n"); return 2; }
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > (2147483647UL - (*s - '0')) / 10) {
            fprintf(stderr, "sleep: %s: not a number of seconds\n", argv[1]);
            return 2;
        }
        n = n * 10 + (*s - '0');
    }
    while (n > 0) {                     /* (sleep takes an unsigned; a day at a time is plenty) */
        unsigned k = n > 86400 ? 86400 : (unsigned)n;
        sleep(k);
        n -= k;
    }
    return 0;
}
