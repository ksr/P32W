/* ls -- list directories and files
 *
 *   ls [-1ACFRSadhilnrst] [name ...]
 *
 *   -l  long: mode, links, owner, group, size, date, name ("total" first: KiB used)
 *   -n  long, with numeric user and group ids
 *   -h  sizes in K / M / G (with -l or -s)
 *   -a  every entry (names starting with "."); -A the same without . and ..
 *   -d  a directory itself, not its contents
 *   -R  each subdirectory too, recursively
 *   -t  newest first; -S largest first; -r the order reversed
 *   -F  a mark after the name: / directory, * executable, | pipe
 *   -i  the inode number first; -s the size in KiB first
 *   -1  one name per line; -C in columns (the default on a terminal)
 *
 * Several names: files first, then each directory under a heading. The
 * width for columns is $COLUMNS, else 80. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

static int lflag, nflag, hflag, aflag, Aflag, dflag, Rflag, tflag, Sflag, rflag, Fflag, iflag, sflag, cols;
static int status, width = 80;
static time_t now;

struct ent { char *name; struct stat st; int ok; };

static void mode(unsigned m, char *s)
{
    const char *rwx = "rwxrwxrwx";
    int k;
    s[0] = S_ISDIR(m) ? 'd' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' : (m & S_IFMT) == S_IFIFO ? 'p' : '-';
    for (k = 0; k < 9; k++) s[1 + k] = m & (0400 >> k) ? rwx[k] : '-';
    if (m & S_ISUID) s[3] = s[3] == 'x' ? 's' : 'S';
    if (m & S_ISGID) s[6] = s[6] == 'x' ? 's' : 'S';
    if (m & S_ISVTX) s[9] = s[9] == 'x' ? 't' : 'T';
    s[10] = 0;
}

static unsigned kib(const struct stat *st)            /* the space a file takes, in 1 KiB blocks */
{
    if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode)) return 0;
    return ((unsigned)st->st_size + 1023) / 1024;
}

static char *human(unsigned n, char *b)               /* 999, 1.5K, 37K, 2.1M */
{
    static const char unit[] = "KMG";
    unsigned v = n, frac = 0;
    int u = -1;
    if (n < 1024) { sprintf(b, "%u", n); return b; }
    while (v >= 1024 && u < 2) { frac = v % 1024; v /= 1024; u++; }
    if (v < 10) {
        unsigned tenths = (frac * 10 + 1023) / 1024;  /* (rounded up, as ls does) */
        if (tenths == 10) { v++; tenths = 0; }
        if (v < 10) { sprintf(b, "%u.%u%c", v, tenths, unit[u]); return b; }
    } else if (frac) v++;
    sprintf(b, "%u%c", v, unit[u]);
    return b;
}

static const char *user(int uid)
{
    static char b[12];
    struct passwd *pw = nflag ? 0 : getpwuid(uid);
    if (pw) return pw->pw_name;
    sprintf(b, "%d", uid);
    return b;
}

static const char *group(int gid)
{
    static char b[12];
    struct group *gr = nflag ? 0 : getgrgid(gid);
    if (gr) return gr->gr_name;
    sprintf(b, "%d", gid);
    return b;
}

static char mark(const struct stat *st)
{
    if (S_ISDIR(st->st_mode)) return '/';
    if ((st->st_mode & S_IFMT) == S_IFIFO) return '|';
    if (S_ISREG(st->st_mode) && (st->st_mode & 0111)) return '*';
    return 0;
}

/* the name as listed, with its -i / -s prefix and -F mark: its width */
static int shortform(const struct ent *e, char *out)
{
    char *o = out, b[16];
    if (iflag) o += sprintf(o, "%5u ", e->st.st_ino);
    if (sflag && hflag) o += sprintf(o, "%4s ", human(kib(&e->st) * 1024, b));
    else if (sflag) o += sprintf(o, "%4u ", kib(&e->st));
    o += sprintf(o, "%s", e->name);
    if (Fflag && e->ok && mark(&e->st)) { *o++ = mark(&e->st); *o = 0; }
    return o - out;
}

static void longform(const struct ent *e)
{
    char m[12], b[16], when[16];
    const struct stat *st = &e->st;
    time_t t = st->st_mtime;
    char *c = ctime(&t);
    mode(st->st_mode, m);
    /* within six months: month day hh:mm; else month day year */
    if (t > now - 182L * 86400 && t <= now + 3600) sprintf(when, "%.12s", c + 4);
    else sprintf(when, "%.7s %.4s", c + 4, c + 20);
    if (iflag) printf("%5u ", st->st_ino);
    if (sflag && hflag) printf("%4s ", human(kib(st) * 1024, b));
    else if (sflag) printf("%4u ", kib(st));
    printf("%s %2d %-8s %-8s ", m, st->st_nlink, user(st->st_uid), group(st->st_gid));
    if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
        printf("%3d,%3d", (st->st_rdev >> 8) & 255, st->st_rdev & 255);
    else if (hflag)
        printf("%7s", human(st->st_size, b));
    else
        printf("%7d", st->st_size);
    printf(" %s %s", when, e->name);
    if (Fflag && mark(st)) putchar(mark(st));
    putchar('\n');
}

static int order(const void *pa, const void *pb)
{
    const struct ent *a = pa, *b = pb;
    int d = 0;
    if (tflag && a->st.st_mtime != b->st.st_mtime) d = a->st.st_mtime > b->st.st_mtime ? -1 : 1;
    else if (Sflag && a->st.st_size != b->st.st_size) d = a->st.st_size > b->st.st_size ? -1 : 1;
    else d = strcmp(a->name, b->name);
    return rflag ? -d : d;
}

