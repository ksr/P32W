/* flight.c -- a very small flight simulator: the horizon, the ground, two airfields, a city and three hills,
 * drawn as lines on the video card's screen, with a simple aeroplane to fly over them. (man gfx)
 *
 *   cc -o flight flight.c -lgfx
 *   ./flight            on the runway, the engine idling
 *   ./flight -a         in the air instead, three kilometres short of the runway and coming down to it
 *
 * The mouse is the stick: to the left and right it rolls, towards you it lifts the nose. Its left button
 * gives more power while it is held and its right one less; the middle one puts the stick in the middle.
 * The keys do the same: the arrows move the stick, the space bar centres it, + and - are the power. r starts
 * again on the runway and a in the air, g takes the ground's lines away and brings them back, q ends.
 * To take off: all the power, and at 60 knots a little stick back. It lands on its wheels anywhere, if it
 * comes down gently and level.
 *
 * The airfield it starts on has a taxiway beside its runway, three hangars, a terminal and a tower. The city
 * is five kilometres off to the north-north-east, a heading of about 025: nine buildings, the tallest 220
 * metres, with streets between them. The other airfield, a runway with a hangar and a tower, is beyond the
 * city. A building is drawn as its outline against the sky, three lines for its twelve, until it is near
 * enough to be a few tens of dots across: every line takes the machine a little more than a thousandth of a
 * second, and the more of them there are in the window, the fewer frames a second.
 *
 * x, c and p choose how the picture is changed from one frame to the next, which is what it looks like:
 *   x   each line is taken away and drawn in its new place, in the ink that turns dots over (G_XOR)
 *   c   the window is cleared and all of it drawn again: it flickers
 *   p   it is drawn out of sight, in the chip's spare memory, and copied to the window when it is whole
 * p is how it starts; in a window of wm it is always x (there the chip is wm's).
 *
 * Options:  -m x|c|p   the way to start with
 *           -g N       the ground's lines: N to each side of the aeroplane, each way (3; 0: none)
 *           -t N       it flies N frames by itself, the same ones on any machine, and says how long they took
 *           -e         for the emulator p32emu, where only waiting takes time: it waits after each frame as
 *                      long as the frame took, and so flies about as fast as the machine itself would
 *
 * All of it is whole numbers, as the machine has no floating point hardware: places in 256ths of a metre,
 * angles in 256ths of a degree, sines from the library's table. A corner of something is turned to where
 * the eye sees it (right, up, ahead, in quarter metres); a line between two corners is cut to the window's
 * four sides there, before it is made flat, so the chip is never asked for a line that leaves the window.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sgtty.h>
#include <sys/times.h>
#include <sys/video.h>
#include <gfx.h>

#define F      512              /* dots from the eye to the window: it shows 32 degrees to each side */
#define PH     96               /* the panel's lines, under the window */
#define NEAR   8                /* nothing nearer than this is drawn (quarter metres) */
#define DEG    256              /* an angle's units in a degree */
#define ONE    256              /* a place's units in a metre, a speed's in a metre a second */
#define VSTALL (27 * ONE)       /* slower than this the wings do not carry it */
#define GRID   2000             /* metres between the ground's lines */
#define FAR    16000            /* things further off than this are not drawn (metres) */
#define CLOSE  12               /* ... and a building nearer than this many times its size has all its lines */
#define SR     96               /* the stick goes this many of the mouse's steps each way */
#define WHOLE  (1 << 24)        /* all the way along a line, as part() counts it */
#define CPY    0x6100           /* the chip's own copy of whole words, rightwards and down (video(4) names the others) */
#define PX     440              /* the panel: where the power's bar begins, */
#define SX     (W - 48)         /* ... and the middle of the stick's box */
#define SY     46

static int W, H, HW, HH;        /* the picture; half the window, across and up */
static int M[9];                /* the eye's three directions in the world: right, up, ahead (times 4096) */
static int ex, ey, ez;          /* where the eye is (quarter metres: east, up, north) */

static int px, py, pz;          /* the aeroplane: east, up (0: its wheels are on the ground), north */
static int psi, theta, phi;     /* its heading (0 north, 90 east), its nose up, its lean to the right */
static int v, power, climb;     /* its speed, the engine (0 to 100), how fast it is going up */
static int stx, sty, but;       /* the stick: to the right and forward (-SR to SR); the mouse's buttons */
static int down, crashed, stall;

