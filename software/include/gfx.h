/* gfx.h -- drawing on the video card's screen: the library libgfx.a (cc ... -lgfx; gfx(3); D111).
   It works through /dev/video (video(4)): the card's chip draws, the library sends it its commands. */
#ifndef _GFX_H
#define _GFX_H

#define G_BLACK 0               /* the inks (gink): dots put out */
#define G_WHITE 1               /* ... lit */
#define G_XOR   2               /* ... turned over: a thing drawn twice is gone again */

#define G_UNDER  2              /* the looks of text (glook), added together: underlined */
#define G_BOLD   4              /* ... bold */
#define G_OPAQUE 8              /* ... the rest of each character's cell drawn too, in the other colour */

#define G_ONE 16384             /* gsin and gcos give the sine and cosine times this */

extern int gwidth, gheight;     /* the screen in dots (640 by 480), from gopen on. x 0, y 0 is its bottom-left dot; y runs up */
extern int gcellw, gcellh;      /* a character of text (8 by 16) */

int  gopen(void);               /* the screen taken and cleared: 0, or -1 (errno) */
void gclose(void);              /* ... given back to the console */
int  gflush(void);              /* what has been drawn so far, sent to the chip: 0, or -1 if a write failed */
int  gsync(void);               /* ... and waited for until the chip has drawn it */

void gclear(void);              /* every dot out */
void gink(int ink);
void gclip(int x0, int y0, int x1, int y1);     /* nothing is drawn outside this rectangle from here on */

void gpoint(int x, int y);
int  gget(int x, int y);        /* 1 if the dot is lit, 0 if not (or off the screen) */
void gline(int x0, int y0, int x1, int y1);
void gmove(int x, int y);       /* the pen put somewhere */
void gdraw(int x, int y);       /* a line from the pen to x, y, where the pen then is */
void gpoly(const int *xy, int n);               /* the outline through n points (x then y of each), closed */
void gbox(int x0, int y0, int x1, int y1);      /* a rectangle's outline: two opposite corners */
void gfbox(int x0, int y0, int x1, int y1);     /* ... filled */
void gcircle(int x, int y, int r);
void gfcircle(int x, int y, int r);
void gellipse(int x, int y, int rx, int ry);
void gfellipse(int x, int y, int rx, int ry);
void garc(int x, int y, int rx, int ry, int a0, int a1);    /* the part of an ellipse from the direction a0 round to a1: */
                                                /* degrees, 0 to the right, anticlockwise */
void gtext(int x, int y, const char *s);        /* text, x, y the bottom-left dot of its first character */
void glook(int look);

int  gsin(int degrees);
int  gcos(int degrees);

void gwords(const unsigned short *w, int n);    /* n of the chip's own command words (video(4)), in their turn */
int  gfd(void);                 /* /dev/video's file descriptor */
#endif
