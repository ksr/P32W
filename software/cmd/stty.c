/* stty -- terminal modes: stty [raw|-raw|cbreak|-cbreak|echo|-echo|crmod|-crmod|sane ...]
 * With no mode, print the modes. sane is -raw -cbreak echo crmod.
 * The terminal's size (D126): stty rows N, stty columns N (or cols N) tell the kernel what it is, for a
 * terminal on a line, whose size nothing else knows; stty size prints it, rows then columns. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sgtty.h>

static void ctl(const char *name, int c)        /* ^H, ^?, or the character */
{
    c &= 0377;
    if (c < 32) printf(" %s ^%c", name, c + 64);
    else if (c == 127) printf(" %s ^?", name);
    else printf(" %s %c", name, c);
}

int main(int argc, char **argv)
{
    struct sgttyb b;
    struct winsize w;
    int i, sized = 0;
    if (gtty(0, &b) < 0) { fprintf(stderr, "stty: the standard input is not a terminal\n"); return 1; }
    if (ioctl(0, TIOCGWINSZ, &w) < 0) w.ws_row = w.ws_col = w.ws_xpixel = w.ws_ypixel = 0;
    if (argc == 1) {
        printf("%s%s%s%s", b.sg_flags & RAW ? "raw " : "", b.sg_flags & CBREAK ? "cbreak " : "",
               b.sg_flags & ECHO ? "echo" : "-echo", b.sg_flags & CRMOD ? " crmod" : " -crmod");
        ctl("erase", b.sg_erase);
        ctl("kill", b.sg_kill);
        if (w.ws_row || w.ws_col) printf(" rows %d columns %d", w.ws_row, w.ws_col);   /* (where it is known) */
        putchar('\n');
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "size")) { printf("%d %d\n", w.ws_row, w.ws_col); return 0; }
    for (i = 1; i < argc; i++) {        /* (all checked before any is set) */
        char *a = argv[i];
        int neg = *a == '-', bit;
        if (!strcmp(a, "rows") || !strcmp(a, "columns") || !strcmp(a, "cols")) {
            int n = i + 1 < argc ? atoi(argv[i + 1]) : -1;
            if (n < 0 || n > 999 || argv[i + 1][0] < '0' || argv[i + 1][0] > '9') { fprintf(stderr, "stty: %s wants a number, 0 to 999\n", a); return 2; }
            if (a[0] == 'r') w.ws_row = (unsigned short)n; else w.ws_col = (unsigned short)n;
            w.ws_xpixel = w.ws_ypixel = 0;
            sized = 1;
            i++;
            continue;
        }
        if (!strcmp(a, "sane")) {
            b.sg_flags = (b.sg_flags & ~(RAW | CBREAK)) | ECHO | CRMOD;
            continue;
        }
        if (neg) a++;
        bit = !strcmp(a, "raw") ? RAW : !strcmp(a, "cbreak") ? CBREAK : !strcmp(a, "echo") ? ECHO :
              !strcmp(a, "crmod") ? CRMOD : 0;
        if (!bit) { fprintf(stderr, "stty: unknown mode %s\n", argv[i]); return 2; }
        if (neg) b.sg_flags &= ~bit; else b.sg_flags |= bit;
    }
    if (stty(0, &b) < 0) { perror("stty"); return 1; }
    if (sized && ioctl(0, TIOCSWINSZ, &w) < 0) { perror("stty"); return 1; }
    return 0;
}
