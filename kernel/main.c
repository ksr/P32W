/* main.c -- P32W Unix starts here (M3, D22; os-design.md 4.6). */
#include "defs.h"

void kmain(void)
{
    video_init();                       /* (the video card's screen and keyboard, if fitted: D50) */
    kprintf("\nP32W Unix (M7)\n");
    kinit();
    procinit();
    binit();
    fileinit();
    if (cf_init() < 0) panic("no disk");
    fsinit(ROOTDEV);
    kprintf("memory: %d MiB of RAM, %d KiB free; filesystem: %u blocks, %u free\n",
            ram_bytes >> 20, nfree_frames() * 4, sb.nblocks, sb.free_blocks);
    if (rtc_init() == 0) {              /* the time of day, from the clock (D23) */
        extern uint boottime;
        boottime = rtc_get() - ticks / HZ;
    }
    usb_init();                         /* (the I/O card's USB controllers, if they answer) */
    net_init();                         /* (... and its network coprocessor: D45) */
    ttyinit();
    IOW(TRELOAD) = TICK;                /* the clock: HZ ticks a second */
    IOW(TCOUNT) = TICK;
    IOW(TCTRL) = 1;
    mtcr_imask(mfcr_imask() | 1 << IRQ_TIMER);
    tlbflush();
    userinit();
    scheduler();
}