static int how = 'p', ngrid = 3, wgrid = 3, emu, test;      /* (ngrid: the ground's lines now; wgrid: when they are on) */
static int chip;                /* the program has the chip itself (it is not in a window of wm) */
static volatile int stop;
static unsigned short org[2][3], rwp[2][4], clr[4], cpy[5];    /* words for the chip: [0] the screen, [1] the spare page */

#define NL 256
static short ln[2][NL][4];      /* the lines of this frame, and of the one on the screen */
static int nln[2], cur;
static int nlines, nwords;      /* lines drawn in all, and about how many of the chip's words they took */

/* ---------------------------------------------------------------- the world: corners, and lines between them */
#define NV 192                          /* (the world below: 187 corners, 246 lines, 24 things) */
#define NE 256
#define NT 28
enum { ALL, OUTLINE, UNSEEN };          /* a thing that is not close: all of it; a box's outline; not drawn */
static short vx[NV], vy[NV], vz[NV];    /* metres: east, up, north */
static unsigned char ea[NE], eb[NE];
static struct thing {
    short x, z, r, v0, nv, e0, ne;      /* its middle, its reach, its corners and lines */
    short close;                        /* nearer than this (metres) it is close, */
    char faroff, parts;                 /* ... else drawn so; the things after it that are parts of it */
} thing[NT];
static int nv, ne, no;

static void corner(int x, int y, int z) { vx[nv] = x; vy[nv] = y; vz[nv] = z; nv++; }
static void edge(int a, int b) { ea[ne] = a; eb[ne] = b; ne++; }
static void begin(int x, int z, int r)
{
    thing[no].x = x; thing[no].z = z; thing[no].r = r; thing[no].close = CLOSE * r;
    thing[no].v0 = nv; thing[no].e0 = ne;
}
static void made(void) { thing[no].nv = nv - thing[no].v0; thing[no].ne = ne - thing[no].e0; no++; }

/* a box standing on the ground: its middle, half its width (east) and its depth (north), its height (0: flat).
   Its lines go round the bottom, round the top and then up the corners, so that most follow on from the last */
static void box(int x, int z, int w, int d, int h)
{
    int k, j, a = nv;
    for (k = 0; k < (h ? 8 : 4); k++) corner(x + ((k ^ k >> 1) & 1 ? w : -w), k & 4 ? h : 0, z + (k & 2 ? d : -d));
    for (j = 0; j < (h ? 8 : 4); j += 4)
        for (k = 0; k < 4; k++) edge(a + j + k, a + j + (k + 1) % 4);
    for (k = 0; h && k < 4; k++) edge(a + k, a + 4 + k);
}

static void house(int x, int z, int w, int d, int h)    /* a building: a box by itself, far off only its outline */
{
    begin(x, z, w + d + h);
    box(x, z, w, d, h);
    thing[no].faroff = OUTLINE;
    made();
}

static void paved(int x, int z, int w, int d, int faroff)       /* something flat on the ground: its four sides */
{
    begin(x, z, w + d);
    box(x, z, w, d, 0);
    thing[no].faroff = faroff;
    made();
}

/* the things made since thing a are parts of it, all within r metres of its middle: when that much round it is
   out of sight, none of them is looked at */
static void group(int a, int r) { thing[a].parts = no - a - 1; thing[a].r = r; }

static void hill(int x, int z, int w, int h)    /* a hill with four sides */
{
    int a = nv;
    begin(x, z, w + h);
    box(x, z, w, w, 0);
    corner(x, h, z);
    edge(a, a + 4); edge(a + 4, a + 2); edge(a + 1, a + 4); edge(a + 4, a + 3);
    made();
}

static void runway(int x, int z, int len, int east)     /* its middle, half its length, which way it runs */
{
    int k, m;
    begin(x, z, len);
    box(x, z, east ? len : 30, east ? 30 : len, 0);
    for (k = -2; k <= 2; k++) {                         /* the line down its middle, in five pieces */
        m = k * len * 2 / 5;
        corner(east ? x + m - len / 10 : x, 0, east ? z : z + m - len / 10);
        corner(east ? x + m + len / 10 : x, 0, east ? z : z + m + len / 10);
        edge(nv - 2, nv - 1);
    }
    made();
}

