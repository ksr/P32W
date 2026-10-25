/* od -- dump files: od [-c | -x] [file ...] (hex bytes by default, or characters);
 * several files are dumped as one stream, as if concatenated. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
static int cflag;
static unsigned char b[16];
static int nb;
static long off;
static void line(void)
{
    int k;
    printf("%07lo ", off);
    for (k = 0; k < nb; k++) {
        if (cflag) {
            int c = b[k];
            if (c >= 32 && c < 127) printf("   %c", c);
            else if (c == '\n') printf("  \\n");
            else if (c == '\t') printf("  \\t");
            else if (c == '\r') printf("  \\r");
            else if (c == '\b') printf("  \\b");
            else if (c == '\f') printf("  \\f");
            else if (c == 0) printf("  \\0");
            else printf(" %03o", c);
        } else
            printf(" %02x", b[k]);
    }
    printf("\n");
    off += nb;
    nb = 0;
}
static void dump(FILE *f)
{
    int c;
    while ((c = getc(f)) != EOF) {
        b[nb++] = c;
        if (nb == 16) line();
    }
}
int main(int argc, char **argv)
{
    int i = 1, st = 0;
    if (i < argc && !strcmp(argv[i], "-c")) { cflag = 1; i++; }
    else if (i < argc && !strcmp(argv[i], "-x")) i++;
    else if (i < argc && !strcmp(argv[i], "--")) i++;
    else if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        fprintf(stderr, "od: unknown option %s\nusage: od [-c | -x] [file ...]\n", argv[i]);
        return 1;
    }
    if (i == argc) dump(stdin);
    for (; i < argc; i++) {
        FILE *f = strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin;
        if (!f) { fprintf(stderr, "od: %s: %s\n", argv[i], strerror(errno)); st = 1; continue; }
        dump(f);
        if (f != stdin) fclose(f);
    }
    if (nb) line();
    printf("%07lo\n", off);
    return st;
}
