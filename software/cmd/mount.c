/* mount, umount -- filesystems on block devices (root)
 *   mount                     list what is mounted (/etc/mtab)
 *   mount special dir         mount the filesystem on special (e.g. /dev/ram) on dir
 *   umount special|dir        unmount it (it must not be in use)
 * /etc/mtab keeps the list: "special dir" a line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>

#define MTAB "/etc/mtab"

static int sscanf_words(const char *s, char *a, char *b)  /* the first two words of s; how many */
{
    char *out[2];
    int n = 0;
    out[0] = a; out[1] = b;
    while (n < 2) {
        int k = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '\n') break;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && k < 79) out[n][k++] = *s++;
        out[n][k] = 0;
        n++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *prog = strrchr(argv[0], '/');
    char lines[16][160], line[160], sp[80], dir[80];
    int n = 0, i, found = -1;
    FILE *f;
    prog = prog ? prog + 1 : argv[0];
    if ((f = fopen(MTAB, "r")) != 0) {
        while (n < 16 && fgets(lines[n], sizeof lines[n], f)) n++;
        fclose(f);
    }
    if (!strcmp(prog, "umount")) {
        if (argc != 2) { fprintf(stderr, "usage: umount special|dir\n"); return 2; }
        for (i = 0; i < n; i++) {
            sp[0] = dir[0] = 0;
            if (sscanf_words(lines[i], sp, dir) == 2 && (!strcmp(sp, argv[1]) || !strcmp(dir, argv[1]))) found = i;
        }
        if (found < 0) { fprintf(stderr, "umount: %s is not mounted (by mount)\n", argv[1]); return 1; }
        sscanf_words(lines[found], sp, dir);
        if (umount(sp) < 0) { fprintf(stderr, "umount: %s: %s\n", sp, errno == EBUSY ? "busy (a file or directory on it is in use)" : strerror(errno)); return 1; }
        if ((f = fopen(MTAB, "w")) != 0) {
            for (i = 0; i < n; i++) if (i != found) fputs(lines[i], f);
            fclose(f);
        }
        return 0;
    }
    if (argc == 1) {
        printf("/dev/disk on /\n");
        for (i = 0; i < n; i++) if (sscanf_words(lines[i], sp, dir) == 2) printf("%s on %s\n", sp, dir);
        return 0;
    }
    if (argc != 3) { fprintf(stderr, "usage: mount [special dir]\n"); return 2; }
    if (mount(argv[1], argv[2], 0) < 0) {
        fprintf(stderr, "mount: %s on %s: %s\n", argv[1], argv[2],
                errno == EINVAL ? "no filesystem there (mkfs it first)" : strerror(errno));
        return 1;
    }
    if ((f = fopen(MTAB, "a")) != 0) {
        sprintf(line, "%s %s\n", argv[1], argv[2]);
        fputs(line, f);
        fclose(f);
    }
    return 0;
}