static void world(void)
{
    static const short town[9][3] = {                   /* the city's buildings, from its south-west corner: half */
        { 55, 55, 90 }, { 40, 60, 140 }, { 60, 45, 60 },        /* the width, half the depth, the height */
        { 45, 45, 120 }, { 50, 50, 220 }, { 65, 40, 80 },
        { 60, 60, 45 }, { 40, 40, 160 }, { 50, 65, 100 } };
    int a, k;

    a = no;
    runway(0, 0, 750, 0);                               /* the airfield it starts on: the runway, */
    paved(50, 0, 8, 750, ALL);                          /* the taxiway beside it, */
    paved(88, -240, 22, 90, UNSEEN);                    /* the apron in front of the hangars, */
    for (k = 0; k < 3; k++) house(140, -300 + 60 * k, 30, 20, 12);     /* three hangars, */
    house(-150, -20, 20, 70, 10);                       /* the terminal on the other side */
    house(-120, 100, 6, 6, 30);                         /* ... and the tower */
    group(a, 800);

    hill(-2500, 5000, 1200, 500);
    hill(1500, 8000, 1800, 800);
    hill(5000, 3000, 1000, 400);

    a = no;                                             /* the city: two streets each way, from side to side */
    begin(2200, 4200, 600);
    for (k = 0; k < 2; k++) {
        corner(2050 + 300 * k, 0, 3700); corner(2050 + 300 * k, 0, 4700); edge(nv - 2, nv - 1);
        corner(1700, 0, 4050 + 300 * k); corner(2700, 0, 4050 + 300 * k); edge(nv - 2, nv - 1);
    }
    thing[no].faroff = UNSEEN; thing[no].close = 3000;
    made();
    for (k = 0; k < 9; k++)                             /* ... and a building in each of the nine blocks */
        house(1900 + 300 * (k % 3), 3900 + 300 * (k / 3), town[k][0], town[k][1], town[k][2]);
    group(a, 600);

    a = no;
    runway(7000, 9400, 600, 1);                         /* somewhere to fly to: a runway, a hangar and a tower */
    house(7150, 9520, 30, 20, 12);
    house(6900, 9500, 6, 6, 25);
    group(a, 650);
}

/* ---------------------------------------------------------------- arithmetic */
static int fsin(int a)          /* the sine of an angle, times 16384: between the library's whole degrees */
{
    int d = a >> 8, s = gsin(d);
    return s + (((gsin(d + 1) - s) * (a & 255)) >> 8);
}
static int fcos(int a) { return fsin(a + 90 * DEG); }

/* n / d, which is 0 to 1, times WHOLE: in two divisions, as there are no numbers of more than 32 bits */
static int part(int n, int d)
{
    int q;
    if (d < 0) { n = -n; d = -d; }
    while (d >= 1 << 19) { n >>= 1; d >>= 1; }
    q = (n << 12) / d;
    return q << 12 | (((n << 12) - q * d) << 12) / d;
}

static int mix(int a, int b, int t)     /* the place t / WHOLE of the way from a to b */
{
    if (!t) return a;
    b -= a;
    return a + ((b * (t >> 12)) >> 12) + ((b * (t & 0xFFF)) >> 24);
}

static void flat(int x, int y, int z, int *d)   /* the dot of a place the eye sees: across and up, never off the window */
{
    if (z < 1) z = 1;
    x = HW + F * x / z; y = PH + HH + F * y / z;
    d[0] = x < 0 ? 0 : x > W - 1 ? W - 1 : x;
    d[1] = y < PH ? PH : y > H - 1 ? H - 1 : y;
}

/* p[0..2] is a place as the eye sees it. p[3..7]: how far it is inside each side of what the window shows (the
   near side, the left, the right, the bottom, the top): below 0 it is outside. p[8]: a bit for each side it is
   outside of. For a place inside them all, p[9] and p[10] are its dot */
#define NP 11
static void sides(int *p)
{
    int a = HW * p[2], b = F * p[0], c = HH * p[2], d = F * p[1];
    p[3] = p[2] - NEAR;
    p[4] = a + b; p[5] = a - b; p[6] = c + d; p[7] = c - d;
    p[8] = (p[3] < 0) << 3 | (p[4] < 0) << 4 | (p[5] < 0) << 5 | (p[6] < 0) << 6 | (p[7] < 0) << 7;
    if (!p[8]) flat(p[0], p[1], p[2], p + 9);
}

