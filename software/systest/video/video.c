/* video.c -- /dev/video (video(4), D110): a program draws with the video card's chip and reads back what it
   drew; the console's text is kept meanwhile and is on the screen again afterwards, whatever the program did
   to the chip. Needs the video card:  sh /usr/tests/run video */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sgtty.h>
#include <sys/video.h>

#define MW      40                      /* words a line */
#define FONT    0x80000                 /* the font EPROM in the chip's memory: the even places, then the odd */
static int fd, checks, failed, nq;
static unsigned short q[64], big[1600];
static struct vidinfo vi;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) { failed++; printf("FAIL: %s\n", what); }
}
static void w(int x) { q[nq++] = (unsigned short)x; }
static void flush(void)
{
    if (nq && write(fd, q, 2 * nq) != 2 * nq) { failed++; printf("FAIL: write: %s\n", strerror(errno)); }
    nq = 0;
}
/* the chip's read / write pointer to word a of its memory: line n of the screen (0 the top one) starts at n * MW */
static void pointer(unsigned a) { w(VC_WPR | 0x0C); w(0x4000 | a >> 12); w(VC_WPR | 0x0D); w((a << 4) & 0xFFFF); }
static unsigned word(unsigned a)
{
    unsigned short v = 0;
    pointer(a); w(VC_RD); flush();
    return read(fd, &v, 2) == 2 ? v : 0x10000;
}
/* the dots of character ch's line k (0-15) in the font's plain picture, and in the EPROM it is made from */
static unsigned glyph(int ch, int k)
{
    return word((480 + 16 * ((ch - 0x20) >> 5) + k) * MW + (ch & 31) / 2) >> ((ch & 1) ? 8 : 0) & 0xFF;
}
static unsigned rom(int ch, int k)
{
    return word(FONT + ((ch & 1) ? 48 * MW : 0) + (16 * ((ch - 0x20) >> 5) + k) * MW + (ch & 31) / 2) >> ((ch & 1) ? 8 : 0) & 0xFF;
}
/* is the console's cell (row r, column c) character ch? */
static int cell(int r, int c, int ch)
{
    int k;
    for (k = 0; k < 16; k++)
        if ((word((40 + 16 * r + k) * MW + c / 2) >> ((c & 1) ? 8 : 0) & 0xFF) != rom(ch, k)) return 0;
    return 1;
}

int main(void)
{
    static const char said[] = "written while a program had the screen";
    unsigned short r8[8], v;
    int t, k, r, bad, any;

    fd = open("/dev/video", O_RDWR);
    if (fd < 0) { printf("video: /dev/video: %s\n", strerror(errno)); return 2; }
    ok(open("/dev/video", O_RDWR) < 0 && errno == EBUSY, "a second open is refused: busy");
    ok(ioctl(fd, VIOCINFO, &vi) == 0 && vi.width == 640 && vi.height == 480 && vi.memw == MW && vi.cellw == 8 &&
       vi.cellh == 16 && vi.fonts == 8 && vi.fonty == -1 && vi.fontstep == 48, "VIOCINFO");
    ok(write(fd, q, 3) < 0 && errno == EINVAL, "an odd count is refused");

    pointer(0); w(VC_CLR); w(0); w(MW - 1); w(-479);            /* the screen black */
    w(VC_AMOVE); w(0); w(479); w(VC_DOT);
    w(VC_AMOVE); w(16); w(479); w(VC_ALINE); w(31); w(479);
    w(VC_AMOVE); w(32); w(478); w(VC_AFRCT); w(47); w(477);
    ok(word(0) == 0x0001, "a dot");
    ok(word(1) == 0x7FFF && word(2) == 0, "a line: up to its end point, which is not drawn");
    ok(word(MW + 2) == 0xFFFF && word(2 * MW + 2) == 0xFFFF && word(3 * MW + 2) == 0 && word(MW + 1) == 0 && word(MW + 3) == 0,
       "a filled rectangle");
    w(VC_AMOVE); w(40); w(478); w(VC_AFRCT | VO_XOR); w(55); w(477);
    ok(word(MW + 2) == 0x00FF && word(MW + 3) == 0x00FF, "one more in XOR over its right half");

    w(VC_AMOVE); w(0); w(463);                                  /* a character, as the console draws one */
    w(VC_AGCPY); w(('A' & 31) * vi.cellw); w(vi.fonty - vi.cellh * (('A' - 0x20) >> 5)); w(vi.cellw - 1); w(-(vi.cellh - 1));
    for (bad = any = k = 0; k < 16; k++) {
        v = word((16 + k) * MW) & 0xFF;
        if (v != glyph('A', k) || v != rom('A', k)) bad++;
        if (v) any = 1;
    }
    ok(!bad && any, "a character copied from the font's picture");

    for (k = 0; k < 400; k++) { big[4 * k] = VC_AMOVE; big[4 * k + 1] = 100 + k; big[4 * k + 2] = 50 + k; big[4 * k + 3] = VC_DOT; }
    ok(write(fd, big, sizeof big) == sizeof big, "1600 words in one write (more than the driver holds)");
    ok(ioctl(fd, VIOCSYNC, 0) == 0, "VIOCSYNC");
    for (bad = k = 0; k < 400; k += 57)
        if (!(word((479 - (50 + k)) * MW + (100 + k) / 16) >> ((100 + k) % 16) & 1)) bad++;
    ok(!bad, "... every dot of them drawn");
    ok(read(fd, &v, 2) == 0, "a read with nothing to come returns 0");
    pointer(0);
    for (k = 0; k < 8; k++) w(VC_RD);
    flush();
    ok(read(fd, r8, 16) == 16 && r8[0] == 0x0001 && r8[1] == 0x7FFF && r8[2] == 0, "eight words read at once");

    t = open("/dev/tty4", O_WRONLY);                            /* text to the console meanwhile: kept, not drawn */
    ok(t >= 0 && write(t, "\r\n", 2) == 2 && write(t, said, sizeof said - 1) == sizeof said - 1 && write(t, "\r\n", 2) == 2,
       "text written to the console meanwhile");
    if (t >= 0) close(t);
    ok(word(0) == 0x0001 && word(1) == 0x7FFF, "... the picture is untouched");

    /* everything the console depends on, wrecked: the font's pictures, the colours, the mask, the pattern, the
       origin; and the device left in the middle of a command */
    pointer(480 * MW); w(VC_CLR); w(0xAAAA); w(MW - 1); w(-383);
    w(VC_WPR | 0); w(0xFFFF); w(VC_WPR | 1); w(0); w(VC_WPR | 4); w(0x0F0F);
    w(VC_WPTN); w(2); w(0x5555); w(0xAAAA);
    w(VC_ORG); w(0x4000); w(0);
    w(VC_APLL); w(50); w(1); w(2);
    flush();
    close(fd);

    fd = open("/dev/video", O_RDWR);                            /* the screen as the console has it again */
    ok(fd >= 0, "open again after a close");
    for (bad = 0, k = 0; k < 16; k++)
        if (glyph('A', k) != rom('A', k) || glyph('z', k) != rom('z', k)) bad++;
    ok(!bad, "the font's pictures are made again");
    for (r = 0; r < 25 && !cell(r, 0, said[0]); r++) ;
    for (bad = r == 25, k = 0; !bad && said[k]; k++)
        if (!cell(r, k, said[k])) bad++;
    ok(!bad, "the text written meanwhile is on the screen");
    ok(word(0) == 0 && word(1) == 0, "... and the picture is gone");
    close(fd);
    printf("video: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
