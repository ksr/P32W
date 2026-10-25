/* procs.c -- system, popen and pclose (exit statuses, two pipes open at
 * once and closed in either order), tmpfile, tmpnam, mkstemp, freopen,
 * setvbuf, and remove on a file, an empty and a non-empty directory.
 * Standard C and POSIX only: the expected output is the build machine's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

static void status(const char *what, int st)
{
    if (st == -1) printf("%s: -1\n", what);
    else if (WIFEXITED(st)) printf("%s: exit %d\n", what, WEXITSTATUS(st));
    else printf("%s: signal %d\n", what, WTERMSIG(st));
}

int main(void)
{
    char line[100], name[L_tmpnam], tmpl[] = "mkXXXXXX";
    FILE *a, *b, *f;
    int fd, n;

    printf("system(0) %s\n", system(0) ? "has a shell" : "has none");
    fflush(stdout);
    status("system echo", system("echo from the shell"));
    status("system exit 3", system("exit 3"));
    status("system no such command", system("no-such-command-xyz 2>/dev/null"));

    a = popen("echo one; echo two; echo three", "r");
    n = 0;
    while (fgets(line, sizeof line, a)) printf("popen read %d: %s", ++n, line);
    status("pclose", pclose(a));

    a = popen("cat > piped.txt; exit 4", "w");
    fprintf(a, "written through a pipe\n");
    status("pclose writer", pclose(a));
    f = fopen("piped.txt", "r");
    fgets(line, sizeof line, f);
    printf("the file has: %s", line);
    fclose(f);

    a = popen("echo first; exit 5", "r");               /* two at once */
    b = popen("echo second; exit 6", "r");
    fgets(line, sizeof line, b); printf("b: %s", line);
    fgets(line, sizeof line, a); printf("a: %s", line);
    status("pclose a", pclose(a));                      /* (in the order opened) */
    status("pclose b", pclose(b));
    a = popen("exit 7", "r");
    b = popen("exit 8", "r");
    status("pclose b first", pclose(b));                /* and the other way */
    status("pclose a then", pclose(a));

    f = tmpfile();
    fprintf(f, "kept in a temporary file\n");
    rewind(f);
    fgets(line, sizeof line, f);
    printf("tmpfile: %s", line);
    fclose(f);

    printf("tmpnam: %s\n", tmpnam(name) && access(name, 0) < 0 ? "a new name" : "failed");
    fd = mkstemp(tmpl);
    printf("mkstemp: %s, %s\n", fd >= 0 ? "a descriptor" : "failed",
           strncmp(tmpl, "mk", 2) == 0 && strcmp(tmpl + 2, "XXXXXX") ? "a new name" : "no name");
    write(fd, "abc", 3);
    close(fd);
    f = fopen(tmpl, "r");
    printf("mkstemp's file holds %d bytes\n", (int)fread(line, 1, sizeof line, f));
    fclose(f);
    remove(tmpl);

    f = fopen("one.txt", "w"); fprintf(f, "file one\n"); fclose(f);
    f = fopen("two.txt", "w"); fprintf(f, "file two\n"); fclose(f);
    f = fopen("one.txt", "r");
    fgets(line, sizeof line, f); printf("before freopen: %s", line);
    f = freopen("two.txt", "r", f);
    fgets(line, sizeof line, f); printf("after freopen: %s", line);
    fclose(f);
    printf("freopen of a missing file: %s\n", freopen("missing.txt", "r", fopen("one.txt", "r")) ? "opened" : "null");

    fflush(stdout);
    setvbuf(stdout, NULL, _IONBF, 0);                   /* unbuffered: in order with write(1) */
    printf("[unbuffered]");
    write(1, "[write]\n", 8);
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    printf("setvbuf bad mode: %d\n", setvbuf(stdout, NULL, 99, 0) != 0);

    mkdir("emptydir", 0755);
    mkdir("fulldir", 0755);
    f = fopen("fulldir/x", "w"); fclose(f);
    printf("remove file: %d\n", remove("one.txt"));
    printf("remove empty directory: %d, gone: %d\n", remove("emptydir"), access("emptydir", 0) < 0);
    printf("remove full directory fails: %d\n", remove("fulldir") != 0);
    remove("fulldir/x");
    remove("fulldir");
    remove("two.txt");
    remove("piped.txt");
    return 0;
}