static void turn(int x, int y, int z, int *p)   /* a place in the world (metres), as the eye sees it */
{
    x = (x << 2) - ex; y = (y << 2) - ey; z = (z << 2) - ez;
    p[0] = (M[0] * x + M[1] * y + M[2] * z) >> 12;
    p[1] = (M[3] * x + M[4] * y + M[5] * z) >> 12;
    p[2] = (M[6] * x + M[7] * y + M[8] * z) >> 12;
    sides(p);
}

/* the line from a to b: the part of it the window shows, made flat and put with this frame's lines */
static void seen(const int *a, const int *b)
{
    int k, t, d[2], t0 = 0, t1 = WHOLE, out = a[8] | b[8];
    short *s = ln[cur][nln[cur]];
    if (a[8] & b[8] || nln[cur] == NL) return;          /* both ends are outside one side: none of it shows */
    for (k = 3; out >> k; k++)                          /* an end is outside a side: where the line goes through it */
        if (out >> k & 1) {
            t = part(a[k], a[k] - b[k]);
            if (a[k] < 0) { if (t > t0) t0 = t; } else if (t < t1) t1 = t;
        }
    if (t0 > t1) return;
    if (a[8]) { flat(mix(a[0], b[0], t0), mix(a[1], b[1], t0), mix(a[2], b[2], t0), d); s[0] = d[0]; s[1] = d[1]; }
    else { s[0] = a[9]; s[1] = a[10]; }
    if (b[8]) { flat(mix(a[0], b[0], t1), mix(a[1], b[1], t1), mix(a[2], b[2], t1), d); s[2] = d[0]; s[3] = d[1]; }
    else { s[2] = b[9]; s[3] = b[10]; }
    nln[cur]++;
}

static int line0(int m)         /* the ground's line nearest to m metres: they are at GRID / 2, 3 GRID / 2, ... */
{
    return (m >= 0 ? m / GRID : -((GRID - 1 - m) / GRID)) * GRID + GRID / 2;
}

/* ---------------------------------------------------------------- a frame: its lines */
static void aim(void)           /* the eye, from where the aeroplane is and how it lies */
{
    int sp = fsin(psi), cp = fcos(psi), st = fsin(theta), ct = fcos(theta), sr = fsin(phi), cr = fcos(phi);
    int a = (sp * st) >> 14, b = (cp * st) >> 14;
    M[0] = (cp * cr + a * sr) >> 16;  M[1] = -(ct * sr) >> 16; M[2] = (b * sr - sp * cr) >> 16;
    M[3] = (cp * sr - a * cr) >> 16;  M[4] = (ct * cr) >> 16;  M[5] = -(b * cr + sp * sr) >> 16;
    M[6] = (sp * ct) >> 16;           M[7] = st >> 2;          M[8] = (cp * ct) >> 16;
    ex = px >> 6; ey = (py >> 6) + 8; ez = pz >> 6;     /* (the eye is two metres above the wheels) */
}

/* a building a long way off: up the corner furthest to one side, along its top and down the corner furthest
   to the other, as the eye sees them: the two that a line from the eye to its middle passes between */
static void outline(const struct thing *o)
{
    int k = o->v0 + ((o->x > px >> 8) == (o->z > pz >> 8)), p[4][NP];
    turn(vx[k], 0, vz[k], p[0]);
    turn(vx[k], vy[k + 4], vz[k], p[1]);
    turn(vx[k + 2], vy[k + 4], vz[k + 2], p[2]);
    turn(vx[k + 2], 0, vz[k + 2], p[3]);
    seen(p[0], p[1]); seen(p[1], p[2]); seen(p[2], p[3]);
}

