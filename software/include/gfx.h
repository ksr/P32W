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

#define G_COPY 0                /* how a picture is put down (gstamp, gwrite): as it is */
#define G_OVER 1                /* ... its lit dots added to what is there */
#define G_MASK 2                /* ... only the dots stay lit that are lit in both */
#define G_FLIP 3                /* ... its lit dots turn over what is there: put down twice, it is gone */

#define G_MOUSE 256             /* ginput: it was the mouse */
#define G_SIZE  257             /* ... the picture has another size (gmaysize) */

#define G_ONE 16384             /* gsin and gcos give the sine and cosine times this */

extern int gwidth, gheight;     /* the screen in dots (640 by 480), from gopen on. x 0, y 0 is its bottom-left dot; y runs up */
extern int gcellw, gcellh;      /* a character of text (8 by 16) */

int  gopen(void);               /* the screen taken and cleared: 0, or -1 (errno) */
void gsize(int w, int h);       /* before gopen: in a window of wm (wm(1)) the picture is to be w by h dots, if */
                                /* WMSIZE does not say. On the screen itself it does nothing */
void gmaysize(int minw, int minh, int stepw, int steph);       /* after gopen: in a window of wm, whoever uses */
                                /* the window may make the picture bigger or smaller: minw by minh dots or more, by */
                                /* stepw and steph dots at a time. ginput then returns G_SIZE: gwidth and gheight */
                                /* are the new size and the picture is all black, for the program to draw again */
void gclose(void);              /* ... given back to the console */
int  gflush(void);              /* what has been drawn so far, sent to the chip: 0, or -1 if a write failed */
int  gsync(void);               /* ... and waited for until the chip has drawn it */

void gclear(void);              /* every dot out */
void gink(int ink);
void gclip(int x0, int y0, int x1, int y1);     /* nothing is drawn outside this rectangle from here on */
void gstyle(unsigned dashes);   /* lines (gline, gdraw, gpoly, gbox) are drawn in these 16 dots over and over, the top */
                                /* bit first: a 1 a dot drawn, a 0 one left as it is. 0xFFFF: whole again */
void gpattern(const unsigned short *rows);      /* filled figures are filled with these 16 rows of 16 dots, the top */
                                /* row first and each row's top bit its left-hand dot, laid over the screen from its */
                                /* top-left corner: a 1 a dot drawn, a 0 one left as it is. 0: solid again */

void gpoint(int x, int y);
int  gget(int x, int y);        /* 1 if the dot is lit, 0 if not (or off the screen) */
void gline(int x0, int y0, int x1, int y1);
void gmove(int x, int y);       /* the pen put somewhere */
void gdraw(int x, int y);       /* a line from the pen to x, y, where the pen then is */
void gpoly(const int *xy, int n);               /* the outline through n points (x then y of each), closed */
void gfpoly(const int *xy, int n);              /* ... filled: every dot inside it or on it (up to 256 points) */
void gbox(int x0, int y0, int x1, int y1);      /* a rectangle's outline: two opposite corners */
void gfbox(int x0, int y0, int x1, int y1);     /* ... filled */
void gcircle(int x, int y, int r);
void gfcircle(int x, int y, int r);
void gellipse(int x, int y, int rx, int ry);
void gfellipse(int x, int y, int rx, int ry);
void garc(int x, int y, int rx, int ry, int a0, int a1);    /* the part of an ellipse from the direction a0 round to a1: */
                                                /* degrees, 0 to the right, anticlockwise */
int  gfill(int x, int y);       /* the dot x, y and all that are joined to it and are as it is (lit, or not) are */
                                /* drawn: an area up to its border. 0, or -1 if it could not all be done */

void gcopy(int x0, int y0, int x1, int y1, int x, int y);   /* that rectangle of the screen copied, its bottom-left */
                                /* dot to x, y (the two may overlap) */
int  gkeep(int x0, int y0, int x1, int y1);     /* ... kept, off the screen in the chip's memory: its number, or -1 */
int  gmake(int w, int h);       /* a kept picture of w by h dots, all out: its number, or -1 */
void gtarget(int n);            /* drawing goes into kept picture n, from here on: its bottom-left dot is 0, 0, */
                                /* gwidth and gheight are its size, and nothing is drawn outside it. -1: onto the */
                                /* screen again. The clipping rectangle is the whole of it after either */
void gstamp(int n, int x, int y, int how);      /* kept picture n put down, its bottom-left dot at x, y */
void gdrop(int n);              /* ... forgotten: its room is free */
int  gread(int x0, int y0, int x1, int y1, unsigned short *bits);   /* that rectangle's dots into bits: its top line */
                                /* first, each line in (width + 15) / 16 words, a word's top bit its left-hand dot */
int  gwrite(int x0, int y0, int x1, int y1, const unsigned short *bits, int how);   /* ... and out of them again */

void gtext(int x, int y, const char *s);        /* text, x, y the bottom-left dot of its first character */
void glook(int look);

int  gmopen(void);              /* the mouse taken, its pointer at the middle of the screen: 0, or -1 (errno) */
void gmclose(void);
int  gmouse(int *x, int *y, int *buttons, int wait);    /* where the pointer is and which buttons are down (1 left, */
                                /* 2 right, 4 middle). wait 0: as things are now; above 0: waits up to that many */
                                /* 50ths of a second for a change; below 0: until there is one. 1 if it moved or a */
                                /* button changed since the last call, 0 if not, -1 if the mouse cannot be read */
void gmat(int x, int y);        /* the pointer put somewhere */
int  gmfd(void);                /* /dev/mouse's file descriptor, for select */
int  ginput(int *x, int *y, int *buttons, int wait);    /* the mouse or the keyboard, whichever does something */
                                /* first: waits as gmouse does. A key typed: its character (1 to 255); G_MOUSE: */
                                /* the mouse moved or a button changed (x, y, buttons as gmouse's); 0: neither */
                                /* in the time, or a signal the program catches came; -1: neither can be read. */
                                /* Without gmopen it is the keys alone. What has been drawn is sent first. The */
                                /* terminal gives each key as it is typed, unechoed, from here until gclose */

int  gsin(int degrees);
int  gcos(int degrees);

void gwords(const unsigned short *w, int n);    /* n of the chip's own command words (video(4)), in their turn */
int  gfd(void);                 /* /dev/video's file descriptor */
#endif
