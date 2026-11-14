/* sys/mouse.h -- P32W Unix: /dev/mouse, the video card's PS/2 mouse for a program (D120; mouse(4)).
   read() gives its packets, three bytes each, as the mouse sends them. */
#ifndef _SYS_MOUSE_H
#define _SYS_MOUSE_H
#define MS_LEFT   0x01          /* a packet's first byte: the buttons that are down ... */
#define MS_RIGHT  0x02
#define MS_MIDDLE 0x04
#define MS_XNEG   0x10          /* ... and the signs of the two movements */
#define MS_YNEG   0x20
#define MS_DX(p)  ((int)(p)[1] - ((p)[0] & MS_XNEG ? 256 : 0))  /* how far right it moved (p: unsigned char *) */
#define MS_DY(p)  ((int)(p)[2] - ((p)[0] & MS_YNEG ? 256 : 0))  /* ... and how far up */
#endif