static void scene(void)
{
    static int tv[NV][NP];
    int k, j, n, faroff, c[NP], d[NP], a[3], b[3];
    struct thing *o;

    nln[cur] = 0;
    for (k = 0; k < 2; k++) {                           /* the horizon: between two places on the ground a very */
        int *p = k ? d : c;                             /* long way off, 60 degrees to each side of its heading */
        int x = fsin(psi + (k ? 60 : -60) * DEG), z = fcos(psi + (k ? 60 : -60) * DEG);
        p[0] = (M[0] * x + M[2] * z) >> 12;
        p[1] = (M[3] * x + M[5] * z) >> 12;
        p[2] = (M[6] * x + M[8] * z) >> 12;
        sides(p);
    }
    seen(c, d);

    if (ngrid) {                                        /* the ground's lines: those round the aeroplane. From the */
        int p[4][NP];                                   /* crossing nearest under it, a[] is half a square east as */
        turn(line0(px >> 8), 0, line0(pz >> 8), c);     /* the eye sees it and b[] half a square north: the rest */
        n = 2 * ngrid + 1;                              /* is adding */
        for (k = 0; k < 3; k++) { a[k] = (M[3 * k] * 2 * GRID) >> 12; b[k] = (M[3 * k + 2] * 2 * GRID) >> 12; }
        for (j = -2 * ngrid; j <= 2 * ngrid; j += 2) {
            for (k = 0; k < 3; k++) {
                p[0][k] = c[k] + j * b[k] - n * a[k]; p[1][k] = c[k] + j * b[k] + n * a[k];     /* one that runs east */
                p[2][k] = c[k] + j * a[k] - n * b[k]; p[3][k] = c[k] + j * a[k] + n * b[k];     /* ... and one north */
            }
            for (k = 0; k < 4; k++) sides(p[k]);
            seen(p[0], p[1]); seen(p[2], p[3]);
        }
    }

    for (o = thing; o < thing + no; o++) {
        j = abs(o->x - (px >> 8)); k = abs(o->z - (pz >> 8));
        if (j > FAR || k > FAR) { o += o->parts; continue; }
        faroff = j > o->close || k > o->close;
        turn(o->x, 0, o->z, c);                         /* its middle: is any of it in the window? */
        j = 4 * o->r;
        if (c[2] < -j || c[4] < -j * (F + HW) || c[5] < -j * (F + HW) || c[6] < -j * (F + HH) || c[7] < -j * (F + HH)) {
            o += o->parts;                              /* (nor any of its parts) */
            continue;
        }
        if (faroff && o->faroff == UNSEEN) continue;
        if (faroff && o->faroff == OUTLINE) { outline(o); continue; }
        for (k = o->v0; k < o->v0 + o->nv; k++) turn(vx[k], vy[k], vz[k], tv[k]);
        for (k = o->e0; k < o->e0 + o->ne; k++) seen(tv[ea[k]], tv[eb[k]]);
    }
}

/* ---------------------------------------------------------------- a frame: on the screen */
static void words(const unsigned short *w, int n) { gwords(w, n); nwords += n; }

static void one(const short *s) { gline(s[0], s[1], s[2], s[3]); nwords += 7; }

static void show(void)
{
    int k, n = nln[cur], o = nln[!cur];
    short *s, *t;
    if (how == 'x') {                                   /* line by line: the old one away, the new one drawn */
        gink(G_XOR);
        for (k = 0; k < n || k < o; k++) {
            s = ln[cur][k]; t = ln[!cur][k];
            if (k < n && k < o && !memcmp(s, t, sizeof ln[0][0])) continue;     /* (it has not moved) */
            if (k < o) one(t);
            if (k < n) one(s);
        }
    } else {
        if (how == 'p') words(org[1], 3);               /* drawing goes to the spare page */
        words(rwp[how == 'p'], 4); words(clr, 4);       /* ... which is cleared, or the window is */
        gink(G_WHITE);
        for (k = 0; k < n; k++) {                       /* a line that begins where the last one ended follows on */
            s = ln[cur][k];
            if (!k || s[0] != s[-2] || s[1] != s[-1]) { gmove(s[0], s[1]); nwords += 4; }
            gdraw(s[2], s[3]);
            nwords += 3;
        }
        if (how == 'p') { words(org[0], 3); words(rwp[0], 4); words(cpy, 5); }  /* the page copied to the window */
    }
    nlines += n;
    cur = !cur;
}

static void way(int m)          /* another way of changing the picture: the window black, nothing in it to take away */
{
    if (!chip) return;
    how = m;
    nln[!cur] = 0;
    words(rwp[0], 4); words(clr, 4);
}

/* ---------------------------------------------------------------- the panel */
static char said[2][81];        /* what its two lines of text say now */

