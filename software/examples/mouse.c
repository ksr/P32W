/* mouse.c -- the mouse: a cross that follows it over the video card's screen. Holding the left button
 * draws; the middle one clears; the right one ends. (man gfx, man 4 mouse)
 *
 *   cc -o mouse mouse.c -lgfx; ./mouse
 *
 * The cross is drawn in the ink that turns dots over, so drawing it a second time takes it away again and
 * leaves what was under it: that is done round every change.
 */
#include <stdio.h>
#include <gfx.h>

static void cross(int x, int y)
{
    gink(G_XOR);
    gline(x - 8, y, x - 2, y); gline(x + 2, y, x + 8, y);
    gline(x, y - 8, x, y - 2); gline(x, y + 2, x, y + 8);
    gink(G_WHITE);
}

int main(void)
{
    int x, y, b, px, py, pb = 0, moves = 0;
    if (gopen() < 0) { perror("the screen"); return 1; }
    if (gmopen() < 0) { gclose(); perror("the mouse"); return 1; }
    gtext(8, gheight - 20, "left button: draw   middle: clear   right: end");
    gmouse(&px, &py, &b, 0);
    cross(px, py);
    gflush();
    for (;;) {
        if (gmouse(&x, &y, &b, -1) <= 0) continue;      /* (waits for the mouse) */
        moves++;
        cross(px, py);                                  /* the cross away */
        if (b & 4) { gclear(); gtext(8, gheight - 20, "left button: draw   middle: clear   right: end"); }
        if (b & 1) { if (pb & 1) gline(px, py, x, y); else gpoint(x, y); }
        if (b & 2) break;
        cross(x, y);
        gflush();
        px = x; py = y; pb = b;
    }
    gmclose();
    gclose();
    printf("the pointer was left at %d, %d\n", x, y);
    return 0;
}
