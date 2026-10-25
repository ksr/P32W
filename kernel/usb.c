/* usb.c -- USB drives through the I/O card's two CH376S (D43, D50;
 * docs/usb-design.md): block device 2 (/dev/usb, /dev/usb1), through the
 * buffer cache, for mount, mkfs and fsck; and the raw sectors as character
 * device 5 (/dev/rusb, /dev/rusb1), for the FAT tools. Minor n is
 * controller n: 0 at FF50, 1 at FF54 (D52).
 *
 * The CH376S in its sector mode: FF51 written is a command, read is the
 * status (bit 7 = INT#, low while an interrupt is pending); FF50 moves the
 * command's data. A command that finishes raises INT#, and GET_STATUS reads
 * the interrupt status and clears it. A sector moves in eight 64-byte
 * chunks, each announced by an interrupt. Polled, as the CF card is; IRQ6
 * is not used yet.
 *
 * A drive is found when it is first opened: the controller says a device is
 * connected, DISK_INIT readies it (some drives need a few tries), and
 * DISK_CAPACITY gives its size. Pulled out, it is noticed by the next
 * access: the transfer fails with EIO and its blocks in the buffer cache are
 * dropped (what was not written is lost). Every wait has a time limit, so a
 * drive that goes away mid-transfer never hangs the system. */
#include "defs.h"
#include <sys/usb.h>

#define NUSB 2
#define UDATA IOB(u->base)
#define UCMD  IOB(u->base + 1)

/* commands and statuses (WCH's CH376INC.H) */
#define RESET_ALL     0x05
#define CHECK_EXIST   0x06
#define SET_USB_MODE  0x15
#define GET_STATUS    0x22
#define RD_USB_DATA0  0x27
#define WR_HOST_DATA  0x2C
#define DISK_CONNECT  0x30
#define DISK_CAPACITY 0x3E
#define DISK_INIT     0x51
#define DISK_READ     0x54
#define DISK_RD_GO    0x55
#define DISK_WRITE    0x56
#define DISK_WR_GO    0x57
#define RET_SUCCESS   0x51
#define INT_SUCCESS   0x14
#define INT_CONNECT   0x15
#define INT_DISCONNECT 0x16
#define INT_DISK_READ  0x1D
#define INT_DISK_WRITE 0x1E

#define WAIT_TICKS (2 * HZ)             /* a chunk, or a command: 2 s at most */
#define INIT_TICKS (5 * HZ)             /* DISK_INIT: a drive spinning up */

static struct usbc {
    uint base;
    int present;                        /* the controller answered at boot */
    int ready;                          /* a drive found and initialised */
    uint nsect;                         /* its size, in 512-byte sectors */
} usbc[NUSB] = { { USBBASE }, { USBBASE1 } };
#define UNIT(u) ((int)((u) - usbc))

/* a command byte; the chip needs 1.5 us before the next access (a few
   instructions) */
static void cmd(struct usbc *u, int c)
{
    volatile int k;
    UCMD = c;
    for (k = 0; k < 2; k++) ;
}

/* the next interrupt's status, or -1 after n ticks without one (at boot,
   before the clock runs, n counts polls instead) */
static int wait_int(struct usbc *u, uint n)
{
    uint t0 = ticks, polls = 0;
    while (UCMD & 0x80) {
        if (ticks - t0 > n || ++polls > 2000000) return -1;
    }
    cmd(u, GET_STATUS);
    return UDATA;
}

/* the drive is gone: forget it, and drop its blocks from the cache */
static void usb_gone(struct usbc *u)
{
    if (u->ready) { if (UNIT(u)) kprintf("usb%d: the drive was removed\n", UNIT(u)); else kprintf("usb: the drive was removed\n"); }
    u->ready = 0;
    u->nsect = 0;
    bdrop(USBDEV + UNIT(u));
}

/* an interrupt nobody asked for: a drive plugged in or pulled out */
static void usb_events(struct usbc *u)
{
    while (!(UCMD & 0x80)) {
        cmd(u, GET_STATUS);
        if (UDATA == INT_DISCONNECT) usb_gone(u);
    }
}

/* a controller, at boot: there, and in host mode? */
static int usb_init1(struct usbc *u)
{
    volatile int k;
    cmd(u, RESET_ALL);
    for (k = 0; k < 40000; k++) ;      /* (about 40 ms: the chip resets) */
    cmd(u, CHECK_EXIST);
    UDATA = 0x55;
    if (UDATA != 0xAA) return -1;       /* no CH376S */
    cmd(u, SET_USB_MODE);
    UDATA = 6;                          /* host, with SOF generation */
    if (UDATA != RET_SUCCESS) return -1;
    u->present = 1;
    return 0;
}

int usb_init(void)
{
    int k, n = 0;
    for (k = 0; k < NUSB; k++)
        if (usb_init1(&usbc[k]) == 0) n++;
    return n ? 0 : -1;
}

/* find and ready a drive: 0, or -ENXIO if there is none */
static int usb_attach(struct usbc *u)
{
    int k, st;
    if (!u->present) return -ENXIO;
    usb_events(u);
    if (u->ready) return 0;
    cmd(u, DISK_CONNECT);
    if (wait_int(u, WAIT_TICKS) != INT_SUCCESS) return -ENXIO;
    for (k = 0; k < 5; k++) {           /* (some drives need a few tries) */
        cmd(u, DISK_INIT);
        if ((st = wait_int(u, INIT_TICKS)) == INT_SUCCESS) break;
        if (st == INT_DISCONNECT || st < 0) return -ENXIO;
    }
    if (k == 5) return -ENXIO;
    cmd(u, DISK_CAPACITY);
    if (wait_int(u, WAIT_TICKS) != INT_SUCCESS) return -ENXIO;
    cmd(u, RD_USB_DATA0);
    if (UDATA != 4) return -ENXIO;
    u->nsect = UDATA;
    u->nsect |= UDATA << 8;
    u->nsect |= UDATA << 16;
    u->nsect |= (uint)UDATA << 24;
    u->ready = 1;
    if (UNIT(u)) kprintf("usb%d: a drive of %u KiB\n", UNIT(u), u->nsect / 2);
    else kprintf("usb: a drive of %u KiB\n", u->nsect / 2);
    return 0;
}