static void say(int row, const char *s) /* a line of it: only the characters that are not there already are drawn */
{
    char run[81];
    int k = 0, n;
    while (s[k]) {
        if (s[k] == said[row][k]) { k++; continue; }
        for (n = 0; s[k + n] && s[k + n] != said[row][k + n]; n++) run[n] = said[row][k + n] = s[k + n];
        run[n] = 0;
        gtext(8 * k, row ? 4 : 48, run);
        nwords += 3 + 5 * n;
        k += n;
    }
}

static void spot(int x, int y) { gfbox(x - 2, y - 2, x + 2, y + 2); nwords += 6; }     /* where the stick is, in its box */

static void panel(void)         /* the part of it that stays as it is, drawn once */
{
    gink(G_WHITE);
    gline(0, PH - 1, W - 1, PH - 1);
    gtext(16, 72, "SPEED       ALTITUDE      HEADING   CLIMB");
    gtext(PX, 72, "POWER");
    gbox(PX, 50, PX + 101, 61);
    gtext(SX - 20, 72, "STICK");
    gbox(SX - 22, SY - 22, SX + 22, SY + 22);
    gink(G_XOR);
    spot(SX, SY);
}

static void numbers(int rate)   /* its two lines of text: the speed, the height, ...; and what is going on */
{
    char s[81];
    gink(G_WHITE);
    glook(G_OPAQUE);
    sprintf(s, "  %3d kt      %5d ft      %03d       %+5d ft/min", (v * 498) >> 16, (py >> 8) * 3281 / 10000 * 10,
            psi / DEG, ((climb * 197) >> 8) / 50 * 50);
    say(0, s);
    sprintf(s, "  %-26s %3d.%d frames a second        Q ends",
            crashed ? "CRASHED: R starts again" : stall ? "STALL: stick forward" : down ? "ON THE GROUND" : "FLYING",
            rate / 10, rate % 10);
    say(1, s);
    glook(0);
}

static void gauges(void)        /* the power's bar and the stick's spot, when they have changed */
{
    static int op, ox, oy;
    int x = stx * 16 / SR, y = sty * 16 / SR;
    gink(G_XOR);
    if (power != op) {                                  /* the bar between the old power and the new, turned over */
        gfbox(PX + 1 + (power < op ? power : op), 51, PX + (power < op ? op : power), 60);
        nwords += 6;
        op = power;
    }
    if (x != ox || y != oy) { spot(SX + ox, SY + oy); spot(SX + x, SY + y); ox = x; oy = y; }
}

/* ---------------------------------------------------------------- flying */
static void start(int air)
{
    px = 0; pz = (air ? -3700 : -700) * ONE; py = air ? 160 * ONE : 0;
    psi = phi = 0; theta = air ? -3 * DEG : 0;
    v = air ? 42 * ONE : 0; power = air ? 27 : 0;
    down = !air; crashed = stall = climb = 0;
    stx = sty = 0;
}

static void fly(int dt)         /* dt 50ths of a second go by */
{
    int s, c, a, sink = 0, bite = v > 30 * ONE ? 256 : v * 256 / (30 * ONE);   /* what the stick does: all of it from 30 m/s */
    if (crashed) return;
    theta -= ((sty * dt * 20 * DEG / (SR * 50)) * bite) >> 8;          /* back, it lifts the nose: 20 degrees a second */
    if (!down) {
        phi += ((stx * dt * 45 * DEG / (SR * 50)) * bite) >> 8;        /* ... to the side it rolls: 45 */
        phi -= phi * dt / 400;                                          /* (left alone it comes level again, slowly) */
    }
    stall = !down && v < VSTALL;
    if (stall) { theta -= (VSTALL - v) * dt / 32; sink = (VSTALL - v) * 2 / 3; }   /* too slow: the nose drops, and it sinks */
    if (phi > 75 * DEG) phi = 75 * DEG;
    if (phi < -75 * DEG) phi = -75 * DEG;
    if (theta > 60 * DEG) theta = 60 * DEG;
    if (theta < -60 * DEG) theta = -60 * DEG;

    s = fsin(theta); c = fcos(theta);
    a = power * 9 - (v >> 4) * (v >> 4) / 1200;                         /* the engine pulls, the air holds it back */
    if (down) a -= v ? (power ? 100 : 740) : 0;                         /* ... so do its wheels, and its brakes at no power */
    else a -= (2511 * s) >> 14;                                         /* ... and climbing costs speed */
    v += a * dt / 50;
    if (v < 0) v = 0;

    if (down) { if (v > ONE) psi += stx * dt * 20 * DEG / (SR * 50); }  /* on the ground the stick steers its wheels */
    else psi += 180 * (fsin(phi) * 4096 / fcos(phi)) * dt / (v > 10 * ONE ? v : 10 * ONE);  /* flying, it turns as it leans */
    if (psi < 0) psi += 360 * DEG;
    if (psi >= 360 * DEG) psi -= 360 * DEG;

    climb = ((v * s) >> 14) - sink;
    if (down && (v < VSTALL || theta <= 0)) climb = 0;                  /* (down, until it is fast enough with its nose up) */
    a = (v * c) >> 14;                                                  /* its speed over the ground */
    px += ((a * fsin(psi)) >> 14) * dt / 50;
    pz += ((a * fcos(psi)) >> 14) * dt / 50;
    py += climb * dt / 50;
    if (py > 6000 * ONE) py = 6000 * ONE;
    if (py <= 0) {                                                      /* the ground: hard, leaning or nose first is a crash */
        if (!down && (climb < -4 * ONE || abs(phi) > 10 * DEG || theta < -4 * DEG)) crashed = 1;
        py = phi = 0;
        down = 1;
        if (theta < 0) theta = 0;
        if (theta > 12 * DEG) theta = 12 * DEG;
    } else down = 0;
}

