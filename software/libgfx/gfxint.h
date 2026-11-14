/* gfxint.h -- libgfx: what its files share (not for programs) */
#include <sys/video.h>

extern int _gfd;                /* /dev/video, or -1 */
extern int _gink;               /* the ink */
extern int _gop;                /* what every drawing command carries: the clipping and the ink's operation */
extern int _glook;              /* the look of text */
extern struct vidinfo _gvi;     /* the screen and the font, as the device says */

void _gw(int w);                /* a word on its way to the chip */
void _gend(void);               /* the pen's line ends here: its last dot is drawn */
void _grun(int x0, int y0, int x1, int y1);     /* a block of dots, both corners in it (a level or upright run, a dot) */
void _gfrun(int x0, int y0, int x1, int y1);    /* ... of a filled figure: in the fill pattern */
void _gclip(int *x0, int *y0, int *x1, int *y1);        /* the clipping rectangle */
int  _gread(int y, int w, int n, unsigned short *buf); /* n words of the screen's line y from its word w, as the chip */
                                /* has them (bit 0 the left-hand dot): 0, or -1 */
unsigned _grev(unsigned w);     /* a word's 16 bits the other way round */
