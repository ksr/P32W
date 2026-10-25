/* man -- the manual
 *
 *   man [section] name ...     show a page (on a terminal, through more)
 *   man -k word                the pages whose NAME line mentions word
 *
 * Pages are plain text in /usr/man/manS/name.S. Without a section the
 * sections are searched in the order 1, 8, 2, 3, 4, 5. man -k reads the
 * index /usr/man/whatis and ignores case. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MANDIR "/usr/man"

static const char order[] = "182345";

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

static int contains(const char *s, const char *w)       /* ignoring case */
{
    int n = strlen(w), k;
    for (; *s; s++) {
        for (k = 0; k < n && s[k] && lower(s[k]) == lower(w[k]); k++) ;
        if (k == n) return 1;
    }
    return 0;
}

static int apropos(const char *word)
{
    FILE *f = fopen(MANDIR "/whatis", "r");
    char line[256];
    int found = 0;
    if (!f) { fprintf(stderr, "man: no index (%s/whatis)\n", MANDIR); return 1; }
    while (fgets(line, sizeof line, f))
        if (contains(line, word)) { fputs(line, stdout); found = 1; }
    fclose(f);
    if (!found) { fprintf(stderr, "%s: nothing appropriate\n", word); return 1; }
    return 0;
}

static int show(const char *path)
{
    if (isatty(1)) {                    /* through more, a page at a time */
        int pid, st;
        fflush(stdout);
        if ((pid = fork()) == 0) {
            execl("/bin/more", "more", path, (char *)0);
            _exit(127);
        }
        if (pid > 0) {
            while (wait(&st) != pid) ;
            if (st == 0) return 0;
        }
    }
    {
        FILE *f = fopen(path, "r");
        int c;
        if (!f) { perror(path); return 1; }
        while ((c = getc(f)) != EOF) putchar(c);
        fclose(f);
    }
    return 0;
}

static int lookup(int section, const char *name)
{
    char path[200];
    const char *s;
    if (strlen(name) > 100) return -1;
    for (s = section ? "" : order; ; s++) {
        int sec = section ? section : *s;
        if (!sec) break;
        sprintf(path, "%s/man%c/%s.%c", MANDIR, sec, name, sec);
        if (access(path, R_OK) == 0) return show(path);
        if (section) break;
    }
    if (section) fprintf(stderr, "No entry for %s in section %c of the manual.\n", name, section);
    else fprintf(stderr, "No manual entry for %s.  (man -k %s searches the descriptions.)\n", name, name);
    return 1;
}

int main(int argc, char **argv)
{
    int i = 1, section = 0, st = 0;
    if (argc > 2 && !strcmp(argv[1], "-k")) {
        for (i = 2; i < argc; i++) st |= apropos(argv[i]);
        return st;
    }
    if (argc > 2 && argv[1][0] >= '1' && argv[1][0] <= '8' && !argv[1][1]) section = argv[i++][0];
    if (i >= argc) {
        fprintf(stderr, "usage: man [section] name ...    man -k word\n"
                        "       (man intro: how the manual is laid out; man man: this command)\n");
        return 2;
    }
    for (; i < argc; i++) st |= lookup(section, argv[i]);
    return st;
}
