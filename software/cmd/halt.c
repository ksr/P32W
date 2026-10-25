/* halt -- write everything out, mark the filesystem clean, stop the machine (root) */
#include <stdio.h>
#include <unistd.h>
int main(void)
{
    sync();
    if (reboot(0) < 0) { perror("halt"); return 1; }
    return 0;
}
