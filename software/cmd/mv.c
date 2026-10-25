/* mv -- move or rename files: mv old new, or mv file ... dir */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#define PATHMAX 256                     /* the kernel's limit, with the NUL */
int rename(const char *, const char *);

/* dir/(last component of path) into buf, without a doubled /; -1 if too long */
static int into(char *buf, const char *dir, const char *path)
{
    int e = strlen(path), s, n = strlen(dir), slash;
    while (e > 1 && path[e - 1] == '/') e--;
    s = e;
    while (s > 0 && path[s - 1] != '/') s--;
    slash = n > 0 && dir[n - 1] != '/';
    if (n + slash + (e - s) >= PATHMAX) return -1;
    strcpy(buf, dir);
    if (slash) buf[n++] = '/';
    memcpy(buf + n, path + s, e - s);
    buf[n + e - s] = 0;
    return 0;
}

/* to another filesystem: copy the file, its mode, owner and time, then
   remove the original. A failed copy is removed and the original kept. */
static int movecopy(const char *from, const char *to, struct stat *sp)
{
    char buf[1024];
    int in, out, n;
    struct utimbuf t;
    if (!S_ISREG(sp->st_mode)) { fprintf(stderr, "mv: %s: cannot move to another filesystem (not a plain file)\n", from); return 1; }
    if ((in = open(from, O_RDONLY)) < 0) { fprintf(stderr, "mv: %s: %s\n", from, strerror(errno)); return 1; }
    if ((out = open(to, O_WRONLY | O_CREAT | O_TRUNC, sp->st_mode & 07777)) < 0) {
        fprintf(stderr, "mv: %s: %s\n", to, strerror(errno));
        close(in);
        return 1;
    }
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, n) != n) { n = -1; break; }
    close(in);
    if (close(out) < 0) n = -1;
    if (n < 0) {
        fprintf(stderr, "mv: %s: copy failed; %s left as it was\n", to, from);
        unlink(to);
        return 1;
    }
    chmod(to, sp->st_mode & 07777);             /* (the umask is not applied to a move) */
    chown(to, sp->st_uid, sp->st_gid);          /* (only root may; otherwise the mover owns it) */
    t.actime = sp->st_atime;
    t.modtime = sp->st_mtime;
    utime(to, &t);
    if (unlink(from) < 0) { fprintf(stderr, "mv: %s: copied, but not removed: %s\n", from, strerror(errno)); return 1; }
    return 0;
}

static int move(const char *from, const char *to)
{
    struct stat st;
    if (stat(from, &st) < 0) { fprintf(stderr, "mv: %s: %s\n", from, strerror(errno)); return 1; }
    if (S_ISDIR(st.st_mode)) { fprintf(stderr, "mv: %s: cannot move a directory\n", from); return 1; }
    if (rename(from, to) == 0) return 0;
    if (errno == EXDEV) return movecopy(from, to, &st);
    fprintf(stderr, "mv: %s: %s\n", errno == EISDIR || errno == ENOENT || errno == ENOTDIR ? to : from, strerror(errno));
    return 1;
}

int main(int argc, char **argv)
{
    char to[PATHMAX];
    struct stat st;
    int i = 1, r = 0;
    if (i < argc && !strcmp(argv[i], "--")) i++;
    else if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        fprintf(stderr, "mv: unknown option %s\n", argv[i]);
        i = argc;
    }
    argv += i - 1;
    argc -= i - 1;
    if (argc < 3) { fprintf(stderr, "usage: mv old new | mv file ... dir\n"); return 2; }
    if (stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode)) {
        for (i = 1; i < argc - 1; i++) {
            if (into(to, argv[argc - 1], argv[i]) < 0) { fprintf(stderr, "mv: %s: name too long\n", argv[i]); r = 1; continue; }
            r |= move(argv[i], to);
        }
        return r;
    }
    if (argc != 3) { fprintf(stderr, "mv: %s is not a directory\n", argv[argc - 1]); return 1; }
    return move(argv[1], argv[2]);
}
