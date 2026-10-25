/* uptime -- the time, how long since boot, and how many users are logged in */
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/times.h>
#include <sys/sysinfo.h>

int main(void)
{
    unsigned up = (unsigned)times(0) / CLK_TCK, users = 0;
    time_t now = time(0);
    struct tm *tm = localtime(&now);
    struct utmp u;
    int fd = open(UTMP, O_RDONLY);
    if (fd >= 0) {
        while (read(fd, &u, sizeof u) == sizeof u)
            if (u.ut_name[0]) users++;
        close(fd);
    }
    printf(" %02d:%02d:%02d up ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    if (up >= 86400) printf("%u day%s, ", up / 86400, up / 86400 == 1 ? "" : "s");
    printf("%2u:%02u, %u user%s", up / 3600 % 24, up / 60 % 60, users, users == 1 ? "" : "s");
    {
        struct sysinfo si;
        if (sysinfo(&si) == 0)
            printf(",  load average: %u.%02u, %u.%02u, %u.%02u", si.loads[0] >> 11, (si.loads[0] & 2047) * 100 >> 11,
                   si.loads[1] >> 11, (si.loads[1] & 2047) * 100 >> 11, si.loads[2] >> 11, (si.loads[2] & 2047) * 100 >> 11);
    }
    putchar('\n');
    return 0;
}
