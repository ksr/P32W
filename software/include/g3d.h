/* g3d.h -- three dimensions in lines, for a program that draws with the graphics library (g3d(3), D148).
 *
 * A world is a list of lines between places of whole numbers: x to the right, y up, z away from the eye. The
 * world is turned about its own middle and then put somewhere in front of the eye, which is where the numbers
 * begin and looks along z. What the eye sees falls on a rectangle of the picture.
 *
 * It is all in this header, as P8X's lib_g3d is put into the programs that use it: include it in the one
 * source file of a program. Nothing is drawn here. g3seen gives the lines as they fall on the picture, for the
 * program to draw as it likes, and to take away again if it draws in the ink that turns dots over.
 *
 *   g3clear();                         an empty world
 *   g3line(x0, y0, z0, x1, y1, z1);    a line in it; g3box(...): the twelve edges of a box
 *   g3turn(ax, ay, az);                turned: ay degrees about its upright axis (y), then tipped ax about
 *                                      the axis from side to side (x), then az about the line of sight (z)
 *   g3put(x, y, z);                    ... and its middle put there (z: how far from the eye)
 *   g3view(x0, y0, x1, y1, d);         the picture's rectangle it is seen in; d: the eye's distance from it
 *                                      in dots (something d across and d away just fills d dots)
 *   n = g3seen(out, max);              out[k][0..3]: x0 y0 x1 y1 of each line that shows, cut to the rectangle
 *
 * Whole numbers of 32 bits: a world within 8000 of its middle each way, and nearer than 30000, is safe.
 */
#ifndef _G3D_H
#define _G3D_H
#include <gfx.h>

#define G3LINES 512                     /* lines of a world, at most */
#define G3NEAR  8                       /* nothing nearer the eye than this is seen */

static short g3w[G3LINES][6];           /* the world's lines: x0 y0 z0 x1 y1 z1 */
static int g3n;
static int g3m[9] = { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 };    /* how it is turned (times 4096) */
static int g3px, g3py, g3pz = 1000;     /* ... and where it is put */
static int g3x0, g3y0, g3x1 = 639, g3y1 = 479, g3d = 480;

static void g3clear(void) { g3n = 0; }

static int g3line(int x0, int y0, int z0, int x1, int y1, int z1)      /* a line more: 0, or -1 if there is no room */
{
    short *l = g3w[g3n];
    if (g3n == G3LINES) return -1;
    l[0] = x0; l[1] = y0; l[2] = z0; l[3] = x1; l[4] = y1; l[5] = z1;
    g3n++;
    return 0;
}

static void g3box(int x0, int y0, int z0, int x1, int y1, int z1)      /* a box between two opposite corners */
{
    int k;
    for (k = 0; k < 4; k++) {           /* (four edges along each of x, y and z) */
        int a = k & 1 ? y1 : y0, b = k & 2 ? z1 : z0, c = k & 1 ? x1 : x0, d = k & 2 ? y1 : y0;
        g3line(x0, a, b, x1, a, b);
        g3line(c, y0, b, c, y1, b);
        g3line(c, d, z0, c, d, z1);
    }
}

static void g3turn(int ax, int ay, int az)      /* about y, then about x, then about z */
{
    int sx = gsin(ax) >> 2, cx = gcos(ax) >> 2, sy = gsin(ay) >> 2, cy = gcos(ay) >> 2, sz = gsin(az) >> 2, cz = gcos(az) >> 2;
    int a = (sx * sy) >> 12, b = (sx * cy) >> 12;
    g3m[0] = (cz * cy - sz * a) >> 12; g3m[1] = (-sz * cx) >> 12; g3m[2] = (cz * sy + sz * b) >> 12;
    g3m[3] = (sz * cy + cz * a) >> 12; g3m[4] = (cz * cx) >> 12;  g3m[5] = (sz * sy - cz * b) >> 12;
    g3m[6] = (-cx * sy) >> 12;         g3m[7] = sx;               g3m[8] = (cx * cy) >> 12;
}

static void g3put(int x, int y, int z) { g3px = x; g3py = y; g3pz = z; }

static void g3view(int x0, int y0, int x1, int y1, int d)
{
    g3x0 = x0 < x1 ? x0 : x1; g3x1 = x0 < x1 ? x1 : x0;
    g3y0 = y0 < y1 ? y0 : y1; g3y1 = y0 < y1 ? y1 : y0;
    g3d = d;
}

static int g3side(int x, int y)         /* which sides of the rectangle a dot is outside of */
{
    return (x < g3x0) | (x > g3x1) << 1 | (y < g3y0) << 2 | (y > g3y1) << 3;
}

static int g3seen(short out[][4], int max)
{
    int k, j, n = 0, p[2][3], s[2][2], ca, cb, c, x, y, cx = (g3x0 + g3x1) / 2, cy = (g3y0 + g3y1) / 2;
    for (k = 0; k < g3n && n < max; k++) {
        short *l = g3w[k];
        for (j = 0; j < 2; j++) {       /* its two ends, turned and put in their place */
            int wx = l[3 * j], wy = l[3 * j + 1], wz = l[3 * j + 2];
            p[j][0] = ((g3m[0] * wx + g3m[1] * wy + g3m[2] * wz) >> 12) + g3px;
            p[j][1] = ((g3m[3] * wx + g3m[4] * wy + g3m[5] * wz) >> 12) + g3py;
            p[j][2] = ((g3m[6] * wx + g3m[7] * wy + g3m[8] * wz) >> 12) + g3pz;
        }
        if (p[0][2] < G3NEAR && p[1][2] < G3NEAR) continue;     /* behind the eye, all of it */
        for (j = 0; j < 2; j++)         /* an end behind it: the line cut where it comes in front */
            if (p[j][2] < G3NEAR) {
                int *a = p[j], *b = p[!j], t = G3NEAR - a[2], d = b[2] - a[2];
                a[0] += (b[0] - a[0]) * t / d; a[1] += (b[1] - a[1]) * t / d; a[2] = G3NEAR;
            }
        for (j = 0; j < 2; j++) { s[j][0] = cx + g3d * p[j][0] / p[j][2]; s[j][1] = cy + g3d * p[j][1] / p[j][2]; }
        for (;;) {                      /* cut to the rectangle, an outside end at a time */
            ca = g3side(s[0][0], s[0][1]); cb = g3side(s[1][0], s[1][1]);
            if (!(ca | cb) || (ca & cb)) break;
            c = ca ? ca : cb; j = ca ? 0 : 1;
            if (c & 8) { x = s[0][0] + (s[1][0] - s[0][0]) * (g3y1 - s[0][1]) / (s[1][1] - s[0][1]); y = g3y1; }
            else if (c & 4) { x = s[0][0] + (s[1][0] - s[0][0]) * (g3y0 - s[0][1]) / (s[1][1] - s[0][1]); y = g3y0; }
            else if (c & 2) { y = s[0][1] + (s[1][1] - s[0][1]) * (g3x1 - s[0][0]) / (s[1][0] - s[0][0]); x = g3x1; }
            else { y = s[0][1] + (s[1][1] - s[0][1]) * (g3x0 - s[0][0]) / (s[1][0] - s[0][0]); x = g3x0; }
            s[j][0] = x; s[j][1] = y;
        }
        if (ca | cb) continue;          /* (none of it in the rectangle) */
        out[n][0] = s[0][0]; out[n][1] = s[0][1]; out[n][2] = s[1][0]; out[n][3] = s[1][1];
        n++;
    }
    return n;
}
#endif
