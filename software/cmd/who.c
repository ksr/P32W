/* who -- who is logged in (from /etc/utmp) */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <utmp.h>
int main(void)
{
    struct utmp u;
    int fd = open(UTMP, O_RDONLY);
    if (fd < 0) { perror(UTMP); return 1; }
    while (read(fd, &u, sizeof u) == sizeof u) {
        time_t t = u.ut_time;
        if (!u.ut_name[0]) continue;
        printf("%-8.8s %-8.8s %.12s\n", u.ut_name, u.ut_line, ctime(&t) + 4);
    }
    return 0;
}