/* n sectors from lba to / from p: 0, or -EIO */
static int usb_rw(struct usbc *u, uint lba, int n, uchar *p, int write)
{
    int c, k, st;
    if (usb_attach(u) < 0) return -EIO;
    if (lba + n > u->nsect || lba + n < lba) return -EIO;
    cmd(u, write ? DISK_WRITE : DISK_READ);
    UDATA = lba;
    UDATA = lba >> 8;
    UDATA = lba >> 16;
    UDATA = lba >> 24;
    UDATA = n;
    for (c = 0; c < 8 * n; c++) {
        st = wait_int(u, WAIT_TICKS);
        if (st != (write ? INT_DISK_WRITE : INT_DISK_READ)) {
            if (st == INT_DISCONNECT || st < 0) usb_gone(u);
            return -EIO;
        }
        if (write) {
            cmd(u, WR_HOST_DATA);
            UDATA = 64;
            for (k = 0; k < 64; k++) UDATA = *p++;
            cmd(u, DISK_WR_GO);
        } else {
            cmd(u, RD_USB_DATA0);
            if (UDATA != 64) return -EIO;
            for (k = 0; k < 64; k++) *p++ = UDATA;
            cmd(u, DISK_RD_GO);
        }
    }
    st = wait_int(u, WAIT_TICKS);
    if (st != INT_SUCCESS) {
        if (st == INT_DISCONNECT || st < 0) usb_gone(u);
        return -EIO;
    }
    return 0;
}

/* ---------------------------------------------------------------- the block device */
int usb_open(int minor, int flags)
{
    (void)flags;
    if (minor >= NUSB) return -ENXIO;
    return usb_attach(&usbc[minor]);
}

int usb_close(int minor) { (void)minor; return 0; }

void usb_strategy(struct buf *b)       /* a 1 KiB block: two sectors */
{
    if (usb_rw(&usbc[minor(b->dev)], b->blkno * 2, 2, b->data, b->flags & B_WRITE) < 0)
        b->flags |= B_ERROR;
}

/* ---------------------------------------------------------------- the raw device */
#define NSEC 4                          /* sectors moved by one command (USBIOREAD, USBIOWRITE) */
static uchar usbsec[NSEC * 512];

int usb_rawopen(int minor, int flags)
{
    (void)flags;
    if (minor >= NUSB) return -ENXIO;
    return usb_attach(&usbc[minor]);
}

int usb_rawread(int minor, uint dst, int n, uint off)
{
    struct usbc *u = &usbc[minor];
    int k = 0;
    while (k < n) {
        uint s = (off + k) / 512, o = (off + k) % 512, c = 512 - o;
        if (s >= u->nsect && u->ready) break;           /* (the end of the drive) */
        if (c > (uint)(n - k)) c = n - k;
        if (usb_rw(u, s, 1, usbsec, 0) < 0) return k ? k : -EIO;
        if (copyout(dst + k, usbsec + o, c) < 0) return -EFAULT;
        k += c;
    }
    return k;
}

int usb_rawwrite(int minor, uint src, int n, uint off)
{
    struct usbc *u = &usbc[minor];
    int k = 0;
    while (k < n) {
        uint s = (off + k) / 512, o = (off + k) % 512, c = 512 - o;
        if (c > (uint)(n - k)) c = n - k;
        if (c < 512 && usb_rw(u, s, 1, usbsec, 0) < 0) return k ? k : -EIO;
        if (copyin(usbsec + o, src + k, c) < 0) return -EFAULT;
        if (usb_rw(u, s, 1, usbsec, 1) < 0) return k ? k : -EIO;
        k += c;
    }
    return n;
}

/* ioctl USBIOSIZE: the drive's size in sectors (dosformat, mkfs's default).
   USBIOREAD, USBIOWRITE: whole sectors by number (struct usbio), for the FAT
   tools: a file offset stops at 2 GB, and a stick is bigger. A request
   moves up to 128 sectors, NSEC to a command, through usbsec (so a bad
   buffer address is found before the drive is touched). */
int usb_rawioctl(int minor, int req, uint arg)
{
    struct usbc *u = &usbc[minor];
    struct usbio io;
    uint k, n;
    if (req != USBIOSIZE && req != USBIOREAD && req != USBIOWRITE) return -EINVAL;
    if (usb_attach(u) < 0) return -ENXIO;
    if (req == USBIOSIZE) return copyout(arg, &u->nsect, 4);
    if (copyin(&io, arg, sizeof io) < 0) return -EFAULT;
    if (io.count > 128 || io.lba + io.count > u->nsect || io.lba + io.count < io.lba) return -EINVAL;
    for (k = 0; k < io.count; k += n) {
        n = io.count - k < NSEC ? io.count - k : NSEC;
        if (req == USBIOREAD) {
            if (usb_rw(u, io.lba + k, n, usbsec, 0) < 0) return -EIO;
            if (copyout((uint)io.buf + k * 512, usbsec, n * 512) < 0) return -EFAULT;
        } else {
            if (copyin(usbsec, (uint)io.buf + k * 512, n * 512) < 0) return -EFAULT;
            if (usb_rw(u, io.lba + k, n, usbsec, 1) < 0) return -EIO;
        }
    }
    return 0;
}
