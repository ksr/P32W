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
