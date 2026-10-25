/* usb_model.c -- the I/O card's USB controller (the CH376S model in p32io.h,
   D43) driven through its two ports, stand-alone: reset, host mode, the drive
   found, its capacity, sectors read, written and read back, one past the end.
   run_tests.sh runs it with a drive, with the drive pulled out after one
   sector (-Uout 1), and with none. */
#include <stdio.h>
#define DATA (*(volatile unsigned char *)0xFFFFFF50)
#define CMD  (*(volatile unsigned char *)0xFFFFFF51)
static int wait_int(void)               /* INT# low, then GET_STATUS */
{
    int k;
    for (k = 0; k < 100000 && (CMD & 0x80); k++) ;
    if (k == 100000) return -1;
    CMD = 0x22;
    return DATA;
}
static unsigned char buf[1024];
static int rw(int write, unsigned lba, int n, unsigned char *p)
{
    int s, k, st;
    CMD = write ? 0x56 : 0x54;
    DATA = lba; DATA = lba >> 8; DATA = lba >> 16; DATA = lba >> 24; DATA = n;
    for (s = 0; s < n * 8; s++) {
        st = wait_int();
        if (st != (write ? 0x1E : 0x1D)) return st;
        if (write) { CMD = 0x2C; DATA = 64; for (k = 0; k < 64; k++) DATA = *p++; CMD = 0x57; }
        else { CMD = 0x27; if (DATA != 64) return -2; for (k = 0; k < 64; k++) *p++ = DATA; CMD = 0x55; }
    }
    st = wait_int();
    return st == 0x14 ? 0 : st;
}
int main(void)
{
    int st, k;
    unsigned cap;
    CMD = 0x05;                                         /* RESET_ALL */
    CMD = 0x06; DATA = 0x55; printf("check_exist %02x\n", DATA);
    CMD = 0x01; printf("version %02x\n", DATA);
    CMD = 0x15; DATA = 7; printf("mode 7 %02x\n", DATA);
    CMD = 0x15; DATA = 6; printf("mode 6 %02x\n", DATA);
    printf("connect %02x\n", wait_int());
    CMD = 0x51; printf("disk_init %02x\n", wait_int());
    CMD = 0x3E; st = wait_int(); CMD = 0x27; k = DATA;
    cap = DATA; cap |= DATA << 8; cap |= DATA << 16; cap |= (unsigned)DATA << 24;
    printf("capacity %02x len %d sectors %u\n", st, k, cap);
    printf("read 0-1 %d: %c%c%c%c ... %02x\n", rw(0, 0, 2, buf), buf[0], buf[1], buf[2], buf[3], buf[1023]);
    for (k = 0; k < 512; k++) buf[k] = k * 7 + 3;
    printf("write 5 %d\n", rw(1, 5, 1, buf));
    for (k = 0; k < 512; k++) buf[k] = 0;
    st = rw(0, 5, 1, buf);
    for (k = 0; k < 512 && buf[k] == (unsigned char)(k * 7 + 3); k++) ;
    printf("read 5 back %d: %s\n", st, k == 512 ? "identical" : "DIFFERENT");
    printf("read past the end: %02x\n", rw(0, cap, 1, buf));
    return 0;
}
