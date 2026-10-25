/* stty -- terminal modes: stty [raw|-raw|cbreak|-cbreak|echo|-echo|crmod|-crmod|sane ...]
 * With no mode, print the modes. sane is -raw -cbreak echo crmod. */
#include <stdio.h>
#include <string.h>
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
    int i;
    if (gtty(0, &b) < 0) { fprintf(stderr, "stty: the standard input is not a terminal\n"); return 1; }
    if (argc == 1) {
        printf("%s%s%s%s", b.sg_flags & RAW ? "raw " : "", b.sg_flags & CBREAK ? "cbreak " : "",
               b.sg_flags & ECHO ? "echo" : "-echo", b.sg_flags & CRMOD ? " crmod" : " -crmod");
        ctl("erase", b.sg_erase);
        ctl("kill", b.sg_kill);
        putchar('\n');
        return 0;
    }
    for (i = 1; i < argc; i++) {        /* (all checked before any is set) */
        char *a = argv[i];
        int neg = *a == '-', bit;
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
    return 0;
}
