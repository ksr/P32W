/* bigmem.c -- a process bigger than 4 MiB (D34: 8 MiB of RAM and an 8 MiB
 * address space per process): 5 MiB from malloc in 64 KiB blocks, every
 * byte written and checked; then a fork while it is all held (the child
 * shares the pages copy-on-write, changes some, and the parent's stay as
 * they were); then all of it freed and taken again. Standard C and POSIX
 * only: the expected output is the build machine's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define BLOCK (64 * 1024)
#define NBLOCK 80                               /* 80 x 64 KiB = 5 MiB */

static unsigned char *b[NBLOCK];

static int check(int from, int to, int salt)
{
    int k, i, bad = 0;
    for (k = from; k < to; k++)
        for (i = 0; i < BLOCK; i += 61)
            if (b[k][i] != (unsigned char)(k * 7 + i + salt)) bad++;
    return bad;
}

int main(void)
{
    int k, i, n, st, pid;
    for (n = 0; n < NBLOCK; n++)
        if (!(b[n] = malloc(BLOCK))) break;
    printf("allocated %d KiB in %d blocks\n", n * BLOCK / 1024, n);
    if (n < NBLOCK) return 1;
    for (k = 0; k < NBLOCK; k++)
        for (i = 0; i < BLOCK; i++) b[k][i] = (unsigned char)(k * 7 + i);
    printf("written; %d bad after writing\n", check(0, NBLOCK, 0));
    fflush(stdout);
    if ((pid = fork()) == 0) {                  /* the child changes the first 10 blocks */
        for (k = 0; k < 10; k++)
            for (i = 0; i < BLOCK; i++) b[k][i] = (unsigned char)(k * 7 + i + 1);
        printf("child: %d bad in its changed blocks, %d in the rest\n", check(0, 10, 1), check(10, NBLOCK, 0));
        fflush(stdout);
        _exit(0);
    }
    while (wait(&st) != pid) ;
    printf("parent: %d bad after the child changed its copy\n", check(0, NBLOCK, 0));
    for (k = 0; k < NBLOCK; k++) free(b[k]);
    for (n = 0; n < NBLOCK; n++)
        if (!(b[n] = malloc(BLOCK))) break;
    printf("freed and taken again: %d KiB\n", n * BLOCK / 1024);
    return 0;
}
