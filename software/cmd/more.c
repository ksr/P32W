/* more -- page through files: a screenful (LINES-1 lines, wrapped at COLUMNS),
 * then wait (space: a page, return: a line, q: quit). When the output is not
 * a terminal, or there is no /dev/tty to read keys from, it copies like cat. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sgtty.h>
static int tty = -1, paging, rows = 23, cols = 80;
static struct sgttyb saved;
static int key(void)
{
    char c = 'q';
    printf("--More--");
    fflush(stdout);
    if (read(tty, &c, 1) != 1) c = 'q';
    printf("\r        \r");
    return c;
}
static int page(FILE *f)
{
    int n = 0, col = 0, c, k;
    if (!paging) {
        while ((c = getc(f)) != EOF) putchar(c);
        return 0;
    }
    while ((c = getc(f)) != EOF) {
        if (c != '\n' && col >= cols) { /* the line wraps: a new screen row */
            col = 0;
            if (++n >= rows) {
                fflush(stdout);
                k = key();
                if (k == 'q') return 1;
                n = k == '\n' || k == '\r' ? rows - 1 : 0;
            }
        }
        putchar(c);
        if (c == '\t') col = (col + 8) & ~7;
        else if (c == '\b') { if (col > 0) col--; }
        else if (c == '\r') col = 0;
        else if (c != '\n') col++;
        if (c == '\n') {
            col = 0;
            if (++n >= rows) {
                fflush(stdout);
                k = key();
                if (k == 'q') return 1;
                n = k == '\n' || k == '\r' ? rows - 1 : 0;
            }
        }
    }
    return 0;
}
int main(int argc, char **argv)
{
    int i, r = 0, st = 0;
    char *e;
    struct sgttyb b;
    if ((e = getenv("LINES")) != 0 && atoi(e) > 1) rows = atoi(e) - 1;
    if ((e = getenv("COLUMNS")) != 0 && atoi(e) > 0) cols = atoi(e);
    if (isatty(1) && (tty = open("/dev/tty", O_RDONLY)) >= 0 && gtty(tty, &saved) == 0) {
        paging = 1;
        b = saved;
        b.sg_flags |= CBREAK;
        b.sg_flags &= ~ECHO;
        stty(tty, &b);
    }
    if (argc == 1) r = page(stdin);
    for (i = 1; i < argc && !r; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { fflush(stdout); fprintf(stderr, "more: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        r = page(f);
        fclose(f);
    }
    fflush(stdout);
    if (paging) stty(tty, &saved);
    return st;
}
