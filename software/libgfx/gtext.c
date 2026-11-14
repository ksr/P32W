/* gtext.c -- libgfx: text. Each character is a copy (the chip's AGCPY) from one of the font's pictures,
 * which the console keeps under the screen (video(4)): plain, + 1 inverse, + 2 underlined, + 4 bold.
 *
 * The ink decides how it is copied. White: the character's dots are lit and the rest of its cell left
 * alone (the plain picture, ORed). Black: its dots are put out (the inverse picture, ANDed). G_XOR: its
 * dots are turned over. With the look G_OPAQUE the whole cell is written, the rest of it in the other
 * colour: white on black, or black on white (the inverse picture).
 */
#include <gfx.h>
#include "gfxint.h"

void gtext(int x, int y, const char *s)
{
    int c, x0 = x, a = _glook & (G_UNDER | G_BOLD), op, at = 0;
    _gend();
    if (_gink == G_XOR) op = VO_XOR;
    else {
        if (_gink == G_BLACK) a |= 1;
        op = _glook & G_OPAQUE ? VO_REPLACE : _gink == G_BLACK ? VO_AND : VO_OR;
    }
    for (; *s; s++) {
        c = *s & 0xFF;
        if (c == '\n') { y -= gcellh; x = x0; at = 0; continue; }
        if (c < 0x20 || c > 0x7E) c = '?';
        if (!at) {                      /* (the chip's pointer: the cell's top-left dot; a copy leaves it at the next cell's) */
            _gw(VC_AMOVE); _gw(x); _gw(y + gcellh - 1);
            at = 1;
        }
        _gw(VC_AGCPY | VA_INSIDE | op);
        _gw((c & 31) * gcellw);
        _gw(_gvi.fonty - a * _gvi.fontstep - gcellh * ((c - 0x20) >> 5));
        _gw(gcellw - 1);
        _gw(-(gcellh - 1));
        x += gcellw;
    }
}
