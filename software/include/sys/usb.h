/* sys/usb.h -- P32W Unix: the USB drive (D43; /dev/usb, /dev/rusb) */
#ifndef _SYS_USB_H
#define _SYS_USB_H
#define USBIOSIZE  0x5501       /* ioctl on /dev/rusb: the drive's size, in 512-byte sectors (an unsigned) */
#define USBIOREAD  0x5502       /* ... count sectors from lba into buf (a struct usbio) */
#define USBIOWRITE 0x5503       /* ... count sectors from buf to lba (the descriptor open for writing) */

/* sectors by number, not by file offset: a file offset stops at 2 GB */
struct usbio {
    unsigned lba;               /* the first sector */
    unsigned count;             /* how many: 1 to 128 */
    char *buf;                  /* count * 512 bytes */
};
#endif