/* a key, or (G_MOUSE) the mouse: the library's pointer has moved to x, y, and the buttons b are down */
static void input(int k, int x, int y, int b)
{
    static int esc;             /* an arrow comes as ESC [ and a letter */
    if (k == G_MOUSE) {
        if (chip) { stx += x - W / 2; sty += y - H / 2; gmat(W / 2, H / 2); }   /* how far it moved: the pointer goes back */
        else { stx = x - W / 2; sty = y - H / 2; }                              /* (in a window: where the pointer is) */
        but = b;
        if (b & 4) stx = sty = 0;
    } else if (esc == 2) {
        esc = 0;
        if (k == 'A') sty += 8;
        if (k == 'B') sty -= 8;
        if (k == 'C') stx += 8;
        if (k == 'D') stx -= 8;
    } else if (esc) esc = k == '[' ? 2 : 0;
    else switch (k) {
    case 27: esc = 1; break;
    case ' ': stx = sty = 0; break;
    case '+': case '=': power += 10; break;
    case '-': power -= 10; break;
    case 'g': ngrid = ngrid ? 0 : wgrid; break;
    case 'r': start(0); break;
    case 'a': start(1); break;
    case 'x': case 'c': case 'p': way(k); break;
    case 'q': stop = 1; break;
    }
    if (stx > SR) stx = SR;
    if (stx < -SR) stx = -SR;
    if (sty > SR) sty = SR;
    if (sty < -SR) sty = -SR;
}

/* -t: it flies itself. On the ground, a take-off: all the power, and the stick back at 60 knots. In the air the
   nose is held a little up (after a take-off) or down (coming in), and the stick goes from side to side about
   level, so that everything in the window moves in every frame */
static void pilot(int frame, int air)
{
    if (down) { power = 100; stx = 0; sty = v > 31 * ONE ? -24 : 0; return; }
    stx = gsin(9 * frame) * 40 / G_ONE - phi / (DEG / 2);
    sty = (theta - (air ? -3 : 7) * DEG) / 32;
}

static void sig(int n) { (void)n; stop = 1; }

/* the chip's words for word a of its memory (video(4)): as its origin, where drawing's dot 0, 0 is; and as its
   read / write pointer, where a clear or a copy begins */
static void origin(unsigned short *w, unsigned a) { w[0] = VC_ORG; w[1] = 0x4000 | a >> 12; w[2] = (a << 4) & 0xFFFF; }
static void pointer(unsigned short *w, unsigned a)
{
    w[0] = VC_WPR | 0x0C; w[1] = 0x4000 | a >> 12;
    w[2] = VC_WPR | 0x0D; w[3] = (a << 4) & 0xFFFF;
}