/* print a set of entries: long, one per line, or in columns (down, then across) */
static void show(struct ent *v, int n, int total)
{
    int i;
    if (lflag) {
        if (total >= 0) {
            char b[16];
            if (hflag) printf("total %s\n", human((unsigned)total * 1024, b));
            else printf("total %d\n", total);
        }
        for (i = 0; i < n; i++) if (v[i].ok) longform(&v[i]);
        return;
    }
    if (!cols) {
        char b[300];
        for (i = 0; i < n; i++) { shortform(&v[i], b); printf("%s\n", b); }
        return;
    }
    {
        static char b[300];
        int w = 1, ncol, nrow, r, k;
        for (i = 0; i < n; i++) { int l = shortform(&v[i], b); if (l > w) w = l; }
        w += 2;
        ncol = width / w;
        if (ncol < 1) ncol = 1;
        nrow = (n + ncol - 1) / ncol;
        for (r = 0; r < nrow; r++) {
            for (k = r; k < n; k += nrow) {
                int l = shortform(&v[k], b);
                fputs(b, stdout);
                if (k + nrow < n) while (l++ < w) putchar(' ');
            }
            putchar('\n');
        }
    }
}

static void list(const char *path, int heading);

static void listdir(const char *path)
{
    DIR *d;
    struct dirent *e;
    struct ent *v = 0;
    char full[300];
    int n = 0, cap = 0, i, total = 0;
    if (!(d = opendir(path))) { fprintf(stderr, "ls: %s: %s\n", path, strerror(errno)); status = 1; return; }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' && !aflag && !Aflag) continue;
        if (Aflag && !aflag && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))) continue;
        if (n == cap) {
            struct ent *nv;
            cap = cap ? 2 * cap : 32;
            if (!(nv = realloc(v, cap * sizeof *v))) { fprintf(stderr, "ls: out of memory\n"); status = 1; break; }
            v = nv;
        }
        v[n].name = strdup(e->d_name);
        v[n].ok = 1;
        memset(&v[n].st, 0, sizeof v[n].st);
        n++;
    }
    closedir(d);
    for (i = 0; i < n; i++) {           /* (stat each: needed for -l -t -S -F -i -s -R) */
        if (!(lflag || tflag || Sflag || Fflag || iflag || sflag || Rflag)) break;
        sprintf(full, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", v[i].name);
        if (stat(full, &v[i].st) < 0) { fprintf(stderr, "ls: %s: %s\n", full, strerror(errno)); status = 1; v[i].ok = 0; }
        else total += kib(&v[i].st);
    }
    qsort(v, n, sizeof *v, order);
    show(v, n, total);
    if (Rflag)
        for (i = 0; i < n; i++)
            if (v[i].ok && S_ISDIR(v[i].st.st_mode) && strcmp(v[i].name, ".") && strcmp(v[i].name, "..")) {
                sprintf(full, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", v[i].name);
                putchar('\n');
                list(full, 1);
            }
    for (i = 0; i < n; i++) free(v[i].name);
    free(v);
}

static void list(const char *path, int heading)
{
    if (heading) printf("%s:\n", path);
    listdir(path);
}

int main(int argc, char **argv)
{
    int i = 1, k, nfiles = 0, ndirs = 0, first = 1;
    struct ent *files;
    char *e;
    cols = isatty(1);
    if ((e = getenv("COLUMNS")) != 0 && atoi(e) > 10) width = atoi(e);
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *f;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (f = argv[i] + 1; *f; f++)
            switch (*f) {
            case 'l': lflag = 1; break;
            case 'n': lflag = nflag = 1; break;
            case 'h': hflag = 1; break;
            case 'a': aflag = 1; break;
            case 'A': Aflag = 1; break;
            case 'd': dflag = 1; break;
            case 'R': Rflag = 1; break;
            case 't': tflag = 1; break;
            case 'S': Sflag = 1; break;
            case 'r': rflag = 1; break;
            case 'F': Fflag = 1; break;
            case 'i': iflag = 1; break;
            case 's': sflag = 1; break;
            case '1': cols = 0; break;
            case 'C': cols = 1; break;
            default:
                fprintf(stderr, "ls: unknown option -%c\nusage: ls [-1ACFRSadhilnrst] [name ...]\n", *f);
                return 2;
            }
    }
    now = time(0);
    if (i == argc) { list(".", 0); return status; }
    /* files (and, with -d, directories) first, then each directory */
    files = malloc((argc - i) * sizeof *files);
    if (!files) { fprintf(stderr, "ls: out of memory\n"); return 1; }
    for (k = i; k < argc; k++) {
        struct stat st;
        if (stat(argv[k], &st) < 0) { fprintf(stderr, "ls: %s: %s\n", argv[k], strerror(errno)); status = 1; argv[k] = 0; continue; }
        if (S_ISDIR(st.st_mode) && !dflag) { ndirs++; continue; }
        files[nfiles].name = argv[k];
        files[nfiles].st = st;
        files[nfiles].ok = 1;
        nfiles++;
        argv[k] = 0;
    }
    qsort(files, nfiles, sizeof *files, order);
    show(files, nfiles, -1);
    if (nfiles) first = 0;
    for (k = i; k < argc; k++) {
        if (!argv[k]) continue;
        if (!first) putchar('\n');
        list(argv[k], nfiles + ndirs > 1 || status);
        first = 0;
    }
    return status;
}
