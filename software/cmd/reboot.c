/* reboot -- write everything out, mark the filesystem clean, restart into the monitor (root) */
#include <stdio.h>
#include <unistd.h>
int main(void)
{
    sync();
    if (reboot(RB_REBOOT) < 0) { perror("reboot"); return 1; }
    return 0;
}