int main(int argc, char **argv)
{
    struct tms tm;
    int k, x, y, b, dt, air = 0, frames = 0, rate = 0, shown = 0;
    long t0, last, now, since, waited = 0;

    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-a")) air = 1;
        else if (!strcmp(argv[k], "-e")) emu = 1;
        else if (!strcmp(argv[k], "-m") && k + 1 < argc && strchr("xcp", argv[k + 1][0])) how = argv[++k][0];
        else if (!strcmp(argv[k], "-g") && k + 1 < argc) { ngrid = atoi(argv[++k]) & 7; if (ngrid) wgrid = ngrid; }
        else if (!strcmp(argv[k], "-t") && k + 1 < argc) test = atoi(argv[++k]);
        else { fprintf(stderr, "usage: flight [-a] [-e] [-m x|c|p] [-g lines] [-t frames]\n"); return 2; }
    }
    world();
    gsize(640, 440);                                    /* (the window to ask for, under wm) */
    if (gopen() < 0) { perror("flight: the screen"); return 1; }
    gmopen();                                           /* (with no mouse the keys fly it) */
    W = gwidth; H = gheight; HW = W / 2; HH = (H - PH) / 2;
    chip = gfd() >= 0;
    if (chip) {                                         /* the chip's memory, a line of the screen after another from */
        struct vidinfo vi;                              /* its top (video(4)): the spare page is the last lines of */
        struct vidspare sp;                             /* what is the program's own, its line y = 0 the very last */
        ioctl(gfd(), VIOCINFO, &vi); ioctl(gfd(), VIOCSPARE, &sp);
        k = vi.height - 1 - sp.y + sp.lines - H;        /* (the page's top line) */
        origin(org[0], (vi.height - 1) * vi.memw); origin(org[1], (k + H - 1) * vi.memw);
        pointer(rwp[0], (vi.height - H) * vi.memw); pointer(rwp[1], k * vi.memw);
        clr[0] = VC_CLR; clr[1] = 0; clr[2] = vi.memw - 1; clr[3] = -(H - PH - 1);   /* the window's words by its lines */
        cpy[0] = CPY; cpy[1] = k * vi.memw >> 12; cpy[2] = (k * vi.memw << 4) & 0xFFFF; cpy[3] = clr[2]; cpy[4] = clr[3];
    } else how = 'x';
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    panel();
    start(air);
    t0 = last = since = times(&tm);

    while (!stop && (!test || frames < test)) {
        while ((k = ginput(&x, &y, &b, 0)) > 0) input(k, x, y, b);
        now = times(&tm);
        dt = test ? 4 : now - last;                     /* (by itself: every frame is 4 50ths of a second of flying) */
        if (dt < 1) { if ((k = ginput(&x, &y, &b, 1)) > 0) input(k, x, y, b); continue; }      /* (too soon: wait a tick) */
        last = now;
        if (dt > 25) dt = 25;
        if (test) pilot(frames, air);
        if (but & 1) power += dt;
        if (but & 2) power -= dt;
        if (power > 100) power = 100;
        if (power < 0) power = 0;

        fly(dt);
        aim();
        scene();                                        /* (the chip may still be drawing the last frame meanwhile) */
        gsync();
        show();
        if (frames % 4 == 0) numbers(rate);             /* (in every fourth frame: the text costs as much as ten lines) */
        gauges();
        gflush();
        frames++;
        if (now - since >= 50) {                        /* ten times the frames a second, for the panel */
            rate = (frames - shown) * 500 / (now - since);
            shown = frames; since = now;
        }
        if (emu) {                                      /* the emulator: wait as long as that took, and do not count it */
            dt = times(&tm) - now;
            while ((k = now + 2 * dt - times(&tm)) > 0)
                if ((k = ginput(&x, &y, &b, k)) > 0) input(k, x, y, b);
            last = times(&tm) - dt;
            since += last - now; waited += last - now;
        }
    }
    gsync();
    now = times(&tm) - waited;
    gmclose();
    gclose();
    if (now > t0)
        printf("flight: %d frames in %ld.%02ld s: %ld.%ld a second (user %u.%02u s, system %u.%02u s); "
               "%d lines, about %d words\n", frames, (now - t0) / 50, (now - t0) % 50 * 2,
               frames * 500L / (now - t0) / 10, frames * 500L / (now - t0) % 10,
               (unsigned)tm.tms_utime / 50, (unsigned)tm.tms_utime % 50 * 2,
               (unsigned)tm.tms_stime / 50, (unsigned)tm.tms_stime % 50 * 2, nlines, nwords);
    return 0;
}
