/* df -- free space on the mounted filesystems (/ and /etc/mtab's)
 *
 *   df [special ...]
 *
 * The kernel's own counts (ustat), so any user may ask; a filesystem that
 * is not mounted is read from its superblock (root, or a readable device). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ustat.h>

static int status;

static void one(const char *special, const char *dir)
{
    struct stat st;
    struct ustat u;
    unsigned sb[9], total, avail;
    if (stat(special, &st) == 0 && (st.st_mode & S_IFMT) == S_IFBLK && ustat(st.st_rdev, &u) == 0) {
        total = u.f_blocks;
        avail = u.f_bfree;
    } else {
        int fd = open(special, O_RDONLY);
        if (fd < 0 || read(fd, sb, sizeof sb) != sizeof sb || sb[0] != 0x50333246) {
            printf("%-12s cannot read its superblock\n", special);
            if (fd >= 0) close(fd);
            status = 1;
            return;
        }
        close(fd);
        total = sb[1];
        avail = sb[7];
    }
    printf("%-12s %9u %7u %7u %3u%%  %s\n", special, total, total - avail, avail,
           total ? (total - avail) * 100 / total : 0, dir);
}

int main(int argc, char **argv)
{
    char line[160], sp[80], dir[80];
    FILE *f;
    int k;
    printf("Filesystem   1K-blocks    Used   Avail Use%%  Mounted on\n");
    if (argc > 1) {                     /* (the directory each is on, from /etc/mtab) */
        for (k = 1; k < argc; k++) {
            dir[0] = 0;
            if (!strcmp(argv[k], "/dev/disk")) strcpy(dir, "/");
            else if ((f = fopen("/etc/mtab", "r")) != 0) {
                while (fgets(line, sizeof line, f)) {
                    char s2[80], d2[80];
                    int a = 0, c = 0;
                    char *p = line;
                    while (*p == ' ') p++;
                    while (*p && *p != ' ' && *p != '\n' && a < 79) s2[a++] = *p++;
                    s2[a] = 0;
                    while (*p == ' ') p++;
                    while (*p && *p != ' ' && *p != '\n' && c < 79) d2[c++] = *p++;
                    d2[c] = 0;
                    if (!strcmp(s2, argv[k])) strcpy(dir, d2);
                }
                fclose(f);
            }
            one(argv[k], dir);
        }
        return status;
    }
    one("/dev/disk", "/");
    if ((f = fopen("/etc/mtab", "r")) != 0) {
        while (fgets(line, sizeof line, f)) {
            int k2 = 0, j = 0;
            char *p = line;
            while (*p == ' ') p++;
            while (*p && *p != ' ' && *p != '\n' && k2 < 79) sp[k2++] = *p++;
            sp[k2] = 0;
            while (*p == ' ') p++;
            while (*p && *p != ' ' && *p != '\n' && j < 79) dir[j++] = *p++;
            dir[j] = 0;
            if (k2 && j) one(sp, dir);
        }
        fclose(f);
    }
    return status;
}
