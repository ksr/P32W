/* dev.c -- /dev/video for libgfx on the host (host.h): the emulators' model of the video card's chip
   (../p32acrtc.h), set up as the kernel sets the real one up (kernel/video.c: video_init, vt_setup), with the
   library's writes going straight into it. The chip is never busy here: this is for which dots are drawn, not
   for time. With GFX_PBM=FILE in the environment the picture is written to FILE (a PBM) whenever the program
   sleeps: a way to look at what a program draws without the emulator. */
#include <stdint.h>
#include "p32acrtc.h"
#include <sys/video.h>

#define MW 40
#define ORGW (479 * MW)
#define PICT(a) ((480 + 48 * (a)) * MW)
#define FONTROM 0x80000
static unsigned long nwords;
static void vw(unsigned w) { ac.now = ac.free_at + 1; ac.ar = 0; ac_write_data((uint16_t)w); nwords++; }
static void creg(int r, unsigned v) { ac_write_ar(r); ac_write_data(v); }
static void wpr(int r, unsigned v) { vw(0x0800 | r); vw(v); }
static void rwp(unsigned a) { wpr(0x0C, 0x4000 | a >> 12); wpr(0x0D, (a << 4) & 0xFFFF); }
static void amove(int x, int y) { vw(0x8000); vw(x & 0xFFFF); vw(y & 0xFFFF); }
static void clr(int words, int lines) { vw(0x5800); vw(0); vw(words - 1); vw(-(lines - 1) & 0xFFFF); }
static void cpy(int op, unsigned from, int words, int lines) { vw(op); vw(from >> 12); vw((from << 4) & 0xFFFF); vw(words - 1); vw(-(lines - 1) & 0xFFFF); }
static void setup(void)        /* kernel/video.c: video_init and vt_setup */
{
    int a, k;
    ac_reset();
    creg(0x02, 0x0000); creg(0x04, 0x8088); creg(0x06, 0x4000); creg(0x82, 0x630C); creg(0x84, 0x054F);
    creg(0x86, 0x020D); creg(0x88, 0x2002); creg(0x8A, 0x01E0); creg(0xCA, 0x0028); creg(0xCC, 0x0000); creg(0xCE, 0x0000);
    creg(0x04, 0xC088);
    vw(0x0400); vw(0x4000 | ORGW >> 12); vw((ORGW << 4) & 0xFFFF);
    wpr(0x00, 0x0000); wpr(0x01, 0xFFFF);
    wpr(0x05, 0x0000); wpr(0x06, 0x0000); wpr(0x07, 0xF0F0);
    vw(0x1800); vw(16);
    for (k = 0; k < 16; k++) vw(0xFFFF);
    rwp(0); clr(MW, (int)(AC_RAMW / MW));
    wpr(0x04, 0x00FF); rwp(PICT(0)); cpy(0x7100, FONTROM, 16, 48);
    wpr(0x04, 0xFF00); rwp(PICT(0)); cpy(0x7100, FONTROM + 1920, 16, 48);
    wpr(0x04, 0xFFFF);
    for (a = 1; a < 8; a++) {
        int y0 = -1 - 48 * a;
        rwp(PICT(a)); cpy(0x6100, PICT(0), 16, 48);
        if (a & 4) { amove(1, y0); vw(0xE101); vw(0); vw(-1 & 0xFFFF); vw(254); vw(-47 & 0xFFFF); }
        if (a & 2) for (k = 0; k < 3; k++) { amove(0, y0 - 16 * k - 15); vw(0x8800); vw(256); vw((y0 - 16 * k - 15) & 0xFFFF); }
        if (a & 1) { amove(0, y0); vw(0xC003); vw(255); vw((y0 - 47) & 0xFFFF); }
    }
}

