/* win -- a program in a window of its own, under the window manager (wm(1), D124).
 *
 *   win [-g WxH] [-s COLSxROWS] [-n NAME] [-t] command [argument ...]
 *
 * win asks wm, by a request written to its terminal, to start the command on a terminal of its own. The
 * program is then the one its terminal listens to, whatever started it: it may ask where the mouse is, and
 * its picture's close box ends it. Its terminal has no window until the program writes something (-t: from
 * the start), so a program that only draws has its picture's window and nothing else.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char req[1000], dir[256], *size = 0, *name = 0, *p;
    int k, cols = 0, rows = 0, quiet = 1, n, fd;
    for (k = 1; k < argc && argv[k][0] == '-'; k++) {
        if (!strcmp(argv[k], "-g") && k + 1 < argc) size = argv[++k];
        else if (!strcmp(argv[k], "-s") && k + 1 < argc) { if (sscanf(argv[++k], "%dx%d", &cols, &rows) != 2) k = argc; }
        else if (!strcmp(argv[k], "-n") && k + 1 < argc) name = argv[++k];
        else if (!strcmp(argv[k], "-t")) quiet = 0;
        else k = argc;
    }
    if (k >= argc) { fprintf(stderr, "usage: win [-g WxH] [-s COLSxROWS] [-n NAME] [-t] command [argument ...]\n"); return 2; }
    if (!getenv("WM") || (fd = open("/dev/tty", O_WRONLY)) < 0) { fprintf(stderr, "win: not in a window of wm\n"); return 1; }
    if (!name) { name = strrchr(argv[k], '/'); name = name ? name + 1 : argv[k]; }
    if (!getcwd(dir, sizeof dir)) strcpy(dir, "/");
    n = sprintf(req, "\033_W%d;%d;%d:%.20s\037%s\037", cols, rows, quiet, name, dir);
    if (size) n += sprintf(req + n, "WMSIZE=%.20s ", size);
    for (; k < argc; k++) {             /* each argument as it is: in quotes if sh would take it apart */
        const char *a = argv[k];
        int plain = *a != 0;
        for (p = argv[k]; *p; p++)
            if (!(*p >= 'a' && *p <= 'z') && !(*p >= 'A' && *p <= 'Z') && !(*p >= '0' && *p <= '9') && !strchr("_./=:,+-@%", *p)) plain = 0;
        if (n + 4 * (int)strlen(a) + 8 > (int)sizeof req) { fprintf(stderr, "win: the command is too long\n"); return 2; }
        if (plain) n += sprintf(req + n, "%s", a);
        else {
            req[n++] = '\'';
            for (; *a; a++) { if (*a == '\'') n += sprintf(req + n, "'\\''"); else req[n++] = *a; }
            req[n++] = '\'';
        }
        if (k + 1 < argc) req[n++] = ' ';
    }
    for (p = req + 2; p < req + n; p++) if ((*p < 0x20 && *p != 0x1F) || *p > 0x7E) *p = '?';
    n += sprintf(req + n, "\033\\");
    return write(fd, req, n) == n ? 0 : 1;
}
