/* sys/video.h -- P32W Unix: /dev/video, the video card's HD63484 for a program's own drawing (D110; video(4)).
   The chip draws from commands, 16-bit words: write() sends them, read() takes what a command read back. */
#ifndef _SYS_VIDEO_H
#define _SYS_VIDEO_H
#define VIOCSYNC 0x5601         /* ioctl: wait until the chip has drawn everything written so far */
#define VIOCINFO 0x5602         /* ... the screen and the font, a struct vidinfo */
#define VIOCSPARE 0x5603        /* ... the chip's memory that is the program's own, a struct vidspare */

struct vidinfo {
    int width, height;          /* dots: 640 x 480. x 0, y 0 is the bottom-left dot; y runs up */
    int memw;                   /* words of the chip's memory a line (40); a word's bit 0 is its left-hand dot */
    int cellw, cellh;           /* a character of the font: 8 x 16 dots */
    int fonts;                  /* pictures of the font, under the screen (8): 0 plain, + 1 inverse, + 2 underlined, + 4 bold */
    int fonty;                  /* the plain picture's top line, a y below 0; picture a is fontstep lines below it. */
    int fontstep;               /* In a picture, character c (0x20-0x7E) has its top-left dot at */
                                /* x = (c & 31) * cellw, y = fonty - a * fontstep - cellh * ((c - 0x20) >> 5) */
};

struct vidspare {               /* lines of the chip's memory under the screen and the font, each the screen's */
    int y, lines;               /* width, that nothing else uses: the first is y (below 0), lines of them, downwards. */
};                              /* A program may keep pictures there (AGCPY) while it has the device */

/* The chip's commands, as its manual has them (Hitachi HD63484 ACRTC; one bit a dot here). A command is its
   word and then its parameters. OR an operation into the drawing ones: */
#define VO_REPLACE 0            /* the dots drawn are set from the pattern (solid white as a program gets the chip) */
#define VO_OR      1
#define VO_AND     2
#define VO_XOR     3
#define VA_INSIDE  0x40         /* ... and this, to draw only the dots inside the rectangle of registers 8 to 0x0B */
#define VM_ONES    0x08         /* ... and this, to draw (from register 1) only the dots where the pattern has a 1. */
                                /* Without it a dot is set from register 1 where the pattern has a 1 and from */
                                /* register 0 where it has a 0 */
#define VC_ORG   0x0400         /* + 2: the origin */
#define VC_WPR   0x0800         /* | register, + 1: a drawing parameter register (0 and 1 the two colours, 4 the mask */
                                /* of MOD and the copies, 5 the pattern's pointer: the row in bits 15-12 and the bit */
                                /* in 7-4 that the next dot drawn takes; 8 to 0x0B a rectangle for VA_INSIDE: its */
                                /* least x, least y, greatest x, greatest y) */
#define VC_RPR   0x0C00         /* | register: ... read: a word for read() */
#define VC_WPTN  0x1800         /* | row, + n, then n words: the pattern RAM's 16 rows, from that one on. A drawing */
                                /* command walks a row a bit a dot (bit 0 first), a filled one a row a line */
#define VC_RD    0x4400         /* the word at the read / write pointer, for read(); the pointer steps on */
#define VC_WT    0x4800         /* + 1: a word written there */
#define VC_MOD   0x4C00         /* | operation, + 1: ... put together with the word there, in the bits register 4 has 1s */
#define VC_CLR   0x5800         /* + data, words - 1, -(lines - 1): an area of memory set, from the pointer down */
#define VC_AMOVE 0x8000         /* + x, y: the current point */
#define VC_RMOVE 0x8400         /* + dx, dy */
#define VC_ALINE 0x8800         /* + x, y: a line from the current point up to x, y (that last dot is not drawn), */
                                /* where the current point then is */
#define VC_RLINE 0x8C00         /* + dx, dy */
#define VC_ARCT  0x9000         /* + x, y: a rectangle's four lines, the current point one corner and x, y the other */
#define VC_RRCT  0x9400         /* + dx, dy */
#define VC_APLL  0x9800         /* + n, then n points: lines from point to point */
#define VC_APLG  0xA000         /* + n, then n points: ... and back to the first */
#define VC_AFRCT 0xC000         /* + x, y: a filled rectangle */
#define VC_RFRCT 0xC400         /* + dx, dy */
#define VC_DOT   0xCC00         /* the dot at the current point */
#define VC_AGCPY 0xED00         /* + x, y, dx, dy: dx + 1 by |dy| + 1 dots copied from x, y (its top-left; dy below 0) */
                                /* to the current point, which ends dx + 1 dots to the right: a character */
#define VC_GCPYUP 0xE000        /* + x, y, dx, dy (0 or more): ... from x, y, its bottom-left, rightwards a line at a */
                                /* time and upwards, to the current point the same way */
#define VC_GCPYDN 0xE300        /* + x, y, dx, dy (0 or less): ... from x, y, its top-right, leftwards and downwards */
#endif