/* ---- the mouse and the keyboard: what a test says they did, in its order (the mouse is file 8) ---- */
static unsigned char msq[3 * 4096], keyq[256];
static int msh, mst, keyh, keyt, ms_is_open;
void dev_mouse(int b0, int b1, int b2) { if (msh + 3 <= (int)sizeof msq) { msq[msh++] = (unsigned char)b0; msq[msh++] = (unsigned char)b1; msq[msh++] = (unsigned char)b2; } }
void dev_key(int c) { if (keyh < (int)sizeof keyq) keyq[keyh++] = (unsigned char)c; }
int sh_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
    int k = 0, m = r && FD_ISSET(8, r) && ms_is_open && mst < msh, key = r && FD_ISSET(0, r) && keyt < keyh;
    (void)n; (void)w; (void)e; (void)t;                 /* (a test never waits: what is not there now will not come) */
    if (r) FD_ZERO(r);
    if (m) { FD_SET(8, r); k++; }
    if (key) { FD_SET(0, r); k++; }
    return k;
}

/* ---- a program's own terminal (file 9), for the library in a window: its two ends are a test's ---- */
void (*dev_tty)(const char *s, int n);
static char ttyq[4096];
static int ttyh, ttyt;
void dev_ttyin(const char *s, int n) { while (n-- > 0 && ttyh < (int)sizeof ttyq) ttyq[ttyh++] = *s++; }

/* ---- the device, as the library sees it ---- */
int sh_open(const char *path, int flags, ...)
{
    (void)flags;
    if (!strcmp(path, "/dev/tty")) { if (!dev_tty) { errno = ENXIO; return -1; } return 9; }
    if (!strcmp(path, "/dev/mouse")) { if (ms_is_open) { errno = EBUSY; return -1; } ms_is_open = 1; return 8; }
    if (strcmp(path, "/dev/video")) { errno = ENOENT; return -1; }
    return 7;
}
int sh_close(int fd) { if (fd == 8) ms_is_open = 0; return 0; }
long sh_write(int fd, const void *buf, unsigned long n)
{
    const uint16_t *w = buf; unsigned long k;
    if (fd == 9) { dev_tty(buf, (int)n); return (long)n; }
    if (fd == 20) { dev_ttyin(buf, (int)n); return (long)n; }  /* (a terminal's master, as wm writes its answers) */
    if (fd != 7 || (n & 1)) { errno = EINVAL; return -1; }
    for (k = 0; k < n / 2; k++) vw(w[k]);
    return (long)n;
}
long sh_read(int fd, void *buf, unsigned long n)
{
    uint16_t *w = buf; unsigned long k = 0;
    if (fd == 8) { unsigned char *b = buf; while (k + 3 <= n && mst < msh) { memcpy(b + k, msq + mst, 3); mst += 3; k += 3; } return (long)k; }
    if (fd == 0) { if (!n || keyt == keyh) return 0; *(unsigned char *)buf = keyq[keyt++]; return 1; }
    if (fd == 9) { if (!n || ttyt == ttyh) return 0; *(char *)buf = ttyq[ttyt++]; if (ttyt == ttyh) ttyt = ttyh = 0; return 1; }
    ac.ar = 0;
    while (k < n / 2 && ac.rfn) w[k++] = (uint16_t)ac_read_data();
    return (long)(2 * k);
}
int sh_ioctl(int fd, int cmd, void *arg)
{
    (void)fd;
    if (cmd == VIOCINFO) { struct vidinfo *v = arg; v->width = 640; v->height = 480; v->memw = MW; v->cellw = 8; v->cellh = 16; v->fonts = 8; v->fonty = -1; v->fontstep = 48; return 0; }
    if (cmd == VIOCSPARE) { struct vidspare *v = arg; v->y = -1 - 8 * 48; v->lines = (int)(AC_RAMW / MW) - 480 - 8 * 48; return 0; }
    return 0;
}


const unsigned short *dev_mem(void) { return ac_ram; }
int dev_bad(void) { return ac.cer || ac.lost || ac.missing; }
unsigned long dev_words(void) { return nwords; }

unsigned sh_sleep(unsigned n)
{
    const char *p = getenv("GFX_PBM"); int x, y; FILE *f;
    (void)n;
    if (!p || !(f = fopen(p, "wb"))) return 0;
    fprintf(f, "P1\n640 480\n");
    for (y = 0; y < 480; y++) { for (x = 0; x < 640; x++) fputc((ac_ram[y * MW + (x >> 4)] >> (x & 15) & 1) ? '0' : '1', f); fputc('\n', f); }
    fclose(f);
    return 0;
}

__attribute__((constructor)) static void dev_start(void) { setup(); }   /* (the chip as a program finds it) */
